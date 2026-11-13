#include "hikari/gfx/matgen/material_graph.hpp"

#include <algorithm>
#include <queue>
#include <unordered_set>

namespace hikari::gfx::matgen {

std::string_view node_type_name(NodeType type) {
    switch (type) {
        case NodeType::MaterialOutput: return "MaterialOutput";
        case NodeType::PrincipledBSDF: return "PrincipledBSDF";
        case NodeType::Emission: return "Emission";
        case NodeType::GlassBSDF: return "GlassBSDF";
        case NodeType::ToonBSDF: return "ToonBSDF";
        case NodeType::TextureSample: return "TextureSample";
        case NodeType::HexTilingSample: return "HexTilingSample";
        case NodeType::TexCoord: return "TexCoord";
        case NodeType::UVTransform: return "UVTransform";
        case NodeType::NoiseTexture: return "NoiseTexture";
        case NodeType::VoronoiTexture: return "VoronoiTexture";
        case NodeType::WaveTexture: return "WaveTexture";
        case NodeType::ColorRamp: return "ColorRamp";
        case NodeType::Mix: return "Mix";
        case NodeType::VectorMath: return "VectorMath";
        case NodeType::MathScalar: return "MathScalar";
        case NodeType::NormalMap: return "NormalMap";
        case NodeType::BumpMap: return "BumpMap";
        case NodeType::Fresnel: return "Fresnel";
        case NodeType::SeparateXYZ: return "SeparateXYZ";
        case NodeType::CombineXYZ: return "CombineXYZ";
        case NodeType::ValueConstant: return "ValueConstant";
        case NodeType::ColorConstant: return "ColorConstant";
    }
    return "Unknown";
}

NodeType node_type_from_name(std::string_view name) {
    if (name == "MaterialOutput") return NodeType::MaterialOutput;
    if (name == "PrincipledBSDF") return NodeType::PrincipledBSDF;
    if (name == "Emission") return NodeType::Emission;
    if (name == "GlassBSDF") return NodeType::GlassBSDF;
    if (name == "ToonBSDF") return NodeType::ToonBSDF;
    if (name == "TextureSample") return NodeType::TextureSample;
    if (name == "HexTilingSample") return NodeType::HexTilingSample;
    if (name == "TexCoord") return NodeType::TexCoord;
    if (name == "UVTransform") return NodeType::UVTransform;
    if (name == "NoiseTexture") return NodeType::NoiseTexture;
    if (name == "VoronoiTexture") return NodeType::VoronoiTexture;
    if (name == "WaveTexture") return NodeType::WaveTexture;
    if (name == "ColorRamp") return NodeType::ColorRamp;
    if (name == "Mix") return NodeType::Mix;
    if (name == "VectorMath") return NodeType::VectorMath;
    if (name == "MathScalar") return NodeType::MathScalar;
    if (name == "NormalMap") return NodeType::NormalMap;
    if (name == "BumpMap") return NodeType::BumpMap;
    if (name == "Fresnel") return NodeType::Fresnel;
    if (name == "SeparateXYZ") return NodeType::SeparateXYZ;
    if (name == "CombineXYZ") return NodeType::CombineXYZ;
    if (name == "ValueConstant") return NodeType::ValueConstant;
    if (name == "ColorConstant") return NodeType::ColorConstant;
    return NodeType::PrincipledBSDF;
}

std::string_view socket_type_name(SocketType type) {
    switch (type) {
        case SocketType::Float: return "Float";
        case SocketType::Float2: return "Float2";
        case SocketType::Float3: return "Float3";
        case SocketType::Float4: return "Float4";
        case SocketType::Color: return "Color";
        case SocketType::Int: return "Int";
        case SocketType::Bool: return "Bool";
        case SocketType::Closure: return "Closure";
    }
    return "Float";
}

SocketType socket_type_from_name(std::string_view name) {
    if (name == "Float2") return SocketType::Float2;
    if (name == "Float3") return SocketType::Float3;
    if (name == "Float4") return SocketType::Float4;
    if (name == "Color") return SocketType::Color;
    if (name == "Int") return SocketType::Int;
    if (name == "Bool") return SocketType::Bool;
    if (name == "Closure") return SocketType::Closure;
    return SocketType::Float;
}

MaterialGraph::MaterialGraph() {
    output_node_id_ = add_node(NodeType::MaterialOutput, 400.0f, 200.0f);
}

void MaterialGraph::clear() {
    nodes_.clear();
    links_.clear();
    next_node_id_ = 1;
    next_socket_id_ = 1;
    next_link_id_ = 1;
    output_node_id_ = add_node(NodeType::MaterialOutput, 400.0f, 200.0f);
}

void MaterialGraph::populate_default_sockets(MaterialNode& node) {
    auto add_in = [&](std::string name, SocketType type, SocketValue def) {
        NodeSocket s;
        s.id = next_socket_id_++;
        s.name = std::move(name);
        s.display_label = s.name;
        s.type = type;
        s.direction = SocketDirection::Input;
        s.default_value = def;
        s.current_value = def;
        node.inputs.push_back(s);
    };

    auto add_out = [&](std::string name, SocketType type) {
        NodeSocket s;
        s.id = next_socket_id_++;
        s.name = std::move(name);
        s.display_label = s.name;
        s.type = type;
        s.direction = SocketDirection::Output;
        node.outputs.push_back(s);
    };

    switch (node.type) {
        case NodeType::MaterialOutput:
            node.title = "Material Output";
            add_in("Surface", SocketType::Closure, 0.0f);
            add_in("Alpha", SocketType::Float, 1.0f);
            add_in("Displacement", SocketType::Float3, std::array<float, 3>{0.0f, 0.0f, 0.0f});
            break;
        case NodeType::PrincipledBSDF:
            node.title = "Principled BSDF";
            add_in("Base Color", SocketType::Color, std::array<float, 4>{1.0f, 1.0f, 1.0f, 1.0f});
            add_in("Metallic", SocketType::Float, 0.0f);
            add_in("Roughness", SocketType::Float, 0.5f);
            add_in("Specular", SocketType::Float, 0.5f);
            add_in("Emission", SocketType::Color, std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f});
            add_in("Normal", SocketType::Float3, std::array<float, 3>{0.0f, 0.0f, 1.0f});
            add_in("Alpha", SocketType::Float, 1.0f);
            add_out("BSDF", SocketType::Closure);
            break;
        case NodeType::Emission:
            node.title = "Emission";
            add_in("Color", SocketType::Color, std::array<float, 4>{1.0f, 1.0f, 1.0f, 1.0f});
            add_in("Strength", SocketType::Float, 1.0f);
            add_out("Emission", SocketType::Closure);
            break;
        case NodeType::GlassBSDF:
            node.title = "Glass BSDF";
            add_in("Color", SocketType::Color, std::array<float, 4>{1.0f, 1.0f, 1.0f, 1.0f});
            add_in("Roughness", SocketType::Float, 0.05f);
            add_in("IOR", SocketType::Float, 1.45f);
            add_in("Normal", SocketType::Float3, std::array<float, 3>{0.0f, 0.0f, 1.0f});
            add_out("BSDF", SocketType::Closure);
            break;
        case NodeType::ToonBSDF:
            node.title = "Toon BSDF";
            add_in("Color", SocketType::Color, std::array<float, 4>{1.0f, 1.0f, 1.0f, 1.0f});
            add_in("Shadow Color", SocketType::Color, std::array<float, 4>{0.5f, 0.4f, 0.5f, 1.0f});
            add_in("Threshold", SocketType::Float, 0.5f);
            add_in("Normal", SocketType::Float3, std::array<float, 3>{0.0f, 0.0f, 1.0f});
            add_out("BSDF", SocketType::Closure);
            break;
        case NodeType::TextureSample:
            node.title = "Texture Sample";
            add_in("UV", SocketType::Float2, std::array<float, 2>{0.0f, 0.0f});
            add_out("Color", SocketType::Color);
            add_out("Alpha", SocketType::Float);
            break;
        case NodeType::HexTilingSample:
            node.title = "Hex-Tiling Sample";
            add_in("UV", SocketType::Float2, std::array<float, 2>{0.0f, 0.0f});
            add_in("Tiling Scale", SocketType::Float, 4.0f);
            add_in("Contrast", SocketType::Float, 3.5f);
            add_out("Color", SocketType::Color);
            add_out("Alpha", SocketType::Float);
            break;
        case NodeType::TexCoord:
            node.title = "Texture Coordinate";
            add_out("UV", SocketType::Float2);
            add_out("Normal", SocketType::Float3);
            add_out("Position", SocketType::Float3);
            break;
        case NodeType::UVTransform:
            node.title = "UV Transform";
            add_in("UV", SocketType::Float2, std::array<float, 2>{0.0f, 0.0f});
            add_in("Scale", SocketType::Float2, std::array<float, 2>{1.0f, 1.0f});
            add_in("Offset", SocketType::Float2, std::array<float, 2>{0.0f, 0.0f});
            add_out("UV", SocketType::Float2);
            break;
        case NodeType::NoiseTexture:
            node.title = "Noise Texture";
            add_in("Vector", SocketType::Float3, std::array<float, 3>{0.0f, 0.0f, 0.0f});
            add_in("Scale", SocketType::Float, 5.0f);
            add_in("Detail", SocketType::Float, 2.0f);
            add_in("Roughness", SocketType::Float, 0.5f);
            add_in("Lacunarity", SocketType::Float, 2.0f);
            add_out("Fac", SocketType::Float);
            add_out("Color", SocketType::Color);
            break;
        case NodeType::VoronoiTexture:
            node.title = "Voronoi Texture";
            add_in("Vector", SocketType::Float3, std::array<float, 3>{0.0f, 0.0f, 0.0f});
            add_in("Scale", SocketType::Float, 5.0f);
            add_in("Randomness", SocketType::Float, 1.0f);
            add_in("Smoothness", SocketType::Float, 0.0f);
            add_out("Distance", SocketType::Float);
            add_out("Color", SocketType::Color);
            break;
        case NodeType::WaveTexture:
            node.title = "Wave Texture";
            add_in("Vector", SocketType::Float3, std::array<float, 3>{0.0f, 0.0f, 0.0f});
            add_in("Scale", SocketType::Float, 5.0f);
            add_in("Distortion", SocketType::Float, 0.0f);
            add_out("Color", SocketType::Color);
            add_out("Fac", SocketType::Float);
            break;
        case NodeType::ColorRamp:
            node.title = "Color Ramp";
            add_in("Fac", SocketType::Float, 0.5f);
            add_out("Color", SocketType::Color);
            add_out("Alpha", SocketType::Float);
            break;
        case NodeType::Mix:
            node.title = "Mix";
            add_in("Fac", SocketType::Float, 0.5f);
            add_in("A", SocketType::Color, std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f});
            add_in("B", SocketType::Color, std::array<float, 4>{1.0f, 1.0f, 1.0f, 1.0f});
            add_out("Result", SocketType::Color);
            break;
        case NodeType::VectorMath:
            node.title = "Vector Math";
            add_in("A", SocketType::Float3, std::array<float, 3>{0.0f, 0.0f, 0.0f});
            add_in("B", SocketType::Float3, std::array<float, 3>{0.0f, 0.0f, 0.0f});
            add_out("Vector", SocketType::Float3);
            add_out("Value", SocketType::Float);
            break;
        case NodeType::MathScalar:
            node.title = "Math";
            add_in("A", SocketType::Float, 0.0f);
            add_in("B", SocketType::Float, 0.0f);
            add_out("Value", SocketType::Float);
            break;
        case NodeType::NormalMap:
            node.title = "Normal Map";
            add_in("Strength", SocketType::Float, 1.0f);
            add_in("Color", SocketType::Color, std::array<float, 4>{0.5f, 0.5f, 1.0f, 1.0f});
            add_out("Normal", SocketType::Float3);
            break;
        case NodeType::BumpMap:
            node.title = "Bump";
            add_in("Strength", SocketType::Float, 0.5f);
            add_in("Distance", SocketType::Float, 0.01f);
            add_in("Height", SocketType::Float, 0.0f);
            add_out("Normal", SocketType::Float3);
            break;
        case NodeType::Fresnel:
            node.title = "Fresnel";
            add_in("IOR", SocketType::Float, 1.45f);
            add_in("Normal", SocketType::Float3, std::array<float, 3>{0.0f, 0.0f, 1.0f});
            add_out("Fac", SocketType::Float);
            break;
        case NodeType::SeparateXYZ:
            node.title = "Separate XYZ";
            add_in("Vector", SocketType::Float3, std::array<float, 3>{0.0f, 0.0f, 0.0f});
            add_out("X", SocketType::Float);
            add_out("Y", SocketType::Float);
            add_out("Z", SocketType::Float);
            break;
        case NodeType::CombineXYZ:
            node.title = "Combine XYZ";
            add_in("X", SocketType::Float, 0.0f);
            add_in("Y", SocketType::Float, 0.0f);
            add_in("Z", SocketType::Float, 0.0f);
            add_out("Vector", SocketType::Float3);
            break;
        case NodeType::ValueConstant:
            node.title = "Value";
            add_in("Value", SocketType::Float, 1.0f);
            add_out("Value", SocketType::Float);
            break;
        case NodeType::ColorConstant:
            node.title = "RGB";
            add_in("Color", SocketType::Color, std::array<float, 4>{1.0f, 1.0f, 1.0f, 1.0f});
            add_out("Color", SocketType::Color);
            break;
    }
}

