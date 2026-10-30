//! High-level Physics Rig for PMX models: manages Jolt rigid bodies, joints, and bone mappings.

use glam::{Quat, Vec3};
use hikari_formats::PmxModel;

use crate::coords;
use crate::ffi::{self, MmdBodyDesc, MmdJointDesc, MmdWorldSettings};
use crate::world::{BodyTransform, RestOverlaps, World};

/// Settings for Jolt physics simulation.
#[derive(Clone, Debug, PartialEq)]
pub struct PhysicsSettings {
    /// Gravity in model units / s² (MMD default is 9.8 * 10 = 98.0).
    pub gravity: f32,
    /// Maximum sub-step duration in seconds (e.g. 1/120).
    pub max_substep: f32,
    /// Maximum number of sub-steps per frame.
    pub max_substeps: u32,
    /// Delta time threshold beyond which physics is reset rather than stepped (e.g. seek / lag).
    pub max_frame: f32,
    pub collision_steps: u32,
    pub velocity_steps: u32,
    pub position_steps: u32,
    /// Damping ratio for joint springs (0.25 gives stable, non-exploding hair/cloth).
    pub spring_damping_ratio: f32,
    /// Global spring stiffness multiplier.
    pub spring_scale: f32,
    /// Extra damping added to all bodies (0..1 fraction of velocity lost per second).
    pub extra_damping: f32,
    /// Overlap policy for colliders intersecting in the rest/bind pose.
    pub rest_overlaps: RestOverlaps,
}

impl Default for PhysicsSettings {
    fn default() -> Self {
        Self {
            gravity: 98.0,
            max_substep: 1.0 / 60.0,
            max_substeps: 3,
            max_frame: 0.25,
            collision_steps: 1,
            velocity_steps: 8,
            position_steps: 2,
            spring_damping_ratio: 0.30,
            spring_scale: 1.0,
            extra_damping: 0.02,
            rest_overlaps: RestOverlaps::IgnoreDynamic,
        }
    }
}

/// A transform imposed by the physics rig on a bone.
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum BoneOverride {
    /// Full override of position and rotation (MMD Mode 1: Dynamic).
    Full(Vec3, Quat),
    /// Override of rotation only; position stays driven by the skeleton (MMD Mode 2: Aligned).
    Rotation(Quat),
}

#[derive(Clone, Copy, Debug)]
struct RigBody {
    bone: Option<usize>,
    mode: u8,
    offset: BodyTransform,
}

pub struct PhysicsRig {
    world: World,
    bodies: Vec<RigBody>,
    /// Bone index -> rigid body driving it (for Mode 1 and Mode 2).
    drivers: Vec<Option<usize>>,
    previous_targets: Vec<BodyTransform>,
    targets: Vec<BodyTransform>,
    interpolated: Vec<BodyTransform>,
    current: Vec<BodyTransform>,
    overrides: Vec<Option<BoneOverride>>,
    names: Vec<String>,
    settings: PhysicsSettings,
    needs_reset: bool,
}

#[inline]
pub fn compose(a: BodyTransform, b: BodyTransform) -> BodyTransform {
    BodyTransform {
        position: a.position + a.rotation * b.position,
        rotation: (a.rotation * b.rotation).normalize(),
    }
}

#[inline]
pub fn inverse(a: BodyTransform) -> BodyTransform {
    let rotation = a.rotation.inverse();
    BodyTransform {
        position: -(rotation * a.position),
        rotation,
    }
}

/// Converts MMD damping fraction (per-second loss) to Jolt rate.
#[inline]
pub fn damping_rate(fraction: f32) -> f32 {
    -(1.0 - fraction.clamp(0.0, 0.999)).ln()
}

