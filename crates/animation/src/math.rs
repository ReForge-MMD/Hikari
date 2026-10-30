//! 3D Linear algebra primitives optimized for animation and skinning.

use std::ops::{Add, Mul, Sub};

#[derive(Debug, Clone, Copy, PartialEq, Default)]
pub struct Vec3 {
    pub x: f32,
    pub y: f32,
    pub z: f32,
}

impl Vec3 {
    pub const ZERO: Self = Self { x: 0.0, y: 0.0, z: 0.0 };

    #[inline]
    pub const fn new(x: f32, y: f32, z: f32) -> Self {
        Self { x, y, z }
    }

    #[inline]
    pub fn from_array(a: [f32; 3]) -> Self {
        Self { x: a[0], y: a[1], z: a[2] }
    }

    #[inline]
    pub fn to_array(self) -> [f32; 3] {
        [self.x, self.y, self.z]
    }

    #[inline]
    pub fn dot(self, other: Self) -> f32 {
        self.x * other.x + self.y * other.y + self.z * other.z
    }

    #[inline]
    pub fn cross(self, other: Self) -> Self {
        Self {
            x: self.y * other.z - self.z * other.y,
            y: self.z * other.x - self.x * other.z,
            z: self.x * other.y - self.y * other.x,
        }
    }

    #[inline]
    pub fn length_squared(self) -> f32 {
        self.dot(self)
    }

    #[inline]
    pub fn length(self) -> f32 {
        self.length_squared().sqrt()
    }

    #[inline]
    pub fn normalize(self) -> Self {
        let len = self.length();
        if len > 1e-6 {
            self * (1.0 / len)
        } else {
            Self::ZERO
        }
    }

    #[inline]
    pub fn lerp(self, other: Self, t: f32) -> Self {
        self + (other - self) * t
    }
}

impl Add for Vec3 {
    type Output = Self;
    #[inline]
    fn add(self, rhs: Self) -> Self {
        Self { x: self.x + rhs.x, y: self.y + rhs.y, z: self.z + rhs.z }
    }
}

impl Sub for Vec3 {
    type Output = Self;
    #[inline]
    fn sub(self, rhs: Self) -> Self {
        Self { x: self.x - rhs.x, y: self.y - rhs.y, z: self.z - rhs.z }
    }
}

impl Mul<f32> for Vec3 {
    type Output = Self;
    #[inline]
    fn mul(self, rhs: f32) -> Self {
        Self { x: self.x * rhs, y: self.y * rhs, z: self.z * rhs }
    }
}

#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Quat {
    pub x: f32,
    pub y: f32,
    pub z: f32,
    pub w: f32,
}

impl Quat {
    pub const IDENTITY: Self = Self { x: 0.0, y: 0.0, z: 0.0, w: 1.0 };

    #[inline]
    pub const fn new(x: f32, y: f32, z: f32, w: f32) -> Self {
        Self { x, y, z, w }
    }

    #[inline]
    pub fn from_array(a: [f32; 4]) -> Self {
        Self { x: a[0], y: a[1], z: a[2], w: a[3] }
    }

    #[inline]
    pub fn to_array(self) -> [f32; 4] {
        [self.x, self.y, self.z, self.w]
    }

    #[inline]
    pub fn from_axis_angle(axis: Vec3, angle: f32) -> Self {
        let half_angle = angle * 0.5;
        let s = half_angle.sin();
        let norm_axis = axis.normalize();
        Self {
            x: norm_axis.x * s,
            y: norm_axis.y * s,
            z: norm_axis.z * s,
            w: half_angle.cos(),
        }
    }

    #[inline]
    pub fn normalize(self) -> Self {
        let len_sq = self.x * self.x + self.y * self.y + self.z * self.z + self.w * self.w;
        if len_sq > 1e-6 {
            let inv_len = 1.0 / len_sq.sqrt();
            Self {
                x: self.x * inv_len,
                y: self.y * inv_len,
                z: self.z * inv_len,
                w: self.w * inv_len,
            }
        } else {
            Self::IDENTITY
        }
    }