uint32_t MaterialGraph::add_node(NodeType type, float x, float y) {
    MaterialNode node;
    node.id = next_node_id_++;
    node.type = type;
    node.pos_x = x;
    node.pos_y = y;
    populate_default_sockets(node);
    nodes_.push_back(std::move(node));
    return nodes_.back().id;
}

bool MaterialGraph::remove_node(uint32_t node_id) {
    if (node_id == output_node_id_) {
        return false;
    }
    auto it = std::find_if(nodes_.begin(), nodes_.end(),
                           [node_id](const MaterialNode& n) { return n.id == node_id; });
    if (it == nodes_.end()) {
        return false;
    }

    links_.erase(std::remove_if(links_.begin(), links_.end(),
                                [node_id](const NodeLink& l) {
                                    return l.from_node_id == node_id || l.to_node_id == node_id;
                                }),
                 links_.end());

    nodes_.erase(it);
    return true;
}

uint32_t MaterialGraph::add_link(uint32_t from_node, uint32_t from_sock, uint32_t to_node,
                                uint32_t to_sock) {
    if (from_node == to_node) {
        return 0;
    }
    MaterialNode* src = find_node(from_node);
    MaterialNode* dst = find_node(to_node);
    if (!src || !dst) {
        return 0;
    }

    NodeSocket* s_out = find_socket(from_node, from_sock, SocketDirection::Output);
    NodeSocket* s_in = find_socket(to_node, to_sock, SocketDirection::Input);
    if (!s_out || !s_in) {
        return 0;
    }

    // Disconnect existing link to target input socket if present
    if (s_in->connected_link_id >= 0) {
        remove_link(static_cast<uint32_t>(s_in->connected_link_id));
    }

    NodeLink link;
    link.id = next_link_id_++;
    link.from_node_id = from_node;
    link.from_socket_id = from_sock;
    link.to_node_id = to_node;
    link.to_socket_id = to_sock;

    links_.push_back(link);
    s_in->connected_link_id = static_cast<int32_t>(link.id);
    return link.id;
}

