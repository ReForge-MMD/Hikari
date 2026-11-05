#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "hikari/formats/pmx.hpp"

namespace hikari::formats {

struct VmdBoneFrame {
    std::string name;
    std::uint32_t frame = 0;
    std::array<float, 3> position{};
    std::array<float, 4> rotation{0.0f, 0.0f, 0.0f, 1.0f};
    std::array<std::uint8_t, 64> interpolation{};
};

struct VmdMorphFrame {
    std::string name;
    std::uint32_t frame = 0;
    float weight = 0.0f;
};

struct VmdCameraFrame {
    std::uint32_t frame = 0;
    float distance = 0.0f;
    std::array<float, 3> position{};
    std::array<float, 3> rotation{};
    std::array<std::uint8_t, 24> interpolation{};
    std::uint32_t fov = 30;
    bool perspective = true;
};

struct VmdLightFrame {
    std::uint32_t frame = 0;
    std::array<float, 3> color{};
    std::array<float, 3> direction{};
};

struct VmdShadowFrame {
    std::uint32_t frame = 0;
    std::uint8_t mode = 0;
    float distance = 0.0f;
};

struct VmdIkSwitch {
    std::string name;
    bool enabled = true;
};

struct VmdIkFrame {
    std::uint32_t frame = 0;
    bool show = true;
    std::vector<VmdIkSwitch> ik_switches;
};

struct VmdMotion {
    std::uint32_t version = 2;
    std::string model_name;
    std::vector<VmdBoneFrame> bone_frames;
    std::vector<VmdMorphFrame> morph_frames;
    std::vector<VmdCameraFrame> camera_frames;
    std::vector<VmdLightFrame> light_frames;
    std::vector<VmdShadowFrame> shadow_frames;
    std::vector<VmdIkFrame> ik_frames;

    static VmdMotion parse(const std::vector<std::uint8_t>& bytes);
    std::vector<std::uint8_t> to_bytes(const std::string& model_name) const;
};

struct VpdBonePose {
    std::uint32_t index = 0;
    std::string name;
    std::array<float, 3> translation{};
    std::array<float, 4> rotation{0.0f, 0.0f, 0.0f, 1.0f};
};

struct VpdMorphPose {
    std::uint32_t index = 0;
    std::string name;
    float weight = 0.0f;
};

struct VpdPose {
    std::string model_name;
    std::vector<VpdBonePose> bones;
    std::vector<VpdMorphPose> morphs;

    static VpdPose from_bytes(const std::vector<std::uint8_t>& bytes);
    static VpdPose parse(const std::string& content);
    std::string to_vpd_string() const;
};

} // namespace hikari::formats
