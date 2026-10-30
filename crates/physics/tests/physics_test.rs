use glam::{Quat, Vec3};
use hikari_formats::{PmxJoint, PmxModel, PmxRigidBody};
use hikari_physics::{
    ffi::MmdWorldSettings,
    rig::{damping_rate, BoneOverride, PhysicsRig, PhysicsSettings},
    world::{BodyTransform, RestOverlaps, World},
};

#[test]
fn test_jolt_world_lifecycle() {
    let settings = MmdWorldSettings {
        gravity: [0.0, -98.0, 0.0],
        spring_damping_ratio: 0.25,
        velocity_steps: 10,
        position_steps: 4,
    };

    let mut world = World::new(&settings, 64);
    assert_eq!(world.body_count(), 0);

    world.set_gravity(Vec3::new(0.0, -9.8, 0.0));
    world.finish(RestOverlaps::IgnoreAll);

    // Step the world without bodies
    world.step(1.0 / 60.0, 1);
}

#[test]
fn test_damping_rate_conversion() {
    // 0% loss => rate = 0
    assert!((damping_rate(0.0) - 0.0).abs() < 1e-5);
    // 50% loss => rate = -ln(0.5) ≈ 0.6931
    assert!((damping_rate(0.5) - 0.693147).abs() < 1e-4);
    // 99.9% loss
    assert!(damping_rate(1.0) > 6.0);
}

#[test]
fn test_physics_rig_dynamic_bone_simulation() {
    let mut model = PmxModel::default();

    // Body 0: Kinematic anchor (mode 0) attached to bone 0
    model.rigid_bodies.push(PmxRigidBody {
        name_local: "HeadAnchor".into(),
        name_universal: "".into(),
        bone_index: 0,
        collision_group: 0,
        collision_mask: 0xFFFF,
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

    // Body 1: Dynamic hair strand (mode 1) attached to bone 1
    model.rigid_bodies.push(PmxRigidBody {
        name_local: "HairStrand".into(),
        name_universal: "".into(),
        bone_index: 1,
        collision_group: 1,
        collision_mask: 0xFFFF,
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

    // Joint 0: Spring constraint connecting anchor (0) and hair strand (1)
    model.joints.push(PmxJoint {
        name_local: "HairJoint".into(),
        name_universal: "".into(),
        joint_type: 0,
        body_a: 0,
        body_b: 1,
        position: [0.0, 9.0, 0.0],
        rotation: [0.0, 0.0, 0.0],
        pos_min: [-0.1, -0.1, -0.1],
        pos_max: [0.1, 0.1, 0.1],
        rot_min: [-0.5, -0.5, -0.5],
        rot_max: [0.5, 0.5, 0.5],
        spring_pos: [100.0, 100.0, 100.0],
        spring_rot: [50.0, 50.0, 50.0],
    });

    let bone_rests = [[0.0, 10.0, 0.0], [0.0, 8.0, 0.0]];
    let settings = PhysicsSettings::default();

    let mut rig = PhysicsRig::new(&model, &bone_rests, &settings)
        .expect("PhysicsRig creation should succeed");

    assert_eq!(rig.body_count(), 2);
    assert_eq!(rig.dynamic_count(), 1);

    // Initial bone world poses at rest
    let bone_world = vec![
        BodyTransform {
            position: Vec3::new(0.0, 10.0, 0.0),
            rotation: Quat::IDENTITY,
        },
        BodyTransform {
            position: Vec3::new(0.0, 8.0, 0.0),
            rotation: Quat::IDENTITY,
        },
    ];

    // Step physics with 60 FPS delta time
    let dt = 1.0 / 60.0;
    let overrides = rig.step(&bone_world, dt);

    // Bone 0 is kinematic, no override
    assert!(overrides[0].is_none());

    // Bone 1 is dynamic, should receive a Full override with valid numbers
    if let Some(BoneOverride::Full(pos, rot)) = overrides[1] {
        assert!(pos.is_finite());
        assert!(rot.is_finite());
        // Hair should be hanging below the head around y ≈ 8.0
        assert!(pos.y < 9.5 && pos.y > 6.0);
    } else {
        panic!("Expected Full bone override for dynamic bone");
    }
}
