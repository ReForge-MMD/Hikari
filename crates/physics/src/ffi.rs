//! FFI bindings to the C bridge `cpp/mmd_physics.h`.

#[repr(C)]
pub struct MmdWorld {
    _private: [u8; 0],
}

pub const SHAPE_SPHERE: u32 = 0;
pub const SHAPE_BOX: u32 = 1;
pub const SHAPE_CAPSULE: u32 = 2;

#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct MmdBodyDesc {
    pub shape: u32,
    pub size: [f32; 3],
    pub position: [f32; 3],
    pub rotation: [f32; 4],
    pub mass: f32,
    pub linear_damping: f32,
    pub angular_damping: f32,
    pub restitution: f32,
    pub friction: f32,
    pub group: u32,
    pub collide_mask: u32,
    pub kinematic: u32,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct MmdJointDesc {
    pub body_a: i32,
    pub body_b: i32,
    pub position: [f32; 3],
    pub rotation: [f32; 4],
    pub linear_lower: [f32; 3],
    pub linear_upper: [f32; 3],
    pub angular_lower: [f32; 3],
    pub angular_upper: [f32; 3],
    pub linear_spring: [f32; 3],
    pub angular_spring: [f32; 3],
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct MmdWorldSettings {
    pub gravity: [f32; 3],
    pub spring_damping_ratio: f32,
    pub velocity_steps: u32,
    pub position_steps: u32,
}

extern "C" {
    pub fn mmd_world_create(settings: *const MmdWorldSettings, max_bodies: u32) -> *mut MmdWorld;
    pub fn mmd_world_destroy(world: *mut MmdWorld);
    pub fn mmd_world_set_gravity(world: *mut MmdWorld, x: f32, y: f32, z: f32);
    pub fn mmd_world_add_body(world: *mut MmdWorld, desc: *const MmdBodyDesc) -> i32;
    pub fn mmd_world_add_joint(world: *mut MmdWorld, desc: *const MmdJointDesc) -> i32;
    pub fn mmd_world_finish(world: *mut MmdWorld, rest_overlaps: u32);
    pub fn mmd_world_body_count(world: *const MmdWorld) -> u32;
    pub fn mmd_world_move_kinematic(world: *mut MmdWorld, transforms: *const f32, count: u32, dt: f32);
    pub fn mmd_world_step(world: *mut MmdWorld, dt: f32, collision_steps: u32);
    pub fn mmd_world_read(world: *const MmdWorld, transforms: *mut f32, count: u32);
    pub fn mmd_world_reset(world: *mut MmdWorld, transforms: *const f32, count: u32);
}