bool MaterialGraph::remove_link(uint32_t link_id) {
    auto it = std::find_if(links_.begin(), links_.end(),
                           [link_id](const NodeLink& l) { return l.id == link_id; });
    if (it == links_.end()) {
        return false;
    }

    NodeSocket* s_in = find_socket(it->to_node_id, it->to_socket_id, SocketDirection::Input);
    if (s_in) {
        s_in->connected_link_id = -1;
    }

    links_.erase(it);
    return true;
}

MaterialNode* MaterialGraph::find_node(uint32_t node_id) {
    for (auto& n : nodes_) {
        if (n.id == node_id) return &n;
    }
    return nullptr;
}

const MaterialNode* MaterialGraph::find_node(uint32_t node_id) const {
    for (const auto& n : nodes_) {
        if (n.id == node_id) return &n;
    }
    return nullptr;
}

NodeSocket* MaterialGraph::find_socket(uint32_t node_id, uint32_t socket_id, SocketDirection dir) {
    MaterialNode* node = find_node(node_id);
    if (!node) return nullptr;
    auto& list = (dir == SocketDirection::Input) ? node->inputs : node->outputs;
    for (auto& s : list) {
        if (s.id == socket_id) return &s;
    }
    return nullptr;
}

const NodeSocket* MaterialGraph::find_socket(uint32_t node_id, uint32_t socket_id,
                                            SocketDirection dir) const {
    const MaterialNode* node = find_node(node_id);
    if (!node) return nullptr;
    const auto& list = (dir == SocketDirection::Input) ? node->inputs : node->outputs;
    for (const auto& s : list) {
        if (s.id == socket_id) return &s;
    }
    return nullptr;
}

