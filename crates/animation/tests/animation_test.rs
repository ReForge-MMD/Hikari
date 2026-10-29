use hikari_animation::{eval_mmd_bezier, Mat4, Quat, Skeleton, Vec3};
use hikari_formats::PmxModel;

#[test]
fn test_bezier_interpolation_endpoints_and_linear() {
    assert_eq!(eval_mmd_bezier(20, 20, 107, 107, 0.0), 0.0);
    assert_eq!(eval_mmd_bezier(20, 20, 107, 107, 1.0), 1.0);
    // Linear curve (x1=y1, x2=y2)
    let mid = eval_mmd_bezier(20, 20, 100, 100, 0.5);
    assert!((mid - 0.5).abs() < 1e-4);
}

#[test]
fn test_skeleton_fk_and_matrix_palette() {
    let mut data = Vec::new();
    data.extend_from_slice(b"PMX ");
    data.extend_from_slice(&2.0f32.to_le_bytes());
    data.push(8);
    data.extend_from_slice(&[1, 0, 1, 1, 1, 1, 1, 1]);

    let push_str = |d: &mut Vec<u8>, s: &str| {
        let b = s.as_bytes();
        d.extend_from_slice(&(b.len() as i32).to_le_bytes());
        d.extend_from_slice(b);
    };

    push_str(&mut data, "Model");
    push_str(&mut data, "Model_EN");
    push_str(&mut data, "");
    push_str(&mut data, "");

    // 0 vertices, 0 indices, 0 textures, 0 materials
    data.extend_from_slice(&0i32.to_le_bytes());
    data.extend_from_slice(&0i32.to_le_bytes());
    data.extend_from_slice(&0i32.to_le_bytes());
    data.extend_from_slice(&0i32.to_le_bytes());

    // 2 Bones: Root (pos=[0,0,0], parent=-1) and Child (pos=[0,2,0], parent=0)
    data.extend_from_slice(&2i32.to_le_bytes());
    // Bone 0
    push_str(&mut data, "Root");
    push_str(&mut data, "Root_EN");
    data.extend_from_slice(&0.0f32.to_le_bytes());
    data.extend_from_slice(&0.0f32.to_le_bytes());
    data.extend_from_slice(&0.0f32.to_le_bytes());
    data.push(255); // parent = -1
    data.extend_from_slice(&0i32.to_le_bytes()); // deform layer = 0
    data.extend_from_slice(&0u16.to_le_bytes()); // flags = 0
    // Connection offset = [0,0,0]
    data.extend_from_slice(&0.0f32.to_le_bytes());
    data.extend_from_slice(&0.0f32.to_le_bytes());
    data.extend_from_slice(&0.0f32.to_le_bytes());

    // Bone 1
    push_str(&mut data, "Child");
    push_str(&mut data, "Child_EN");
    data.extend_from_slice(&0.0f32.to_le_bytes());
    data.extend_from_slice(&2.0f32.to_le_bytes());
    data.extend_from_slice(&0.0f32.to_le_bytes());
    data.push(0); // parent = 0 (Root)
    data.extend_from_slice(&0i32.to_le_bytes()); // deform layer = 0
    data.extend_from_slice(&0u16.to_le_bytes()); // flags = 0
    // Connection offset = [0,0,0]
    data.extend_from_slice(&0.0f32.to_le_bytes());
    data.extend_from_slice(&0.0f32.to_le_bytes());
    data.extend_from_slice(&0.0f32.to_le_bytes());

    // 0 morphs, 0 display, 0 bodies, 0 joints
    data.extend_from_slice(&0i32.to_le_bytes());
    data.extend_from_slice(&0i32.to_le_bytes());
    data.extend_from_slice(&0i32.to_le_bytes());
    data.extend_from_slice(&0i32.to_le_bytes());

    let pmx = PmxModel::parse(&data).expect("Failed to parse PMX");
    let mut skel = Skeleton::from_pmx(&pmx);
    assert_eq!(skel.bones.len(), 2);

    // Initial bind: skinning palette should be identity matrices
    assert_eq!(skel.skinning_palette[0], Mat4::IDENTITY);
    assert_eq!(skel.skinning_palette[1], Mat4::IDENTITY);

    // Translate root by [0, 5, 0]
    skel.set_bone_transform(0, Vec3::new(0.0, 5.0, 0.0), Quat::IDENTITY);
    skel.update_transforms();

    // World position of Child should now be [0, 2 + 5, 0] = [0, 7, 0]
    let child_world = skel.world_matrices[1].transform_point(Vec3::ZERO);
    assert!((child_world.y - 7.0).abs() < 1e-4);
}

