#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "hikari/animation/bezier.hpp"
#include "hikari/core/effect.hpp"
#include "hikari/core/shader.hpp"

using namespace hikari::core;

TEST_CASE("Annotation parser respects quoted semicolons", "[effect]") {
    const auto annotations = parse_annotations_block(
        R"(< string UIName = "Slider; label"; float UIMin = 0.0; float UIMax = 2.5; >)");
    REQUIRE(annotations.at("UIName") == "Slider; label");
    REQUIRE(annotations.at("UIMin") == "0.0");
    REQUIRE(annotations.at("UIMax") == "2.5");
}

TEST_CASE("Technique parser extracts passes and states", "[effect]") {
    const std::string source = R"(
technique ObjectTechnique < string MMDPass = "object"; >
{
    pass P0
    {
        VertexShader = compile vs_5_0 VS_Main();
        PixelShader = compile ps_5_0 PS_Main();
        AlphaBlendEnable = TRUE;
        ZWriteEnable = FALSE;
        FillMode = Wireframe;
    }
}
)";
    const auto techniques = parse_techniques(source);
    REQUIRE(techniques.size() == 1);
    REQUIRE(techniques[0].name == "ObjectTechnique");
    REQUIRE(techniques[0].annotations.at("MMDPass") == "object");
    REQUIRE(techniques[0].passes.size() == 1);
    const auto& pass = techniques[0].passes[0];
    REQUIRE(pass.vs_entry->first == "VS_Main");
    REQUIRE(pass.vs_entry->second == "vs_5_0");
    REQUIRE(pass.ps_entry->first == "PS_Main");
    REQUIRE(pass.blend_preset == BlendPreset::AlphaBlend);
    REQUIRE(pass.depth_preset == DepthPreset::ReadOnlyLessEqual);
    REQUIRE(pass.raster_preset == RasterizerPreset::Wireframe);
}

TEST_CASE("Global variable parser captures annotations and defaults", "[effect]") {
    const std::string source = R"(
float Exposure <
    string UIName = "Exposure";
    float UIMin = 0.1;
    float UIMax = 5.0;
> = 1.0;
)";
    const auto variables = parse_global_variables(source);
    REQUIRE(variables.size() == 1);
    REQUIRE(variables[0].name == "Exposure");
    REQUIRE(variables[0].default_val == "1.0");
    REQUIRE(variables[0].annotations.at("UIName") == "Exposure");
}

TEST_CASE("MME wildcard matching is case insensitive", "[effect]") {
    REQUIRE(hikari::core::match_mme_pattern("*.pmx", "Model.PMX"));
    REQUIRE(hikari::core::match_mme_pattern("hair*", "Hair01.x"));
    REQUIRE(hikari::core::match_mme_pattern("?ody", "body"));
    REQUIRE_FALSE(hikari::core::match_mme_pattern("*.x", "model.pmx"));
    REQUIRE(hikari::core::match_mme_pattern("*", "anything"));
}

TEST_CASE("MMD Bezier evaluator hits its endpoints", "[effect]") {
    const auto& bezier = hikari::animation::eval_mmd_bezier;
    REQUIRE(bezier(20, 20, 107, 107, 0.0f) == Catch::Approx(0.0f));
    REQUIRE(bezier(20, 20, 107, 107, 1.0f) == Catch::Approx(1.0f));
    REQUIRE(bezier(0, 0, 127, 127, 0.5f) == Catch::Approx(0.5f).margin(1.0e-4f));
}
