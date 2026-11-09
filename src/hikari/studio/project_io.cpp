#include "hikari/studio/project_io.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>

#include <algorithm>
#include <cmath>

namespace hikari::studio {
namespace {

constexpr int kFileVersion = 1;

void set_error(QString* error, const QString& message) {
    if (error != nullptr) {
        *error = message;
    }
}

QJsonArray to_json(const std::array<float, 3>& value) {
    return QJsonArray{value[0], value[1], value[2]};
}

QJsonArray to_json(const std::array<float, 4>& value) {
    return QJsonArray{value[0], value[1], value[2], value[3]};
}

std::array<float, 3> read_vec3(const QJsonValue& value,
                               const std::array<float, 3>& fallback = {}) {
    const QJsonArray array = value.toArray();
    if (array.size() < 3) {
        return fallback;
    }
    return {static_cast<float>(array.at(0).toDouble()),
            static_cast<float>(array.at(1).toDouble()),
            static_cast<float>(array.at(2).toDouble())};
}

std::array<float, 4> read_vec4(const QJsonValue& value,
                               const std::array<float, 4>& fallback) {
    const QJsonArray array = value.toArray();
    if (array.size() < 4) {
        return fallback;
    }
    return {static_cast<float>(array.at(0).toDouble()),
            static_cast<float>(array.at(1).toDouble()),
            static_cast<float>(array.at(2).toDouble()),
            static_cast<float>(array.at(3).toDouble())};
}

template <std::size_t N>
QJsonArray bytes_to_json(const std::array<std::uint8_t, N>& bytes) {
    QJsonArray array;
    for (const std::uint8_t byte : bytes) {
        array.append(static_cast<int>(byte));
    }
    return array;
}

template <std::size_t N>
std::array<std::uint8_t, N> json_to_bytes(const QJsonValue& value) {
    std::array<std::uint8_t, N> bytes{};
    const QJsonArray array = value.toArray();
    const std::size_t count = std::min(N, static_cast<std::size_t>(array.size()));
    for (std::size_t i = 0; i < count; ++i) {
        bytes[i] = static_cast<std::uint8_t>(
            std::clamp(array.at(static_cast<int>(i)).toInt(), 0, 255));
    }
    return bytes;
}

QJsonObject vmd_to_json(const formats::VmdMotion& motion) {
    QJsonObject object;
    object[QStringLiteral("version")] = static_cast<int>(motion.version);
    object[QStringLiteral("model_name")] = QString::fromStdString(motion.model_name);

    QJsonArray bones;
    for (const auto& frame : motion.bone_frames) {
        QJsonObject item;
        item[QStringLiteral("name")] = QString::fromStdString(frame.name);
        item[QStringLiteral("frame")] = static_cast<int>(frame.frame);
        item[QStringLiteral("position")] = to_json(frame.position);
        item[QStringLiteral("rotation")] = to_json(frame.rotation);
        item[QStringLiteral("interpolation")] = bytes_to_json(frame.interpolation);
        bones.append(item);
    }
    object[QStringLiteral("bones")] = bones;

    QJsonArray morphs;
    for (const auto& frame : motion.morph_frames) {
        QJsonObject item;
        item[QStringLiteral("name")] = QString::fromStdString(frame.name);
        item[QStringLiteral("frame")] = static_cast<int>(frame.frame);
        item[QStringLiteral("weight")] = frame.weight;
        morphs.append(item);
    }
    object[QStringLiteral("morphs")] = morphs;

    QJsonArray cameras;
    for (const auto& frame : motion.camera_frames) {
        QJsonObject item;
        item[QStringLiteral("frame")] = static_cast<int>(frame.frame);
        item[QStringLiteral("distance")] = frame.distance;
        item[QStringLiteral("position")] = to_json(frame.position);
        item[QStringLiteral("rotation")] = to_json(frame.rotation);
        item[QStringLiteral("interpolation")] = bytes_to_json(frame.interpolation);
        item[QStringLiteral("fov")] = static_cast<int>(frame.fov);
        item[QStringLiteral("perspective")] = frame.perspective;
        cameras.append(item);
    }
    object[QStringLiteral("cameras")] = cameras;

    QJsonArray lights;
    for (const auto& frame : motion.light_frames) {
        QJsonObject item;
        item[QStringLiteral("frame")] = static_cast<int>(frame.frame);
        item[QStringLiteral("color")] = to_json(frame.color);
        item[QStringLiteral("direction")] = to_json(frame.direction);
        lights.append(item);
    }
    object[QStringLiteral("lights")] = lights;

    QJsonArray shadows;
    for (const auto& frame : motion.shadow_frames) {
        QJsonObject item;
        item[QStringLiteral("frame")] = static_cast<int>(frame.frame);
        item[QStringLiteral("mode")] = static_cast<int>(frame.mode);
        item[QStringLiteral("distance")] = frame.distance;
        shadows.append(item);
    }
    object[QStringLiteral("shadows")] = shadows;

    QJsonArray ik_frames;
    for (const auto& frame : motion.ik_frames) {
        QJsonObject item;
        item[QStringLiteral("frame")] = static_cast<int>(frame.frame);
        item[QStringLiteral("show")] = frame.show;
        QJsonArray switches;
        for (const auto& sw : frame.ik_switches) {
            QJsonObject entry;
            entry[QStringLiteral("name")] = QString::fromStdString(sw.name);
            entry[QStringLiteral("enabled")] = sw.enabled;
            switches.append(entry);
        }
        item[QStringLiteral("switches")] = switches;
        ik_frames.append(item);
    }
    object[QStringLiteral("ik")] = ik_frames;
    return object;
}

formats::VmdMotion vmd_from_json(const QJsonObject& object) {
    formats::VmdMotion motion;
    motion.version = static_cast<std::uint32_t>(
        std::max(object.value(QStringLiteral("version")).toInt(2), 0));
    motion.model_name = object.value(QStringLiteral("model_name")).toString().toStdString();

    for (const auto& value : object.value(QStringLiteral("bones")).toArray()) {
        const QJsonObject item = value.toObject();
        formats::VmdBoneFrame frame;
        frame.name = item.value(QStringLiteral("name")).toString().toStdString();
        frame.frame = static_cast<std::uint32_t>(
            std::max(item.value(QStringLiteral("frame")).toInt(0), 0));
        frame.position = read_vec3(item.value(QStringLiteral("position")));
        frame.rotation =
            read_vec4(item.value(QStringLiteral("rotation")), {0.0f, 0.0f, 0.0f, 1.0f});
        frame.interpolation = json_to_bytes<64>(item.value(QStringLiteral("interpolation")));
        motion.bone_frames.push_back(std::move(frame));
    }
    for (const auto& value : object.value(QStringLiteral("morphs")).toArray()) {
        const QJsonObject item = value.toObject();
        formats::VmdMorphFrame frame;
        frame.name = item.value(QStringLiteral("name")).toString().toStdString();
        frame.frame = static_cast<std::uint32_t>(
            std::max(item.value(QStringLiteral("frame")).toInt(0), 0));
        frame.weight = static_cast<float>(item.value(QStringLiteral("weight")).toDouble());
        motion.morph_frames.push_back(std::move(frame));
    }
    for (const auto& value : object.value(QStringLiteral("cameras")).toArray()) {
        const QJsonObject item = value.toObject();
        formats::VmdCameraFrame frame;
        frame.frame = static_cast<std::uint32_t>(
            std::max(item.value(QStringLiteral("frame")).toInt(0), 0));
        frame.distance = static_cast<float>(item.value(QStringLiteral("distance")).toDouble());
        frame.position = read_vec3(item.value(QStringLiteral("position")));
        frame.rotation = read_vec3(item.value(QStringLiteral("rotation")));
        frame.interpolation = json_to_bytes<24>(item.value(QStringLiteral("interpolation")));
        frame.fov = static_cast<std::uint32_t>(
            std::max(item.value(QStringLiteral("fov")).toInt(30), 0));
        frame.perspective = item.value(QStringLiteral("perspective")).toBool(true);
        motion.camera_frames.push_back(frame);
    }
    for (const auto& value : object.value(QStringLiteral("lights")).toArray()) {
        const QJsonObject item = value.toObject();
        formats::VmdLightFrame frame;
        frame.frame = static_cast<std::uint32_t>(
            std::max(item.value(QStringLiteral("frame")).toInt(0), 0));
        frame.color = read_vec3(item.value(QStringLiteral("color")));
        frame.direction = read_vec3(item.value(QStringLiteral("direction")));
        motion.light_frames.push_back(frame);
    }
    for (const auto& value : object.value(QStringLiteral("shadows")).toArray()) {
        const QJsonObject item = value.toObject();
        formats::VmdShadowFrame frame;
        frame.frame = static_cast<std::uint32_t>(
            std::max(item.value(QStringLiteral("frame")).toInt(0), 0));
        frame.mode = static_cast<std::uint8_t>(
            std::clamp(item.value(QStringLiteral("mode")).toInt(0), 0, 255));
        frame.distance = static_cast<float>(item.value(QStringLiteral("distance")).toDouble());
        motion.shadow_frames.push_back(frame);
    }
    for (const auto& value : object.value(QStringLiteral("ik")).toArray()) {
        const QJsonObject item = value.toObject();
        formats::VmdIkFrame frame;
        frame.frame = static_cast<std::uint32_t>(
            std::max(item.value(QStringLiteral("frame")).toInt(0), 0));
        frame.show = item.value(QStringLiteral("show")).toBool(true);
        for (const auto& switch_value : item.value(QStringLiteral("switches")).toArray()) {
            const QJsonObject entry = switch_value.toObject();
            formats::VmdIkSwitch sw;
            sw.name = entry.value(QStringLiteral("name")).toString().toStdString();
            sw.enabled = entry.value(QStringLiteral("enabled")).toBool(true);
            frame.ik_switches.push_back(std::move(sw));
        }
        motion.ik_frames.push_back(std::move(frame));
    }
    return motion;
}

QJsonObject model_key_to_json(const ModelTransformKey& key) {
    QJsonObject object;
    object[QStringLiteral("frame")] = static_cast<int>(key.frame);
    object[QStringLiteral("scale")] = key.scale;
    object[QStringLiteral("position")] = to_json(key.position);
    object[QStringLiteral("rotation")] = to_json(key.rotation);
    object[QStringLiteral("visible")] = key.visible;
    return object;
}

ModelTransformKey model_key_from_json(const QJsonObject& object) {
    ModelTransformKey key;
    key.frame = static_cast<std::uint32_t>(
        std::max(object.value(QStringLiteral("frame")).toInt(0), 0));
    key.scale = static_cast<float>(object.value(QStringLiteral("scale")).toDouble(1.0));
    key.position = read_vec3(object.value(QStringLiteral("position")));
    key.rotation = read_vec3(object.value(QStringLiteral("rotation")));
    key.visible = object.value(QStringLiteral("visible")).toBool(true);
    return key;
}

QJsonObject key_to_json(const TrackKeyframe& key) {
    QJsonObject object;
    object[QStringLiteral("position")] = to_json(key.position);
    object[QStringLiteral("rotation")] = to_json(key.rotation);
    object[QStringLiteral("angles")] = to_json(key.angles);
    object[QStringLiteral("weight")] = key.weight;
    object[QStringLiteral("distance")] = key.distance;
    object[QStringLiteral("fov")] = key.fov;
    object[QStringLiteral("scale")] = key.scale;
    object[QStringLiteral("visible")] = key.visible;
    object[QStringLiteral("parent_model")] = key.parent_model;
    object[QStringLiteral("parent_bone")] = key.parent_bone;
    object[QStringLiteral("follow_offset")] = to_json(key.follow_offset);
    object[QStringLiteral("interpolation")] = bytes_to_json(key.interpolation);
    QJsonObject ik;
    for (auto it = key.ik_enabled.begin(); it != key.ik_enabled.end(); ++it) {
        ik[it.key()] = it.value();
    }
    object[QStringLiteral("ik")] = ik;
    QJsonObject op;
    for (auto it = key.op_bindings.begin(); it != key.op_bindings.end(); ++it) {
        QJsonObject entry;
        entry[QStringLiteral("model")] = it.value().model;
        entry[QStringLiteral("bone")] = it.value().bone;
        op[it.key()] = entry;
    }
    object[QStringLiteral("op")] = op;
    QJsonObject model_ik;
    for (auto model = key.model_ik.begin(); model != key.model_ik.end(); ++model) {
        QJsonObject states;
        for (auto it = model.value().begin(); it != model.value().end(); ++it) {
            states[it.key()] = it.value();
        }
        model_ik[model.key()] = states;
    }
    object[QStringLiteral("model_ik")] = model_ik;
    QJsonObject model_op;
    for (auto model = key.model_op.begin(); model != key.model_op.end(); ++model) {
        QJsonObject entries;
        for (auto it = model.value().begin(); it != model.value().end(); ++it) {
            QJsonObject entry;
            entry[QStringLiteral("model")] = it.value().model;
            entry[QStringLiteral("bone")] = it.value().bone;
            entries[it.key()] = entry;
        }
        model_op[model.key()] = entries;
    }
    object[QStringLiteral("model_op")] = model_op;
    return object;
}

TrackKeyframe key_from_json(const QJsonObject& object) {
    TrackKeyframe key;
    key.position = read_vec3(object.value(QStringLiteral("position")));
    key.rotation =
        read_vec4(object.value(QStringLiteral("rotation")), {0.0f, 0.0f, 0.0f, 1.0f});
    key.angles = read_vec3(object.value(QStringLiteral("angles")));
    key.weight = static_cast<float>(object.value(QStringLiteral("weight")).toDouble());
    key.distance = static_cast<float>(object.value(QStringLiteral("distance")).toDouble());
    key.fov = static_cast<float>(object.value(QStringLiteral("fov")).toDouble());
    key.scale = static_cast<float>(object.value(QStringLiteral("scale")).toDouble(1.0));
    key.visible = object.value(QStringLiteral("visible")).toBool(true);
    key.parent_model = std::max(object.value(QStringLiteral("parent_model")).toInt(-1), -1);
    key.parent_bone =
        std::max(object.value(QStringLiteral("parent_bone")).toInt(0), 0);
    key.follow_offset = read_vec3(object.value(QStringLiteral("follow_offset")));
    key.interpolation = json_to_bytes<4>(object.value(QStringLiteral("interpolation")));
    if (key.interpolation == std::array<std::uint8_t, 4>{0, 0, 0, 0}) {
        key.interpolation = {20, 20, 20, 20};
    }
    const QJsonObject ik = object.value(QStringLiteral("ik")).toObject();
    for (auto it = ik.begin(); it != ik.end(); ++it) {
        key.ik_enabled.insert(it.key(), it.value().toBool(true));
    }
    const QJsonObject op = object.value(QStringLiteral("op")).toObject();
    for (auto it = op.begin(); it != op.end(); ++it) {
        const QJsonObject entry = it.value().toObject();
        OpTarget target;
        target.model = entry.value(QStringLiteral("model")).toString();
        target.bone = entry.value(QStringLiteral("bone")).toString();
        key.op_bindings.insert(it.key(), target);
    }
    const QJsonObject model_ik = object.value(QStringLiteral("model_ik")).toObject();
    for (auto model = model_ik.begin(); model != model_ik.end(); ++model) {
        QMap<QString, bool> states;
        const QJsonObject entries = model.value().toObject();
        for (auto it = entries.begin(); it != entries.end(); ++it) {
            states.insert(it.key(), it.value().toBool(true));
        }
        key.model_ik.insert(model.key(), states);
    }
    const QJsonObject model_op = object.value(QStringLiteral("model_op")).toObject();
    for (auto model = model_op.begin(); model != model_op.end(); ++model) {
        QMap<QString, OpTarget> entries;
        const QJsonObject objects = model.value().toObject();
        for (auto it = objects.begin(); it != objects.end(); ++it) {
            const QJsonObject entry = it.value().toObject();
            OpTarget target;
            target.model = entry.value(QStringLiteral("model")).toString();
            target.bone = entry.value(QStringLiteral("bone")).toString();
            entries.insert(it.key(), target);
        }
        key.model_op.insert(model.key(), entries);
    }
    return key;
}

QJsonObject motion_to_object(const HikariMotion& motion) {
    QJsonObject object;
    object[QStringLiteral("vmd")] = vmd_to_json(motion.vmd);
    QJsonArray keys;
    for (const auto& key : motion.model_keys) {
        keys.append(model_key_to_json(key));
    }
    object[QStringLiteral("model_keys")] = keys;
    return object;
}

HikariMotion motion_from_object(const QJsonObject& object) {
    HikariMotion motion;
    motion.vmd = vmd_from_json(object.value(QStringLiteral("vmd")).toObject());
    for (const auto& value : object.value(QStringLiteral("model_keys")).toArray()) {
        motion.model_keys.push_back(model_key_from_json(value.toObject()));
    }
    return motion;
}

} // namespace

QByteArray hkm_to_json(const HikariMotion& motion) {
    QJsonObject root;
    root[QStringLiteral("format")] = QStringLiteral("hikari-motion");
    root[QStringLiteral("version")] = kFileVersion;
    const QJsonObject payload = motion_to_object(motion);
    root[QStringLiteral("vmd")] = payload.value(QStringLiteral("vmd"));
    root[QStringLiteral("model_keys")] = payload.value(QStringLiteral("model_keys"));
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

std::optional<HikariMotion> hkm_from_json(const QByteArray& bytes, QString* error) {
    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parse_error);
    if (document.isNull() || !document.isObject()) {
        set_error(error, QStringLiteral("Invalid HKM JSON: %1").arg(parse_error.errorString()));
        return std::nullopt;
    }
    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("format")).toString() != QStringLiteral("hikari-motion")) {
        set_error(error, QStringLiteral("Not a Hikari motion file"));
        return std::nullopt;
    }
    if (root.value(QStringLiteral("version")).toInt(0) > kFileVersion) {
        set_error(error, QStringLiteral("Unsupported HKM version"));
        return std::nullopt;
    }
    return motion_from_object(root);
}