impl PhysicsRig {
    /// Creates a new physics rig from a PMX model and rest bone positions.
    /// Returns `None` if the model has no rigid bodies.
    pub fn new(
        model: &PmxModel,
        bone_rest_positions: &[[f32; 3]],
        settings: &PhysicsSettings,
    ) -> Option<Self> {
        if model.rigid_bodies.is_empty() {
            return None;
        }

        let world_settings = MmdWorldSettings {
            gravity: [0.0, -settings.gravity, 0.0],
            spring_damping_ratio: settings.spring_damping_ratio,
            velocity_steps: settings.velocity_steps,
            position_steps: settings.position_steps,
        };

        let mut world = World::new(&world_settings, model.rigid_bodies.len() as u32 + 16);
        let num_bones = bone_rest_positions.len();
        let mut bodies = Vec::with_capacity(model.rigid_bodies.len());
        let mut drivers = vec![None; num_bones];
        let mut ids = Vec::with_capacity(model.rigid_bodies.len());
        let mut names = Vec::new();
        let extra = damping_rate(settings.extra_damping);

        for rb in &model.rigid_bodies {
            let bone = if rb.bone_index >= 0 && (rb.bone_index as usize) < num_bones {
                Some(rb.bone_index as usize)
            } else {
                None
            };

            let world_tf = BodyTransform {
                position: coords::position(rb.position),
                rotation: coords::euler_yxz(rb.rotation),
            };

            let bone_tf = bone.map_or(BodyTransform::IDENTITY, |b| BodyTransform {
                position: coords::position(bone_rest_positions[b]),
                rotation: Quat::IDENTITY,
            });

            let mode = rb.mode;
            let desc = MmdBodyDesc {
                shape: match rb.shape {
                    0 => ffi::SHAPE_SPHERE,
                    1 => ffi::SHAPE_BOX,
                    2 => ffi::SHAPE_CAPSULE,
                    _ => ffi::SHAPE_SPHERE,
                },
                size: [rb.size[0].abs(), rb.size[1].abs(), rb.size[2].abs()],
                position: world_tf.position.to_array(),
                rotation: world_tf.rotation.to_array(),
                mass: if rb.mass > 0.0 { rb.mass } else { 1.0 },
                linear_damping: damping_rate(rb.move_damping) + extra,
                angular_damping: damping_rate(rb.rot_damping) + extra,
                restitution: rb.restitution.clamp(0.0, 1.0),
                friction: rb.friction.max(0.0),
                group: (rb.collision_group & 0x0F) as u32,
                collide_mask: rb.collision_mask as u32 & 0xFFFF,
                kinematic: if mode == 0 { 1 } else { 0 },
            };

            let id = world.add_body(&desc);
            ids.push(id);
            if id.is_none() {
                continue;
            }

            let index = bodies.len();
            names.push(rb.name_local.clone());
            bodies.push(RigBody {
                bone,
                mode,
                offset: compose(inverse(bone_tf), world_tf),
            });

            if let Some(b) = bone {
                if mode != 0 && drivers[b].is_none() {
                    drivers[b] = Some(index);
                }
            }
        }

        for j in &model.joints {
            let get_body = |i: i32| -> Option<usize> {
                if i >= 0 && (i as usize) < ids.len() {
                    ids[i as usize]
                } else {
                    None
                }
            };

            let (Some(a), Some(b)) = (get_body(j.body_a), get_body(j.body_b)) else {
                continue;
            };

            let (linear_lower, linear_upper) = coords::linear_range(j.pos_min, j.pos_max);
            let (angular_lower, angular_upper) = coords::angular_range(j.rot_min, j.rot_max);

            let desc = MmdJointDesc {
                body_a: a as i32,
                body_b: b as i32,
                position: coords::position(j.position).to_array(),
                rotation: coords::euler_yxz(j.rotation).to_array(),
                linear_lower,
                linear_upper,
                angular_lower,
                angular_upper,
                linear_spring: [
                    j.spring_pos[0].abs() * settings.spring_scale,
                    j.spring_pos[1].abs() * settings.spring_scale,
                    j.spring_pos[2].abs() * settings.spring_scale,
                ],
                angular_spring: [
                    j.spring_rot[0].abs() * settings.spring_scale,
                    j.spring_rot[1].abs() * settings.spring_scale,
                    j.spring_rot[2].abs() * settings.spring_scale,
                ],
            };

            world.add_joint(&desc);
        }

        world.finish(settings.rest_overlaps);

        let n = bodies.len();
        Some(Self {
            world,
            bodies,
            drivers,
            previous_targets: vec![BodyTransform::IDENTITY; n],
            targets: vec![BodyTransform::IDENTITY; n],
            interpolated: vec![BodyTransform::IDENTITY; n],
            current: Vec::with_capacity(n),
            overrides: vec![None; num_bones],
            names,
            settings: settings.clone(),
            needs_reset: true,
        })
    }

