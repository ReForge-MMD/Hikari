#include "hikari/animation/timeline.hpp"

#include <algorithm>

#include "hikari/animation/bezier.hpp"

namespace hikari::animation {
namespace {

std::pair<Vec3, Quat> sample_bone_track(const std::vector<formats::VmdBoneFrame>& frames, float frame) {
    if (frames.empty()) {
        return {Vec3::zero(), Quat::identity()};
    }
    if (frames.size() == 1 || frame <= static_cast<float>(frames.front().frame)) {
        return {Vec3::from_array(frames.front().position),
                Quat::from_array(frames.front().rotation)};
    }
    const auto& last = frames.back();
    if (frame >= static_cast<float>(last.frame)) {
        return {Vec3::from_array(last.position), Quat::from_array(last.rotation)};
    }

    const auto upper = std::lower_bound(
        frames.begin(), frames.end(), frame,
        [](const formats::VmdBoneFrame& f, float value) { return static_cast<float>(f.frame) < value; });
    if (upper != frames.end() && static_cast<float>(upper->frame) == frame) {
        return {Vec3::from_array(upper->position), Quat::from_array(upper->rotation)};
    }
    if (upper == frames.begin()) {
        return {Vec3::from_array(frames.front().position),
                Quat::from_array(frames.front().rotation)};
    }

    const auto& f0 = *(upper - 1);
    const auto& f1 = *upper;
    const float span = static_cast<float>(f1.frame) - static_cast<float>(f0.frame);
    const float t = span > 0.0f ? (frame - static_cast<float>(f0.frame)) / span : 1.0f;

    const auto cx = unpack_bone_bezier(f1.interpolation, 0);
    const auto cy = unpack_bone_bezier(f1.interpolation, 1);
    const auto cz = unpack_bone_bezier(f1.interpolation, 2);
    const auto cr = unpack_bone_bezier(f1.interpolation, 3);
    const float tx = eval_mmd_bezier(cx[0], cx[1], cx[2], cx[3], t);
    const float ty = eval_mmd_bezier(cy[0], cy[1], cy[2], cy[3], t);
    const float tz = eval_mmd_bezier(cz[0], cz[1], cz[2], cz[3], t);
    const float tr = eval_mmd_bezier(cr[0], cr[1], cr[2], cr[3], t);

    const Vec3 p0 = Vec3::from_array(f0.position);
    const Vec3 p1 = Vec3::from_array(f1.position);
    const Vec3 position(p0.x + (p1.x - p0.x) * tx,
                        p0.y + (p1.y - p0.y) * ty,
                        p0.z + (p1.z - p0.z) * tz);
    const Quat rotation =
        Quat::from_array(f0.rotation).slerp(Quat::from_array(f1.rotation), tr);
    return {position, rotation};
}

float sample_morph_track(const std::vector<formats::VmdMorphFrame>& frames, float frame) {
    if (frames.empty()) {
        return 0.0f;
    }
    if (frames.size() == 1 || frame <= static_cast<float>(frames.front().frame)) {
        return frames.front().weight;
    }
    if (frame >= static_cast<float>(frames.back().frame)) {
        return frames.back().weight;
    }
    const auto upper = std::lower_bound(
        frames.begin(), frames.end(), frame,
        [](const formats::VmdMorphFrame& f, float value) { return static_cast<float>(f.frame) < value; });
    if (upper != frames.end() && static_cast<float>(upper->frame) == frame) {
        return upper->weight;
    }
    if (upper == frames.begin()) {
        return frames.front().weight;
    }
    const auto& f0 = *(upper - 1);
    const auto& f1 = *upper;
    const float span = static_cast<float>(f1.frame) - static_cast<float>(f0.frame);
    const float t = span > 0.0f ? (frame - static_cast<float>(f0.frame)) / span : 1.0f;
    return f0.weight + (f1.weight - f0.weight) * t;
}

} // namespace

MotionPlayer::MotionPlayer(const formats::VmdMotion& motion) {
    for (const auto& frame : motion.bone_frames) {
        max_frame = std::max(max_frame, frame.frame);
        bone_tracks_[frame.name].push_back(frame);
    }
    for (const auto& frame : motion.morph_frames) {
        max_frame = std::max(max_frame, frame.frame);
        morph_tracks_[frame.name].push_back(frame);
    }
    for (auto& [name, track] : bone_tracks_) {
        std::stable_sort(track.begin(), track.end(),
                         [](const auto& a, const auto& b) { return a.frame < b.frame; });
    }
    for (auto& [name, track] : morph_tracks_) {
        std::stable_sort(track.begin(), track.end(),
                         [](const auto& a, const auto& b) { return a.frame < b.frame; });
    }
}

void MotionPlayer::evaluate_pose(float frame, Skeleton& skeleton, MorphEvaluator& morphs) const {
    std::fill(skeleton.local_translations.begin(), skeleton.local_translations.end(), Vec3::zero());
    std::fill(skeleton.local_rotations.begin(), skeleton.local_rotations.end(), Quat::identity());

    for (std::size_t i = 0; i < skeleton.bones.size(); ++i) {
        const auto it = bone_tracks_.find(skeleton.bones[i].name);
        if (it != bone_tracks_.end()) {
            const auto [translation, rotation] = sample_bone_track(it->second, frame);
            skeleton.set_bone_transform(i, translation, rotation);
        }
    }
    skeleton.update_transforms();

    morphs.reset();
    for (const auto& [name, track] : morph_tracks_) {
        morphs.set_weight(name, sample_morph_track(track, frame));
    }
}

} // namespace hikari::animation
