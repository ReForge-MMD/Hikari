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

std::pair<const formats::VmdCameraFrame*, const formats::VmdCameraFrame*> camera_segment(
    const std::vector<formats::VmdCameraFrame>& frames, float frame) {
    const auto upper = std::lower_bound(
        frames.begin(), frames.end(), frame,
        [](const formats::VmdCameraFrame& f, float value) {
            return static_cast<float>(f.frame) < value;
        });
    if (upper == frames.end()) {
        return {&frames.back(), &frames.back()};
    }
    if (upper == frames.begin()) {
        return {&*upper, &*upper};
    }
    return {&*(upper - 1), &*upper};
}

float segment_t(std::uint32_t a, std::uint32_t b, float frame) {
    const float span = static_cast<float>(b) - static_cast<float>(a);
    return span > 0.0f ? std::clamp((frame - static_cast<float>(a)) / span, 0.0f, 1.0f) : 0.0f;
}

} // namespace

MotionChannelPresence motion_channels(const formats::VmdMotion& motion) {
    MotionChannelPresence presence;
    presence.scene = !motion.camera_frames.empty() || !motion.light_frames.empty() ||
                     !motion.shadow_frames.empty();
    presence.model = !motion.bone_frames.empty() || !motion.morph_frames.empty() ||
                     !motion.ik_frames.empty();
    return presence;
}

formats::VmdMotion merge_motion_channels(const formats::VmdMotion& incoming,
                                         const formats::VmdMotion& existing) {
    formats::VmdMotion merged = existing;
    if (!incoming.camera_frames.empty()) {
        merged.camera_frames = incoming.camera_frames;
    }
    if (!incoming.light_frames.empty()) {
        merged.light_frames = incoming.light_frames;
    }
    if (!incoming.shadow_frames.empty()) {
        merged.shadow_frames = incoming.shadow_frames;
    }
    if (motion_channels(incoming).model) {
        merged.bone_frames = incoming.bone_frames;
        merged.morph_frames = incoming.morph_frames;
        merged.ik_frames = incoming.ik_frames;
        merged.model_name = incoming.model_name;
    }
    return merged;
}

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
    camera_track_ = motion.camera_frames;
    light_track_ = motion.light_frames;
    shadow_track_ = motion.shadow_frames;
    ik_track_ = motion.ik_frames;
    for (const auto& frame : camera_track_) {
        max_frame = std::max(max_frame, frame.frame);
    }
    for (const auto& frame : light_track_) {
        max_frame = std::max(max_frame, frame.frame);
    }
    for (const auto& frame : shadow_track_) {
        max_frame = std::max(max_frame, frame.frame);
    }
    for (const auto& frame : ik_track_) {
        max_frame = std::max(max_frame, frame.frame);
    }
    const auto by_frame = [](const auto& a, const auto& b) { return a.frame < b.frame; };
    std::stable_sort(camera_track_.begin(), camera_track_.end(), by_frame);
    std::stable_sort(light_track_.begin(), light_track_.end(), by_frame);
    std::stable_sort(shadow_track_.begin(), shadow_track_.end(), by_frame);
    std::stable_sort(ik_track_.begin(), ik_track_.end(), by_frame);
}

void MotionPlayer::apply_ik(float frame, Skeleton& skeleton) const {
    if (ik_track_.empty()) {
        return;
    }
    if (skeleton.ik_enabled.size() != skeleton.bones.size()) {
        skeleton.ik_enabled.assign(skeleton.bones.size(), true);
    }
    const auto upper = std::upper_bound(
        ik_track_.begin(), ik_track_.end(), frame,
        [](float value, const formats::VmdIkFrame& key) {
            return value < static_cast<float>(key.frame);
        });
    const formats::VmdIkFrame& key = upper == ik_track_.begin() ? ik_track_.front() : *(upper - 1);
    for (const auto& sw : key.ik_switches) {
        const auto index = skeleton.find_bone_index(sw.name);
        if (index && *index < skeleton.ik_enabled.size()) {
            skeleton.ik_enabled[*index] = sw.enabled;
        }
    }
}