#[test]
fn test_euler_round_trip() {
    let euler = Vec3::new(0.3, 0.2, -0.4);
    let q = Quat::from_euler_xyz(euler);
    let back = q.to_euler_xyz();
    assert!((back.x - euler.x).abs() < 1e-4, "x mismatch: {} vs {}", back.x, euler.x);
    assert!((back.y - euler.y).abs() < 1e-4, "y mismatch: {} vs {}", back.y, euler.y);
    assert!((back.z - euler.z).abs() < 1e-4, "z mismatch: {} vs {}", back.z, euler.z);
}

#[test]
fn test_knee_ik_limits() {
    let mut data = Vec::new();
    data.extend_from_slice(b"PMX ");
    data.extend_from_slice(&2.0f32.to_le_bytes());
    data.push(8);
    data.extend_from_slice(&[1, 0, 1, 1, 1, 1, 1, 1]);

    let push_str = |d: &mut Vec<u8>, s: &str| {
        let b = s.as_bytes();
        d.extend_from_slice(&(b.len() as i32).to_le_bytes());
        d.extend_from_slice(b);
    };

    push_str(&mut data, "LegModel");
    push_str(&mut data, "LegModel_EN");
    push_str(&mut data, "");
    push_str(&mut data, "");

    // 0 vertices, 0 indices, 0 textures, 0 materials
    data.extend_from_slice(&0i32.to_le_bytes());
    data.extend_from_slice(&0i32.to_le_bytes());
    data.extend_from_slice(&0i32.to_le_bytes());
    data.extend_from_slice(&0i32.to_le_bytes());

    // 4 Bones:
    // 0: Leg (hip) at [0, 10, 0], parent=-1
    // 1: Knee at [0, 5, 0], parent=0
    // 2: Ankle at [0, 0, 0], parent=1
    // 3: Leg IK at [0, 0, 0], parent=-1, target=2, links=[1 (Knee)]
    data.extend_from_slice(&4i32.to_le_bytes());

    // Bone 0: Leg
    push_str(&mut data, "Leg");
    push_str(&mut data, "Leg_EN");
    data.extend_from_slice(&0.0f32.to_le_bytes());
    data.extend_from_slice(&10.0f32.to_le_bytes());
    data.extend_from_slice(&0.0f32.to_le_bytes());
    data.push(255); // parent = -1
    data.extend_from_slice(&0i32.to_le_bytes());
    data.extend_from_slice(&0u16.to_le_bytes()); // flags = 0
    data.extend_from_slice(&0.0f32.to_le_bytes());
    data.extend_from_slice(&0.0f32.to_le_bytes());
    data.extend_from_slice(&0.0f32.to_le_bytes());

    // Bone 1: Knee
    push_str(&mut data, "Knee");
    push_str(&mut data, "Knee_EN");
    data.extend_from_slice(&0.0f32.to_le_bytes());
    data.extend_from_slice(&5.0f32.to_le_bytes());
    data.extend_from_slice(&0.0f32.to_le_bytes());
    data.push(0); // parent = 0 (Leg)
    data.extend_from_slice(&0i32.to_le_bytes());
    data.extend_from_slice(&0u16.to_le_bytes());
    data.extend_from_slice(&0.0f32.to_le_bytes());
    data.extend_from_slice(&0.0f32.to_le_bytes());
    data.extend_from_slice(&0.0f32.to_le_bytes());

    // Bone 2: Ankle
    push_str(&mut data, "Ankle");
    push_str(&mut data, "Ankle_EN");
    data.extend_from_slice(&0.0f32.to_le_bytes());
    data.extend_from_slice(&0.0f32.to_le_bytes());
    data.extend_from_slice(&0.0f32.to_le_bytes());
    data.push(1); // parent = 1 (Knee)
    data.extend_from_slice(&0i32.to_le_bytes());
    data.extend_from_slice(&0u16.to_le_bytes());
    data.extend_from_slice(&0.0f32.to_le_bytes());
    data.extend_from_slice(&0.0f32.to_le_bytes());
    data.extend_from_slice(&0.0f32.to_le_bytes());

    // Bone 3: Leg IK
    push_str(&mut data, "LegIK");
    push_str(&mut data, "LegIK_EN");
    data.extend_from_slice(&0.0f32.to_le_bytes());
    data.extend_from_slice(&0.0f32.to_le_bytes());
    data.extend_from_slice(&0.0f32.to_le_bytes());
    data.push(255); // parent = -1
    data.extend_from_slice(&0i32.to_le_bytes());
    data.extend_from_slice(&0x0020u16.to_le_bytes()); // flags = IK
    data.extend_from_slice(&0.0f32.to_le_bytes());
    data.extend_from_slice(&0.0f32.to_le_bytes());
    data.extend_from_slice(&0.0f32.to_le_bytes());
    // IK Data
    data.push(2); // target bone = 2 (Ankle)
    data.extend_from_slice(&40i32.to_le_bytes()); // loop_count = 40
    data.extend_from_slice(&1.57f32.to_le_bytes()); // angle_limit
    data.extend_from_slice(&1i32.to_le_bytes()); // 1 link
    // Link 0: Bone 1 (Knee) with limits
    data.push(1);
    data.push(1); // has_limits = 1
    // min: [0.0087, 0, 0]
    data.extend_from_slice(&0.0087f32.to_le_bytes());
    data.extend_from_slice(&0.0f32.to_le_bytes());
    data.extend_from_slice(&0.0f32.to_le_bytes());
    // max: [3.1415, 0, 0]
    data.extend_from_slice(&3.1415f32.to_le_bytes());
    data.extend_from_slice(&0.0f32.to_le_bytes());
    data.extend_from_slice(&0.0f32.to_le_bytes());

    // 0 morphs, 0 display, 0 bodies, 0 joints
    data.extend_from_slice(&0i32.to_le_bytes());
    data.extend_from_slice(&0i32.to_le_bytes());
    data.extend_from_slice(&0i32.to_le_bytes());
    data.extend_from_slice(&0i32.to_le_bytes());

    let pmx = PmxModel::parse(&data).expect("Failed to parse PMX");
    let mut skel = Skeleton::from_pmx(&pmx);

    // Initially at rest: Ankle at [0, 0, 0]
    assert_eq!(skel.world_matrices[2].translation(), Vec3::new(0.0, 0.0, 0.0));

    // Lift IK target up and back: [0, 2.0, -2.0]
    skel.set_bone_transform(3, Vec3::new(0.0, 2.0, -2.0), Quat::IDENTITY);
    skel.update_transforms();

    let knee_rot = skel.local_rotations[1];
    // Knee must ONLY have rotated about X
    assert_eq!(knee_rot.y, 0.0);
    assert_eq!(knee_rot.z, 0.0);
    // Knee X angle must be within limits [0.0087, 3.1415]
    let knee_x = 2.0 * knee_rot.x.atan2(knee_rot.w);
    assert!(knee_x >= 0.0087 && knee_x <= 3.1415, "Knee X angle out of limits: {}", knee_x);
}
