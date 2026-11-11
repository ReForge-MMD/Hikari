#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "hikari/gfx/ibl.hpp"
#include "hikari/gfx/settings.hpp"
#include "hikari/gfx/sky.hpp"

namespace {

constexpr std::uint32_t kHeaderBytes = 148;

void put_u32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value & 0xffu);
    bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xffu);
    bytes[offset + 2] = static_cast<std::uint8_t>((value >> 16) & 0xffu);
    bytes[offset + 3] = static_cast<std::uint8_t>((value >> 24) & 0xffu);
}

std::uint32_t four_cc(char a, char b, char c, char d) {
    return static_cast<std::uint32_t>(static_cast<std::uint8_t>(a)) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(b)) << 8) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(c)) << 16) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(d)) << 24);
}

// Minimal DDS with a DX10 header, so the tests exercise the same path a real
// file takes instead of a hand built IblImage.
std::vector<std::uint8_t> make_dds(std::uint32_t width, std::uint32_t height,
                                   std::uint32_t dxgi_format, bool cubemap,
                                   const std::vector<std::uint8_t>& pixels) {
    std::vector<std::uint8_t> bytes(kHeaderBytes + pixels.size(), 0);
    bytes[0] = 'D';
    bytes[1] = 'D';
    bytes[2] = 'S';
    bytes[3] = ' ';
    put_u32(bytes, 4, 124);
    put_u32(bytes, 8, 0x1u | 0x2u | 0x4u | 0x1000u);
    put_u32(bytes, 12, height);
    put_u32(bytes, 16, width);
    put_u32(bytes, 28, 1);
    put_u32(bytes, 76, 32);
    put_u32(bytes, 80, 0x4u);
    put_u32(bytes, 84, four_cc('D', 'X', '1', '0'));
    put_u32(bytes, 108, 0x1000u);
    put_u32(bytes, 112, cubemap ? (0x200u | 0xfc00u) : 0u);
    put_u32(bytes, 128, dxgi_format);
    put_u32(bytes, 132, 3);
    put_u32(bytes, 136, cubemap ? 4u : 0u);
    put_u32(bytes, 140, cubemap ? 6u : 1u);
    std::copy(pixels.begin(), pixels.end(), bytes.begin() + kHeaderBytes);
    return bytes;
}

// Legacy block compressed DDS: only the header matters, because the loader must
// reject it before touching pixel data.
std::vector<std::uint8_t> make_block_compressed_dds() {
    std::vector<std::uint8_t> bytes(128, 0);
    bytes[0] = 'D';
    bytes[1] = 'D';
    bytes[2] = 'S';
    bytes[3] = ' ';
    put_u32(bytes, 4, 124);
    put_u32(bytes, 8, 0x1u | 0x2u | 0x4u | 0x1000u);
    put_u32(bytes, 12, 16);
    put_u32(bytes, 16, 32);
    put_u32(bytes, 28, 1);
    put_u32(bytes, 76, 32);
    put_u32(bytes, 80, 0x4u);
    put_u32(bytes, 84, four_cc('D', 'X', 'T', '1'));
    put_u32(bytes, 108, 0x1000u);
    return bytes;
}

std::vector<std::uint8_t> solid_rgba8(std::uint32_t width, std::uint32_t height,
                                      std::uint32_t faces, const std::array<std::uint8_t, 4>& color) {
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * faces * 4);
    for (std::size_t texel = 0; texel < pixels.size() / 4; ++texel) {
        std::copy(color.begin(), color.end(), pixels.begin() + texel * 4);
    }
    return pixels;
}

// Equirectangular map whose red channel is the row index, so latitude survives
// the conversion and longitude cannot leak into the poles.
std::vector<std::uint8_t> latitude_gradient(std::uint32_t width, std::uint32_t height) {
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4, 0);
    for (std::uint32_t y = 0; y < height; ++y) {
        const auto red = static_cast<std::uint8_t>(y * 255u / (height - 1));
        for (std::uint32_t x = 0; x < width; ++x) {
            std::uint8_t* texel = pixels.data() + (static_cast<std::size_t>(y) * width + x) * 4;
            texel[0] = red;
            texel[1] = 0;
            texel[2] = 0;
            texel[3] = 255;
        }
    }
    return pixels;
}

