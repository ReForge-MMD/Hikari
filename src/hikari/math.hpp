#pragma once

// Minimal, dependency-free row-vector linear algebra used across Hikari.
// The layout matches the original glam-based Rust runtime: matrices are
// row-major with the translation in row 3, so they can be uploaded to HLSL
// `row_major` constant buffers without transposition.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>

namespace hikari {

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    constexpr Vec3() = default;
    constexpr Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

    static constexpr Vec3 zero() { return Vec3(0.0f, 0.0f, 0.0f); }
    static constexpr Vec3 one() { return Vec3(1.0f, 1.0f, 1.0f); }
    static constexpr Vec3 axis_x() { return Vec3(1.0f, 0.0f, 0.0f); }
    static constexpr Vec3 axis_y() { return Vec3(0.0f, 1.0f, 0.0f); }
    static constexpr Vec3 axis_z() { return Vec3(0.0f, 0.0f, 1.0f); }

    static constexpr Vec3 from_array(const std::array<float, 3>& a) { return Vec3(a[0], a[1], a[2]); }
    constexpr std::array<float, 3> to_array() const { return {x, y, z}; }

    constexpr Vec3 operator+(const Vec3& r) const { return Vec3(x + r.x, y + r.y, z + r.z); }
    constexpr Vec3 operator-(const Vec3& r) const { return Vec3(x - r.x, y - r.y, z - r.z); }
    constexpr Vec3 operator-() const { return Vec3(-x, -y, -z); }
    constexpr Vec3 operator*(float s) const { return Vec3(x * s, y * s, z * s); }
    constexpr Vec3 operator/(float s) const { return Vec3(x / s, y / s, z / s); }
    Vec3& operator+=(const Vec3& r) { x += r.x; y += r.y; z += r.z; return *this; }
    Vec3& operator-=(const Vec3& r) { x -= r.x; y -= r.y; z -= r.z; return *this; }
    Vec3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }

    constexpr bool operator==(const Vec3& r) const { return x == r.x && y == r.y && z == r.z; }
    constexpr bool operator!=(const Vec3& r) const { return !(*this == r); }
    constexpr float operator[](std::size_t i) const { return i == 0 ? x : (i == 1 ? y : z); }

    constexpr float dot(const Vec3& r) const { return x * r.x + y * r.y + z * r.z; }
    constexpr Vec3 cross(const Vec3& r) const {
        return Vec3(y * r.z - z * r.y, z * r.x - x * r.z, x * r.y - y * r.x);
    }
    constexpr float length_squared() const { return dot(*this); }
    float length() const { return std::sqrt(length_squared()); }
    Vec3 normalize() const {
        const float len = length();
        return len > 1.0e-6f ? *this * (1.0f / len) : Vec3::zero();
    }
    constexpr Vec3 lerp(const Vec3& other, float t) const { return *this + (other - *this) * t; }
    bool is_finite() const {
        return std::isfinite(x) && std::isfinite(y) && std::isfinite(z);
    }
};

inline constexpr Vec3 operator*(float s, const Vec3& v) { return v * s; }

struct Vec4 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 0.0f;

    constexpr Vec4() = default;
    constexpr Vec4(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
    static constexpr Vec4 from_array(const std::array<float, 4>& a) { return Vec4(a[0], a[1], a[2], a[3]); }
    constexpr std::array<float, 4> to_array() const { return {x, y, z, w}; }
};

struct Quat {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;

    constexpr Quat() = default;
    constexpr Quat(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}

    static constexpr Quat identity() { return Quat(0.0f, 0.0f, 0.0f, 1.0f); }
    static constexpr Quat from_array(const std::array<float, 4>& a) { return Quat(a[0], a[1], a[2], a[3]); }
    constexpr std::array<float, 4> to_array() const { return {x, y, z, w}; }

    static Quat from_axis_angle(const Vec3& axis, float angle) {
        const float half = angle * 0.5f;
        const float s = std::sin(half);
        const Vec3 n = axis.normalize();
        return Quat(n.x * s, n.y * s, n.z * s, std::cos(half));
    }

    Quat normalize() const {
        const float len_sq = x * x + y * y + z * z + w * w;
        if (len_sq > 1.0e-6f) {
            const float inv = 1.0f / std::sqrt(len_sq);
            return Quat(x * inv, y * inv, z * inv, w * inv);
        }
        return identity();
    }

    constexpr Quat conjugate() const { return Quat(-x, -y, -z, w); }

    Quat inverse() const {
        const float len_sq = x * x + y * y + z * z + w * w;
        if (len_sq > 1.0e-6f) {
            const float inv = 1.0f / len_sq;
            return Quat(-x * inv, -y * inv, -z * inv, w * inv);
        }
        return identity();
    }