    #[inline]
    pub fn slerp(self, mut other: Self, t: f32) -> Self {
        let mut dot = self.x * other.x + self.y * other.y + self.z * other.z + self.w * other.w;
        if dot < 0.0 {
            other = Self {
                x: -other.x,
                y: -other.y,
                z: -other.z,
                w: -other.w,
            };
            dot = -dot;
        }

        if dot > 0.9995 {
            // Linear interpolation when quaternions are very close
            return Self {
                x: self.x + (other.x - self.x) * t,
                y: self.y + (other.y - self.y) * t,
                z: self.z + (other.z - self.z) * t,
                w: self.w + (other.w - self.w) * t,
            }.normalize();
        }

        let theta_0 = dot.acos();
        let theta = theta_0 * t;
        let sin_theta = theta.sin();
        let sin_theta_0 = theta_0.sin();

        let s0 = (theta_0 - theta).cos() - dot * sin_theta / sin_theta_0;
        let s1 = sin_theta / sin_theta_0;

        Self {
            x: self.x * s0 + other.x * s1,
            y: self.y * s0 + other.y * s1,
            z: self.z * s0 + other.z * s1,
            w: self.w * s0 + other.w * s1,
        }.normalize()
    }

    #[inline]
    pub fn conjugate(self) -> Self {
        Self {
            x: -self.x,
            y: -self.y,
            z: -self.z,
            w: self.w,
        }
    }

    #[inline]
    pub fn to_euler_xyz(self) -> Vec3 {
        let sin_p = 2.0 * (self.w * self.y - self.z * self.x);
        let y = if sin_p.abs() >= 1.0 {
            std::f32::consts::FRAC_PI_2.copysign(sin_p)
        } else {
            sin_p.asin()
        };

        let sin_r_cos_p = 2.0 * (self.w * self.x + self.y * self.z);
        let cos_r_cos_p = 1.0 - 2.0 * (self.x * self.x + self.y * self.y);
        let x = sin_r_cos_p.atan2(cos_r_cos_p);

        let sin_y_cos_p = 2.0 * (self.w * self.z + self.x * self.y);
        let cos_y_cos_p = 1.0 - 2.0 * (self.y * self.y + self.z * self.z);
        let z = sin_y_cos_p.atan2(cos_y_cos_p);

        Vec3::new(x, y, z)
    }

    #[inline]
    pub fn from_euler_xyz(e: Vec3) -> Self {
        let qx = Quat::from_axis_angle(Vec3::new(1.0, 0.0, 0.0), e.x);
        let qy = Quat::from_axis_angle(Vec3::new(0.0, 1.0, 0.0), e.y);
        let qz = Quat::from_axis_angle(Vec3::new(0.0, 0.0, 1.0), e.z);
        (qz * qy * qx).normalize()
    }

    #[inline]
    pub fn rotate_vec3(self, v: Vec3) -> Vec3 {
        let u = Vec3::new(self.x, self.y, self.z);
        let s = self.w;
        u * (2.0 * u.dot(v)) + v * (s * s - u.dot(u)) + u.cross(v) * (2.0 * s)
    }
}

