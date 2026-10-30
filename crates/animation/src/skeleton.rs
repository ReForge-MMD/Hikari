//! Skeleton hierarchy, bone transforms, CCD-IK solver, and matrix palette generator.

use crate::math::{Mat4, Quat, Vec3};
use hikari_formats::PmxModel;

#[derive(Debug, Clone)]
pub struct BoneNode {
    pub name: String,
    pub parent_index: Option<usize>,
    pub initial_position: Vec3,
    pub offset: Vec3,
    pub deform_layer: i32,
    pub flags: u16,
    pub after_physics: bool,
    pub append_parent: Option<(usize, f32)>, // (parent_bone, ratio)
    pub fixed_axis: Option<Vec3>,
    pub ik: Option<IkData>,
}

#[derive(Debug, Clone)]
pub struct IkData {
    pub target_bone: usize,
    pub loop_count: i32,
    pub angle_limit: f32,
    pub links: Vec<IkLink>,
}

#[derive(Debug, Clone)]
pub struct IkLink {
    pub bone_index: usize,
    pub limits: Option<(Vec3, Vec3)>, // (min, max)
}

#[derive(Debug, Clone)]
pub struct Skeleton {
    pub bones: Vec<BoneNode>,
    pub children: Vec<Vec<usize>>,
    pub inv_bind_matrices: Vec<Mat4>,
    pub local_translations: Vec<Vec3>,
    pub local_rotations: Vec<Quat>,
    pub world_positions: Vec<Vec3>,
    pub world_rotations: Vec<Quat>,
    pub world_matrices: Vec<Mat4>,
    pub skinning_palette: Vec<Mat4>,
    pub sorted_eval_order: Vec<usize>,
    /// Reusable buffer for physics bone transforms; avoids a per-frame heap allocation.
    bone_transform_scratch: Vec<hikari_physics::world::BodyTransform>,
}

impl Skeleton {
    pub fn from_pmx(pmx: &PmxModel) -> Self {
        let count = pmx.bones.len();
        let mut bones = Vec::with_capacity(count);
        let mut inv_bind_matrices = Vec::with_capacity(count);
        let local_translations = vec![Vec3::ZERO; count];
        let local_rotations = vec![Quat::IDENTITY; count];
        let world_positions = vec![Vec3::ZERO; count];
        let world_rotations = vec![Quat::IDENTITY; count];
        let world_matrices = vec![Mat4::IDENTITY; count];
        let skinning_palette = vec![Mat4::IDENTITY; count];

        for b in &pmx.bones {
            let parent_index = if b.parent_index >= 0 && (b.parent_index as usize) < count {
                Some(b.parent_index as usize)
            } else {
                None
            };

            let append_parent = b.append_parent.and_then(|(p, r)| {
                if p >= 0 && (p as usize) < count {
                    Some((p as usize, r))
                } else {
                    None
                }
            });

            let fixed_axis = b.fixed_axis.map(Vec3::from_array);

            let ik = b.ik.as_ref().map(|ik| {
                let links = ik.links.iter().filter_map(|l| {
                    if l.bone_index >= 0 && (l.bone_index as usize) < count {
                        Some(IkLink {
                            bone_index: l.bone_index as usize,
                            limits: l.limits.map(|(min, max)| (Vec3::from_array(min), Vec3::from_array(max))),
                        })
                    } else {
                        None
                    }
                }).collect();

                IkData {
                    target_bone: ik.target_bone.max(0) as usize,
                    loop_count: ik.loop_count,
                    angle_limit: ik.angle_limit,
                    links,
                }
            });

            let pos = Vec3::from_array(b.position);
            let offset = parent_index.map_or(pos, |p| pos - Vec3::from_array(pmx.bones[p].position));
            let after_physics = (b.flags & 0x1000) != 0;

            bones.push(BoneNode {
                name: b.name_local.clone(),
                parent_index,
                initial_position: pos,
                offset,
                deform_layer: b.deform_layer,
                flags: b.flags,
                after_physics,
                append_parent,
                fixed_axis,
                ik,
            });

            // Inv bind matrix = Translation(-initial_position)
            inv_bind_matrices.push(Mat4::from_translation(Vec3::ZERO - pos));
        }

        // Build child index lists for fast hierarchical updates
        let mut children = vec![Vec::new(); count];
        for i in 0..count {
            if let Some(parent_idx) = bones[i].parent_index {
                children[parent_idx].push(i);
            }
        }

        // Topological sort order respecting after_physics, deform layer, and parent hierarchy
        let mut sorted: Vec<usize> = (0..count).collect();
        sorted.sort_by_key(|&i| (bones[i].after_physics, bones[i].deform_layer, i));
        let mut placed = vec![false; count];
        let mut sorted_eval_order = Vec::with_capacity(count);
        while sorted_eval_order.len() < count {
            let before = sorted_eval_order.len();
            for &i in &sorted {
                if !placed[i] && bones[i].parent_index.map_or(true, |p| placed[p]) {
                    placed[i] = true;
                    sorted_eval_order.push(i);
                }
            }
            if sorted_eval_order.len() == before {
                // Break cycle if malformed parent indices exist
                for &i in &sorted {
                    if !placed[i] {
                        placed[i] = true;
                        sorted_eval_order.push(i);
                        break;
                    }
                }
            }
        }

        let mut skel = Self {
            bones,
            children,
            inv_bind_matrices,
            local_translations,
            local_rotations,
            world_positions,
            world_rotations,
            world_matrices,
            skinning_palette,
            sorted_eval_order,
            bone_transform_scratch: Vec::new(),
        };

        skel.update_transforms();
        skel
    }

