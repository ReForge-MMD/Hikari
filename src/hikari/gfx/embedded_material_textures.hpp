#pragma once

#include <filesystem>
#include <string_view>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include "hikari/core/d3d11.hpp"
#include "hikari/render/texture.hpp"

namespace hikari::gfx {

struct EmbeddedTextureResource {
    std::string_view filename;
    const wchar_t* resource_id;
};

inline constexpr EmbeddedTextureResource kEmbeddedMaterialTextures[] = {
    {"bodyline.gif", L"HIKARI_TEX_BODYLINE"},
    {"bricks_ao.dds", L"HIKARI_TEX_BRICKS_AO"},
    {"bricks_n.dds", L"HIKARI_TEX_BRICKS_N"},
    {"curvature_body.png", L"HIKARI_TEX_CURVATURE_BODY"},
    {"curvature_face.png", L"HIKARI_TEX_CURVATURE_FACE"},
    {"Fabric02_N by 2gou.bmp", L"HIKARI_TEX_FABRIC02_N"},
    {"flow.png", L"HIKARI_TEX_FLOW"},
    {"foam.png", L"HIKARI_TEX_FOAM"},
    {"hair.png", L"HIKARI_TEX_HAIR"},
    {"noise.png", L"HIKARI_TEX_NOISE"},
    {"perlin.png", L"HIKARI_TEX_PERLIN"},
    {"rainbow.gif", L"HIKARI_TEX_RAINBOW"},
    {"README.png", L"HIKARI_TEX_README"},
    {"ripple.png", L"HIKARI_TEX_RIPPLE"},
    {"shift.png", L"HIKARI_TEX_SHIFT"},
    {"shift2.png", L"HIKARI_TEX_SHIFT2"},
    {"shift4.png", L"HIKARI_TEX_SHIFT4"},
    {"Skin_-_Human_Albedo.png", L"HIKARI_TEX_SKIN_HUMAN_ALBEDO"},
    {"Skin_-_Human_Gloss.PNG", L"HIKARI_TEX_SKIN_HUMAN_GLOSS"},
    {"Skin_-_Human_Normal.png", L"HIKARI_TEX_SKIN_HUMAN_NORMAL"},
    {"skin.png", L"HIKARI_TEX_SKIN"},
    {"source.png", L"HIKARI_TEX_SOURCE"},
    {"thickness_body.png", L"HIKARI_TEX_THICKNESS_BODY"},
    {"thickness_face.png", L"HIKARI_TEX_THICKNESS_FACE"},
    {"wave.png", L"HIKARI_TEX_WAVE"},
    {"wetness.png", L"HIKARI_TEX_WETNESS"},
    {"worn_metal.png", L"HIKARI_TEX_WORN_METAL"},
};

inline const wchar_t* find_embedded_material_texture(std::string_view name) {
    for (const auto& entry : kEmbeddedMaterialTextures) {
        if (entry.filename == name) return entry.resource_id;
    }
    return nullptr;
}

inline bool try_load_material_texture(core::D3D11Context& context, std::string_view filename,
                                      render::GpuTexture& out) {
    const wchar_t* res_name = find_embedded_material_texture(filename);
#ifdef _WIN32
    if (res_name != nullptr) {
        if (HRSRC res = FindResourceW(nullptr, res_name, RT_RCDATA)) {
            if (HGLOBAL mem = LoadResource(nullptr, res)) {
                const DWORD size = SizeofResource(nullptr, res);
                const void* data = LockResource(mem);
                if (data != nullptr && size > 0) {
                    try {
                        out = render::GpuTexture::from_memory_mipped(
                            context, static_cast<const std::uint8_t*>(data),
                            static_cast<std::size_t>(size));
                        return true;
                    } catch (...) {
                    }
                }
            }
        }
    }
#endif
    std::filesystem::path fname(filename);
    const std::filesystem::path candidates[] = {
        std::filesystem::path("assets/materials/textures") / fname,
        std::filesystem::path("assets") / fname,
        fname,
    };
    for (const auto& p : candidates) {
        if (std::filesystem::exists(p)) {
            try {
                out = render::GpuTexture::from_file_mipped(context, p.wstring());
                return true;
            } catch (...) {
            }
        }
    }
    return false;
}

} // namespace hikari::gfx