const NodeLink* MaterialGraph::find_link(uint32_t link_id) const {
    for (const auto& l : links_) {
        if (l.id == link_id) return &l;
    }
    return nullptr;
}

GraphValidationResult MaterialGraph::validate() const {
    GraphValidationResult res;
    if (output_node_id_ == 0 || !find_node(output_node_id_)) {
        res.valid = false;
        res.errors.push_back("Missing active MaterialOutput node");
        return res;
    }

    // Check for cycles using DFS
    std::unordered_map<uint32_t, int> visited; // 0 = unvisited, 1 = visiting, 2 = visited
    bool has_cycle = false;

    auto dfs = [&](auto self, uint32_t node_id) -> void {
        visited[node_id] = 1;
        for (const auto& link : links_) {
            if (link.from_node_id == node_id) {
                if (visited[link.to_node_id] == 1) {
                    has_cycle = true;
                    return;
                }
                if (visited[link.to_node_id] == 0) {
                    self(self, link.to_node_id);
                }
            }
        }
        visited[node_id] = 2;
    };

    for (const auto& node : nodes_) {
        if (visited[node.id] == 0) {
            dfs(dfs, node.id);
            if (has_cycle) {
                res.valid = false;
                res.errors.push_back("Graph contains cyclic dependency loop");
                return res;
            }
        }
    }

    return res;
}

