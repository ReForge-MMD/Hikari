use hikari_animation::physics::{PhysicsRig, PhysicsSettings};
use hikari_animation::{Mat4, Skeleton};
use hikari_formats::{PmxBone, PmxJoint, PmxModel, PmxRigidBody};

#[test]
fn test_skeleton_physics_end_to_end() {
    let mut pmx = PmxModel::default();

    // Bone 0: Head (Parent)
    pmx.bones.push(PmxBone {
        name_local: "Head".into(),
        name_universal: "".into(),
        position: [0.0, 10.0, 0.0],
        parent_index: -1,
        deform_layer: 0,
        flags: 0,
        connection_target: Ok(-1),
        append_parent: None,
        fixed_axis: None,
        local_axis: None,
        external_parent_key: None,
        ik: None,
    });

    // Bone 1: Hair strand 1 (Child of Head)
    pmx.bones.push(PmxBone {
        name_local: "Hair1".into(),
        name_universal: "".into(),
        position: [0.0, 8.0, 0.0],
        parent_index: 0,
        deform_layer: 0,
        flags: 0,
        connection_target: Ok(-1),
        append_parent: None,
        fixed_axis: None,
        local_axis: None,
        external_parent_key: None,
        ik: None,
    });

    // Rigid body 0: Head collider (Kinematic, Mode 0)
    pmx.rigid_bodies.push(PmxRigidBody {
        name_local: "HeadRigid".into(),
        name_universal: "".into(),
        bone_index: 0,
        collision_group: 0,
        collision_mask: 0,
        shape: 0, // Sphere
        size: [1.0, 0.0, 0.0],
        position: [0.0, 10.0, 0.0],
        rotation: [0.0, 0.0, 0.0],
        mass: 1.0,
        move_damping: 0.1,
        rot_damping: 0.1,
        restitution: 0.0,
        friction: 0.5,
        mode: 0, // Kinematic
    });

    // Rigid body 1: Hair strand collider (Dynamic, Mode 1)
    pmx.rigid_bodies.push(PmxRigidBody {
        name_local: "HairRigid".into(),
        name_universal: "".into(),
        bone_index: 1,
        collision_group: 1,
        collision_mask: 0,
        shape: 2, // Capsule
        size: [0.5, 2.0, 0.0],
        position: [0.0, 8.0, 0.0],
        rotation: [0.0, 0.0, 0.0],
        mass: 0.5,
        move_damping: 0.05,
        rot_damping: 0.05,
        restitution: 0.0,
        friction: 0.5,
        mode: 1, // Dynamic
    });

    // Joint 0: 6DOF spring connecting Head and Hair
    pmx.joints.push(PmxJoint {
        name_local: "HairSpring".into(),
        name_universal: "".into(),
        joint_type: 0,
        body_a: 0,
        body_b: 1,
        position: [0.0, 9.0, 0.0],
        rotation: [0.0, 0.0, 0.0],
        pos_min: [-0.05, -0.05, -0.05],
        pos_max: [0.05, 0.05, 0.05],
        rot_min: [-0.5, -0.5, -0.5],
        rot_max: [0.5, 0.5, 0.5],
        spring_pos: [200.0, 200.0, 200.0],
        spring_rot: [100.0, 100.0, 100.0],
    });

    let mut skeleton = Skeleton::from_pmx(&pmx);
    let bone_rests: Vec<[f32; 3]> = pmx.bones.iter().map(|b| b.position).collect();
    let settings = PhysicsSettings::default();

    let mut rig = PhysicsRig::new(&pmx, &bone_rests, &settings)
        .expect("PhysicsRig should be created");

    assert_eq!(rig.body_count(), 2);
    assert_eq!(rig.dynamic_count(), 1);

    // Initial FK pass
    skeleton.update_transforms();

    // Initial Hair bone position should be at y = 8.0
    let initial_hair_y = skeleton.world_matrices[1].translation().y;
    assert!((initial_hair_y - 8.0).abs() < 1e-4);

    // Step physics under gravity (dt = 1/60)
    for _ in 0..10 {
        skeleton.apply_physics(&mut rig, 1.0 / 60.0);
    }

    // Dynamic hair bone must still have valid numbers and be pulled slightly downward by gravity
    let simulated_hair_pos = skeleton.world_matrices[1].translation();
    assert!(simulated_hair_pos.x.is_finite());
    assert!(simulated_hair_pos.y.is_finite());
    assert!(simulated_hair_pos.z.is_finite());
    assert!(simulated_hair_pos.y <= 8.05 && simulated_hair_pos.y >= 7.0);

    // Skinning palette for GPU must be non-identity and valid
    assert!(skeleton.skinning_palette[1] != Mat4::IDENTITY);
}
