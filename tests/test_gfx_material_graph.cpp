#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "hikari/gfx/matgen/material_graph.hpp"
#include "hikari/gfx/matgen/material_compiler.hpp"

using namespace hikari::gfx::matgen;

TEST_CASE("material graph construction and manipulation", "[matgen][graph]") {
    MaterialGraph graph;
    REQUIRE(graph.nodes().size() == 1); // MaterialOutput
    REQUIRE(graph.output_node_id() > 0);

    uint32_t bsdf_id = graph.add_node(NodeType::PrincipledBSDF, 100.0f, 100.0f);
    REQUIRE(graph.nodes().size() == 2);
    const MaterialNode* bsdf = graph.find_node(bsdf_id);
    REQUIRE(bsdf != nullptr);
    REQUIRE(bsdf->type == NodeType::PrincipledBSDF);

    const MaterialNode* out_node = graph.find_node(graph.output_node_id());
    REQUIRE(out_node != nullptr);

    // Link BSDF closure to MaterialOutput surface
    REQUIRE(!bsdf->outputs.empty());
    REQUIRE(!out_node->inputs.empty());
    uint32_t link_id = graph.add_link(bsdf_id, bsdf->outputs[0].id, out_node->id, out_node->inputs[0].id);
    REQUIRE(link_id > 0);
    REQUIRE(graph.links().size() == 1);

    // Validation passes
    auto val = graph.validate();
    REQUIRE(val.valid);
    REQUIRE(val.errors.empty());

    // Topological sort puts BSDF before MaterialOutput
    auto sorted = graph.topological_sort();
    REQUIRE(sorted.size() == 2);
    REQUIRE(sorted[0] == bsdf_id);
    REQUIRE(sorted[1] == out_node->id);

    // Dead code pruning removes disconnected nodes
    uint32_t dead_node = graph.add_node(NodeType::NoiseTexture, 0.0f, 0.0f);
    auto active = graph.prune_dead_nodes();
    REQUIRE(active.size() == 2);
    REQUIRE(std::find(active.begin(), active.end(), dead_node) == active.end());
}

TEST_CASE("material graph detects cyclic dependencies", "[matgen][graph]") {
    MaterialGraph graph;
    uint32_t n1 = graph.add_node(NodeType::MathScalar, 0.0f, 0.0f);
    uint32_t n2 = graph.add_node(NodeType::MathScalar, 50.0f, 50.0f);

    MaterialNode* node1 = graph.find_node(n1);
    MaterialNode* node2 = graph.find_node(n2);

    graph.add_link(n1, node1->outputs[0].id, n2, node2->inputs[0].id);
    graph.add_link(n2, node2->outputs[0].id, n1, node1->inputs[0].id);

    auto val = graph.validate();
    REQUIRE_FALSE(val.valid);
    REQUIRE(!val.errors.empty());
}

TEST_CASE("material compiler emits HLSL surface function", "[matgen][compiler]") {
    MaterialGraph graph;
    uint32_t bsdf_id = graph.add_node(NodeType::PrincipledBSDF, 100.0f, 100.0f);
    uint32_t noise_id = graph.add_node(NodeType::NoiseTexture, -100.0f, 50.0f);

    MaterialNode* bsdf = graph.find_node(bsdf_id);
    MaterialNode* noise = graph.find_node(noise_id);
    const MaterialNode* out_node = graph.find_node(graph.output_node_id());

    // Connect noise Fac -> bsdf Roughness
    NodeSocket* rough_in = graph.find_socket(bsdf_id, 0, SocketDirection::Input);
    for (auto& in : bsdf->inputs) {
        if (in.name == "Roughness") {
            rough_in = &in;
            break;
        }
    }
    REQUIRE(rough_in != nullptr);

    graph.add_link(noise_id, noise->outputs[0].id, bsdf_id, rough_in->id);
    graph.add_link(bsdf_id, bsdf->outputs[0].id, out_node->id, out_node->inputs[0].id);

    CompiledMaterialShader compiled = MaterialCompiler::compile(graph);
    REQUIRE(!compiled.hlsl_source.empty());
    REQUIRE(compiled.cache_hash != 0);
    REQUIRE(compiled.hlsl_source.find("EvaluateMaterialSurface") != std::string::npos);
    REQUIRE(compiled.hlsl_source.find("FractalNoise3D") != std::string::npos);
    REQUIRE(compiled.hlsl_source.find("surface.roughness") != std::string::npos);
}
