#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "hikari/gfx/frame_graph.hpp"

TEST_CASE("frame graph accepts a valid chain", "[gfx][frame-graph]") {
    hikari::gfx::FrameGraph graph;
    graph.add_external("Scene");
    graph.add_pass("Geometry", {"Scene"}, {"GBuffer"}, [] {});
    graph.add_pass("Lighting", {"GBuffer"}, {"Hdr"}, [] {});
    graph.add_pass("Tonemap", {"Hdr"}, {"Ldr"}, [] {});
    std::string error;
    REQUIRE(graph.validate(&error));
    REQUIRE(error.empty());
    REQUIRE(graph.passes().size() == 3);
}

TEST_CASE("frame graph rejects reads before a producer", "[gfx][frame-graph]") {
    hikari::gfx::FrameGraph graph;
    graph.add_pass("Lighting", {"GBuffer"}, {"Hdr"}, [] {});
    std::string error;
    REQUIRE_FALSE(graph.validate(&error));
    REQUIRE(error.find("GBuffer") != std::string::npos);
}

TEST_CASE("frame graph rejects duplicate producers and names", "[gfx][frame-graph]") {
    hikari::gfx::FrameGraph graph;
    graph.add_external("Scene");
    graph.add_pass("A", {"Scene"}, {"X"}, [] {});
    graph.add_pass("B", {"Scene"}, {"X"}, [] {});
    std::string error;
    REQUIRE_FALSE(graph.validate(&error));

    hikari::gfx::FrameGraph names;
    names.add_external("Scene");
    names.add_pass("A", {"Scene"}, {"X"}, [] {});
    names.add_pass("A", {"X"}, {"Y"}, [] {});
    REQUIRE_FALSE(names.validate(&error));
}

TEST_CASE("frame graph executes in registration order", "[gfx][frame-graph]") {
    hikari::gfx::FrameGraph graph;
    graph.add_external("Scene");
    std::vector<int> order;
    graph.add_pass("A", {"Scene"}, {"X"}, [&order] { order.push_back(1); });
    graph.add_pass("B", {"X"}, {"Y"}, [&order] { order.push_back(2); });
    graph.add_pass("C", {"Y"}, {"Z"}, [&order] { order.push_back(3); });
    graph.execute();
    REQUIRE(order == std::vector<int>{1, 2, 3});
}