    pub fn set_bone_transform(&mut self, bone_idx: usize, trans: Vec3, rot: Quat) {
        if bone_idx < self.bones.len() {
            self.local_translations[bone_idx] = trans;
            self.local_rotations[bone_idx] = rot;
        }
    }

    pub fn reset_pose(&mut self) {
        for t in &mut self.local_translations {
            *t = Vec3::ZERO;
        }
        for r in &mut self.local_rotations {
            *r = Quat::IDENTITY;
        }
        self.update_transforms();
    }

    /// Evaluates forward kinematics, Append/Grant bones, and IK solving.
    pub fn update_transforms(&mut self) {
        self.update_transforms_stage(false);
        self.update_skinning_palette();
    }

    /// Evaluates one deformation stage: false = before physics, true = after physics.
    pub fn update_transforms_stage(&mut self, after_physics: bool) {
        for i in 0..self.sorted_eval_order.len() {
            let idx = self.sorted_eval_order[i];
            if self.bones[idx].after_physics != after_physics {
                continue;
            }

            // Append / Grant (Fuyo) Bones (Rotation / Translation inheritance)
            if let Some((p_idx, ratio)) = self.bones[idx].append_parent {
                if (self.bones[idx].flags & 0x0100) != 0 {
                    let parent_rot = self.local_rotations[p_idx];
                    let grant_rot = Quat::IDENTITY.slerp(parent_rot, ratio);
                    self.local_rotations[idx] = (grant_rot * self.local_rotations[idx]).normalize();
                }
                if (self.bones[idx].flags & 0x0200) != 0 {
                    let parent_trans = self.local_translations[p_idx];
                    self.local_translations[idx] = self.local_translations[idx] + parent_trans * ratio;
                }
            }

            self.compute_world_matrix(idx);
        }

        // Solve IK Chains for this stage
        self.solve_ik(after_physics);
    }

    /// Recomputes final bone skinning matrix palette for GPU: Palette[i] = World[i] * InvBind[i]
    pub fn update_skinning_palette(&mut self) {
        for i in 0..self.bones.len() {
            self.skinning_palette[i] = self.inv_bind_matrices[i] * self.world_matrices[i];
        }
    }

    /// Fills `buf` with bone transforms in Jolt physics world space, reusing its allocation.
    fn fill_bone_world_transforms(&self, buf: &mut Vec<hikari_physics::world::BodyTransform>) {
        let count = self.bones.len();
        buf.clear();
        buf.reserve(count.saturating_sub(buf.capacity()));
        for i in 0..count {
            let pos = self.world_positions[i];
            let rot = self.world_rotations[i];
            buf.push(hikari_physics::world::BodyTransform {
                position: hikari_physics::glam::Vec3::new(pos.x, pos.y, -pos.z),
                rotation: hikari_physics::glam::Quat::from_xyzw(-rot.x, -rot.y, rot.z, rot.w).normalize(),
            });
        }
    }

    /// Extracts bone transforms in Jolt physics world space.
    pub fn get_bone_world_transforms(&self) -> Vec<hikari_physics::world::BodyTransform> {
        let count = self.bones.len();
        let mut transforms = Vec::with_capacity(count);
        self.fill_bone_world_transforms(&mut transforms);
        transforms
    }