std::optional<SceneSample> MotionPlayer::evaluate_scene(float frame) const {
    if (camera_track_.empty() && light_track_.empty() && shadow_track_.empty()) {
        return std::nullopt;
    }
    SceneSample result;
    if (!camera_track_.empty()) {
        const auto [first, second] = camera_segment(camera_track_, frame);
        const float t = first == second ? 0.0f : segment_t(first->frame, second->frame, frame);
        const auto mix = [t](float a, float b) { return a + (b - a) * t; };
        SceneCameraSample camera;
        camera.distance = mix(first->distance, second->distance);
        for (std::size_t i = 0; i < 3; ++i) {
            camera.position[i] = mix(first->position[i], second->position[i]);
            camera.rotation[i] = mix(first->rotation[i], second->rotation[i]);
        }
        camera.fov = mix(static_cast<float>(first->fov), static_cast<float>(second->fov));
        camera.perspective = first->perspective;
        result.camera = camera;
    }
    if (!light_track_.empty()) {
        const auto upper = std::lower_bound(
            light_track_.begin(), light_track_.end(), frame,
            [](const formats::VmdLightFrame& f, float value) {
                return static_cast<float>(f.frame) < value;
            });
        const formats::VmdLightFrame* first = nullptr;
        const formats::VmdLightFrame* second = nullptr;
        if (upper == light_track_.end()) {
            first = second = &light_track_.back();
        } else if (upper == light_track_.begin()) {
            first = second = &*upper;
        } else {
            first = &*(upper - 1);
            second = &*upper;
        }
        const float t = first == second ? 0.0f : segment_t(first->frame, second->frame, frame);
        SceneLightSample light;
        for (std::size_t i = 0; i < 3; ++i) {
            light.color[i] = first->color[i] + (second->color[i] - first->color[i]) * t;
            light.direction[i] =
                first->direction[i] + (second->direction[i] - first->direction[i]) * t;
        }
        result.light = light;
    }
    if (!shadow_track_.empty()) {
        const auto upper = std::lower_bound(
            shadow_track_.begin(), shadow_track_.end(), frame,
            [](const formats::VmdShadowFrame& f, float value) {
                return static_cast<float>(f.frame) < value;
            });
        const formats::VmdShadowFrame* first = nullptr;
        const formats::VmdShadowFrame* second = nullptr;
        if (upper == shadow_track_.end()) {
            first = second = &shadow_track_.back();
        } else if (upper == shadow_track_.begin()) {
            first = second = &*upper;
        } else {
            first = &*(upper - 1);
            second = &*upper;
        }
        const float t = first == second ? 0.0f : segment_t(first->frame, second->frame, frame);
        SceneShadowSample shadow;
        shadow.mode = t < 0.5f ? first->mode : second->mode;
        shadow.distance = first->distance + (second->distance - first->distance) * t;
        result.shadow = shadow;
    }
    return result;
}

void MotionPlayer::evaluate_pose(float frame, Skeleton& skeleton, MorphEvaluator& morphs) const {
    std::fill(skeleton.local_translations.begin(), skeleton.local_translations.end(), Vec3::zero());
    std::fill(skeleton.local_rotations.begin(), skeleton.local_rotations.end(), Quat::identity());

    std::vector<const std::vector<formats::VmdBoneFrame>*> resolved(skeleton.bones.size(),
                                                                    nullptr);
    for (const auto& [name, track] : bone_tracks_) {
        const auto index = skeleton.find_bone_index(name);
        if (!index || *index >= resolved.size() || resolved[*index] != nullptr) {
            continue;
        }
        resolved[*index] = &track;
    }
    for (std::size_t i = 0; i < resolved.size(); ++i) {
        if (resolved[i] == nullptr) {
            continue;
        }
        const auto [translation, rotation] = sample_bone_track(*resolved[i], frame);
        skeleton.set_bone_transform(i, translation, rotation);
    }
    skeleton.update_transforms();

    morphs.reset();
    for (const auto& [name, track] : morph_tracks_) {
        morphs.set_weight(name, sample_morph_track(track, frame));
    }
}

} // namespace hikari::animation
