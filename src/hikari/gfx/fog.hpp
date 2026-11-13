#pragma once

namespace hikari::gfx {

// Raymarch step budgets selected by FOG_QUALITY. The value becomes the
// compile-time FOG_STEPS shader define, so the renderer and the tests must agree
// on the mapping. FOG_ENABLE 1 is the analytical-only path and raymarches
// nothing, so it reports zero steps.
int fog_step_count(int enable, int quality);

// Maximum raymarched distance in metres. The analytical integral continues the
// medium beyond it, which keeps the analytical and volumetric paths seamless.
float fog_raymarch_max_distance();

} // namespace hikari::gfx