#pragma once

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

} // namespace hikari::render