    Quat slerp(Quat other, float t) const {
        float d = x * other.x + y * other.y + z * other.z + w * other.w;
        if (d < 0.0f) {
            other = Quat(-other.x, -other.y, -other.z, -other.w);
            d = -d;
        }
        if (d > 0.9995f) {
            return Quat(x + (other.x - x) * t, y + (other.y - y) * t,
                        z + (other.z - z) * t, w + (other.w - w) * t).normalize();
        }
        const float theta_0 = std::acos(std::clamp(d, -1.0f, 1.0f));
        const float theta = theta_0 * t;
        const float sin_theta_0 = std::sin(theta_0);
        if (std::abs(sin_theta_0) < 1.0e-8f) {
            return *this;
        }
        const float s0 = std::sin((1.0f - t) * theta_0) / sin_theta_0;
        const float s1 = std::sin(theta) / sin_theta_0;
        return Quat(x * s0 + other.x * s1, y * s0 + other.y * s1,
                    z * s0 + other.z * s1, w * s0 + other.w * s1).normalize();
    }

    Vec3 to_euler_xyz() const {
        const float sin_p = 2.0f * (w * y - z * x);
        const float y_angle = std::abs(sin_p) >= 1.0f
                                  ? std::copysign(1.5707963267948966f, sin_p)
                                  : std::asin(sin_p);
        const float sin_r_cos_p = 2.0f * (w * x + y * z);
        const float cos_r_cos_p = 1.0f - 2.0f * (x * x + y * y);
        const float x_angle = std::atan2(sin_r_cos_p, cos_r_cos_p);
        const float sin_y_cos_p = 2.0f * (w * z + x * y);
        const float cos_y_cos_p = 1.0f - 2.0f * (y * y + z * z);
        const float z_angle = std::atan2(sin_y_cos_p, cos_y_cos_p);
        return Vec3(x_angle, y_angle, z_angle);
    }

    static Quat from_euler_xyz(const Vec3& e) {
        const Quat qx = from_axis_angle(Vec3::axis_x(), e.x);
        const Quat qy = from_axis_angle(Vec3::axis_y(), e.y);
        const Quat qz = from_axis_angle(Vec3::axis_z(), e.z);
        return (qz * qy * qx).normalize();
    }

    // Intrinsic YXZ used by PMX rigid bodies and joints: R = Ry * Rx * Rz.
    // This is the MMD authoring convention; it intentionally does not apply the
    // static-frame parity signs that glam's EulerRot::YXZ helper adds.
    static Quat from_euler_yxz(float y_angle, float x_angle, float z_angle) {
        const Quat qx = from_axis_angle(Vec3::axis_x(), x_angle);
        const Quat qy = from_axis_angle(Vec3::axis_y(), y_angle);
        const Quat qz = from_axis_angle(Vec3::axis_z(), z_angle);
        return (qy * qx * qz).normalize();
    }

    Vec3 rotate_vec3(const Vec3& v) const {
        const Vec3 u(x, y, z);
        const float s = w;
        return u * (2.0f * u.dot(v)) + v * (s * s - u.dot(u)) + u.cross(v) * (2.0f * s);
    }

    constexpr Quat operator*(const Quat& r) const {
        return Quat(
            w * r.x + x * r.w + y * r.z - z * r.y,
            w * r.y - x * r.z + y * r.w + z * r.x,
            w * r.z + x * r.y - y * r.x + z * r.w,
            w * r.w - x * r.x - y * r.y - z * r.z);
    }

    constexpr Quat operator*(float s) const { return Quat(x * s, y * s, z * s, w * s); }
    constexpr Quat operator+(const Quat& r) const { return Quat(x + r.x, y + r.y, z + r.z, w + r.w); }

    bool is_finite() const {
        return std::isfinite(x) && std::isfinite(y) && std::isfinite(z) && std::isfinite(w);
    }
};

struct Mat4 {
    // m[row][column], row-vector convention (v * M).
    std::array<std::array<float, 4>, 4> m{};

    static constexpr Mat4 identity() {
        Mat4 r;
        r.m = {{{1.0f, 0.0f, 0.0f, 0.0f},
                {0.0f, 1.0f, 0.0f, 0.0f},
                {0.0f, 0.0f, 1.0f, 0.0f},
                {0.0f, 0.0f, 0.0f, 1.0f}}};
        return r;
    }

    static constexpr Mat4 from_translation(const Vec3& t) {
        Mat4 r = identity();
        r.m[3][0] = t.x;
        r.m[3][1] = t.y;
        r.m[3][2] = t.z;
        return r;
    }

    static Mat4 from_quat(const Quat& q) {
        const float x2 = q.x + q.x, y2 = q.y + q.y, z2 = q.z + q.z;
        const float xx = q.x * x2, xy = q.x * y2, xz = q.x * z2;
        const float yy = q.y * y2, yz = q.y * z2, zz = q.z * z2;
        const float wx = q.w * x2, wy = q.w * y2, wz = q.w * z2;
        Mat4 r = identity();
        r.m[0][0] = 1.0f - (yy + zz);
        r.m[0][1] = xy + wz;
        r.m[0][2] = xz - wy;
        r.m[1][0] = xy - wz;
        r.m[1][1] = 1.0f - (xx + zz);
        r.m[1][2] = yz + wx;
        r.m[2][0] = xz + wy;
        r.m[2][1] = yz - wx;
        r.m[2][2] = 1.0f - (xx + yy);
        return r;
    }