    pub fn body_count(&self) -> usize {
        self.bodies.len()
    }

    pub fn dynamic_count(&self) -> usize {
        self.bodies.iter().filter(|b| b.mode != 0).count()
    }

    pub fn names(&self) -> &[String] {
        &self.names
    }

    /// Requests full state reset on the next step (after seek, loop, or animation reload).
    pub fn request_reset(&mut self) {
        self.needs_reset = true;
    }

    pub fn set_gravity(&mut self, g: f32) {
        self.settings.gravity = g;
        self.world.set_gravity(Vec3::new(0.0, -g, 0.0));
    }

    fn update_targets(&mut self, bone_world: &[BodyTransform]) {
        for (t, body) in self.targets.iter_mut().zip(&self.bodies) {
            let bone_tf = body.bone.and_then(|b| bone_world.get(b).copied()).unwrap_or(BodyTransform::IDENTITY);
            *t = compose(bone_tf, body.offset);
        }
    }

    /// Steps the physics simulation by `dt` seconds and returns per-bone overrides.
    pub fn step(
        &mut self,
        bone_world: &[BodyTransform],
        dt: f32,
    ) -> &[Option<BoneOverride>] {
        self.update_targets(bone_world);

        if self.needs_reset || !(dt > 0.0) || dt > self.settings.max_frame {
            self.world.reset(&self.targets);
            self.previous_targets.clone_from(&self.targets);
            self.needs_reset = false;

            self.world.move_kinematic(&self.targets, self.settings.max_substep);
            for _ in 0..2 {
                self.world.step(self.settings.max_substep, self.settings.collision_steps);
            }
        } else {
            let n = ((dt / self.settings.max_substep).ceil() as u32)
                .clamp(1, self.settings.max_substeps.max(1));
            let h = dt / n as f32;

            for s in 1..=n {
                let a = s as f32 / n as f32;
                for ((out, from), to) in self
                    .interpolated
                    .iter_mut()
                    .zip(&self.previous_targets)
                    .zip(&self.targets)
                {
                    *out = BodyTransform {
                        position: from.position.lerp(to.position, a),
                        rotation: from.rotation.slerp(to.rotation, a),
                    };
                }
                self.world.move_kinematic(&self.interpolated, h);
                self.world.step(h, self.settings.collision_steps);
            }
            self.previous_targets.clone_from(&self.targets);
        }

        self.world.read(&mut self.current);
        self.overrides.fill(None);

        for (bone, driver) in self.drivers.iter().enumerate() {
            let Some(i) = *driver else { continue };
            let body = &self.bodies[i];
            let Some(tf) = self.current.get(i) else { continue };

            if !tf.position.is_finite() || !tf.rotation.is_finite() {
                self.needs_reset = true;
                continue;
            }

            let bone_tf = compose(*tf, inverse(body.offset));
            self.overrides[bone] = Some(match body.mode {
                2 => BoneOverride::Rotation(bone_tf.rotation),
                _ => BoneOverride::Full(bone_tf.position, bone_tf.rotation),
            });
        }

        &self.overrides
    }

    pub fn body_transforms(&self) -> &[BodyTransform] {
        &self.current
    }
}
