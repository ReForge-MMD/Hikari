#include <catch2/catch_test_macros.hpp>

#include <windows.h>
#include <objbase.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "hikari/video/video_encoder.hpp"

namespace {

std::array<std::uint8_t, 12> read_header(const std::filesystem::path& path) {
    std::array<std::uint8_t, 12> header{};
    std::ifstream file(path, std::ios::binary);
    file.read(reinterpret_cast<char*>(header.data()),
              static_cast<std::streamsize>(header.size()));
    return header;
}

bool has_text(const std::array<std::uint8_t, 12>& header, std::size_t offset,
              const char* text) {
    for (std::size_t i = 0; i < 4; ++i) {
        if (header[offset + i] != static_cast<std::uint8_t>(text[i])) {
            return false;
        }
    }
    return true;
}

} // namespace

TEST_CASE("RGBA converts to limited-range BT.709", "[video]") {
    constexpr int width = 4;
    constexpr int height = 4;
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(width) * height * 4, 0);
    for (int i = 0; i < width * height; ++i) {
        rgba[static_cast<std::size_t>(i) * 4 + 0] = 255;
        rgba[static_cast<std::size_t>(i) * 4 + 3] = 255;
    }

    std::vector<std::uint8_t> y(static_cast<std::size_t>(width) * height);
    std::vector<std::uint8_t> u(static_cast<std::size_t>(width) * height / 4);
    std::vector<std::uint8_t> v(static_cast<std::size_t>(width) * height / 4);
    hikari::video::rgba_to_yuv420p(rgba.data(), width, height, y.data(), width, u.data(),
                                   width / 2, v.data(), width / 2, width, height);

    for (const std::uint8_t value : y) {
        REQUIRE(value == 63);
    }
    for (const std::uint8_t value : u) {
        REQUIRE(value == 102);
    }
    for (const std::uint8_t value : v) {
        REQUIRE(value == 240);
    }
}

TEST_CASE("RGBA conversion pads odd source sizes", "[video]") {
    constexpr int width = 3;
    constexpr int height = 3;
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(width) * height * 4, 255);
    std::vector<std::uint8_t> y(4 * 4);
    std::vector<std::uint8_t> uv(2 * 2 * 2);
    hikari::video::rgba_to_nv12(rgba.data(), width, height, y.data(), 4, uv.data(), 4, 4, 4);
    for (const std::uint8_t value : y) {
        REQUIRE(value == 235);
    }
    for (const std::uint8_t value : uv) {
        REQUIRE(value == 128);
    }
}

TEST_CASE("Video encoder writes every available container", "[video]") {    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() / "hikari_video_tests";
    std::filesystem::create_directories(directory);

    constexpr int width = 64;
    constexpr int height = 64;
    constexpr int fps = 30;
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(width) * height * 4);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            std::uint8_t* pixel = rgba.data() + (static_cast<std::size_t>(y) * width + x) * 4;
            pixel[0] = static_cast<std::uint8_t>(x * 4);
            pixel[1] = static_cast<std::uint8_t>(y * 4);
            pixel[2] = static_cast<std::uint8_t>((x + y) * 2);
            pixel[3] = 255;
        }
    }
    constexpr int audio_frames = 48'000 / fps;
    std::vector<float> audio(static_cast<std::size_t>(audio_frames) * 2);
    for (int i = 0; i < audio_frames; ++i) {
        const float value =
            0.1f * std::sin(2.0f * 3.14159265f * 440.0f * static_cast<float>(i) / 48'000.0f);
        audio[static_cast<std::size_t>(i) * 2 + 0] = value;
        audio[static_cast<std::size_t>(i) * 2 + 1] = value;
    }

    for (const auto& info : hikari::video::video_formats()) {
        if (!hikari::video::video_format_available(info.format)) {
            WARN("skipping unavailable format " << info.label);
            continue;
        }
        INFO("format: " << info.label);
        const std::filesystem::path path =
            directory / (std::string("hikari_test_") +
                         std::to_string(static_cast<int>(info.format)) + "." + info.extension);

        hikari::video::VideoEncoderSettings settings;
        settings.format = info.format;
        settings.width = width;
        settings.height = height;
        settings.fps = fps;
        settings.video_bitrate = 2'000'000;
        settings.include_audio = true;
        hikari::video::VideoEncoderThread encoder;
        std::string error;
        if (!encoder.open(path.string(), settings, &error)) {
            if (info.format == hikari::video::VideoFormat::Mp4Hevc) {
                WARN("HEVC encoder is not usable on this system: " << error);
                continue;
            }
            FAIL("cannot open " << info.label << ": " << error);
        }
        for (int frame = 0; frame < 5; ++frame) {
            REQUIRE(encoder.write_video_frame(rgba.data(), &error));
            REQUIRE(encoder.write_audio(audio.data(), audio_frames, &error));
        }
        REQUIRE(encoder.finish(&error));
        REQUIRE(std::filesystem::exists(path));
        REQUIRE(std::filesystem::file_size(path) > 0);

        const auto header = read_header(path);
        switch (info.format) {
        case hikari::video::VideoFormat::Mp4H264:
        case hikari::video::VideoFormat::Mp4Hevc:
        case hikari::video::VideoFormat::MovH264:
            REQUIRE(has_text(header, 4, "ftyp"));
            break;
        case hikari::video::VideoFormat::MkvH264:
            REQUIRE(header[0] == 0x1A);
            REQUIRE(header[1] == 0x45);
            REQUIRE(header[2] == 0xDF);
            REQUIRE(header[3] == 0xA3);
            break;
        case hikari::video::VideoFormat::MpegTsH264:
            REQUIRE(header[0] == 0x47);
            break;
        case hikari::video::VideoFormat::AviMpeg4:
            REQUIRE(has_text(header, 0, "RIFF"));
            REQUIRE(has_text(header, 8, "AVI "));
            break;
        }
        std::filesystem::remove(path);
    }
    std::filesystem::remove(directory);
}

TEST_CASE("Threaded encoder works from an STA caller", "[video][sta]") {
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    REQUIRE(SUCCEEDED(com));
    REQUIRE(hikari::video::video_format_available(hikari::video::VideoFormat::Mp4H264));

    hikari::video::VideoEncoderSettings settings;
    settings.format = hikari::video::VideoFormat::Mp4H264;
    settings.width = 64;
    settings.height = 64;
    settings.include_audio = false;
    hikari::video::VideoEncoderThread encoder;
    std::string error;
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "hikari_sta_test.mp4";
    const bool opened = encoder.open(path.string(), settings, &error);
    bool wrote = false;
    if (opened) {
        std::vector<std::uint8_t> rgba(64 * 64 * 4, 255);
        wrote = encoder.write_video_frame(rgba.data(), &error) && encoder.finish(&error);
    }
    CoUninitialize();
    INFO(error);
    REQUIRE(opened);
    REQUIRE(wrote);
    REQUIRE(std::filesystem::file_size(path) > 0);
    std::filesystem::remove(path);
}