    /// Steps the physics rig and applies physics body overrides to dynamic and aligned bones.
    pub fn apply_physics(&mut self, rig: &mut hikari_physics::PhysicsRig, dt: f32) {
        // Reuse the scratch buffer to avoid a per-frame heap allocation.
        let mut scratch = std::mem::take(&mut self.bone_transform_scratch);
        self.fill_bone_world_transforms(&mut scratch);
        let overrides = rig.step(scratch.as_slice(), dt);
        self.bone_transform_scratch = scratch;

        for i in 0..self.sorted_eval_order.len() {
            let idx = self.sorted_eval_order[i];
            if self.bones[idx].after_physics {
                continue;
            }

            let (pp, pr) = match self.bones[idx].parent_index {
                Some(p) => (self.world_positions[p], self.world_rotations[p]),
                None => (Vec3::ZERO, Quat::IDENTITY),
            };

            match overrides.get(idx).copied().flatten() {
                Some(hikari_physics::rig::BoneOverride::Full(jolt_pos, jolt_rot)) => {
                    let mmd_pos = Vec3::new(jolt_pos.x, jolt_pos.y, -jolt_pos.z);
                    let mmd_rot = Quat {
                        x: -jolt_rot.x,
                        y: -jolt_rot.y,
                        z: jolt_rot.z,
                        w: jolt_rot.w,
                    }.normalize();

                    let inv_pr = pr.inverse();
                    self.local_rotations[idx] = (inv_pr * mmd_rot).normalize();
                    self.local_translations[idx] = inv_pr.rotate_vec3(mmd_pos - pp) - self.bones[idx].offset;
                    self.world_positions[idx] = mmd_pos;
                    self.world_rotations[idx] = mmd_rot;
                    self.world_matrices[idx] = Mat4::from_translation_rotation(mmd_pos, mmd_rot);
                }
                Some(hikari_physics::rig::BoneOverride::Rotation(jolt_rot)) => {
                    let mmd_rot = Quat {
                        x: -jolt_rot.x,
                        y: -jolt_rot.y,
                        z: jolt_rot.z,
                        w: jolt_rot.w,
                    }.normalize();

                    let inv_pr = pr.inverse();
                    self.local_rotations[idx] = (inv_pr * mmd_rot).normalize();
                    let wp = pp + pr.rotate_vec3(self.bones[idx].offset + self.local_translations[idx]);
                    self.world_positions[idx] = wp;
                    self.world_rotations[idx] = mmd_rot;
                    self.world_matrices[idx] = Mat4::from_translation_rotation(wp, mmd_rot);
                }
                None => {
                    self.compute_world_matrix(idx);
                }
            }
        }

        // Evaluate after-physics stage (e.g. post-physics IK)
        self.update_transforms_stage(true);
        self.update_skinning_palette();
    }

    /// Applies secondary soft-tissue jiggle (chest / hips) to the skeleton.
    pub fn apply_jiggle(
        &mut self,
        jiggle: &mut crate::jiggle::Jiggle,
        dt: f32,
        settings: &crate::jiggle::JiggleSettings,
    ) {
        if let Some(offsets) = jiggle.step(&self.world_positions, &self.world_rotations, dt, settings) {
            for i in 0..self.sorted_eval_order.len() {
                let idx = self.sorted_eval_order[i];
                if let Some(offset) = offsets.get(idx).copied().flatten() {
                    self.world_positions[idx] = self.world_positions[idx] + offset;
                    self.world_matrices[idx] = Mat4::from_translation_rotation(
                        self.world_positions[idx],
                        self.world_rotations[idx],
                    );
                    for c in 0..self.children[idx].len() {
                        let child_idx = self.children[idx][c];
                        self.update_subtree(child_idx);
                    }
                }
            }
            self.update_skinning_palette();
        }
    }

    fn compute_world_matrix(&mut self, idx: usize) {
        let bone = &self.bones[idx];
        let local_t = bone.offset + self.local_translations[idx];
        let local_r = self.local_rotations[idx];

        if let Some(parent_idx) = bone.parent_index {
            let pp = self.world_positions[parent_idx];
            let pr = self.world_rotations[parent_idx];
            let wp = pp + pr.rotate_vec3(local_t);
            let wr = (pr * local_r).normalize();
            self.world_positions[idx] = wp;
            self.world_rotations[idx] = wr;
            self.world_matrices[idx] = Mat4::from_translation_rotation(wp, wr);
        } else {
            let wp = local_t;
            let wr = local_r.normalize();
            self.world_positions[idx] = wp;
            self.world_rotations[idx] = wr;
            self.world_matrices[idx] = Mat4::from_translation_rotation(wp, wr);
        }
    }

    /// Recursively updates world matrices for a bone and all of its descendants.
    pub fn update_subtree(&mut self, idx: usize) {
        self.compute_world_matrix(idx);
        for i in 0..self.children[idx].len() {
            let child_idx = self.children[idx][i];
            self.update_subtree(child_idx);
        }
    }