// Four flat longitude bars shifted by an eighth of a turn, so the +X, -X, +Z,
// and -Z face centres land inside a bar and the wrap seam falls inside red.
std::vector<std::uint8_t> longitude_bars(std::uint32_t width, std::uint32_t height) {
    const std::array<std::array<std::uint8_t, 4>, 4> colors = {
        std::array<std::uint8_t, 4>{255, 26, 26, 255},
        std::array<std::uint8_t, 4>{26, 255, 26, 255},
        std::array<std::uint8_t, 4>{26, 26, 255, 255},
        std::array<std::uint8_t, 4>{255, 255, 255, 255},
    };
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4, 0);
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(width);
            const float shifted = u + 0.125f;
            const auto bar = static_cast<std::uint32_t>(
                (shifted - std::floor(shifted)) * 4.0f);
            std::uint8_t* texel = pixels.data() + (static_cast<std::size_t>(y) * width + x) * 4;
            std::copy(colors[bar].begin(), colors[bar].end(), texel);
        }
    }
    return pixels;
}

std::array<float, 4> face_center_color(const hikari::gfx::IblImage& cube, std::uint32_t face) {
    const float* texels = cube.face_data(face);
    REQUIRE(texels != nullptr);
    const std::uint32_t half = cube.width / 2;
    const float* centre =
        texels + (static_cast<std::size_t>(half) * cube.width + half) * 4;
    return {centre[0], centre[1], centre[2], centre[3]};
}

float channel(const std::array<float, 4>& color, std::uint32_t index) {
    return color[index];
}

} // namespace

TEST_CASE("cube face texel directions follow the Direct3D order") {
    const std::array<std::array<float, 3>, 6> expected = {
        std::array<float, 3>{1.0f, 0.0f, 0.0f},  std::array<float, 3>{-1.0f, 0.0f, 0.0f},
        std::array<float, 3>{0.0f, 1.0f, 0.0f},  std::array<float, 3>{0.0f, -1.0f, 0.0f},
        std::array<float, 3>{0.0f, 0.0f, 1.0f},  std::array<float, 3>{0.0f, 0.0f, -1.0f},
    };
    for (std::uint32_t face = 0; face < 6; ++face) {
        const hikari::Vec3 direction = hikari::gfx::ibl_cube_texel_direction(face, 0.5f, 0.5f);
        REQUIRE(direction.x == Catch::Approx(expected[face][0]).margin(1.0e-5f));
        REQUIRE(direction.y == Catch::Approx(expected[face][1]).margin(1.0e-5f));
        REQUIRE(direction.z == Catch::Approx(expected[face][2]).margin(1.0e-5f));
        REQUIRE(direction.length() == Catch::Approx(1.0f).margin(1.0e-5f));
    }
    // v grows downward, so the bottom edge of the +Y face meets the +Z face and
    // the left edge of +Z points at -X.
    const hikari::Vec3 bottom = hikari::gfx::ibl_cube_texel_direction(2, 0.5f, 1.0f);
    REQUIRE(bottom.y == Catch::Approx(0.70710678f).margin(1.0e-4f));
    REQUIRE(bottom.z == Catch::Approx(0.70710678f).margin(1.0e-4f));
    const hikari::Vec3 top = hikari::gfx::ibl_cube_texel_direction(2, 0.5f, 0.0f);
    REQUIRE(top.z == Catch::Approx(-0.70710678f).margin(1.0e-4f));
    const hikari::Vec3 left = hikari::gfx::ibl_cube_texel_direction(4, 0.0f, 0.5f);
    REQUIRE(left.x == Catch::Approx(-0.70710678f).margin(1.0e-4f));
    REQUIRE(left.z == Catch::Approx(0.70710678f).margin(1.0e-4f));
}

TEST_CASE("equirectangular conversion maps latitude to the vertical axis") {
    const std::vector<std::uint8_t> pixels = latitude_gradient(64, 32);
    const std::vector<std::uint8_t> bytes = make_dds(64, 32, 28, false, pixels);
    const hikari::gfx::IblLoadResult loaded =
        hikari::gfx::load_ibl_image_from_memory(bytes.data(), bytes.size());
    REQUIRE(loaded.ok());
    REQUIRE_FALSE(loaded.image.cubemap);
    REQUIRE(loaded.image.width == 64u);
    REQUIRE(loaded.image.height == 32u);

    const hikari::gfx::IblLoadResult converted =
        hikari::gfx::ibl_image_to_cubemap(loaded.image, 16);
    REQUIRE(converted.ok());
    REQUIRE(converted.image.cubemap);
    REQUIRE(converted.image.width == 16u);

    // +Y is the top of the source, -Y the bottom, the sides the middle row.
    REQUIRE(channel(face_center_color(converted.image, 2), 0) < 0.05f);
    REQUIRE(channel(face_center_color(converted.image, 3), 0) > 0.95f);
    for (const std::uint32_t face : {0u, 1u, 4u, 5u}) {
        const std::array<float, 4> color = face_center_color(converted.image, face);
        REQUIRE(channel(color, 0) == Catch::Approx(0.5f).margin(0.03f));
        REQUIRE(channel(color, 1) == Catch::Approx(0.0f).margin(1.0e-3f));
        REQUIRE(channel(color, 2) == Catch::Approx(0.0f).margin(1.0e-3f));
        REQUIRE(channel(color, 3) == Catch::Approx(1.0f).margin(1.0e-3f));
    }
}

