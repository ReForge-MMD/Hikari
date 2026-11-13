#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace hikari::gfx::matgen {

enum class SocketType : uint8_t {
    Float = 0,
    Float2,
    Float3,
    Float4,
    Color,
    Int,
    Bool,
    Closure
};

enum class SocketDirection : uint8_t {
    Input = 0,
    Output = 1
};

using SocketValue = std::variant<
    float,
    std::array<float, 2>,
    std::array<float, 3>,
    std::array<float, 4>,
    int32_t,
    bool,
    std::string
>;

struct NodeSocket {
    uint32_t id = 0;
    std::string name;
    std::string display_label;
    SocketType type = SocketType::Float;
    SocketDirection direction = SocketDirection::Input;
    SocketValue default_value = 0.0f;
    SocketValue current_value = 0.0f;
    int32_t connected_link_id = -1;
};

struct NodeLink {
    uint32_t id = 0;
    uint32_t from_node_id = 0;
    uint32_t from_socket_id = 0;
    uint32_t to_node_id = 0;
    uint32_t to_socket_id = 0;
};

enum class NodeType : uint16_t {
    MaterialOutput = 0,
    PrincipledBSDF,
    Emission,
    GlassBSDF,
    ToonBSDF,
    TextureSample,
    HexTilingSample,
    TexCoord,
    UVTransform,
    NoiseTexture,
    VoronoiTexture,
    WaveTexture,
    ColorRamp,
    Mix,
    VectorMath,
    MathScalar,
    NormalMap,
    BumpMap,
    Fresnel,
    SeparateXYZ,
    CombineXYZ,
    ValueConstant,
    ColorConstant
};

std::string_view node_type_name(NodeType type);
NodeType node_type_from_name(std::string_view name);
std::string_view socket_type_name(SocketType type);
SocketType socket_type_from_name(std::string_view name);

struct MaterialNode {
    uint32_t id = 0;
    NodeType type = NodeType::PrincipledBSDF;
    std::string title;
    float pos_x = 0.0f;
    float pos_y = 0.0f;
    std::vector<NodeSocket> inputs;
    std::vector<NodeSocket> outputs;
    int32_t sub_mode = 0;
};

struct GraphValidationResult {
    bool valid = true;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
};

class MaterialGraph {
public:
    MaterialGraph();
    ~MaterialGraph() = default;

    uint32_t add_node(NodeType type, float x = 0.0f, float y = 0.0f);
    bool remove_node(uint32_t node_id);

    uint32_t add_link(uint32_t from_node, uint32_t from_sock, uint32_t to_node, uint32_t to_sock);
    bool remove_link(uint32_t link_id);

    MaterialNode* find_node(uint32_t node_id);
    const MaterialNode* find_node(uint32_t node_id) const;

    NodeSocket* find_socket(uint32_t node_id, uint32_t socket_id, SocketDirection dir);
    const NodeSocket* find_socket(uint32_t node_id, uint32_t socket_id, SocketDirection dir) const;

    const NodeLink* find_link(uint32_t link_id) const;

    GraphValidationResult validate() const;
    std::vector<uint32_t> topological_sort() const;
    std::vector<uint32_t> prune_dead_nodes() const;

    uint64_t compute_graph_hash() const;

    const std::vector<MaterialNode>& nodes() const { return nodes_; }
    const std::vector<NodeLink>& links() const { return links_; }

    uint32_t output_node_id() const { return output_node_id_; }
    void set_output_node_id(uint32_t id) { output_node_id_ = id; }

    void clear();

private:
    uint32_t next_node_id_ = 1;
    uint32_t next_socket_id_ = 1;
    uint32_t next_link_id_ = 1;
    uint32_t output_node_id_ = 0;

    std::vector<MaterialNode> nodes_;
    std::vector<NodeLink> links_;

    void populate_default_sockets(MaterialNode& node);
};

} // namespace hikari::gfx::matgen
