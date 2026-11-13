#include "scene.hpp"

#include <algorithm>
#include <array>
#include <cmath>
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

LabScene LabScene::phase6_room(hikari::core::D3D11Context& context) {
    LabScene scene;
    scene.name = "Phase 6 room";
    scene.bounds_min = hikari::Vec3(-6.0f, 0.0f, -6.0f);
    scene.bounds_max = hikari::Vec3(6.0f, 8.0f, 6.0f);

    struct Surface {
        hikari::Vec3 color;
        float roughness;
        float alpha;
    };
    const Surface surfaces[] = {
        {{0.30f, 0.29f, 0.27f}, 0.88f, 1.0f},  // stone floor
        {{0.34f, 0.33f, 0.30f}, 0.92f, 1.0f},  // walls
        {{0.19f, 0.19f, 0.20f}, 0.95f, 1.0f},  // ceiling
        {{0.58f, 0.55f, 0.48f}, 0.55f, 1.0f},  // pillar
        {{0.30f, 0.36f, 0.40f}, 0.06f, 0.22f},  // glass pane, translucent
    };
    for (const Surface& surface : surfaces) {
        hikari::formats::PmxMaterial pmx_material;
        pmx_material.diffuse = {surface.color.x, surface.color.y, surface.color.z, surface.alpha};
        pmx_material.specular = {0.2f, 0.2f, 0.2f};
        pmx_material.shininess = 8.0f;
        scene.pmx.materials.push_back(pmx_material);
        scene.materials.push_back(
            hikari::render::GpuMaterial::from_pmx_material(context, pmx_material));
        scene.textures.emplace_back(std::nullopt);
        hikari::gfx::MaterialConstants constants =
            hikari::gfx::material_from_pmx(scene.pmx.materials.back());
        constants.specular_roughness = {0.04f, 0.04f, 0.04f, surface.roughness};
        constants.emissive_metallic = {0.0f, 0.0f, 0.0f, 0.0f};
        scene.material_overrides.push_back(constants);
    }

    std::vector<hikari::render::ModelVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::size_t subset_start = 0;
    // Quads of one material accumulate into a single subset, so the tiled walls
    // stay one draw call per material.
    const auto begin_surface = [&] { subset_start = indices.size(); };
    const auto end_surface = [&](std::size_t material_index) {
        const std::uint32_t count = static_cast<std::uint32_t>(indices.size() - subset_start);
        if (count == 0) {
            return;
        }
        scene.mesh.subsets.push_back(
            {material_index, static_cast<std::uint32_t>(subset_start), count});
    };
    const auto add_quad = [&](const hikari::Vec3& a, const hikari::Vec3& b,
                              const hikari::Vec3& c, const hikari::Vec3& d,
                              const hikari::Vec3& normal) {
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
        indices.insert(indices.end(), quad_indices, quad_indices + 6);
    };

    begin_surface();
    add_quad({-6.0f, 0.0f, -6.0f}, {-6.0f, 0.0f, 6.0f}, {6.0f, 0.0f, 6.0f},
             {6.0f, 0.0f, -6.0f}, {0.0f, 1.0f, 0.0f});
    end_surface(0);

    begin_surface();
    add_quad({-6.0f, 8.0f, -6.0f}, {6.0f, 8.0f, -6.0f}, {6.0f, 8.0f, 6.0f},
             {-6.0f, 8.0f, 6.0f}, {0.0f, -1.0f, 0.0f});
    end_surface(2);

    // Window wall at +x. Three openings between y 2.4 and 6.4 pass the sun, and
    // the piers between them keep the rest of the wall in shadow.
    begin_surface();
    add_quad({6.0f, 0.0f, -6.0f}, {6.0f, 0.0f, 6.0f}, {6.0f, 2.4f, 6.0f}, {6.0f, 2.4f, -6.0f},
             {-1.0f, 0.0f, 0.0f});
    add_quad({6.0f, 6.4f, -6.0f}, {6.0f, 6.4f, 6.0f}, {6.0f, 8.0f, 6.0f}, {6.0f, 8.0f, -6.0f},
             {-1.0f, 0.0f, 0.0f});
    add_quad({6.0f, 2.4f, -6.0f}, {6.0f, 2.4f, -3.8f}, {6.0f, 6.4f, -3.8f}, {6.0f, 6.4f, -6.0f},
             {-1.0f, 0.0f, 0.0f});
    add_quad({6.0f, 2.4f, -2.2f}, {6.0f, 2.4f, -0.8f}, {6.0f, 6.4f, -0.8f}, {6.0f, 6.4f, -2.2f},
             {-1.0f, 0.0f, 0.0f});
    add_quad({6.0f, 2.4f, 0.8f}, {6.0f, 2.4f, 2.2f}, {6.0f, 6.4f, 2.2f}, {6.0f, 6.4f, 0.8f},
             {-1.0f, 0.0f, 0.0f});
    add_quad({6.0f, 2.4f, 3.8f}, {6.0f, 2.4f, 6.0f}, {6.0f, 6.4f, 6.0f}, {6.0f, 6.4f, 3.8f},
             {-1.0f, 0.0f, 0.0f});
    add_quad({-6.0f, 0.0f, -6.0f}, {-6.0f, 0.0f, 6.0f}, {-6.0f, 8.0f, 6.0f},
             {-6.0f, 8.0f, -6.0f}, {1.0f, 0.0f, 0.0f});
    add_quad({-6.0f, 0.0f, -6.0f}, {6.0f, 0.0f, -6.0f}, {6.0f, 8.0f, -6.0f}, {-6.0f, 8.0f, -6.0f},
             {0.0f, 0.0f, 1.0f});
    add_quad({-6.0f, 0.0f, 6.0f}, {6.0f, 0.0f, 6.0f}, {6.0f, 8.0f, 6.0f}, {-6.0f, 8.0f, 6.0f},
             {0.0f, 0.0f, -1.0f});
    end_surface(1);

    // Pillar beside the first beam: it breaks that shaft and gives the mist a
    // silhouette, while the three windows stay visible in the camera frame.
    begin_surface();
    const hikari::Vec3 pillar_min(1.0f, 0.0f, -4.0f);
    const hikari::Vec3 pillar_max(2.6f, 5.2f, -2.6f);
    add_quad({pillar_max.x, pillar_min.y, pillar_min.z}, {pillar_max.x, pillar_min.y, pillar_max.z},
             {pillar_max.x, pillar_max.y, pillar_max.z}, {pillar_max.x, pillar_max.y, pillar_min.z},
             {1.0f, 0.0f, 0.0f});
    add_quad({pillar_min.x, pillar_min.y, pillar_min.z}, {pillar_min.x, pillar_min.y, pillar_max.z},
             {pillar_min.x, pillar_max.y, pillar_max.z}, {pillar_min.x, pillar_max.y, pillar_min.z},
             {-1.0f, 0.0f, 0.0f});
    add_quad({pillar_min.x, pillar_min.y, pillar_max.z}, {pillar_max.x, pillar_min.y, pillar_max.z},
             {pillar_max.x, pillar_max.y, pillar_max.z}, {pillar_min.x, pillar_max.y, pillar_max.z},
             {0.0f, 0.0f, 1.0f});
    add_quad({pillar_min.x, pillar_min.y, pillar_min.z}, {pillar_max.x, pillar_min.y, pillar_min.z},
             {pillar_max.x, pillar_max.y, pillar_min.z}, {pillar_min.x, pillar_max.y, pillar_min.z},
             {0.0f, 0.0f, -1.0f});
    add_quad({pillar_min.x, pillar_max.y, pillar_min.z}, {pillar_max.x, pillar_max.y, pillar_min.z},
             {pillar_max.x, pillar_max.y, pillar_max.z}, {pillar_min.x, pillar_max.y, pillar_max.z},
             {0.0f, 1.0f, 0.0f});
    end_surface(3);

    // Glass pane across the middle beam. It is drawn by the transparent pass,
    // which runs after the fog composite, so the shafts behind it must stay
    // visible through the tint instead of being painted over.
    begin_surface();
    add_quad({-1.2f, 0.4f, -2.4f}, {-1.2f, 0.4f, -0.4f}, {-1.2f, 2.0f, -0.4f},
             {-1.2f, 2.0f, -2.4f}, {1.0f, 0.0f, 0.0f});
    end_surface(4);

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

LabScene LabScene::phase7_chart(hikari::core::D3D11Context& context, bool pmx_maps,
                               int shading_model) {
    LabScene scene;
    scene.name = "Phase 7 material chart";
    scene.bounds_min = hikari::Vec3(-5.8f, 0.0f, -2.4f);
    scene.bounds_max = hikari::Vec3(5.8f, 3.4f, 1.4f);
    struct ChartEntry {
        hikari::gfx::ShadingModel model;
        float roughness;
        hikari::Vec3 specular;
        hikari::Vec3 emissive;
        float custom_a;
        float custom_b;
        float alpha;
    };
    // Every sphere shares the same base colour and roughness unless the model
    // itself needs another value, so the visible difference comes from the
    // surface library and not from the material table. Glass is the one entry
    // with alpha below one: it belongs to the transparent pass, which is where
    // the refraction can read the scene copy.
    const ChartEntry entries[] = {
        {hikari::gfx::ShadingModel::Default, 0.35f, {0.30f, 0.30f, 0.30f}, {}, 0.0f, 0.0f, 1.0f},
        {hikari::gfx::ShadingModel::Skin, 0.38f, {0.30f, 0.30f, 0.30f}, {}, 0.0f, 0.0f, 1.0f},
        {hikari::gfx::ShadingModel::Emissive, 0.45f, {0.30f, 0.30f, 0.30f}, {1.6f, 0.75f, 0.25f},
         1.5f, 0.0f, 1.0f},
        {hikari::gfx::ShadingModel::Anisotropy, 0.30f, {0.30f, 0.30f, 0.30f}, {}, 0.75f, 0.0f,
         1.0f},
        {hikari::gfx::ShadingModel::Glass, 0.08f, {0.30f, 0.30f, 0.30f}, {}, 0.0f, 0.0f, 1.0f},
        {hikari::gfx::ShadingModel::Cloth, 0.80f, {0.55f, 0.50f, 0.44f}, {}, 0.60f, 0.0f, 1.0f},
        {hikari::gfx::ShadingModel::ClearCoat, 0.55f, {0.30f, 0.30f, 0.30f}, {}, 1.0f, 0.10f,
         1.0f},
        {hikari::gfx::ShadingModel::Subsurface, 0.45f, {0.30f, 0.30f, 0.30f}, {}, 0.0f, 0.0f,
         1.0f},
        {hikari::gfx::ShadingModel::Cel, 0.50f, {0.30f, 0.30f, 0.30f}, {}, 0.50f, 0.0f, 1.0f},
        {hikari::gfx::ShadingModel::ToneBased, 0.50f, {0.30f, 0.30f, 0.30f}, {}, 0.0f, 0.0f,
         1.0f},
        {hikari::gfx::ShadingModel::Eye, 0.25f, {0.30f, 0.30f, 0.30f}, {}, 0.0f, 0.0f, 1.0f},
    };
    const auto add_material = [&](const hikari::Vec3& color, float roughness,
                                  const hikari::Vec3& specular, const hikari::Vec3& emissive,
                                  float alpha) {
        hikari::formats::PmxMaterial pmx_material;
        pmx_material.diffuse = {color.x, color.y, color.z, alpha};
        pmx_material.specular = {0.25f, 0.25f, 0.25f};
        pmx_material.shininess = 8.0f;
        // Authored ink so the inverted hull outline mode has something to draw.
        // It is bright because the rim lands on the dark backdrop, not on the
        // sphere, and the chart has to show the ink.
        pmx_material.edge_size = 0.022f;
        pmx_material.edge_color = {0.85f, 0.35f, 0.25f, 1.0f};
        scene.pmx.materials.push_back(pmx_material);
        scene.materials.push_back(
            hikari::render::GpuMaterial::from_pmx_material(context, pmx_material));
        scene.textures.emplace_back(std::nullopt);
        hikari::gfx::MaterialConstants constants =
            hikari::gfx::material_from_pmx(scene.pmx.materials.back());
        constants.specular_roughness = {specular.x, specular.y, specular.z, roughness};
        constants.emissive_metallic = {emissive.x, emissive.y, emissive.z, 0.0f};
        return constants;
    };

    const hikari::Vec3 chart_base(0.62f, 0.60f, 0.58f);
    for (const ChartEntry& entry : entries) {
        hikari::gfx::MaterialConstants constants =
            add_material(chart_base, entry.roughness, entry.specular, entry.emissive,
                         entry.alpha);
        constants.custom_data = {entry.custom_a, entry.custom_b, 1.0f,
                                 constants.custom_data[3]};
        hikari::gfx::set_shading_model(constants, static_cast<int>(entry.model));
        scene.material_overrides.push_back(constants);
    }

    // PMX texture slots. A sphere map is a matcap indexed by the view-space
    // normal, so the chart synthesises a lit hemisphere instead of shipping a
    // texture: the highlight sits where the normal points at the light. The
    // toon models read the built-in ramp rows, which is what a PMX material with
    // an internal toon index asks for.
    if (pmx_maps) {
        constexpr int kMatcapSize = 64;
        std::vector<std::uint8_t> matcap(static_cast<std::size_t>(kMatcapSize) * kMatcapSize * 4);
        const hikari::Vec3 matcap_light = hikari::Vec3(0.35f, 0.55f, 0.75f).normalize();
        const auto to_byte = [](float value) {
            return static_cast<std::uint8_t>(
                std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
        };
        for (int y = 0; y < kMatcapSize; ++y) {
            for (int x = 0; x < kMatcapSize; ++x) {
                const float u = (static_cast<float>(x) + 0.5f) / kMatcapSize * 2.0f - 1.0f;
                const float v = (static_cast<float>(y) + 0.5f) / kMatcapSize * 2.0f - 1.0f;
                const float nz = std::sqrt(std::max(0.0f, 1.0f - u * u - v * v));
                const hikari::Vec3 normal(u, -v, nz);
                const float no_l = std::max(0.0f, normal.dot(matcap_light));
                const float rim = (1.0f - nz) * (1.0f - nz);
                const hikari::Vec3 color = hikari::Vec3(0.16f, 0.20f, 0.30f) +
                                           hikari::Vec3(0.90f, 0.95f, 1.00f) * no_l +
                                           hikari::Vec3(0.35f, 0.55f, 0.95f) * rim;
                std::uint8_t* texel =
                    matcap.data() + (static_cast<std::size_t>(y) * kMatcapSize + x) * 4;
                texel[0] = to_byte(color.x);
                texel[1] = to_byte(color.y);
                texel[2] = to_byte(color.z);
                texel[3] = 255;
            }
        }
        scene.textures.emplace_back(hikari::render::GpuTexture::from_rgba_bytes(
            context, kMatcapSize, kMatcapSize, matcap.data()));
        const std::int32_t matcap_index = static_cast<std::int32_t>(scene.textures.size() - 1);
        // Default multiplies the matcap, ClearCoat adds it, and the two toon
        // models take the first two built-in ramps.
        struct PmxSlot {
            std::size_t entry;
            int sphere_mode;
            int toon_slice;
        };
        const PmxSlot slots[] = {{0, 1, 0}, {6, 2, 0}, {8, 0, 1}, {9, 0, 2}};
        for (const PmxSlot& slot : slots) {
            scene.materials[slot.entry].sphere_texture_index = matcap_index;
            scene.material_overrides[slot.entry].pmx_params[0] =
                static_cast<float>(slot.sphere_mode);
            scene.material_overrides[slot.entry].pmx_params[1] =
                static_cast<float>(slot.toon_slice);
        }
    }

    const std::size_t floor_material = scene.pmx.materials.size();
    scene.material_overrides.push_back(add_material({0.11f, 0.11f, 0.12f}, 0.85f,
                                                    {0.04f, 0.04f, 0.04f}, {}, 1.0f));
    const std::size_t backdrop_material = scene.pmx.materials.size();
    scene.material_overrides.push_back(add_material({0.06f, 0.065f, 0.075f}, 0.92f,
                                                    {0.04f, 0.04f, 0.04f}, {}, 1.0f));
    // Stage check for the UV transform. No chart sphere samples a texture by UV,
    // so the transform has nothing to move; this floor checker is the surface
    // that does. It only exists in the PMX map capture, so the regular chart
    // goldens keep the plain floor.
    if (pmx_maps) {
        constexpr int kCheckerSize = 64;
        constexpr int kCheckerCell = 8;
        std::vector<std::uint8_t> checker(static_cast<std::size_t>(kCheckerSize) *
                                          kCheckerSize * 4);
        for (int y = 0; y < kCheckerSize; ++y) {
            for (int x = 0; x < kCheckerSize; ++x) {
                const bool lit_cell = ((x / kCheckerCell) + (y / kCheckerCell)) % 2 == 0;
                std::uint8_t* texel =
                    checker.data() + (static_cast<std::size_t>(y) * kCheckerSize + x) * 4;
                const std::uint8_t value = lit_cell ? 220 : 80;
                texel[0] = value;
                texel[1] = value;
                texel[2] = value;
                texel[3] = 255;
            }
        }
        scene.textures.emplace_back(hikari::render::GpuTexture::from_rgba_bytes(
            context, kCheckerSize, kCheckerSize, checker.data()));
        scene.materials[floor_material].texture_index =
            static_cast<std::int32_t>(scene.textures.size() - 1);
        // Eight tiles across the floor quad, shifted by a quarter tile. The scale
        // and the offset of UvTransform.xy and .zw are both exercised, and the
        // base colour is lifted because the chart floor is authored dark.
        hikari::gfx::MaterialConstants& floor = scene.material_overrides[floor_material];
        floor.base_color = {0.90f, 0.90f, 0.92f, 1.0f};
        floor.uv_transform = {8.0f, 8.0f, 0.25f, 0.25f};
    }
    // Lightbox behind the glass sphere. Refraction can only show what is behind
    // the sheet, and the chart backdrop is almost black, so the glass entry
    // would read as an empty hole without a bright source to bend. Two emissive
    // materials alternate in a checker, because a flat panel hides the bending:
    // a refracted checker shows the magnification and the chromatic split.
    const std::size_t lightbox_material = scene.pmx.materials.size();
    {
        hikari::gfx::MaterialConstants constants =
            add_material({0.90f, 0.72f, 0.42f}, 0.60f, {0.04f, 0.04f, 0.04f},
                         {2.6f, 1.45f, 0.45f}, 1.0f);
        constants.custom_data = {1.0f, 0.0f, 1.0f, constants.custom_data[3]};
        hikari::gfx::set_shading_model(
            constants, static_cast<int>(hikari::gfx::ShadingModel::Emissive));
        scene.material_overrides.push_back(constants);
    }
    const std::size_t lightbox_alt_material = scene.pmx.materials.size();
    {
        hikari::gfx::MaterialConstants constants =
            add_material({0.30f, 0.55f, 0.85f}, 0.60f, {0.04f, 0.04f, 0.04f},
                         {0.35f, 1.05f, 2.30f}, 1.0f);
        constants.custom_data = {1.0f, 0.0f, 1.0f, constants.custom_data[3]};
        hikari::gfx::set_shading_model(
            constants, static_cast<int>(hikari::gfx::ShadingModel::Emissive));
        scene.material_overrides.push_back(constants);
    }

    std::vector<hikari::render::ModelVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::size_t subset_start = 0;
    const auto begin_surface = [&] { subset_start = indices.size(); };
    const auto end_surface = [&](std::size_t material_index) {
        const std::uint32_t count = static_cast<std::uint32_t>(indices.size() - subset_start);
        if (count == 0) {
            return;
        }
        scene.mesh.subsets.push_back(
            {material_index, static_cast<std::uint32_t>(subset_start), count});
    };
    const auto add_quad = [&](const hikari::Vec3& a, const hikari::Vec3& b,
                              const hikari::Vec3& c, const hikari::Vec3& d,
                              const hikari::Vec3& normal) {
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
        indices.insert(indices.end(), quad_indices, quad_indices + 6);
    };
    const auto add_sphere = [&](const hikari::Vec3& center, float radius,
                                std::size_t material_index) {
        constexpr int kStacks = 18;
        constexpr int kSlices = 36;
        constexpr float kPi = 3.14159265358979f;
        const std::uint32_t base = static_cast<std::uint32_t>(vertices.size());
        for (int stack = 0; stack <= kStacks; ++stack) {
            const float v = static_cast<float>(stack) / static_cast<float>(kStacks);
            const float phi = v * kPi;
            for (int slice = 0; slice <= kSlices; ++slice) {
                const float u = static_cast<float>(slice) / static_cast<float>(kSlices);
                const float theta = u * 2.0f * kPi;
                const hikari::Vec3 normal(std::sin(phi) * std::sin(theta), std::cos(phi),
                                          std::sin(phi) * std::cos(theta));
                hikari::render::ModelVertex vertex;
                vertex.position = (center + normal * radius).to_array();
                vertex.normal = normal.to_array();
                vertex.uv = {u, v};
                vertices.push_back(vertex);
            }
        }
        const std::uint32_t start = static_cast<std::uint32_t>(indices.size());
        for (int stack = 0; stack < kStacks; ++stack) {
            for (int slice = 0; slice < kSlices; ++slice) {
                const std::uint32_t row = base + static_cast<std::uint32_t>(stack) *
                                                     (kSlices + 1);
                const std::uint32_t next = row + kSlices + 1;
                const std::uint32_t offset = static_cast<std::uint32_t>(slice);
                indices.push_back(row + offset);
                indices.push_back(next + offset);
                indices.push_back(row + offset + 1);
                indices.push_back(row + offset + 1);
                indices.push_back(next + offset);
                indices.push_back(next + offset + 1);
            }
        }
        scene.mesh.subsets.push_back(
            {material_index, start, static_cast<std::uint32_t>(indices.size()) - start});
    };

    begin_surface();
    add_quad({-10.0f, 0.0f, -16.0f}, {-10.0f, 0.0f, 1.4f}, {10.0f, 0.0f, 1.4f},
             {10.0f, 0.0f, -16.0f}, {0.0f, 1.0f, 0.0f});
    end_surface(floor_material);

    begin_surface();
    add_quad({-10.0f, 0.0f, 1.4f}, {-10.0f, 6.0f, 1.4f}, {10.0f, 6.0f, 1.4f}, {10.0f, 0.0f, 1.4f},
             {0.0f, 0.0f, -1.0f});
    end_surface(backdrop_material);

    // The glass sphere is the fifth entry, so its backlight panel is centred on
    // the line of sight through x = -1. The checker is six by four cells over a
    // panel a little larger than the sphere, so every refracted sample lands on
    // a lit cell and the bending is readable as a shifted pattern.
    {
        constexpr int kColumns = 6;
        constexpr int kRows = 4;
        constexpr float kLeft = -1.62f;
        constexpr float kRight = -0.38f;
        constexpr float kBottom = 0.02f;
        constexpr float kTop = 1.08f;
        constexpr float kCellWidth = (kRight - kLeft) / static_cast<float>(kColumns);
        constexpr float kCellHeight = (kTop - kBottom) / static_cast<float>(kRows);
        for (int row = 0; row < kRows; ++row) {
            for (int column = 0; column < kColumns; ++column) {
                const float x0 = kLeft + kCellWidth * static_cast<float>(column);
                const float x1 = x0 + kCellWidth;
                const float y0 = kBottom + kCellHeight * static_cast<float>(row);
                const float y1 = y0 + kCellHeight;
                begin_surface();
                add_quad({x0, y0, 1.36f}, {x0, y1, 1.36f}, {x1, y1, 1.36f}, {x1, y0, 1.36f},
                         {0.0f, 0.0f, -1.0f});
                end_surface(((row + column) % 2 == 0) ? lightbox_material
                                                      : lightbox_alt_material);
            }
        }
    }

    for (std::size_t i = 0; i < std::size(entries); ++i) {
        // A single-model capture still keeps the sphere at its chart position, so
        // the glass entry stays in front of its lightbox; only the camera moves.
        if (shading_model >= 0 && static_cast<int>(entries[i].model) != shading_model) {
            continue;
        }
        const float x = -5.0f + static_cast<float>(i) * 1.0f;
        begin_surface();
        add_sphere(hikari::Vec3(x, 0.55f, 0.0f), 0.46f, i);
        end_surface(i);
    }

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
