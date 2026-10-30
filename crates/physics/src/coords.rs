//! Coordinate transformations between MMD (left-handed Z-forward) and standard right-handed physics.

use glam::{EulerRot, Quat, Vec3};

#[inline]
pub fn position(v: [f32; 3]) -> Vec3 {
    Vec3::new(v[0], v[1], -v[2])
}

#[inline]
pub fn to_mmd_position(v: Vec3) -> [f32; 3] {
    [v.x, v.y, -v.z]
}

#[inline]
pub fn quat(q: [f32; 4]) -> Quat {
    let q = Quat::from_xyzw(-q[0], -q[1], q[2], q[3]);
    if q.length_squared() > 1.0e-12 {
        q.normalize()
    } else {
        Quat::IDENTITY
    }
}

#[inline]
pub fn to_mmd_quat(q: Quat) -> [f32; 4] {
    [-q.x, -q.y, q.z, q.w]
}

/// PMX rigid bodies and joints store Euler angles applied in Y, then X, then Z order.
#[inline]
pub fn euler_yxz(r: [f32; 3]) -> Quat {
    Quat::from_euler(EulerRot::YXZ, -r[1], -r[0], r[2])
}

/// Linear range limits: z flips and swaps.
#[inline]
pub fn linear_range(lower: [f32; 3], upper: [f32; 3]) -> ([f32; 3], [f32; 3]) {
    (
        [lower[0], lower[1], -upper[2]],
        [upper[0], upper[1], -lower[2]],
    )
}

/// Angular range limits: x and y flip and swap.
#[inline]
pub fn angular_range(lower: [f32; 3], upper: [f32; 3]) -> ([f32; 3], [f32; 3]) {
    (
        [-upper[0], -upper[1], lower[2]],
        [-lower[0], -lower[1], upper[2]],
    )
}
