//! Owning wrapper over native MmdWorld.

use glam::{Quat, Vec3};

use crate::ffi::{self, MmdBodyDesc, MmdJointDesc, MmdWorldSettings};

/// Position and rotation of a body in physics world space.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct BodyTransform {
    pub position: Vec3,
    pub rotation: Quat,
}

impl BodyTransform {
    pub const IDENTITY: Self = Self {
        position: Vec3::ZERO,
        rotation: Quat::IDENTITY,
    };

    pub fn write(&self, out: &mut [f32]) {
        out[..3].copy_from_slice(&self.position.to_array());
        out[3..7].copy_from_slice(&self.rotation.to_array());
    }

    pub fn read(v: &[f32]) -> Self {
        Self {
            position: Vec3::new(v[0], v[1], v[2]),
            rotation: Quat::from_xyzw(v[3], v[4], v[5], v[6]).normalize(),
        }
    }
}

/// What to do with bodies that already interpenetrate in the bind pose.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum RestOverlaps {
    Collide = 0,
    IgnoreDynamic = 1,
    #[default]
    IgnoreAll = 2,
}

pub struct World {
    raw: *mut ffi::MmdWorld,
    scratch: Vec<f32>,
}

unsafe impl Send for World {}
unsafe impl Sync for World {}

impl World {
    pub fn new(settings: &MmdWorldSettings, max_bodies: u32) -> Self {
        let raw = unsafe { ffi::mmd_world_create(settings, max_bodies) };
        assert!(!raw.is_null(), "Jolt MmdWorld creation failed");
        Self {
            raw,
            scratch: Vec::new(),
        }
    }

    pub fn set_gravity(&mut self, g: Vec3) {
        unsafe { ffi::mmd_world_set_gravity(self.raw, g.x, g.y, g.z) }
    }

    pub fn add_body(&mut self, desc: &MmdBodyDesc) -> Option<usize> {
        let i = unsafe { ffi::mmd_world_add_body(self.raw, desc) };
        (i >= 0).then_some(i as usize)
    }

    pub fn add_joint(&mut self, desc: &MmdJointDesc) -> Option<usize> {
        let i = unsafe { ffi::mmd_world_add_joint(self.raw, desc) };
        (i >= 0).then_some(i as usize)
    }

    /// Builds the broad phase; filters bind-pose overlaps based on `rest_overlaps`.
    pub fn finish(&mut self, rest_overlaps: RestOverlaps) {
        unsafe { ffi::mmd_world_finish(self.raw, rest_overlaps as u32) }
    }

    pub fn body_count(&self) -> usize {
        unsafe { ffi::mmd_world_body_count(self.raw) as usize }
    }

    fn pack(&mut self, transforms: &[BodyTransform]) {
        self.scratch.resize(transforms.len() * 7, 0.0);
        for (t, out) in transforms.iter().zip(self.scratch.chunks_exact_mut(7)) {
            t.write(out);
        }
    }

    /// Kinematic bodies move to their target pose over `dt`.
    pub fn move_kinematic(&mut self, targets: &[BodyTransform], dt: f32) {
        self.pack(targets);
        unsafe {
            ffi::mmd_world_move_kinematic(
                self.raw,
                self.scratch.as_ptr(),
                targets.len() as u32,
                dt,
            )
        }
    }

    pub fn step(&mut self, dt: f32, collision_steps: u32) {
        unsafe { ffi::mmd_world_step(self.raw, dt, collision_steps) }
    }

    pub fn read(&mut self, out: &mut Vec<BodyTransform>) {
        let n = self.body_count();
        self.scratch.resize(n * 7, 0.0);
        unsafe { ffi::mmd_world_read(self.raw, self.scratch.as_mut_ptr(), n as u32) };
        out.clear();
        out.extend(self.scratch.chunks_exact(7).map(BodyTransform::read));
    }

    pub fn reset(&mut self, transforms: &[BodyTransform]) {
        self.pack(transforms);
        unsafe {
            ffi::mmd_world_reset(
                self.raw,
                self.scratch.as_ptr(),
                transforms.len() as u32,
            )
        }
    }
}

impl Drop for World {
    fn drop(&mut self) {
        if !self.raw.is_null() {
            unsafe { ffi::mmd_world_destroy(self.raw) }
            self.raw = std::ptr::null_mut();
        }
    }
}