TEST_CASE("equirectangular conversion maps longitude to the four face centres") {
    const std::vector<std::uint8_t> pixels = longitude_bars(64, 32);
    const std::vector<std::uint8_t> bytes = make_dds(64, 32, 28, false, pixels);
    const hikari::gfx::IblLoadResult loaded =
        hikari::gfx::load_ibl_image_from_memory(bytes.data(), bytes.size());
    REQUIRE(loaded.ok());
    const hikari::gfx::IblLoadResult converted =
        hikari::gfx::ibl_image_to_cubemap(loaded.image, 16);
    REQUIRE(converted.ok());

    const std::array<std::array<float, 4>, 4> expected = {
        std::array<float, 4>{1.0f, 26.0f / 255.0f, 26.0f / 255.0f, 1.0f},
        std::array<float, 4>{26.0f / 255.0f, 1.0f, 26.0f / 255.0f, 1.0f},
        std::array<float, 4>{26.0f / 255.0f, 26.0f / 255.0f, 1.0f, 1.0f},
        std::array<float, 4>{1.0f, 1.0f, 1.0f, 1.0f},
    };
    // -Z red, -X green, +Z blue, +X white under the reference longitude mapping.
    const std::array<std::uint32_t, 4> faces = {5u, 1u, 4u, 0u};
    for (std::size_t index = 0; index < faces.size(); ++index) {
        const std::array<float, 4> color = face_center_color(converted.image, faces[index]);
        for (std::uint32_t component = 0; component < 4; ++component) {
            INFO("face " << faces[index] << " channel " << component);
            REQUIRE(channel(color, component) ==
                    Catch::Approx(expected[index][component]).margin(0.02f));
        }
    }
}

TEST_CASE("a cubemap source keeps each face on its own axis") {
    std::vector<std::uint8_t> pixels;
    for (std::uint32_t face = 0; face < 6; ++face) {
        const std::array<std::uint8_t, 4> color = {
            static_cast<std::uint8_t>(40u * face), static_cast<std::uint8_t>(255u - 40u * face),
            static_cast<std::uint8_t>(20u + 30u * face), 255};
        const std::vector<std::uint8_t> face_pixels = solid_rgba8(32, 32, 1, color);
        pixels.insert(pixels.end(), face_pixels.begin(), face_pixels.end());
    }
    const std::vector<std::uint8_t> bytes = make_dds(32, 32, 28, true, pixels);
    const hikari::gfx::IblLoadResult loaded =
        hikari::gfx::load_ibl_image_from_memory(bytes.data(), bytes.size());
    REQUIRE(loaded.ok());
    REQUIRE(loaded.image.cubemap);

    const hikari::gfx::IblLoadResult converted =
        hikari::gfx::ibl_image_to_cubemap(loaded.image, 16);
    REQUIRE(converted.ok());
    for (std::uint32_t face = 0; face < 6; ++face) {
        const std::array<float, 4> color = face_center_color(converted.image, face);
        INFO("face " << face);
        REQUIRE(channel(color, 0) == Catch::Approx(40.0f * face / 255.0f).margin(1.0e-3f));
        REQUIRE(channel(color, 1) ==
                Catch::Approx((255.0f - 40.0f * face) / 255.0f).margin(1.0e-3f));
        REQUIRE(channel(color, 2) == Catch::Approx((20.0f + 30.0f * face) / 255.0f).margin(1.0e-3f));
    }
}

TEST_CASE("a constant environment stays constant through the conversion") {
    const std::vector<std::uint8_t> pixels =
        solid_rgba8(32, 16, 1, {128, 64, 32, 255});
    const std::vector<std::uint8_t> bytes = make_dds(32, 16, 28, false, pixels);
    const hikari::gfx::IblLoadResult loaded =
        hikari::gfx::load_ibl_image_from_memory(bytes.data(), bytes.size());
    REQUIRE(loaded.ok());
    const hikari::gfx::IblLoadResult converted =
        hikari::gfx::ibl_image_to_cubemap(loaded.image, 16);
    REQUIRE(converted.ok());
    for (const float value : converted.image.rgba) {
        REQUIRE(std::isfinite(value));
    }
    for (std::uint32_t face = 0; face < 6; ++face) {
        REQUIRE(channel(face_center_color(converted.image, face), 0) ==
                Catch::Approx(128.0f / 255.0f).margin(1.0e-3f));
        REQUIRE(channel(face_center_color(converted.image, face), 1) ==
                Catch::Approx(64.0f / 255.0f).margin(1.0e-3f));
        REQUIRE(channel(face_center_color(converted.image, face), 2) ==
                Catch::Approx(32.0f / 255.0f).margin(1.0e-3f));
    }
}