    static Mat4 from_translation_rotation(const Vec3& t, const Quat& q) {
        Mat4 r = from_quat(q);
        r.m[3][0] = t.x;
        r.m[3][1] = t.y;
        r.m[3][2] = t.z;
        return r;
    }

    static Mat4 from_scale(const Vec3& s) {
        Mat4 r = identity();
        r.m[0][0] = s.x;
        r.m[1][1] = s.y;
        r.m[2][2] = s.z;
        return r;
    }

    Vec3 transform_point(const Vec3& p) const {
        return Vec3(
            p.x * m[0][0] + p.y * m[1][0] + p.z * m[2][0] + m[3][0],
            p.x * m[0][1] + p.y * m[1][1] + p.z * m[2][1] + m[3][1],
            p.x * m[0][2] + p.y * m[1][2] + p.z * m[2][2] + m[3][2]);
    }

    Vec3 transform_vector(const Vec3& v) const {
        return Vec3(
            v.x * m[0][0] + v.y * m[1][0] + v.z * m[2][0],
            v.x * m[0][1] + v.y * m[1][1] + v.z * m[2][1],
            v.x * m[0][2] + v.y * m[1][2] + v.z * m[2][2]);
    }

    Mat4 inverse_transform() const {
        // Inverse of an affine rotation+translation matrix, row-vector layout.
        const float r0[3] = {m[0][0], m[1][0], m[2][0]};
        const float r1[3] = {m[0][1], m[1][1], m[2][1]};
        const float r2[3] = {m[0][2], m[1][2], m[2][2]};
        const float t[3] = {m[3][0], m[3][1], m[3][2]};
        const float tx = -(t[0] * m[0][0] + t[1] * m[0][1] + t[2] * m[0][2]);
        const float ty = -(t[0] * m[1][0] + t[1] * m[1][1] + t[2] * m[1][2]);
        const float tz = -(t[0] * m[2][0] + t[1] * m[2][1] + t[2] * m[2][2]);
        Mat4 out = identity();
        out.m[0][0] = r0[0]; out.m[0][1] = r0[1]; out.m[0][2] = r0[2];
        out.m[1][0] = r1[0]; out.m[1][1] = r1[1]; out.m[1][2] = r1[2];
        out.m[2][0] = r2[0]; out.m[2][1] = r2[1]; out.m[2][2] = r2[2];
        out.m[3][0] = tx; out.m[3][1] = ty; out.m[3][2] = tz;
        return out;
    }

    Vec3 translation() const { return Vec3(m[3][0], m[3][1], m[3][2]); }

    Quat rotation() const {
        const float m00 = m[0][0], m01 = m[0][1], m02 = m[0][2];
        const float m10 = m[1][0], m11 = m[1][1], m12 = m[1][2];
        const float m20 = m[2][0], m21 = m[2][1], m22 = m[2][2];
        const float trace = m00 + m11 + m22;
        if (trace > 0.0f) {
            const float s = std::sqrt(trace + 1.0f) * 2.0f;
            const float inv = 1.0f / s;
            return Quat((m12 - m21) * inv, (m20 - m02) * inv, (m01 - m10) * inv, 0.25f * s).normalize();
        }
        if (m00 > m11 && m00 > m22) {
            const float s = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
            const float inv = 1.0f / s;
            return Quat(0.25f * s, (m01 + m10) * inv, (m20 + m02) * inv, (m12 - m21) * inv).normalize();
        }
        if (m11 > m22) {
            const float s = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
            const float inv = 1.0f / s;
            return Quat((m01 + m10) * inv, 0.25f * s, (m12 + m21) * inv, (m20 - m02) * inv).normalize();
        }
        const float s = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
        const float inv = 1.0f / s;
        return Quat((m20 + m02) * inv, (m12 + m21) * inv, 0.25f * s, (m01 - m10) * inv).normalize();
    }

    constexpr Mat4 operator*(const Mat4& rhs) const {
        Mat4 out;
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                float sum = 0.0f;
                for (int k = 0; k < 4; ++k) {
                    sum += m[i][k] * rhs.m[k][j];
                }
                out.m[i][j] = sum;
            }
        }
        return out;
    }
};

inline Mat4 perspective_lh(float fov_y, float aspect, float near_z, float far_z) {
    const float h = 1.0f / std::tan(fov_y * 0.5f);
    const float w = h / aspect;
    const float q = far_z / (far_z - near_z);
    Mat4 r;
    r.m = {{{w, 0.0f, 0.0f, 0.0f},
            {0.0f, h, 0.0f, 0.0f},
            {0.0f, 0.0f, q, 1.0f},
            {0.0f, 0.0f, -q * near_z, 0.0f}}};
    return r;
}

inline Mat4 orthographic_lh(float width, float height, float near_z, float far_z) {
    Mat4 r = Mat4::identity();
    r.m[0][0] = 2.0f / width;
    r.m[1][1] = 2.0f / height;
    r.m[2][2] = 1.0f / (far_z - near_z);
    r.m[3][2] = -near_z / (far_z - near_z);
    return r;
}

} // namespace hikari
