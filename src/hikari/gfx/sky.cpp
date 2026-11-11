#include "hikari/gfx/sky.hpp"

#include <algorithm>
#include <cmath>

namespace hikari::gfx {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kHoursPerDay = 24.0f;
constexpr float kSunriseHour = 6.0f;
constexpr float kPeakSunElevationDegrees = 65.0f;
constexpr float kDegreesPerHour = 15.0f;

float wrap_hours(float hours) {
    const float wrapped = std::fmod(hours, kHoursPerDay);
    return wrapped < 0.0f ? wrapped + kHoursPerDay : wrapped;
}

} // namespace

int atmosphere_step_count(int quality) {
    static constexpr int kStepCounts[] = {8, 12, 16, 24, 32};
    const int index = std::clamp(quality, 0, 4);
    return kStepCounts[index];
}

std::uint32_t sky_capture_resolution(int ibl_quality) {
    if (ibl_quality <= 0) {
        return 0;
    }
    return ibl_quality >= 2 ? 256 : 128;
}

std::uint32_t sky_prefilter_mip_count(std::uint32_t capture_size) {
    constexpr std::uint32_t kMaximumMips = 5;
    constexpr std::uint32_t kSmallestFace = 8;
    std::uint32_t mips = 1;
    for (std::uint32_t size = capture_size; size > kSmallestFace; size >>= 1) {
        ++mips;
    }
    return std::min(mips, kMaximumMips);
}

float sun_elevation_from_time_of_day(float hours) {
    const float h = wrap_hours(hours);
    return kPeakSunElevationDegrees *
           std::sin(kPi * (h - kSunriseHour) / (kHoursPerDay * 0.5f));
}

Vec3 sun_direction_from_time_of_day(float hours) {
    const float h = wrap_hours(hours);
    const float elevation = sun_elevation_from_time_of_day(h) * (kPi / 180.0f);
    // East is +X and south is -Z, matching the left-handed world space.
    const float azimuth = (h - kSunriseHour) * kDegreesPerHour * (kPi / 180.0f) + kPi * 0.5f;
    const float horizontal = std::cos(elevation);
    return Vec3(horizontal * std::sin(azimuth), std::sin(elevation),
                horizontal * std::cos(azimuth))
        .normalize();
}

const CubeFaceBasis& cube_face_basis(std::uint32_t face) {
    static constexpr CubeFaceBasis kBasis[6] = {
        {Vec3(0.0f, 0.0f, -1.0f), Vec3(0.0f, 1.0f, 0.0f), Vec3(1.0f, 0.0f, 0.0f)},
        {Vec3(0.0f, 0.0f, 1.0f), Vec3(0.0f, 1.0f, 0.0f), Vec3(-1.0f, 0.0f, 0.0f)},
        {Vec3(1.0f, 0.0f, 0.0f), Vec3(0.0f, 0.0f, -1.0f), Vec3(0.0f, 1.0f, 0.0f)},
        {Vec3(1.0f, 0.0f, 0.0f), Vec3(0.0f, 0.0f, 1.0f), Vec3(0.0f, -1.0f, 0.0f)},
        {Vec3(1.0f, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f), Vec3(0.0f, 0.0f, 1.0f)},
        {Vec3(-1.0f, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f), Vec3(0.0f, 0.0f, -1.0f)},
    };
    return kBasis[face % 6];
}

} // namespace hikari::gfx