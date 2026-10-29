//! Skeleton hierarchy, bone transforms, CCD-IK solver, and matrix palette generator.

use crate::math::{Mat4, Quat, Vec3};
use hikari_formats::PmxModel;

#[derive(Debug, Clone)]
pub struct BoneNode {
    pub name: String,
    pub parent_index: Option<usize>,
    pub initial_position: Vec3,
    pub deform_layer: i32,
    pub flags: u16,
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
    pub world_matrices: Vec<Mat4>,
    pub skinning_palette: Vec<Mat4>,
    pub sorted_eval_order: Vec<usize>,
}

impl Skeleton {
    pub fn from_pmx(pmx: &PmxModel) -> Self {
        let count = pmx.bones.len();
        let mut bones = Vec::with_capacity(count);
        let mut inv_bind_matrices = Vec::with_capacity(count);
        let local_translations = vec![Vec3::ZERO; count];
        let local_rotations = vec![Quat::IDENTITY; count];
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
            bones.push(BoneNode {
                name: b.name_local.clone(),
                parent_index,
                initial_position: pos,
                deform_layer: b.deform_layer,
                flags: b.flags,
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

        // Topological sort order respecting parent hierarchy and deform layer
        let mut sorted_eval_order: Vec<usize> = (0..count).collect();
        sorted_eval_order.sort_by(|&a, &b| {
            let layer_a = bones[a].deform_layer;
            let layer_b = bones[b].deform_layer;
            if layer_a != layer_b {
                layer_a.cmp(&layer_b)
            } else {
                a.cmp(&b)
            }
        });

        let mut skel = Self {
            bones,
            children,
            inv_bind_matrices,
            local_translations,
            local_rotations,
            world_matrices,
            skinning_palette,
            sorted_eval_order,
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
        let order = self.sorted_eval_order.clone();

        // Step 1: Forward Kinematics
        for &idx in &order {
            self.compute_world_matrix(idx);
        }

        // Step 2: Append / Grant (Fuyo) Bones (Rotation / Translation inheritance)
        for &idx in &order {
            if let Some((p_idx, ratio)) = self.bones[idx].append_parent {
                if (self.bones[idx].flags & 0x0100) != 0 {
                    // Append rotation
                    let parent_rot = self.local_rotations[p_idx];
                    let grant_rot = Quat::IDENTITY.slerp(parent_rot, ratio);
                    self.local_rotations[idx] = (grant_rot * self.local_rotations[idx]).normalize();
                }
                if (self.bones[idx].flags & 0x0200) != 0 {
                    // Append translation
                    let parent_trans = self.local_translations[p_idx];
                    self.local_translations[idx] = self.local_translations[idx] + parent_trans * ratio;
                }
                self.compute_world_matrix(idx);
            }
        }

        // Step 3: Solve IK Chains
        self.solve_ik();

        // Step 4: Final Matrix Palette for GPU Skinning: Palette[i] = World[i] * InvBind[i]
        for i in 0..self.bones.len() {
            self.skinning_palette[i] = self.inv_bind_matrices[i] * self.world_matrices[i];
        }
    }

    fn compute_world_matrix(&mut self, idx: usize) {
        let bone = &self.bones[idx];
        let local_rot = self.local_rotations[idx];

        if let Some(parent_idx) = bone.parent_index {
            let parent_initial = self.bones[parent_idx].initial_position;
            let relative_offset = bone.initial_position - parent_initial;
            let local_pos = relative_offset + self.local_translations[idx];
            let local_m = Mat4::from_translation_rotation(local_pos, local_rot);
            self.world_matrices[idx] = local_m * self.world_matrices[parent_idx];
        } else {
            let local_pos = bone.initial_position + self.local_translations[idx];
            self.world_matrices[idx] = Mat4::from_translation_rotation(local_pos, local_rot);
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
    fn solve_ik(&mut self) {
        for i in 0..self.bones.len() {
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
