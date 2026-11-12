#include "scene.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <utility>

namespace hikari::gfx_lab {
namespace {

std::filesystem::path utf8_path(const std::string& text) {
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

std::vector<std::uint8_t> read_bytes(const std::string& path) {
    std::ifstream file(utf8_path(path), std::ios::binary);
    if (!file) {
        return {};
    }
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(file),
                                     std::istreambuf_iterator<char>());
}

std::wstring to_wide(const std::filesystem::path& path) {
    return path.wstring();
}

} // namespace

bool LabScene::load(hikari::core::D3D11Context& context, const std::string& path,
                    std::string* error) {
    const std::vector<std::uint8_t> bytes = read_bytes(path);
    if (bytes.empty()) {
        if (error != nullptr) {
            *error = "cannot read PMX file: " + path;
        }
        return false;
    }
    try {
        pmx = hikari::formats::PmxModel::parse(bytes);
    } catch (const std::exception& exception) {
        if (error != nullptr) {
            *error = std::string("PMX parse failed: ") + exception.what();
        }
        return false;
    }

    source_path = path;
    name = pmx.info.name_universal.empty() ? pmx.info.name_local : pmx.info.name_universal;
    if (name.empty()) {
        name = utf8_path(path).stem().string();
    }
    mesh = hikari::render::GpuMesh::from_pmx(context, pmx);
    materials.clear();
    materials.reserve(pmx.materials.size());
    for (const hikari::formats::PmxMaterial& material : pmx.materials) {
        materials.push_back(hikari::render::GpuMaterial::from_pmx_material(context, material));
    }

    const std::filesystem::path base = utf8_path(path).parent_path();
    textures.clear();
    textures.reserve(pmx.textures.size());
    for (const std::string& texture_name : pmx.textures) {
        std::optional<hikari::render::GpuTexture> texture;
        if (!texture_name.empty()) {
            const std::filesystem::path texture_path = base / utf8_path(texture_name);
            try {
                texture = hikari::render::GpuTexture::from_file(context, to_wide(texture_path));
            } catch (const std::exception&) {
                texture.reset();
            }
        }
        if (texture) {
            ++loaded_textures;
        }
        textures.push_back(std::move(texture));
    }

    const std::size_t palette_size = std::max<std::size_t>(pmx.bones.size(), 1);
    std::vector<hikari::Mat4> identity_palette(palette_size, hikari::Mat4::identity());
    skinning.update(context, identity_palette);

    bounds_min = hikari::Vec3(std::numeric_limits<float>::max(),
                              std::numeric_limits<float>::max(),
                              std::numeric_limits<float>::max());
    bounds_max = hikari::Vec3(-std::numeric_limits<float>::max(),
                              -std::numeric_limits<float>::max(),
                              -std::numeric_limits<float>::max());
    for (const hikari::formats::PmxVertex& vertex : pmx.vertices) {
        const hikari::Vec3 position = hikari::Vec3::from_array(vertex.position);
        bounds_min.x = std::min(bounds_min.x, position.x);
        bounds_min.y = std::min(bounds_min.y, position.y);
        bounds_min.z = std::min(bounds_min.z, position.z);
        bounds_max.x = std::max(bounds_max.x, position.x);
        bounds_max.y = std::max(bounds_max.y, position.y);
        bounds_max.z = std::max(bounds_max.z, position.z);
    }
    if (pmx.vertices.empty()) {
        bounds_min = hikari::Vec3(-1.0f, 0.0f, -1.0f);
        bounds_max = hikari::Vec3(1.0f, 2.0f, 1.0f);
    }
    return true;
}

LabScene LabScene::synthetic(hikari::core::D3D11Context& context) {
    LabScene scene;
    scene.name = "Synthetic chart";
    scene.bounds_min = hikari::Vec3(-10.0f, 0.0f, -10.0f);
    scene.bounds_max = hikari::Vec3(10.0f, 0.0f, 10.0f);

    hikari::formats::PmxMaterial material;
    material.name_local = "Synthetic";
    material.name_universal = "Synthetic";
    material.diffuse = {0.72f, 0.72f, 0.72f, 1.0f};
    material.specular = {0.35f, 0.35f, 0.35f};
    material.shininess = 12.0f;
    material.ambient = {0.5f, 0.5f, 0.5f};
    scene.pmx.materials.push_back(material);
    scene.materials.push_back(
        hikari::render::GpuMaterial::from_pmx_material(context, scene.pmx.materials.front()));
    scene.textures.emplace_back(std::nullopt);

    std::vector<hikari::render::ModelVertex> vertices(4);
    vertices[0].position = {-10.0f, 0.0f, -10.0f};
    vertices[1].position = {10.0f, 0.0f, -10.0f};
    vertices[2].position = {10.0f, 0.0f, 10.0f};
    vertices[3].position = {-10.0f, 0.0f, 10.0f};
    for (hikari::render::ModelVertex& vertex : vertices) {
        vertex.normal = {0.0f, 1.0f, 0.0f};
        vertex.bone_indices = {0, 0, 0, 0};
        vertex.bone_weights = {1.0f, 0.0f, 0.0f, 0.0f};
    }
    vertices[0].uv = {0.0f, 0.0f};
    vertices[1].uv = {1.0f, 0.0f};
    vertices[2].uv = {1.0f, 1.0f};
    vertices[3].uv = {0.0f, 1.0f};
    const std::array<std::uint32_t, 6> indices = {0, 1, 2, 0, 2, 3};

    D3D11_BUFFER_DESC vertex_desc{};
    vertex_desc.ByteWidth = static_cast<UINT>(vertices.size() * sizeof(hikari::render::ModelVertex));
    vertex_desc.Usage = D3D11_USAGE_IMMUTABLE;
    vertex_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    scene.mesh.vertex_buffer = context.create_buffer(
        vertex_desc, vertices.data(), vertices.size() * sizeof(hikari::render::ModelVertex));

    D3D11_BUFFER_DESC index_desc{};
    index_desc.ByteWidth = static_cast<UINT>(indices.size() * sizeof(std::uint32_t));
    index_desc.Usage = D3D11_USAGE_IMMUTABLE;
    index_desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    scene.mesh.index_buffer = context.create_buffer(
        index_desc, indices.data(), indices.size() * sizeof(std::uint32_t));

    scene.mesh.vertex_count = static_cast<std::uint32_t>(vertices.size());
    scene.mesh.index_count = static_cast<std::uint32_t>(indices.size());
    scene.mesh.subsets.push_back({0, 0, static_cast<std::uint32_t>(indices.size())});
    scene.mesh.base_vertices = vertices;
    scene.mesh.current_vertices = std::move(vertices);
    scene.skinning.update(context, {hikari::Mat4::identity()});
    return scene;
}

LabScene LabScene::phase5_room(hikari::core::D3D11Context& context) {
    LabScene scene;
    scene.name = "Phase 5 room";
    scene.bounds_min = hikari::Vec3(-6.0f, 0.0f, -6.0f);
    scene.bounds_max = hikari::Vec3(6.0f, 8.0f, 6.0f);

    struct Surface {
        hikari::Vec3 color;
        float roughness;
        float metallic;
        hikari::Vec3 emissive;
    };
    const Surface surfaces[] = {
        {{0.52f, 0.55f, 0.60f}, 0.12f, 0.94f, {0.0f, 0.0f, 0.0f}},  // metal floor
        {{0.42f, 0.075f, 0.055f}, 0.72f, 0.0f, {0.0f, 0.0f, 0.0f}},   // -x wall, red
        {{0.055f, 0.11f, 0.40f}, 0.72f, 0.0f, {0.0f, 0.0f, 0.0f}},    // +x wall, blue
        {{0.075f, 0.30f, 0.10f}, 0.72f, 0.0f, {0.0f, 0.0f, 0.0f}},    // -z wall, green
        {{0.36f, 0.30f, 0.075f}, 0.72f, 0.0f, {0.0f, 0.0f, 0.0f}},    // +z wall, yellow
        {{0.045f, 0.045f, 0.055f}, 0.85f, 0.0f, {0.0f, 0.0f, 0.0f}},  // ceiling
        {{0.10f, 0.09f, 0.08f}, 0.45f, 0.0f, {9.0f, 7.0f, 4.0f}},     // emitter
    };
    for (const Surface& surface : surfaces) {
        hikari::formats::PmxMaterial pmx_material;
        pmx_material.diffuse = {surface.color.x, surface.color.y, surface.color.z, 1.0f};
        pmx_material.specular = {0.2f, 0.2f, 0.2f};
        pmx_material.shininess = 8.0f;
        scene.pmx.materials.push_back(pmx_material);
        scene.materials.push_back(
            hikari::render::GpuMaterial::from_pmx_material(context, pmx_material));
        scene.textures.emplace_back(std::nullopt);
        hikari::gfx::MaterialConstants constants =
            hikari::gfx::material_from_pmx(scene.pmx.materials.back());
        constants.specular_roughness = {0.04f, 0.04f, 0.04f, surface.roughness};
        constants.emissive_metallic = {surface.emissive.x, surface.emissive.y,
                                       surface.emissive.z, surface.metallic};
        scene.material_overrides.push_back(constants);
    }

    std::vector<hikari::render::ModelVertex> vertices;
    std::vector<std::uint32_t> indices;
    const auto add_quad = [&](std::size_t material_index, const hikari::Vec3& a,
                              const hikari::Vec3& b, const hikari::Vec3& c,
                              const hikari::Vec3& d, const hikari::Vec3& normal) {
        const std::uint32_t base = static_cast<std::uint32_t>(vertices.size());
        const hikari::Vec3 corners[4] = {a, b, c, d};
        for (int i = 0; i < 4; ++i) {
            hikari::render::ModelVertex vertex;
            vertex.position = corners[i].to_array();
            vertex.normal = normal.to_array();
            vertex.uv = {(i == 1 || i == 2) ? 1.0f : 0.0f, i >= 2 ? 1.0f : 0.0f};
            vertices.push_back(vertex);
        }
        const std::uint32_t quad_indices[6] = {base, base + 1, base + 2,
                                               base, base + 2, base + 3};
        const std::uint32_t start = static_cast<std::uint32_t>(indices.size());
        indices.insert(indices.end(), quad_indices, quad_indices + 6);
        scene.mesh.subsets.push_back({material_index, start, 6});
    };

    add_quad(0, {-6.0f, 0.0f, -6.0f}, {-6.0f, 0.0f, 6.0f}, {6.0f, 0.0f, 6.0f},
             {6.0f, 0.0f, -6.0f}, {0.0f, 1.0f, 0.0f});
    add_quad(5, {-6.0f, 8.0f, -6.0f}, {6.0f, 8.0f, -6.0f}, {6.0f, 8.0f, 6.0f},
             {-6.0f, 8.0f, 6.0f}, {0.0f, -1.0f, 0.0f});
    add_quad(1, {-6.0f, 0.0f, -6.0f}, {-6.0f, 0.0f, 6.0f}, {-6.0f, 8.0f, 6.0f},
             {-6.0f, 8.0f, -6.0f}, {1.0f, 0.0f, 0.0f});
    add_quad(2, {6.0f, 0.0f, -6.0f}, {6.0f, 0.0f, 6.0f}, {6.0f, 8.0f, 6.0f},
             {6.0f, 8.0f, -6.0f}, {-1.0f, 0.0f, 0.0f});
    add_quad(3, {-6.0f, 0.0f, -6.0f}, {6.0f, 0.0f, -6.0f}, {6.0f, 8.0f, -6.0f},
             {-6.0f, 8.0f, -6.0f}, {0.0f, 0.0f, 1.0f});
    add_quad(4, {-6.0f, 0.0f, 6.0f}, {6.0f, 0.0f, 6.0f}, {6.0f, 8.0f, 6.0f},
             {-6.0f, 8.0f, 6.0f}, {0.0f, 0.0f, -1.0f});
    // Emitter panel on the +z wall, framed by the room camera and mirrored by
    // the metal floor.
    add_quad(6, {-2.0f, 2.6f, 5.92f}, {2.0f, 2.6f, 5.92f}, {2.0f, 5.4f, 5.92f},
             {-2.0f, 5.4f, 5.92f}, {0.0f, 0.0f, -1.0f});

    D3D11_BUFFER_DESC vertex_desc{};
    vertex_desc.ByteWidth = static_cast<UINT>(vertices.size() * sizeof(hikari::render::ModelVertex));
    vertex_desc.Usage = D3D11_USAGE_IMMUTABLE;
    vertex_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    scene.mesh.vertex_buffer = context.create_buffer(
        vertex_desc, vertices.data(), vertices.size() * sizeof(hikari::render::ModelVertex));

    D3D11_BUFFER_DESC index_desc{};
    index_desc.ByteWidth = static_cast<UINT>(indices.size() * sizeof(std::uint32_t));
    index_desc.Usage = D3D11_USAGE_IMMUTABLE;
    index_desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    scene.mesh.index_buffer = context.create_buffer(
        index_desc, indices.data(), indices.size() * sizeof(std::uint32_t));

    scene.mesh.vertex_count = static_cast<std::uint32_t>(vertices.size());
    scene.mesh.index_count = static_cast<std::uint32_t>(indices.size());
    scene.pmx.indices = indices;
    scene.mesh.base_vertices = vertices;
    scene.mesh.current_vertices = std::move(vertices);
    scene.skinning.update(context, {hikari::Mat4::identity()});
    return scene;
}

} // namespace hikari::gfx_lab