    /// Iterative CCD-IK (Cyclic Coordinate Descent) solver with angle limits and branch hierarchy updates.
    fn solve_ik(&mut self, after_physics: bool) {
        for i in 0..self.bones.len() {
            if self.bones[i].after_physics != after_physics {
                continue;
            }
            let ik_data = match self.bones[i].ik.clone() {
                Some(ik) => ik,
                None => continue,
            };

            let ik_bone = i;
            let target_bone = ik_data.target_bone;
            if target_bone >= self.bones.len() {
                continue;
            }

            let max_loops = ik_data.loop_count.max(1).min(100);
            let angle_limit = ik_data.angle_limit.max(0.001);

            for _ in 0..max_loops {
                for link in &ik_data.links {
                    let link_idx = link.bone_index;
                    if link_idx >= self.bones.len() {
                        continue;
                    }

                    // Target (effector) bone position and Goal (IK bone) position in world space
                    let target_pos = self.world_matrices[target_bone].translation();
                    let goal_pos = self.world_matrices[ik_bone].translation();

                    // If already reached goal within tolerance, break out of link iteration
                    if (target_pos - goal_pos).length_squared() < 1e-6 {
                        break;
                    }

                    let link_pos = self.world_matrices[link_idx].translation();

                    let v_target_world = target_pos - link_pos;
                    let v_goal_world = goal_pos - link_pos;

                    if v_target_world.length_squared() < 1e-8 || v_goal_world.length_squared() < 1e-8 {
                        continue;
                    }

                    // Extract orthonormal basis from link world matrix
                    let m = self.world_matrices[link_idx].m;
                    let r0 = Vec3::new(m[0][0], m[0][1], m[0][2]);
                    let r1 = Vec3::new(m[1][0], m[1][1], m[1][2]);
                    let r2 = Vec3::new(m[2][0], m[2][1], m[2][2]);

                    // Transform world vectors to link local space
                    let v_target = Vec3::new(
                        v_target_world.dot(r0),
                        v_target_world.dot(r1),
                        v_target_world.dot(r2),
                    ).normalize();

                    let v_goal = Vec3::new(
                        v_goal_world.dot(r0),
                        v_goal_world.dot(r1),
                        v_goal_world.dot(r2),
                    ).normalize();

                    let dot = v_target.dot(v_goal).clamp(-1.0, 1.0);
                    if dot > 0.999999 {
                        continue;
                    }

                    let angle = dot.acos().min(angle_limit);
                    let axis = v_target.cross(v_goal);
                    let axis_len_sq = axis.length_squared();
                    if axis_len_sq < 1e-8 {
                        continue;
                    }
                    let axis = axis * (1.0 / axis_len_sq.sqrt());

                    if let Some((min_limit, max_limit)) = link.limits {
                        let is_x_hinge = (max_limit.y - min_limit.y).abs() < 1e-4
                            && (max_limit.z - min_limit.z).abs() < 1e-4;

                        if is_x_hinge {
                            // 1-DOF X hinge joint (e.g. Knee): clamp rotation strictly to local X
                            let mut q = self.local_rotations[link_idx];
                            if q.w < 0.0 {
                                q.x = -q.x;
                                q.y = -q.y;
                                q.z = -q.z;
                                q.w = -q.w;
                            }
                            let cur_x = 2.0 * q.x.atan2(q.w);
                            let delta_x = angle * axis.x;
                            let new_x = (cur_x + delta_x).clamp(min_limit.x, max_limit.x);
                            self.local_rotations[link_idx] = Quat::from_axis_angle(Vec3::new(1.0, 0.0, 0.0), new_x);
                        } else {
                            let delta_rot = Quat::from_axis_angle(axis, angle);
                            let trial_rot = (self.local_rotations[link_idx] * delta_rot).normalize();
                            let mut euler = trial_rot.to_euler_xyz();
                            euler.x = euler.x.clamp(min_limit.x, max_limit.x);
                            euler.y = euler.y.clamp(min_limit.y, max_limit.y);
                            euler.z = euler.z.clamp(min_limit.z, max_limit.z);
                            self.local_rotations[link_idx] = Quat::from_euler_xyz(euler);
                        }
                    } else {
                        let delta_rot = Quat::from_axis_angle(axis, angle);
                        self.local_rotations[link_idx] = (self.local_rotations[link_idx] * delta_rot).normalize();
                    }

                    self.update_subtree(link_idx);
                }
            }
        }
    }
}
