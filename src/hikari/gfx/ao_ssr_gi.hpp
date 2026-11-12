#pragma once

namespace hikari::gfx {

// Sample budgets selected by the AO, SSR, and GI quality controls. The values
// become compile-time shader defines, so the renderer and the tests must agree
// on the mapping. Quality 0 disables the pass and returns 0 taps.

// SSDO hemisphere rays per pixel (AO_TYPE 0), 8 to 28.
int ao_sample_count(int type, int quality);
// HBAO and GTAO slice directions per pixel (AO_TYPE 1 and 2).
int ao_slice_count(int type, int quality);
// HBAO and GTAO horizon steps per direction.
int ao_step_count(int type, int quality);
// Radius of the separable bilateral AO filter, in half-resolution pixels.
int ao_blur_radius(int quality);

// SSR screen-space tap budget: 0, 128, 256, 384, or 512.
int ssr_tap_budget(int quality);

// SSGI (GI_ENABLE 1 and 2) and SSPT (GI_ENABLE 3) ray counts.
int gi_ray_count(int enable, int quality);
// Raymarch steps per GI ray.
int gi_step_count(int enable, int quality);
// Taps of the separable GI denoiser, clamped to the unroll budget.
int gi_filter_taps(int filter_radius);

} // namespace hikari::gfx
