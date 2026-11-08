#pragma once

#include <algorithm>
#include <cmath>

#include "hikari/math.hpp"

namespace hikari::render {

class Camera {
public:
    Vec3 position{0.0f, 10.0f, -35.0f};
    Vec3 target{0.0f, 10.0f, 0.0f};
    Vec3 up{0.0f, 1.0f, 0.0f};
    float fov_y = 0.5235987756f;
    float aspect = 16.0f / 9.0f;
    float near_z = 0.1f;
    float far_z = 1000.0f;
    bool orthographic = false;
    // World-space height of the orthographic view. When zero it is derived from fov_y.
    float ortho_height = 0.0f;

    // MMD does not build a rotated view matrix. It rotates the scene by
    // Ry(yaw) * Rx(pitch) * Rz(roll) around a focus point and looks down +Z
    // from a fixed eye. `orientation` is that rotation's inverse, i.e. the
    // camera's world basis, and `signed_distance` is the eye's Z coordinate in
    // that basis. MMD stores it negative (-45 by default), which places the
    // focus in front of the eye; a positive value puts the focus behind it.
    void place_mmd(const Vec3& focus, const Quat& orientation, float signed_distance) {
        position = focus + orientation.rotate_vec3(Vec3(0.0f, 0.0f, signed_distance));
        target = position + orientation.rotate_vec3(Vec3(0.0f, 0.0f, 1.0f));
        up = orientation.rotate_vec3(Vec3(0.0f, 1.0f, 0.0f));
    }

    // MMD's orthographic projection is m00 = -2 / distance and m11 = aspect * m00,
    // so the visible world height is |distance| / aspect. `scale` preserves the
    // followed-model unit scale used by the studio.
    static float mmd_ortho_height(float signed_distance, float aspect, float scale = 1.0f) {
        const float safe_aspect = std::max(aspect, 1.0e-4f);
        return std::max(std::abs(signed_distance) * scale / safe_aspect, 1.0e-4f);
    }

    Mat4 view_matrix() const {
        const Vec3 z_axis = (target - position).normalize();
        const Vec3 x_axis = up.cross(z_axis).normalize();
        const Vec3 y_axis = z_axis.cross(x_axis);
        const float tx = -x_axis.dot(position);
        const float ty = -y_axis.dot(position);
        const float tz = -z_axis.dot(position);
        Mat4 result;
        result.m = {{{x_axis.x, y_axis.x, z_axis.x, 0.0f},
                     {x_axis.y, y_axis.y, z_axis.y, 0.0f},
                     {x_axis.z, y_axis.z, z_axis.z, 0.0f},
                     {tx, ty, tz, 1.0f}}};
        return result;
    }

    Mat4 projection_matrix() const {
        if (!orthographic) {
            return perspective_lh(fov_y, aspect, near_z, far_z);
        }
        const float height =
            ortho_height > 0.0f ? ortho_height : 2.0f * std::tan(fov_y * 0.5f);
        return orthographic_lh(height * aspect, height, near_z, far_z);
    }
    Mat4 view_projection_matrix() const { return view_matrix() * projection_matrix(); }
};

// MMD rotates the scene by Ry(yaw) * Rx(pitch) * Rz(roll) and builds the view
// from a fixed +Z look-at, so the camera's world basis is the inverse of that
// product. Keeping the authoring order here matters: composing the axes in a
// different order banks the camera when yaw and pitch are combined.
inline Quat mmd_camera_orientation(float yaw, float pitch, float roll) {
    const Quat scene_rotation = Quat::from_axis_angle(Vec3::axis_z(), roll) *
                                Quat::from_axis_angle(Vec3::axis_x(), pitch) *
                                Quat::from_axis_angle(Vec3::axis_y(), yaw);
    return scene_rotation.inverse().normalize();
}

} // namespace hikari::render
