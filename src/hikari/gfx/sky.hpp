#pragma once

#include <cstdint>

#include "hikari/math.hpp"

namespace hikari::gfx {

// Primary ray sample count used by the analytic atmosphere. Modes are ordered by
// cost: 0 -> 8, 1 -> 12, 2 -> 16, 3 -> 24, 4 -> 32 samples.
int atmosphere_step_count(int quality);

// Square resolution of one sky cubemap face for an IBL quality mode: 0 disables
// the capture (no image based lighting), 1 captures at 128, 2 at 256.
std::uint32_t sky_capture_resolution(int ibl_quality);

// Mip count of the GGX prefiltered cubemap for a capture size. The chain stops at
// 8 pixels per face and never exceeds five levels, so roughness 0, 0.25, 0.5,
// 0.75, and 1 map onto the mips.
std::uint32_t sky_prefilter_mip_count(std::uint32_t capture_size);

// Direction from the scene toward the sun for a time of day in hours. The result
// is periodic over 24 hours and continuous across midnight.
Vec3 sun_direction_from_time_of_day(float hours);

// Sun elevation in degrees for a time of day in hours. Positive above the
// horizon, negative at night.
float sun_elevation_from_time_of_day(float hours);

// Orientation of one cubemap face. Rows of the reconstructed face view are
// right, up, and forward, so right * x + up * y + forward points along the
// texel that a TextureCube sample of that face would read.
struct CubeFaceBasis {
    Vec3 right;
    Vec3 up;
    Vec3 forward;
};

// Faces follow the Direct3D cubemap order: +X, -X, +Y, -Y, +Z, -Z. `face` is
// taken modulo six so callers do not have to clamp before indexing.
const CubeFaceBasis& cube_face_basis(std::uint32_t face);

} // namespace hikari::gfx