namespace {

std::vector<std::uint8_t> half_pixels(std::uint32_t width, std::uint32_t height,
                                      std::uint16_t red, std::uint16_t green,
                                      std::uint16_t blue, std::uint16_t alpha) {
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 8, 0);
    for (std::size_t texel = 0; texel < static_cast<std::size_t>(width) * height; ++texel) {
        const std::uint16_t values[4] = {red, green, blue, alpha};
        for (std::uint32_t channel_index = 0; channel_index < 4; ++channel_index) {
            pixels[texel * 8 + channel_index * 2] =
                static_cast<std::uint8_t>(values[channel_index] & 0xffu);
            pixels[texel * 8 + channel_index * 2 + 1] =
                static_cast<std::uint8_t>(values[channel_index] >> 8);
        }
    }
    return pixels;
}

} // namespace

TEST_CASE("half float DDS texels decode to their float values") {
    const std::vector<std::uint8_t> pixels = half_pixels(32, 16, 0x3c00, 0x3800, 0x0001, 0x3c00);
    const std::vector<std::uint8_t> bytes = make_dds(32, 16, 10, false, pixels);
    const hikari::gfx::IblLoadResult loaded =
        hikari::gfx::load_ibl_image_from_memory(bytes.data(), bytes.size());
    REQUIRE(loaded.ok());
    const float* texel = loaded.image.rgba.data();
    REQUIRE(texel[0] == Catch::Approx(1.0f).margin(1.0e-3f));
    REQUIRE(texel[1] == Catch::Approx(0.5f).margin(1.0e-3f));
    // 0x0001 is the smallest subnormal, 2^-24.
    REQUIRE(texel[2] == Catch::Approx(5.96046e-8f).epsilon(1.0e-3f));
    REQUIRE(texel[3] == Catch::Approx(1.0f).margin(1.0e-3f));

    const std::vector<std::uint8_t> extremes =
        half_pixels(32, 16, 0xc000, 0x7bff, 0x0000, 0x3c00);
    const std::vector<std::uint8_t> extreme_bytes = make_dds(32, 16, 10, false, extremes);
    const hikari::gfx::IblLoadResult extreme_loaded =
        hikari::gfx::load_ibl_image_from_memory(extreme_bytes.data(), extreme_bytes.size());
    REQUIRE(extreme_loaded.ok());
    REQUIRE(extreme_loaded.image.rgba[0] == Catch::Approx(-2.0f).margin(1.0e-3f));
    REQUIRE(extreme_loaded.image.rgba[1] == Catch::Approx(65504.0f).margin(0.5f));
    REQUIRE(extreme_loaded.image.rgba[2] == Catch::Approx(0.0f).margin(1.0e-6f));
}

