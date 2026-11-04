#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "hikari/animation/morph.hpp"
#include "hikari/animation/skeleton.hpp"
#include "hikari/formats/vmd.hpp"

namespace hikari::animation {

class MotionPlayer {
public:
    explicit MotionPlayer(const formats::VmdMotion& motion);

    void evaluate_pose(float frame, Skeleton& skeleton, MorphEvaluator& morphs) const;

    std::uint32_t max_frame = 0;

private:
    std::unordered_map<std::string, std::vector<formats::VmdBoneFrame>> bone_tracks_;
    std::unordered_map<std::string, std::vector<formats::VmdMorphFrame>> morph_tracks_;
};

} // namespace hikari::animation
