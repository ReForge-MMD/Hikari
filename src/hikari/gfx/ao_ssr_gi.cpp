#include "hikari/gfx/ao_ssr_gi.hpp"

#include <algorithm>

namespace hikari::gfx {
namespace {

int table_lookup(const int* values, int count, int index) {
    if (index <= 0) {
        return 0;
    }
    return values[std::min(index, count) - 1];
}

} // namespace

int ao_sample_count(int type, int quality) {
    // Only SSDO traces a full hemisphere per pixel; the horizon algorithms use
    // slices and steps instead.
    if (type != 0) {
        return 0;
    }
    static const int kSamples[] = {8, 12, 16, 20, 24, 28};
    return table_lookup(kSamples, 6, quality);
}

int ao_slice_count(int type, int quality) {
    static const int kHbao[] = {4, 6, 6, 8, 8, 8};
    static const int kGtao[] = {2, 3, 4, 6, 8, 12};
    if (type == 1) {
        return table_lookup(kHbao, 6, quality);
    }
    if (type == 2) {
        return table_lookup(kGtao, 6, quality);
    }
    return 0;
}

int ao_step_count(int type, int quality) {
    static const int kHorizon[] = {4, 4, 6, 6, 8, 8};
    if (type == 1 || type == 2) {
        return table_lookup(kHorizon, 6, quality);
    }
    return 0;
}

int ao_blur_radius(int quality) {
    static const int kRadius[] = {4, 6, 6, 6, 6, 6};
    return table_lookup(kRadius, 6, quality);
}

int ssr_tap_budget(int quality) {
    static const int kTaps[] = {128, 256, 384, 512};
    if (quality <= 0) {
        return 0;
    }
    return kTaps[std::min(quality, 4) - 1];
}

int gi_ray_count(int enable, int quality) {
    if (enable <= 0) {
        return 0;
    }
    // SSGI stays in the 4 to 16 ray range the phase plan budgets for. SSPT
    // spends the same ray budget on longer paths instead of more rays.
    static const int kRays[] = {4, 8, 12, 16};
    return kRays[std::clamp(quality, 1, 4) - 1];
}

int gi_step_count(int enable, int quality) {
    if (enable <= 0) {
        return 0;
    }
    static const int kSsgi[] = {12, 13, 14, 16};
    static const int kSspt[] = {12, 16, 24, 32};
    const int index = std::clamp(quality, 1, 4) - 1;
    return enable >= 3 ? kSspt[index] : kSsgi[index];
}

int gi_filter_taps(int filter_radius) {
    return std::clamp(filter_radius, 1, 8);
}

} // namespace hikari::gfx