std::vector<uint32_t> MaterialGraph::prune_dead_nodes() const {
    if (output_node_id_ == 0) {
        return {};
    }

    std::unordered_set<uint32_t> reachable;
    std::queue<uint32_t> queue;
    queue.push(output_node_id_);
    reachable.insert(output_node_id_);

    while (!queue.empty()) {
        uint32_t curr = queue.front();
        queue.pop();

        for (const auto& link : links_) {
            if (link.to_node_id == curr) {
                if (reachable.insert(link.from_node_id).second) {
                    queue.push(link.from_node_id);
                }
            }
        }
    }

    std::vector<uint32_t> result(reachable.begin(), reachable.end());
    return result;
}

std::vector<uint32_t> MaterialGraph::topological_sort() const {
    std::vector<uint32_t> active_nodes = prune_dead_nodes();
    std::unordered_set<uint32_t> active_set(active_nodes.begin(), active_nodes.end());

    std::unordered_map<uint32_t, int> in_degree;
    std::unordered_map<uint32_t, std::vector<uint32_t>> adj;

    for (uint32_t id : active_nodes) {
        in_degree[id] = 0;
    }

    for (const auto& link : links_) {
        if (active_set.count(link.from_node_id) && active_set.count(link.to_node_id)) {
            adj[link.from_node_id].push_back(link.to_node_id);
            in_degree[link.to_node_id]++;
        }
    }

    std::queue<uint32_t> q;
    for (uint32_t id : active_nodes) {
        if (in_degree[id] == 0) {
            q.push(id);
        }
    }

    std::vector<uint32_t> order;
    while (!q.empty()) {
        uint32_t u = q.front();
        q.pop();
        order.push_back(u);

        for (uint32_t v : adj[u]) {
            if (--in_degree[v] == 0) {
                q.push(v);
            }
        }
    }

    return order;
}

uint64_t MaterialGraph::compute_graph_hash() const {
    uint64_t hash = 14695981039346656037ULL; // FNV-1a 64-bit basis
    auto fnv = [&](uint64_t val) {
        hash ^= val;
        hash *= 1099511628211ULL;
    };

    std::vector<uint32_t> sorted = topological_sort();
    for (uint32_t nid : sorted) {
        const MaterialNode* node = find_node(nid);
        if (!node) continue;
        fnv(static_cast<uint64_t>(node->type));
        fnv(static_cast<uint64_t>(node->sub_mode));
    }

    for (const auto& link : links_) {
        fnv((static_cast<uint64_t>(link.from_node_id) << 32) | link.from_socket_id);
        fnv((static_cast<uint64_t>(link.to_node_id) << 32) | link.to_socket_id);
    }

    return hash;
}

} // namespace hikari::gfx::matgen