TEST_CASE("invalid environment sources are rejected with a reason") {
    SECTION("missing data") {
        const hikari::gfx::IblLoadResult result = hikari::gfx::load_ibl_image_from_memory(nullptr, 0);
        REQUIRE_FALSE(result.ok());
        REQUIRE(result.error.find("too small") != std::string::npos);
    }
    SECTION("unknown container") {
        const std::string text = "this is not an environment image";
        const hikari::gfx::IblLoadResult result =
            hikari::gfx::load_ibl_image_from_memory(text.data(), text.size());
        REQUIRE_FALSE(result.ok());
        REQUIRE(result.error.find("unsupported image container") != std::string::npos);
    }
    SECTION("block compression") {
        const std::vector<std::uint8_t> bytes = make_block_compressed_dds();
        const hikari::gfx::IblLoadResult result =
            hikari::gfx::load_ibl_image_from_memory(bytes.data(), bytes.size());
        REQUIRE_FALSE(result.ok());
        REQUIRE(result.error.find("block compressed") != std::string::npos);
    }
    SECTION("unknown dxgi format") {
        const std::vector<std::uint8_t> bytes = make_dds(32, 16, 71, false, {});
        const hikari::gfx::IblLoadResult result =
            hikari::gfx::load_ibl_image_from_memory(bytes.data(), bytes.size());
        REQUIRE_FALSE(result.ok());
        REQUIRE(result.error.find("not supported") != std::string::npos);
    }
    SECTION("dimension ceiling") {
        const std::vector<std::uint8_t> bytes = make_dds(8192, 4096, 28, false, {});
        const hikari::gfx::IblLoadResult result =
            hikari::gfx::load_ibl_image_from_memory(bytes.data(), bytes.size());
        REQUIRE_FALSE(result.ok());
        REQUIRE(result.error.find("4096") != std::string::npos);
    }
    SECTION("texel budget") {
        const std::vector<std::uint8_t> bytes = make_dds(4096, 2048, 28, true, {});
        const hikari::gfx::IblLoadResult result =
            hikari::gfx::load_ibl_image_from_memory(bytes.data(), bytes.size());
        REQUIRE_FALSE(result.ok());
        REQUIRE(result.error.find("texel budget") != std::string::npos);
    }
    SECTION("aspect ratio") {
        const std::vector<std::uint8_t> pixels = solid_rgba8(16, 16, 1, {255, 255, 255, 255});
        const std::vector<std::uint8_t> bytes = make_dds(16, 16, 28, false, pixels);
        const hikari::gfx::IblLoadResult result =
            hikari::gfx::load_ibl_image_from_memory(bytes.data(), bytes.size());
        REQUIRE_FALSE(result.ok());
        REQUIRE(result.error.find("2:1") != std::string::npos);
    }
    SECTION("truncated pixels") {
        std::vector<std::uint8_t> pixels = solid_rgba8(32, 16, 1, {255, 255, 255, 255});
        pixels.resize(pixels.size() / 2);
        const std::vector<std::uint8_t> bytes = make_dds(32, 16, 28, false, pixels);
        const hikari::gfx::IblLoadResult result =
            hikari::gfx::load_ibl_image_from_memory(bytes.data(), bytes.size());
        REQUIRE_FALSE(result.ok());
        REQUIRE(result.error.find("truncated") != std::string::npos);
    }
    SECTION("missing file") {
        const hikari::gfx::IblLoadResult result =
            hikari::gfx::load_ibl_image("no such environment.hdr");
        REQUIRE_FALSE(result.ok());
        REQUIRE(result.error.find("cannot open") != std::string::npos);
    }
}

TEST_CASE("cubemap conversion validates its arguments") {
    const std::vector<std::uint8_t> pixels = solid_rgba8(32, 16, 1, {255, 255, 255, 255});
    const std::vector<std::uint8_t> bytes = make_dds(32, 16, 28, false, pixels);
    const hikari::gfx::IblLoadResult loaded =
        hikari::gfx::load_ibl_image_from_memory(bytes.data(), bytes.size());
    REQUIRE(loaded.ok());

    const hikari::gfx::IblLoadResult too_small =
        hikari::gfx::ibl_image_to_cubemap(loaded.image, 8);
    REQUIRE_FALSE(too_small.ok());
    REQUIRE(too_small.error.find("16..4096") != std::string::npos);

    const hikari::gfx::IblLoadResult too_large =
        hikari::gfx::ibl_image_to_cubemap(loaded.image, 8192);
    REQUIRE_FALSE(too_large.ok());

    hikari::gfx::IblImage empty;
    const hikari::gfx::IblLoadResult no_pixels = hikari::gfx::ibl_image_to_cubemap(empty, 16);
    REQUIRE_FALSE(no_pixels.ok());
    REQUIRE(no_pixels.error.find("no pixels") != std::string::npos);

    hikari::gfx::IblImage damaged = loaded.image;
    damaged.rgba.pop_back();
    const hikari::gfx::IblLoadResult mismatch =
        hikari::gfx::ibl_image_to_cubemap(damaged, 16);
    REQUIRE_FALSE(mismatch.ok());
    REQUIRE(mismatch.error.find("does not match") != std::string::npos);
}

TEST_CASE("custom ibl settings clamp to usable ranges") {
    hikari::gfx::GraphicsSettings settings;
    settings.ibl.rotation_degrees = 400.0f;
    settings.ibl.intensity = -3.0f;
    settings.validate();
    REQUIRE(settings.ibl.rotation_degrees == Catch::Approx(180.0f));
    REQUIRE(settings.ibl.intensity == Catch::Approx(0.0f));

    settings.ibl.rotation_degrees = -400.0f;
    settings.ibl.intensity = 42.0f;
    settings.validate();
    REQUIRE(settings.ibl.rotation_degrees == Catch::Approx(-180.0f));
    REQUIRE(settings.ibl.intensity == Catch::Approx(10.0f));
}