QByteArray hkp_to_json(const ProjectData& project) {
    QJsonObject root;
    root[QStringLiteral("format")] = QStringLiteral("hikari-project");
    root[QStringLiteral("version")] = kFileVersion;
    root[QStringLiteral("active_model")] = project.active_model;

    QJsonObject timeline;
    timeline[QStringLiteral("current")] = project.current_frame;
    timeline[QStringLiteral("max")] = project.max_frame;
    timeline[QStringLiteral("play_start")] = project.play_start;
    timeline[QStringLiteral("play_end")] = project.play_end;
    timeline[QStringLiteral("camera_mode")] = project.camera_mode;
    root[QStringLiteral("timeline")] = timeline;

    QJsonObject scene;
    scene[QStringLiteral("camera_pan")] = to_json(project.scene.camera_pan);
    scene[QStringLiteral("camera_yaw")] = project.scene.camera_yaw;
    scene[QStringLiteral("camera_pitch")] = project.scene.camera_pitch;
    scene[QStringLiteral("camera_roll")] = project.scene.camera_roll;
    scene[QStringLiteral("camera_distance")] = project.scene.camera_distance;
    scene[QStringLiteral("camera_fov_deg")] = project.scene.camera_fov_deg;
    scene[QStringLiteral("camera_orthographic")] = project.scene.camera_orthographic;
    scene[QStringLiteral("camera_target")] = static_cast<int>(project.scene.camera_target);
    scene[QStringLiteral("camera_follow")] = project.scene.camera_follow;
    scene[QStringLiteral("camera_parent_model")] = project.scene.camera_parent_model;
    scene[QStringLiteral("camera_parent_bone")] = project.scene.camera_parent_bone;
    scene[QStringLiteral("camera_follow_offset")] =
        to_json(project.scene.camera_follow_offset);
    scene[QStringLiteral("camera_near_clip")] = project.scene.camera_near_clip;
    scene[QStringLiteral("camera_far_clip")] = project.scene.camera_far_clip;
    scene[QStringLiteral("audio_path")] = project.scene.audio_path;
    scene[QStringLiteral("audio_offset_frames")] = project.scene.audio_offset_frames;
    scene[QStringLiteral("audio_volume")] = project.scene.audio_volume;
    scene[QStringLiteral("light_color")] = to_json(project.scene.light_color);
    scene[QStringLiteral("light_direction")] = to_json(project.scene.light_direction);
    scene[QStringLiteral("shadow_mode")] = static_cast<int>(project.scene.shadow_mode);
    scene[QStringLiteral("shadow_range")] = project.scene.shadow_range;
    scene[QStringLiteral("shadow_detail")] = project.scene.shadow_detail;
    scene[QStringLiteral("gravity")] = to_json(project.scene.gravity);
    scene[QStringLiteral("gravity_noise")] = project.scene.gravity_noise;
    const QJsonDocument graphics_document =
        QJsonDocument::fromJson(QByteArray::fromStdString(project.scene.graphics.to_json()));
    if (graphics_document.isObject()) {
        scene[QStringLiteral("graphics")] = graphics_document.object();
    }
    root[QStringLiteral("scene")] = scene;

    QJsonArray models;
    for (const auto& model : project.models) {
        QJsonObject item;
        item[QStringLiteral("path")] = model.path;
        item[QStringLiteral("name")] = model.name;
        item[QStringLiteral("offset")] = to_json(model.offset.to_array());
        item[QStringLiteral("rotation")] = to_json(model.rotation.to_array());
        item[QStringLiteral("scale")] = model.scale;
        item[QStringLiteral("visible")] = model.visible;
        models.append(item);
    }
    root[QStringLiteral("models")] = models;

    QJsonArray tracks;
    for (const auto& track : project.tracks) {
        QJsonObject item;
        item[QStringLiteral("name")] = track.name;
        item[QStringLiteral("frame")] = static_cast<int>(track.frame);
        item[QStringLiteral("key")] = key_to_json(track.key);
        tracks.append(item);
    }
    root[QStringLiteral("tracks")] = tracks;

    if (project.motion) {
        const QJsonObject motion = motion_to_object(*project.motion);
        root[QStringLiteral("motion")] = motion;
    } else {
        root[QStringLiteral("motion")] = QJsonValue::Null;
    }
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

std::optional<ProjectData> hkp_from_json(const QByteArray& bytes, QString* error) {
    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parse_error);
    if (document.isNull() || !document.isObject()) {
        set_error(error, QStringLiteral("Invalid HKP JSON: %1").arg(parse_error.errorString()));
        return std::nullopt;
    }
    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("format")).toString() != QStringLiteral("hikari-project")) {
        set_error(error, QStringLiteral("Not a Hikari project file"));
        return std::nullopt;
    }
    if (root.value(QStringLiteral("version")).toInt(0) > kFileVersion) {
        set_error(error, QStringLiteral("Unsupported HKP version"));
        return std::nullopt;
    }

    ProjectData project;
    project.active_model = root.value(QStringLiteral("active_model")).toInt(-1);
    const QJsonObject timeline = root.value(QStringLiteral("timeline")).toObject();
    project.current_frame = timeline.value(QStringLiteral("current")).toInt(0);
    project.max_frame = timeline.value(QStringLiteral("max")).toInt(300);
    project.play_start = timeline.value(QStringLiteral("play_start")).toInt(0);
    project.play_end = timeline.value(QStringLiteral("play_end")).toInt(300);
    project.camera_mode = timeline.value(QStringLiteral("camera_mode")).toBool(true);

    const QJsonObject scene = root.value(QStringLiteral("scene")).toObject();
    project.scene.camera_pan =
        read_vec3(scene.value(QStringLiteral("camera_pan")), {0.0f, 10.0f, 0.0f});
    project.scene.camera_yaw =
        static_cast<float>(scene.value(QStringLiteral("camera_yaw")).toDouble(0.0));
    project.scene.camera_pitch =
        static_cast<float>(scene.value(QStringLiteral("camera_pitch")).toDouble(0.0));
    project.scene.camera_roll =
        static_cast<float>(scene.value(QStringLiteral("camera_roll")).toDouble(0.0));
    project.scene.camera_distance =
        static_cast<float>(scene.value(QStringLiteral("camera_distance")).toDouble(45.0));
    project.scene.camera_fov_deg =
        static_cast<float>(scene.value(QStringLiteral("camera_fov_deg")).toDouble(30.0));
    project.scene.camera_orthographic =
        scene.value(QStringLiteral("camera_orthographic")).toBool(false);
    project.scene.camera_target = static_cast<CameraTarget>(
        std::clamp(scene.value(QStringLiteral("camera_target")).toInt(0), 0, 2));
    project.scene.camera_follow =
        scene.value(QStringLiteral("camera_follow")).toBool(false);
    project.scene.camera_parent_model =
        std::max(scene.value(QStringLiteral("camera_parent_model")).toInt(-1), -1);
    project.scene.camera_parent_bone =
        std::max(scene.value(QStringLiteral("camera_parent_bone")).toInt(0), 0);
    project.scene.camera_follow_offset =
        read_vec3(scene.value(QStringLiteral("camera_follow_offset")));
    project.scene.camera_near_clip =
        static_cast<float>(scene.value(QStringLiteral("camera_near_clip")).toDouble(0.1));
    project.scene.camera_far_clip =
        static_cast<float>(scene.value(QStringLiteral("camera_far_clip")).toDouble(1000.0));
    project.scene.audio_path =
        scene.value(QStringLiteral("audio_path")).toString();
    project.scene.audio_offset_frames =
        static_cast<float>(scene.value(QStringLiteral("audio_offset_frames")).toDouble(0.0));
    project.scene.audio_volume =
        static_cast<float>(scene.value(QStringLiteral("audio_volume")).toDouble(1.0));
    project.scene.light_color =
        read_vec3(scene.value(QStringLiteral("light_color")), {0.6f, 0.6f, 0.6f});
    project.scene.light_direction =
        read_vec3(scene.value(QStringLiteral("light_direction")), {-0.5f, -1.0f, -0.5f});
    project.scene.shadow_mode = static_cast<ShadowMode>(
        std::clamp(scene.value(QStringLiteral("shadow_mode")).toInt(1), 0, 2));
    project.scene.shadow_range =
        static_cast<float>(scene.value(QStringLiteral("shadow_range")).toDouble(8875.0));
    project.scene.shadow_detail =
        static_cast<float>(scene.value(QStringLiteral("shadow_detail")).toDouble(0.5));
    project.scene.gravity =
        read_vec3(scene.value(QStringLiteral("gravity")), {0.0f, -9.8f, 0.0f});
    project.scene.gravity_noise =
        static_cast<float>(scene.value(QStringLiteral("gravity_noise")).toDouble(0.0));
    const QJsonObject graphics = scene.value(QStringLiteral("graphics")).toObject();
    if (!graphics.isEmpty()) {
        const QByteArray graphics_bytes =
            QJsonDocument(graphics).toJson(QJsonDocument::Compact);
        project.scene.graphics.from_json(std::string(
            graphics_bytes.constData(), static_cast<std::size_t>(graphics_bytes.size())));
    }

    for (const auto& value : root.value(QStringLiteral("models")).toArray()) {
        const QJsonObject item = value.toObject();
        ProjectModel model;
        model.path = item.value(QStringLiteral("path")).toString();
        model.name = item.value(QStringLiteral("name")).toString();
        model.offset = Vec3::from_array(read_vec3(item.value(QStringLiteral("offset"))));
        model.rotation = Quat::from_array(
            read_vec4(item.value(QStringLiteral("rotation")), {0.0f, 0.0f, 0.0f, 1.0f}))
                             .normalize();
        model.scale = static_cast<float>(item.value(QStringLiteral("scale")).toDouble(1.0));
        model.visible = item.value(QStringLiteral("visible")).toBool(true);
        project.models.push_back(std::move(model));
    }

    for (const auto& value : root.value(QStringLiteral("tracks")).toArray()) {
        const QJsonObject item = value.toObject();
        ProjectTrack track;
        track.name = item.value(QStringLiteral("name")).toString();
        track.frame = static_cast<unsigned int>(
            std::max(item.value(QStringLiteral("frame")).toInt(0), 0));
        track.key = key_from_json(item.value(QStringLiteral("key")).toObject());
        project.tracks.push_back(std::move(track));
    }

    if (root.value(QStringLiteral("motion")).isObject()) {
        project.motion = motion_from_object(root.value(QStringLiteral("motion")).toObject());
    }
    return project;
}

bool write_project_file(const QString& path, const QByteArray& bytes, QString* error) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        set_error(error, QStringLiteral("Failed to open %1 for writing").arg(path));
        return false;
    }
    if (file.write(bytes) != bytes.size()) {
        set_error(error, QStringLiteral("Failed to write %1").arg(path));
        return false;
    }
    return true;
}

std::optional<QByteArray> read_project_file(const QString& path, QString* error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        set_error(error, QStringLiteral("Failed to open %1").arg(path));
        return std::nullopt;
    }
    return file.readAll();
}

} // namespace hikari::studio
