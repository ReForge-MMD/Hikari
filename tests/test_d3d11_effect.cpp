#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <string>

#include "hikari/core/d3d11.hpp"
#include "hikari/core/effect.hpp"
#include "hikari/core/reflection.hpp"
#include "hikari/core/shader.hpp"

namespace {

const char* kEffectSource = R"(
float Exposure <
    string UIName = "Exposure";
    float UIMin = 0.0;
    float UIMax = 2.0;
> = 0.5;

cbuffer Params : register(b0) {
    row_major float4x4 WorldViewProj : WORLDVIEWPROJECTION;
    float ExposureValue;
};

Texture2D DiffuseTexture : register(t0);
SamplerState DiffuseSampler : register(s0);

float4 VS_Main(float4 position : POSITION) : SV_POSITION {
    return mul(position, WorldViewProj) * Exposure;
}

float4 PS_Main(float2 uv : TEXCOORD0) : SV_Target {
    return DiffuseTexture.Sample(DiffuseSampler, uv) * ExposureValue;
}

technique ObjectTechnique < string MMDPass = "object"; >
{
    pass P0
    {
        VertexShader = compile vs_5_0 VS_Main();
        PixelShader = compile ps_5_0 PS_Main();
    }
}
)";

class DeviceFixture {
public:
    DeviceFixture()
        : context(nullptr, 0, 0) {}

    hikari::core::D3D11Context context;
};

} // namespace

TEST_CASE_METHOD(DeviceFixture, "Shader reflection exposes constant buffers", "[d3d11]") {
    const auto bytecode = hikari::core::compile_hlsl(kEffectSource, "PS_Main", "ps_5_0");
    REQUIRE_FALSE(bytecode.empty());
    const auto reflection = hikari::core::ShaderReflectionData::reflect(bytecode);
    REQUIRE_FALSE(reflection.cbuffers.empty());
    REQUIRE(reflection.find_texture_slot("DiffuseTexture").has_value());
    REQUIRE(reflection.find_sampler_slot("DiffuseSampler").has_value());
}

TEST_CASE_METHOD(DeviceFixture, "Effect engine compiles, binds and executes a pass", "[d3d11]") {
    auto* effect = hikari::core::HikariEffect::create(kEffectSource, "test.fx", context);
    REQUIRE(effect != nullptr);

    REQUIRE(effect->techniques.size() == 1);
    REQUIRE(effect->techniques[0].passes.size() == 1);
    const auto exposure = effect->param_by_name.find("exposure");
    REQUIRE(exposure != effect->param_by_name.end());
    REQUIRE(effect->parameters[exposure->second].ui.has_value());
    REQUIRE(effect->parameters[exposure->second].ui->label == "Exposure");

    effect->set_param_value_f32(exposure->second, 1.25f);
    REQUIRE(effect->get_param_value_f32(exposure->second).value() == Catch::Approx(1.25f));
    effect->update_gpu_constants();
    REQUIRE_FALSE(effect->constant_dirty);

    REQUIRE(effect->find_technique_for_pass(1).has_value());
    std::uint32_t passes = 0;
    REQUIRE(SUCCEEDED(effect->vtbl->Begin(effect, &passes, 0)));
    REQUIRE(passes == 1);
    REQUIRE(SUCCEEDED(effect->vtbl->BeginPass(effect, 0)));
    REQUIRE(SUCCEEDED(effect->vtbl->CommitChanges(effect)));
    REQUIRE(SUCCEEDED(effect->vtbl->EndPass(effect)));
    REQUIRE(SUCCEEDED(effect->vtbl->End(effect)));

    effect->release();
}
