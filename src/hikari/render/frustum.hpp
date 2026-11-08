#pragma once

#include <algorithm>
#include <array>
#include <cmath>

#include "hikari/math.hpp"

namespace hikari::render {

struct BoundingSphere {
    Vec3 center{0.0f, 0.0f, 0.0f};
    float radius = 0.0f;
};

// Six normalized clip planes in world space. A plane stores the normal
// (a, b, c) and the offset d so the signed distance of a point is
// a*x + b*y + c*z + d; negative values are outside.
struct FrustumPlane {
    float a = 0.0f;
    float b = 0.0f;
    float c = 0.0f;
    float d = 0.0f;

    float distance(const Vec3& point) const {
        return a * point.x + b * point.y + c * point.z + d;
    }
};

struct Frustum {
    std::array<FrustumPlane, 6> planes{};

    // Extracts left, right, bottom, top, near and far planes from a row-vector
    // view-projection matrix (v * M). Direct3D clip space uses z in [0, 1].
    static Frustum from_view_projection(const Mat4& view_projection) {
        const auto& m = view_projection.m;
        const auto plane_from = [&m](int x_sign, int y_sign, int z_sign, int w_sign) {
            FrustumPlane plane;
            plane.a = m[0][0] * x_sign + m[0][1] * y_sign + m[0][2] * z_sign +
                      m[0][3] * w_sign;
            plane.b = m[1][0] * x_sign + m[1][1] * y_sign + m[1][2] * z_sign +
                      m[1][3] * w_sign;
            plane.c = m[2][0] * x_sign + m[2][1] * y_sign + m[2][2] * z_sign +
                      m[2][3] * w_sign;
            plane.d = m[3][0] * x_sign + m[3][1] * y_sign + m[3][2] * z_sign +
                      m[3][3] * w_sign;
            const float length = std::sqrt(plane.a * plane.a + plane.b * plane.b +
                                           plane.c * plane.c);
            if (length > 1.0e-8f) {
                const float inv = 1.0f / length;
                plane.a *= inv;
                plane.b *= inv;
                plane.c *= inv;
                plane.d *= inv;
            }
            return plane;
        };

        Frustum frustum;
        frustum.planes[0] = plane_from(1, 0, 0, 1);   // left:   w + x >= 0
        frustum.planes[1] = plane_from(-1, 0, 0, 1);  // right:  w - x >= 0
        frustum.planes[2] = plane_from(0, 1, 0, 1);   // bottom: w + y >= 0
        frustum.planes[3] = plane_from(0, -1, 0, 1);  // top:    w - y >= 0
        frustum.planes[4] = plane_from(0, 0, 1, 0);   // near:   z >= 0
        frustum.planes[5] = plane_from(0, 0, -1, 1);  // far:    w - z >= 0
        return frustum;
    }

    bool intersects(const BoundingSphere& sphere) const {
        if (sphere.radius < 0.0f || !sphere.center.is_finite()) {
            return true;
        }
        for (const FrustumPlane& plane : planes) {
            if (plane.distance(sphere.center) < -sphere.radius) {
                return false;
            }
        }
        return true;
    }

    bool contains(const Vec3& point) const {
        for (const FrustumPlane& plane : planes) {
            if (plane.distance(point) < 0.0f) {
                return false;
            }
        }
        return true;
    }
};

} // namespace hikari::render
