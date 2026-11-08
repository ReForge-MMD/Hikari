#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "hikari/animation/morph.hpp"
#include "hikari/animation/skeleton.hpp"
#include "hikari/formats/vmd.hpp"

namespace hikari::animation {

struct SceneCameraSample {
    float distance = 0.0f;
    std::array<float, 3> position{};
    // MMD Euler angles in radians: x = pitch, y = yaw, z = roll.
    std::array<float, 3> rotation{};
    float fov = 30.0f;
    bool perspective = true;
};

struct SceneLightSample {
    std::array<float, 3> color{};
    std::array<float, 3> direction{};
};

struct SceneShadowSample {
    std::uint8_t mode = 0;
    float distance = 0.0f;
};

struct SceneSample {
    std::optional<SceneCameraSample> camera;
    std::optional<SceneLightSample> light;
    std::optional<SceneShadowSample> shadow;
};

// Which independent documents a VMD carries. Camera/light/shadow form the
// scene motion; bone/morph/IK form the model motion.
struct MotionChannelPresence {
    bool scene = false;
    bool model = false;
};

MotionChannelPresence motion_channels(const formats::VmdMotion& motion);

// Merges the channels present in incoming into existing. Scene channels are
// replaced one by one, while a model motion replaces the whole bone/morph/IK
// group so a file without morph keys does not keep stale morphs.
formats::VmdMotion merge_motion_channels(const formats::VmdMotion& incoming,
                                         const formats::VmdMotion& existing);

class MotionPlayer {
public:
    explicit MotionPlayer(const formats::VmdMotion& motion);

    void evaluate_pose(float frame, Skeleton& skeleton, MorphEvaluator& morphs) const;
    void apply_ik(float frame, Skeleton& skeleton) const;
    std::optional<SceneSample> evaluate_scene(float frame) const;

    std::uint32_t max_frame = 0;

private:
    std::unordered_map<std::string, std::vector<formats::VmdBoneFrame>> bone_tracks_;
    std::unordered_map<std::string, std::vector<formats::VmdMorphFrame>> morph_tracks_;
    std::vector<formats::VmdCameraFrame> camera_track_;
    std::vector<formats::VmdLightFrame> light_track_;
    std::vector<formats::VmdShadowFrame> shadow_track_;
    std::vector<formats::VmdIkFrame> ik_track_;
};

} // namespace hikari::animation