impl Mul for Quat {
    type Output = Self;
    #[inline]
    fn mul(self, rhs: Self) -> Self {
        Self {
            w: self.w * rhs.w - self.x * rhs.x - self.y * rhs.y - self.z * rhs.z,
            x: self.w * rhs.x + self.x * rhs.w + self.y * rhs.z - self.z * rhs.y,
            y: self.w * rhs.y - self.x * rhs.z + self.y * rhs.w + self.z * rhs.x,
            z: self.w * rhs.z + self.x * rhs.y - self.y * rhs.x + self.z * rhs.w,
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Mat4 {
    pub m: [[f32; 4]; 4],
}

impl Mat4 {
    pub const IDENTITY: Self = Self {
        m: [
            [1.0, 0.0, 0.0, 0.0],
            [0.0, 1.0, 0.0, 0.0],
            [0.0, 0.0, 1.0, 0.0],
            [0.0, 0.0, 0.0, 1.0],
        ],
    };

    #[inline]
    pub fn from_translation(t: Vec3) -> Self {
        Self {
            m: [
                [1.0, 0.0, 0.0, 0.0],
                [0.0, 1.0, 0.0, 0.0],
                [0.0, 0.0, 1.0, 0.0],
                [t.x, t.y, t.z, 1.0],
            ],
        }
    }

    #[inline]
    pub fn from_quat(q: Quat) -> Self {
        let x2 = q.x + q.x;
        let y2 = q.y + q.y;
        let z2 = q.z + q.z;
        let xx = q.x * x2;
        let xy = q.x * y2;
        let xz = q.x * z2;
        let yy = q.y * y2;
        let yz = q.y * z2;
        let zz = q.z * z2;
        let wx = q.w * x2;
        let wy = q.w * y2;
        let wz = q.w * z2;

        Self {
            m: [
                [1.0 - (yy + zz), xy + wz, xz - wy, 0.0],
                [xy - wz, 1.0 - (xx + zz), yz + wx, 0.0],
                [xz + wy, yz - wx, 1.0 - (xx + yy), 0.0],
                [0.0, 0.0, 0.0, 1.0],
            ],
        }
    }

    #[inline]
    pub fn from_translation_rotation(t: Vec3, q: Quat) -> Self {
        let mut m = Self::from_quat(q);
        m.m[3][0] = t.x;
        m.m[3][1] = t.y;
        m.m[3][2] = t.z;
        m
    }

    #[inline]
    pub fn transform_point(self, p: Vec3) -> Vec3 {
        Vec3 {
            x: p.x * self.m[0][0] + p.y * self.m[1][0] + p.z * self.m[2][0] + self.m[3][0],
            y: p.x * self.m[0][1] + p.y * self.m[1][1] + p.z * self.m[2][1] + self.m[3][1],
            z: p.x * self.m[0][2] + p.y * self.m[1][2] + p.z * self.m[2][2] + self.m[3][2],
        }
    }

    #[inline]
    pub fn inverse_transform(self) -> Self {
        // Fast inverse for rigid body / affine matrices (Rotation + Translation)
        let r0 = [self.m[0][0], self.m[1][0], self.m[2][0]];
        let r1 = [self.m[0][1], self.m[1][1], self.m[2][1]];
        let r2 = [self.m[0][2], self.m[1][2], self.m[2][2]];
        let t = [self.m[3][0], self.m[3][1], self.m[3][2]];

        let tx = -(t[0] * r0[0] + t[1] * r0[1] + t[2] * r0[2]);
        let ty = -(t[0] * r1[0] + t[1] * r1[1] + t[2] * r1[2]);
        let tz = -(t[0] * r2[0] + t[1] * r2[1] + t[2] * r2[2]);

        Self {
            m: [
                [r0[0], r0[1], r0[2], 0.0],
                [r1[0], r1[1], r1[2], 0.0],
                [r2[0], r2[1], r2[2], 0.0],
                [tx, ty, tz, 1.0],
            ],
        }
    }

    #[inline]
    pub fn translation(self) -> Vec3 {
        Vec3::new(self.m[3][0], self.m[3][1], self.m[3][2])
    }

    /// Extracts rotation quaternion from an orthonormal rotation/translation matrix.
    #[inline]
    pub fn rotation(self) -> Quat {
        let m00 = self.m[0][0];
        let m01 = self.m[0][1];
        let m02 = self.m[0][2];
        let m10 = self.m[1][0];
        let m11 = self.m[1][1];
        let m12 = self.m[1][2];
        let m20 = self.m[2][0];
        let m21 = self.m[2][1];
        let m22 = self.m[2][2];

        let trace = m00 + m11 + m22;
        if trace > 0.0 {
            let s = (trace + 1.0).sqrt() * 2.0;
            let inv_s = 1.0 / s;
            Quat {
                x: (m12 - m21) * inv_s,
                y: (m20 - m02) * inv_s,
                z: (m01 - m10) * inv_s,
                w: 0.25 * s,
            }.normalize()
        } else if (m00 > m11) && (m00 > m22) {
            let s = (1.0 + m00 - m11 - m22).sqrt() * 2.0;
            let inv_s = 1.0 / s;
            Quat {
                x: 0.25 * s,
                y: (m01 + m10) * inv_s,
                z: (m20 + m02) * inv_s,
                w: (m12 - m21) * inv_s,
            }.normalize()
        } else if m11 > m22 {
            let s = (1.0 + m11 - m00 - m22).sqrt() * 2.0;
            let inv_s = 1.0 / s;
            Quat {
                x: (m01 + m10) * inv_s,
                y: 0.25 * s,
                z: (m12 + m21) * inv_s,
                w: (m20 - m02) * inv_s,
            }.normalize()
        } else {
            let s = (1.0 + m22 - m00 - m11).sqrt() * 2.0;
            let inv_s = 1.0 / s;
            Quat {
                x: (m20 + m02) * inv_s,
                y: (m12 + m21) * inv_s,
                z: 0.25 * s,
                w: (m01 - m10) * inv_s,
            }.normalize()
        }
    }
}

impl Mul for Mat4 {
    type Output = Self;
    #[inline]
    fn mul(self, rhs: Self) -> Self {
        let mut out = [[0.0f32; 4]; 4];
        for i in 0..4 {
            for j in 0..4 {
                out[i][j] = self.m[i][0] * rhs.m[0][j]
                    + self.m[i][1] * rhs.m[1][j]
                    + self.m[i][2] * rhs.m[2][j]
                    + self.m[i][3] * rhs.m[3][j];
            }
        }
        Self { m: out }
    }
}
