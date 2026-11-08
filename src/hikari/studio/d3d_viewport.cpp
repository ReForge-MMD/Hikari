#include "hikari/studio/d3d_viewport.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QSet>
#include <QThread>
#include <QTimer>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <utility>

#include "hikari/animation/bezier.hpp"
#include "hikari/audio/audio_player.hpp"
#include "hikari/audio/audio_track.hpp"
#include "hikari/core/log.hpp"
#include "hikari/studio/viewport_chrome.hpp"

namespace hikari::studio {
namespace {

QString to_qstring(const std::string& value) {
    return QString::fromUtf8(value.data(), static_cast<int>(value.size()));
}

QString bone_label(const std::string& local, const std::string& english) {
    return english.empty() ? to_qstring(local) : to_qstring(english);
}

std::optional<Vec3> project_to_screen(const Vec3& point, const Mat4& view_projection,
                                      const QRect& rect) {
    const float x = point.x * view_projection.m[0][0] + point.y * view_projection.m[1][0] +
                    point.z * view_projection.m[2][0] + view_projection.m[3][0];
    const float y = point.x * view_projection.m[0][1] + point.y * view_projection.m[1][1] +
                    point.z * view_projection.m[2][1] + view_projection.m[3][1];
    const float w = point.x * view_projection.m[0][3] + point.y * view_projection.m[1][3] +
                    point.z * view_projection.m[2][3] + view_projection.m[3][3];
    if (w <= 0.001f) {
        return std::nullopt;
    }
    const float ndc_x = x / w;
    const float ndc_y = y / w;
    if (ndc_x < -1.8f || ndc_x > 1.8f || ndc_y < -1.8f || ndc_y > 1.8f) {
        return std::nullopt;
    }
    return Vec3(rect.left() + (ndc_x * 0.5f + 0.5f) * rect.width(),
                rect.top() + (-ndc_y * 0.5f + 0.5f) * rect.height(), 0.0f);
}

std::wstring to_wstring(const QString& text) {
    return text.toStdWString();
}

Vec3 skinned_position(const render::ModelVertex& vertex, const std::vector<Mat4>& palette) {
    const Vec3 position = Vec3::from_array(vertex.position);
    float total_weight = 0.0f;
    for (const float weight : vertex.bone_weights) {
        total_weight += weight;
    }
    if (total_weight <= 1.0e-4f) {
        return palette.empty() ? position : palette[0].transform_point(position);
    }
    Vec3 result;
    for (std::size_t i = 0; i < 4; ++i) {
        const float weight = vertex.bone_weights[i];
        if (weight <= 0.0f) {
            continue;
        }
        const std::size_t bone = vertex.bone_indices[i];
        if (bone >= palette.size()) {
            continue;
        }
        result += palette[bone].transform_point(position) * (weight / total_weight);
    }
    return result;
}

// Approximate world-space center of a material subset. Sampling caps the cost
// while still giving stable back-to-front ordering for blended materials.
Vec3 subset_world_center(const std::vector<std::uint32_t>& indices,
                         const std::vector<render::ModelVertex>& vertices,
                         const render::MeshSubset& subset, const std::vector<Mat4>& palette) {
    if (subset.index_count == 0 || indices.empty()) {
        return Vec3::zero();
    }
    const std::size_t step = std::max<std::size_t>(1, subset.index_count / 256);
    Vec3 sum;
    std::size_t samples = 0;
    for (std::size_t i = 0; i < subset.index_count; i += step) {
        const std::uint32_t vertex_index = indices[subset.index_start + i];
        if (vertex_index >= vertices.size()) {
            continue;
        }
        sum += skinned_position(vertices[vertex_index], palette);
        ++samples;
    }
    return samples == 0 ? Vec3::zero() : sum / static_cast<float>(samples);
}

Vec3 bone_tail_world(const animation::Skeleton& skeleton, std::size_t index) {
    const animation::BoneNode& bone = skeleton.bones[index];
    if (bone.has_tail_bone && bone.tail_bone < skeleton.bones.size()) {
        return skeleton.world_positions[bone.tail_bone];
    }
    Vec3 offset = bone.tail_offset;
    if (offset.length_squared() < 1.0e-8f) {
        if (index < skeleton.children.size() && !skeleton.children[index].empty()) {
            return skeleton.world_positions[skeleton.children[index].front()];
        }
        offset = Vec3(0.0f, 0.8f, 0.0f);
    }
    return skeleton.world_positions[index] + skeleton.world_rotations[index].rotate_vec3(offset);
}

// PMX morph panels: 0 = system (hidden), 1 = brow, 2 = eye, 3 = mouth, 4 = other.
// The facial tab uses Eyes, Mouth, Brow, Other in that order.
std::optional<int> morph_panel_slot(std::uint8_t panel) {
    switch (panel) {
    case 1:
        return 2;
    case 2:
        return 0;
    case 3:
        return 1;
    case 4:
        return 3;
    default:
        return std::nullopt;
    }
}

} // namespace

D3DViewport::D3DViewport(StudioAppState& state, QWidget* parent)
    : QWidget(parent), state_(state), camera_() {
    setAttribute(Qt::WA_NativeWindow);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAttribute(Qt::WA_PaintOnScreen);
    setAttribute(Qt::WA_NoSystemBackground);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setMinimumSize(320, 240);
    timer_ = new QTimer(this);
    timer_->setTimerType(Qt::PreciseTimer);
    timer_->setInterval(1);
    connect(timer_, &QTimer::timeout, this, &D3DViewport::tick);
    frame_timer_.start();
    fps_timer_.start();

    gizmo_ = new ViewGizmo(this);
    gizmo_->setAttribute(Qt::WA_NativeWindow);
    gizmo_->setAttribute(Qt::WA_OpaquePaintEvent);
    gizmo_->setAttribute(Qt::WA_NoSystemBackground);
    gizmo_->set_local_mode(state_.is_local_coord);
    connect(gizmo_, &ViewGizmo::rotateRequested, this, &D3DViewport::rotate_selection);
    connect(gizmo_, &ViewGizmo::translateRequested, this, &D3DViewport::translate_selection);
    connect(gizmo_, &ViewGizmo::localModeChanged, this,
            [this](bool local) { state_.is_local_coord = local; });
    place_gizmo();
}

D3DViewport::~D3DViewport() = default;

bool D3DViewport::initialize() {
    if (initialized_) {
        return true;
    }
    try {
        const HWND hwnd = reinterpret_cast<HWND>(winId());
#ifdef _WIN32
        SetWindowLongPtr(hwnd, GWL_STYLE,
                         GetWindowLongPtr(hwnd, GWL_STYLE) | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
#endif
        context_ = std::make_unique<core::D3D11Context>(hwnd, static_cast<std::uint32_t>(width()),
                                                         static_cast<std::uint32_t>(height()));
        renderer_ = std::make_unique<render::SceneRenderer>(*context_);
        initialized_ = true;
        apply_screen_size();
        place_gizmo();
        core::log_msg("[studio] Direct3D 11 viewport initialized");
        timer_->start();
        return true;
    } catch (const std::exception& error) {
        emit statusMessage(QStringLiteral("Direct3D 11 initialization failed: %1")
                               .arg(QString::fromUtf8(error.what())));
        return false;
    }
}

void D3DViewport::paintEvent(QPaintEvent* /*event*/) {
    if (initialized_) {
        tick();
    }
}

bool D3DViewport::nativeEvent(const QByteArray& eventType, void* message, qintptr* result) {
#ifdef _WIN32
    if (eventType == "windows_generic_MSG") {
        MSG* msg = static_cast<MSG*>(message);
        if (msg->message == WM_ERASEBKGND) {
            *result = 1;
            return true;
        }
    }
#endif
    return QWidget::nativeEvent(eventType, message, result);
}

void D3DViewport::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    initialize();
    place_gizmo();
}

void D3DViewport::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (context_) {
        try {
            context_->resize(static_cast<std::uint32_t>(std::max(width(), 1)),
                             static_cast<std::uint32_t>(std::max(height(), 1)));
            if (state_.screen_size_width <= 0 || state_.screen_size_height <= 0) {
                apply_screen_size();
            }
        } catch (const std::exception& error) {
            emit statusMessage(QString::fromUtf8(error.what()));
        }
    }
    place_gizmo();
    if (initialized_) {
        tick();
    }
}

bool D3DViewport::load_model(const QString& path) {
    if (!context_) {
        return false;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        emit statusMessage(QStringLiteral("Failed to open model: %1").arg(path));
        return false;
    }
    const QByteArray bytes = file.readAll();
    try {
        auto instance = std::make_unique<ModelInstance>();
        instance->path = path;
        instance->pmx = formats::PmxModel::parse(
            std::vector<std::uint8_t>(bytes.begin(), bytes.end()));

        const QFileInfo info(path);
        const QDir directory = info.absoluteDir();
        instance->textures.reserve(instance->pmx.textures.size());
        for (const auto& name : instance->pmx.textures) {
            QString normalized = to_qstring(name);
            normalized.replace('\\', '/');
            QString target = directory.filePath(normalized);
            if (!QFileInfo::exists(target)) {
                target = directory.filePath(QFileInfo(normalized).fileName());
            }
            if (!QFileInfo::exists(target)) {
                instance->textures.push_back(std::nullopt);
                continue;
            }
            try {
                instance->textures.push_back(
                    render::GpuTexture::from_file(*context_, to_wstring(target)));
            } catch (...) {
                instance->textures.push_back(std::nullopt);
            }
        }

        instance->mesh = render::GpuMesh::from_pmx(*context_, instance->pmx);
        instance->materials.reserve(instance->pmx.materials.size());
        for (const auto& material : instance->pmx.materials) {
            try {
                instance->materials.push_back(
                    render::GpuMaterial::from_pmx_material(*context_, material));
            } catch (...) {
                instance->materials.push_back(render::GpuMaterial::fallback(
                    *context_, material.name_local, material.texture_index));
            }
        }
        instance->skeleton = animation::Skeleton::from_pmx(instance->pmx);
        std::vector<std::array<float, 3>> rest_positions;
        rest_positions.reserve(instance->pmx.bones.size());
        for (const auto& bone : instance->pmx.bones) {
            rest_positions.push_back(bone.position);
        }
        instance->physics_rig = physics::PhysicsRig::create(
            instance->pmx, rest_positions, physics::PhysicsSettings{});
        if (instance->physics_rig) {
            instance->physics_rig->set_time_scale(state_.physics_speed);
            instance->physics_rig->set_ground_enabled(state_.ground_collision);
        }
        auto jiggle = animation::Jiggle(instance->pmx);
        instance->jiggle =
            jiggle.body_count() > 0 ? std::optional<animation::Jiggle>(std::move(jiggle))
                                    : std::nullopt;
        instance->morphs = animation::MorphEvaluator::from_pmx(instance->pmx);
        instance->name = to_qstring(instance->pmx.info.name_local);
        if (instance->name.isEmpty()) {
            instance->name = info.completeBaseName();
        }

        float minimum[3] = {1.0e30f, 1.0e30f, 1.0e30f};
        float maximum[3] = {-1.0e30f, -1.0e30f, -1.0e30f};
        for (const auto& vertex : instance->pmx.vertices) {
            for (int axis = 0; axis < 3; ++axis) {
                minimum[axis] =
                    std::min(minimum[axis], vertex.position[static_cast<std::size_t>(axis)]);
                maximum[axis] =
                    std::max(maximum[axis], vertex.position[static_cast<std::size_t>(axis)]);
            }
        }
        instance->bounds_min = Vec3(minimum[0], minimum[1], minimum[2]);
        instance->bounds_max = Vec3(maximum[0], maximum[1], maximum[2]);
        instance->shadow_center = Vec3((minimum[0] + maximum[0]) * 0.5f, 0.0f,
                                       (minimum[2] + maximum[2]) * 0.5f);
        instance->shadow_radius =
            std::max({(maximum[0] - minimum[0]) * 0.5f, (maximum[2] - minimum[2]) * 0.5f, 2.0f});
        // Every model keeps its authored origin. MMD scenes rely on this: stages and
        // characters are authored around world zero and must line up without an offset.
        instance->offset = Vec3(0.0f, 0.0f, 0.0f);

        core::log_msg("[studio] PMX counts: vertices=" +
                      std::to_string(instance->mesh.vertex_count) + " indices=" +
                      std::to_string(instance->mesh.index_count) + " materials=" +
                      std::to_string(instance->pmx.materials.size()) + " bones=" +
                      std::to_string(instance->skeleton.bones.size()) + " morphs=" +
                      std::to_string(instance->pmx.morphs.size()) + " rigid=" +
                      std::to_string(instance->pmx.rigid_bodies.size()) + " joints=" +
                      std::to_string(instance->pmx.joints.size()));
        core::log_msg("[studio] PMX bounds: min=(" + std::to_string(minimum[0]) + ", " +
                      std::to_string(minimum[1]) + ", " + std::to_string(minimum[2]) +
                      ") max=(" + std::to_string(maximum[0]) + ", " +
                      std::to_string(maximum[1]) + ", " + std::to_string(maximum[2]) + ")");

        models_.push_back(std::move(instance));
        active_model_ = static_cast<int>(models_.size()) - 1;
        motion_player_.reset();
        motion_data_.reset();
        rebuild_motion_keyframes();
        last_editor_frame_ = -1.0f;
        sync_active_model_state();
        state_.mode = EditorMode::Model;
        update_stats();
        emit modelLoaded(state_.model_name, vertex_count_, bone_count_);
        emit modelsChanged();
        emit activeModelChanged();
        emit statusMessage(QStringLiteral("Loaded model: %1").arg(path));
        core::log_msg("[studio] Loaded PMX: " + path.toStdString());
        return true;
    } catch (const std::exception& error) {
        emit statusMessage(QStringLiteral("Failed to load model: %1")
                               .arg(QString::fromUtf8(error.what())));
        return false;
    }
}

ModelInstance* D3DViewport::active_model() {
    if (active_model_ < 0 || active_model_ >= static_cast<int>(models_.size())) {
        return nullptr;
    }
    return models_[static_cast<std::size_t>(active_model_)].get();
}

const ModelInstance* D3DViewport::active_model() const {
    if (active_model_ < 0 || active_model_ >= static_cast<int>(models_.size())) {
        return nullptr;
    }
    return models_[static_cast<std::size_t>(active_model_)].get();
}

const animation::Skeleton* D3DViewport::skeleton() const {
    const ModelInstance* model = active_model();
    return model != nullptr ? &model->skeleton : nullptr;
}

const physics::PhysicsRig* D3DViewport::physics_rig() const {
    const ModelInstance* model = active_model();
    return model != nullptr && model->physics_rig ? &*model->physics_rig : nullptr;
}

QStringList D3DViewport::model_names() const {
    QStringList names;
    for (const auto& model : models_) {
        names.push_back(model->name);
    }
    return names;
}

void D3DViewport::update_stats() {
    const ModelInstance* model = active_model();
    vertex_count_ = model != nullptr ? static_cast<int>(model->mesh.vertex_count) : 0;
    index_count_ = model != nullptr ? static_cast<int>(model->mesh.index_count) : 0;
    bone_count_ = model != nullptr ? static_cast<int>(model->skeleton.bones.size()) : 0;
}

void D3DViewport::sync_active_model_state() {
    state_.selected_model_idx = active_model_;
    state_.loaded_models = model_names();
    ModelInstance* model = active_model();
    if (model == nullptr) {
        state_.model_loaded = false;
        state_.model_name.clear();
        state_.model_scale = 1.0f;
        state_.model_scale_dirty = false;
        state_.model_bone_names.clear();
        state_.selected_bone_idx.reset();
        state_.morph_weights.clear();
        state_.morph_slider_values = {0.0f, 0.0f, 0.0f, 0.0f};
        for (auto& names : state_.category_morph_names) {
            names.clear();
        }
        state_.mode = EditorMode::Camera;
        state_.track_keys_dirty = true;
        ++state_.keyframe_revision;
        return;
    }

    state_.model_loaded = true;
    state_.model_name = model->name;
    state_.model_scale = model->scale;
    state_.model_scale_dirty = false;
    state_.model_visible = model->visible;
    state_.model_bone_names.clear();
    for (std::size_t i = 0; i < model->skeleton.bones.size(); ++i) {
        const auto& bone = model->skeleton.bones[i];
        const std::string& name = bone.name_en.empty() ? bone.name : bone.name_en;
        state_.model_bone_names.push_back(
            name.empty() ? QStringLiteral("bone %1").arg(static_cast<int>(i))
                         : to_qstring(name));
    }
    state_.selected_bone_idx.reset();
    state_.morph_weights.clear();
    state_.morph_slider_values = {0.0f, 0.0f, 0.0f, 0.0f};
    for (auto& names : state_.category_morph_names) {
        names.clear();
    }
    for (const auto& morph : model->pmx.morphs) {
        const auto slot = morph_panel_slot(morph.category);
        if (!slot) {
            continue;
        }
        const std::string& name =
            morph.name_universal.empty() ? morph.name_local : morph.name_universal;
        state_.category_morph_names[static_cast<std::size_t>(*slot)].push_back(to_qstring(name));
    }
    for (int i = 0; i < 4; ++i) {
        QStringList& names = state_.category_morph_names[static_cast<std::size_t>(i)];
        state_.selected_morph_names[static_cast<std::size_t>(i)] =
            names.isEmpty() ? QString() : names.first();
    }
    state_.track_keys_dirty = true;
    ++state_.keyframe_revision;
}

void D3DViewport::set_active_model(int index) {
    if (index < 0 || index >= static_cast<int>(models_.size())) {
        return;
    }
    if (index == active_model_) {
        if (state_.mode != EditorMode::Model) {
            state_.mode = EditorMode::Model;
            emit activeModelChanged();
        }
        return;
    }
    active_model_ = index;
    last_editor_frame_ = -1.0f;
    state_.mode = EditorMode::Model;
    sync_active_model_state();
    rebuild_motion_keyframes();
    update_stats();
    emit activeModelChanged();
    emit statusMessage(QStringLiteral("Active model: %1").arg(state_.model_name));
}

void D3DViewport::set_model_visible(int index, bool visible) {
    if (index < 0 || index >= static_cast<int>(models_.size())) {
        return;
    }
    models_[static_cast<std::size_t>(index)]->visible = visible;
    if (index == active_model_) {
        state_.model_visible = visible;
    }
}

void D3DViewport::set_model_scale(int index, float scale) {
    if (index < 0 || index >= static_cast<int>(models_.size())) {
        return;
    }
    scale = std::clamp(scale, 0.05f, 20.0f);
    ModelInstance& model = *models_[static_cast<std::size_t>(index)];
    if (std::abs(model.scale - scale) < 1.0e-6f) {
        return;
    }
    model.scale = scale;
    if (index == active_model_) {
        state_.model_scale = scale;
        // Keep the edited value visible until the user registers a scale key.
        state_.model_scale_dirty = true;
    }
    state_.set_status(QStringLiteral("Model scale: %1x").arg(scale, 0, 'f', 3));
}

void D3DViewport::remove_active_model() {
    if (active_model_ < 0 || active_model_ >= static_cast<int>(models_.size())) {
        return;
    }
    const int removed = active_model_;
    models_.erase(models_.begin() + active_model_);
    for (auto& model : models_) {
        for (auto& attachment : model->skeleton.attachments) {
            if (attachment.target_model == removed) {
                attachment.target_model = -1;
                attachment.target_bone = 0;
            } else if (attachment.target_model > removed) {
                --attachment.target_model;
            }
        }
    }
    if (state_.camera_parent_model == removed) {
        state_.camera_parent_model = -1;
        state_.camera_parent_bone = 0;
    } else if (state_.camera_parent_model > removed) {
        --state_.camera_parent_model;
    }
    for (auto& [track, frames] : state_.track_keyframes) {
        if (track != QStringLiteral("camera")) {
            continue;
        }
        for (auto& [frame, key] : frames) {
            if (key.parent_model == removed) {
                key.parent_model = -1;
                key.parent_bone = 0;
            } else if (key.parent_model > removed) {
                --key.parent_model;
            }
        }
    }
    if (models_.empty()) {
        active_model_ = -1;
    } else {
        active_model_ = std::min(active_model_, static_cast<int>(models_.size()) - 1);
    }
    motion_player_.reset();
    motion_data_.reset();
    rebuild_motion_keyframes();
    last_editor_frame_ = -1.0f;
    sync_active_model_state();
    update_stats();
    emit modelsChanged();
    emit activeModelChanged();
    emit statusMessage(models_.empty()
                           ? QStringLiteral("All models removed from scene")
                           : QStringLiteral("Active model removed from scene"));
}

bool D3DViewport::load_motion(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        emit statusMessage(QStringLiteral("Failed to open motion: %1").arg(path));
        return false;
    }
    const QByteArray bytes = file.readAll();
    try {
        const auto motion = formats::VmdMotion::parse(
            std::vector<std::uint8_t>(bytes.begin(), bytes.end()));
        return apply_parsed_motion(motion, path);
    } catch (const std::exception& error) {
        emit statusMessage(QStringLiteral("Failed to parse motion: %1")
                               .arg(QString::fromUtf8(error.what())));
        return false;
    }
}

bool D3DViewport::load_vpd(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        emit statusMessage(QStringLiteral("Failed to read VPD: %1").arg(path));
        return false;
    }
    const QByteArray bytes = file.readAll();
    try {
        const auto pose = formats::VpdPose::from_bytes(
            std::vector<std::uint8_t>(bytes.begin(), bytes.end()));
        if (ModelInstance* model = active_model()) {
            for (const auto& bone : pose.bones) {
                const auto index = model->skeleton.find_bone_index(bone.name);
                if (!index) {
                    continue;
                }
                model->skeleton.local_translations[*index] = Vec3::from_array(bone.translation);
                model->skeleton.local_rotations[*index] = Quat::from_array(bone.rotation);
            }
            model->skeleton.update_transforms();
        }
        for (const auto& morph : pose.morphs) {
            state_.morph_weights[to_qstring(morph.name)] = morph.weight;
        }
        state_.morph_weights_dirty = true;
        emit statusMessage(QStringLiteral("Loaded VPD: %1").arg(path));
        return true;
    } catch (const std::exception& error) {
        emit statusMessage(QStringLiteral("Failed to parse VPD: %1")
                               .arg(QString::fromUtf8(error.what())));
        return false;
    }
}

bool D3DViewport::save_vpd(const QString& path) {
    const ModelInstance* model = active_model();
    if (model == nullptr) {
        return false;
    }
    formats::VpdPose pose;
    pose.model_name = state_.model_name.toStdString();
    for (std::size_t i = 0; i < model->skeleton.bones.size(); ++i) {
        const auto& bone = model->skeleton.bones[i];
        const Vec3 translation = model->skeleton.local_translations[i];
        const Quat rotation = model->skeleton.local_rotations[i];
        pose.bones.push_back({static_cast<std::uint32_t>(i), bone.name,
                              translation.to_array(), rotation.to_array()});
    }
    for (auto it = state_.morph_weights.begin(); it != state_.morph_weights.end(); ++it) {
        pose.morphs.push_back({static_cast<std::uint32_t>(pose.morphs.size()),
                               it.key().toStdString(), it.value()});
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit statusMessage(QStringLiteral("Failed to write VPD: %1").arg(path));
        return false;
    }
    const std::string text = pose.to_vpd_string();
    file.write(text.data(), static_cast<qint64>(text.size()));
    emit statusMessage(QStringLiteral("Saved VPD: %1").arg(path));
    return true;
}

bool D3DViewport::load_audio(const QString& path) {
    if (path.isEmpty()) {
        return false;
    }
    audio::DecodeResult decoded = audio::decode_audio_file(path.toStdString());
    if (!decoded.ok()) {
        emit statusMessage(QStringLiteral("Failed to load audio: %1")
                               .arg(QString::fromStdString(decoded.error)));
        return false;
    }
    if (!audio_player_) {
        audio_player_ = std::make_unique<audio::AudioPlayer>();
    }
    const double duration = decoded.track.duration_seconds;
    std::vector<float> peaks = decoded.track.peaks;
    std::string error;
    audio_player_->load(std::move(decoded.track), &error);
    if (!error.empty()) {
        emit statusMessage(QStringLiteral("Failed to start audio playback: %1")
                               .arg(QString::fromStdString(error)));
        return false;
    }
    state_.audio_path = path;
    state_.audio_name = QFileInfo(path).completeBaseName();
    state_.audio_duration = duration;
    state_.audio_peaks_per_second =
        static_cast<double>(audio::kOutputSampleRate) / audio::kPeakBlockFrames;
    state_.audio_peaks = std::move(peaks);
    state_.audio_volume = std::clamp(state_.audio_volume, 0.0f, 1.0f);
    audio_player_->set_volume(state_.audio_volume);
    audio_playing_ = false;
    emit statusMessage(QStringLiteral("Loaded audio: %1").arg(state_.audio_name));
    return true;
}

void D3DViewport::unload_audio() {
    if (audio_player_) {
        audio_player_->unload();
    }
    audio_playing_ = false;
    state_.audio_path.clear();
    state_.audio_name.clear();
    state_.audio_duration = 0.0;
    state_.audio_peaks_per_second = 0.0;
    state_.audio_peaks.clear();
}

formats::VmdMotion D3DViewport::build_authored_vmd() const {
    const ModelInstance* model = active_model();
    formats::VmdMotion motion;
    motion.version = 2;
    motion.model_name = state_.model_name.toStdString();

    QSet<QString> morph_names;
    for (const auto& category : state_.category_morph_names) {
        for (const QString& name : category) {
            morph_names.insert(name);
        }
    }

    const auto bone_interpolation = [](const std::array<std::uint8_t, 4>& curve) {
        std::array<std::uint8_t, 64> buffer{};
        for (std::size_t channel = 0; channel < 4; ++channel) {
            buffer[channel] = curve[0];
            buffer[4 + channel] = curve[1];
            buffer[8 + channel] = curve[2];
            buffer[12 + channel] = curve[3];
        }
        return buffer;
    };

    std::size_t exported = 0;
    for (const auto& [track, keys] : state_.track_keyframes) {
        if (keys.empty() || track == QStringLiteral("view cnt") ||
            track == QStringLiteral("Facial")) {
            continue;
        }
        if (track == QStringLiteral("Disp/IK/OP")) {
            for (const auto& [frame, key] : keys) {
                formats::VmdIkFrame ik;
                ik.frame = frame;
                ik.show = true;
                for (auto it = key.ik_enabled.begin(); it != key.ik_enabled.end(); ++it) {
                    ik.ik_switches.push_back({it.key().toStdString(), it.value()});
                }
                motion.ik_frames.push_back(std::move(ik));
                ++exported;
            }
            continue;
        }
        if (track == QStringLiteral("camera")) {
            for (const auto& [frame, key] : keys) {
                formats::VmdCameraFrame camera;
                camera.frame = frame;
                // The studio keeps a positive distance for an eye that sits away
                // from the focus; VMD expects MMD's negative eye offset.
                camera.distance = -key.distance;
                camera.position = key.position;
                camera.rotation = {key.angles[1], key.angles[0], key.angles[2]};
                // VMD stores six interpolation channels for a camera key. The
                // editor keeps one curve for the key, so mirror it across all
                // six instead of forcing linear bytes.
                for (std::size_t channel = 0; channel < 6; ++channel) {
                    std::copy(key.interpolation.begin(), key.interpolation.end(),
                              camera.interpolation.begin() +
                                  static_cast<std::ptrdiff_t>(channel * 4));
                }
                camera.fov = static_cast<std::uint32_t>(std::lround(std::max(0.0f, key.fov)));
                camera.perspective = !state_.camera_orthographic;
                motion.camera_frames.push_back(camera);
                ++exported;
            }
            continue;
        }
        if (track == QStringLiteral("light")) {
            for (const auto& [frame, key] : keys) {
                formats::VmdLightFrame light;
                light.frame = frame;
                light.color = key.position;
                light.direction = key.angles;
                motion.light_frames.push_back(light);
                ++exported;
            }
            continue;
        }
        if (track == QStringLiteral("shadow")) {
            for (const auto& [frame, key] : keys) {
                formats::VmdShadowFrame shadow;
                shadow.frame = frame;
                shadow.mode = static_cast<std::uint8_t>(
                    std::clamp(std::lround(key.weight), 0L, 2L));
                shadow.distance = key.distance;
                motion.shadow_frames.push_back(shadow);
                ++exported;
            }
            continue;
        }
        if (track == QStringLiteral("gravity") || track == QStringLiteral("accessory") ||
            track == QStringLiteral("model scale")) {
            continue;
        }

        const bool is_morph =
            morph_names.contains(track) ||
            (model != nullptr && !model->skeleton.find_bone_index(track.toStdString()));
        if (is_morph) {
            for (const auto& [frame, key] : keys) {
                formats::VmdMorphFrame morph;
                morph.name = track.toStdString();
                morph.frame = frame;
                morph.weight = key.weight;
                motion.morph_frames.push_back(std::move(morph));
                ++exported;
            }
            continue;
        }
        std::string bone_name = track.toStdString();
        if (model != nullptr) {
            if (const auto index = model->skeleton.find_bone_index(bone_name)) {
                bone_name = model->skeleton.bones[*index].name;
            }
        }
        for (const auto& [frame, key] : keys) {
            formats::VmdBoneFrame bone;
            bone.name = bone_name;
            bone.frame = frame;
            bone.position = key.position;
            bone.rotation = key.rotation;
            bone.interpolation = bone_interpolation(key.interpolation);
            motion.bone_frames.push_back(std::move(bone));
            ++exported;
        }
    }

    return motion;
}

bool D3DViewport::save_motion(const QString& path) {
    const formats::VmdMotion motion = build_authored_vmd();
    const std::size_t exported = motion.bone_frames.size() + motion.morph_frames.size() +
                                 motion.camera_frames.size() + motion.light_frames.size() +
                                 motion.shadow_frames.size() + motion.ik_frames.size();
    const std::vector<std::uint8_t> bytes = motion.to_bytes(motion.model_name);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit statusMessage(QStringLiteral("Failed to write VMD: %1").arg(path));
        return false;
    }
    file.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<qint64>(bytes.size()));
    emit statusMessage(QStringLiteral("Saved motion: %1 (%2 keyframes)")
                           .arg(path)
                           .arg(static_cast<int>(exported)));
    return true;
}

bool D3DViewport::apply_parsed_motion(const formats::VmdMotion& motion, const QString& label) {
    // A loaded motion owns the scene animation; authored scene tracks are cleared so
    // VMD camera/light/shadow can drive the viewport. Any keys registered afterwards
    // keep priority over the VMD data.
    for (const QString& track :
         {QStringLiteral("camera"), QStringLiteral("light"), QStringLiteral("shadow"),
          QStringLiteral("gravity"), QStringLiteral("accessory")}) {
        state_.track_keyframes.erase(track);
    }
    state_.selected_keyframes.clear();
    state_.undo_stack.clear();
    state_.redo_stack.clear();
    ++state_.keyframe_revision;
    motion_player_.emplace(motion);
    motion_data_ = motion;
    rebuild_motion_keyframes();
    state_.max_timeline_frame = static_cast<int>(motion_player_->max_frame);
    state_.play_range = {0, state_.max_timeline_frame};
    last_editor_frame_ = -1.0f;
    state_.track_keys_dirty = true;
    state_.model_scale_dirty = false;
    if (ModelInstance* model = active_model(); model != nullptr && model->physics_rig) {
        model->physics_rig->request_reset();
    }
    emit statusMessage(QStringLiteral("Loaded motion: %1").arg(label));
    return true;
}

void D3DViewport::rebuild_motion_keyframes() {
    state_.motion_keyframes.clear();
    if (!motion_data_) {
        return;
    }
    const formats::VmdMotion& motion = *motion_data_;
    for (const auto& frame : motion.camera_frames) {
        state_.motion_keyframes[QStringLiteral("camera")].insert(frame.frame);
    }
    for (const auto& frame : motion.light_frames) {
        state_.motion_keyframes[QStringLiteral("light")].insert(frame.frame);
    }
    for (const auto& frame : motion.shadow_frames) {
        state_.motion_keyframes[QStringLiteral("shadow")].insert(frame.frame);
    }
    for (const auto& frame : motion.ik_frames) {
        state_.motion_keyframes[QStringLiteral("Disp/IK/OP")].insert(frame.frame);
    }

    const ModelInstance* model = active_model();
    if (model == nullptr) {
        return;
    }
    // VMD frames are keyed by the names stored in the file. Map them onto the
    // timeline row names (English bone names, universal morph names) used by
    // sync_active_model_state().
    for (const auto& frame : motion.bone_frames) {
        const auto index = model->skeleton.find_bone_index(frame.name);
        if (!index) {
            continue;
        }
        const int bone_index = static_cast<int>(*index);
        if (bone_index < 0 || bone_index >= state_.model_bone_names.size()) {
            continue;
        }
        state_.motion_keyframes[state_.model_bone_names[bone_index]].insert(frame.frame);
    }
    for (const auto& frame : motion.morph_frames) {
        for (const auto& morph : model->pmx.morphs) {
            if (morph.name_local != frame.name && morph.name_universal != frame.name) {
                continue;
            }
            const std::string& display =
                morph.name_universal.empty() ? morph.name_local : morph.name_universal;
            state_.motion_keyframes[to_qstring(display)].insert(frame.frame);
            break;
        }
    }
}

HikariMotion D3DViewport::build_hikari_motion() const {
    HikariMotion result;
    result.vmd = build_authored_vmd();
    const ModelInstance* model = active_model();
    const auto track = state_.track_keyframes.find(QStringLiteral("model scale"));
    if (track != state_.track_keyframes.end() && !track->second.empty()) {
        for (const auto& [frame, key] : track->second) {
            ModelTransformKey transform;
            transform.frame = frame;
            transform.scale = key.scale;
            if (model != nullptr) {
                transform.position = model->offset.to_array();
                transform.visible = model->visible;
            }
            result.model_keys.push_back(transform);
        }
    } else if (model != nullptr) {
        ModelTransformKey transform;
        transform.frame = 0;
        transform.scale = model->scale;
        transform.position = model->offset.to_array();
        transform.visible = model->visible;
        result.model_keys.push_back(transform);
    }
    return result;
}

bool D3DViewport::save_motion_hkm(const QString& path) {
    const QByteArray bytes = hkm_to_json(build_hikari_motion());
    QString error;
    if (!write_project_file(path, bytes, &error)) {
        emit statusMessage(QStringLiteral("Failed to save Hikari motion: %1").arg(error));
        return false;
    }
    emit statusMessage(QStringLiteral("Saved Hikari motion: %1").arg(path));
    return true;
}

bool D3DViewport::load_motion_hkm(const QString& path) {
    QString error;
    const auto bytes = read_project_file(path, &error);
    if (!bytes) {
        emit statusMessage(QStringLiteral("Failed to read Hikari motion: %1").arg(error));
        return false;
    }
    auto motion = hkm_from_json(*bytes, &error);
    if (!motion) {
        emit statusMessage(QStringLiteral("Failed to parse Hikari motion: %1").arg(error));
        return false;
    }
    if (!apply_parsed_motion(motion->vmd, path)) {
        return false;
    }
    if (!motion->model_keys.empty()) {
        std::stable_sort(motion->model_keys.begin(), motion->model_keys.end(),
                         [](const ModelTransformKey& a, const ModelTransformKey& b) {
                             return a.frame < b.frame;
                         });
        auto& track = state_.track_keyframes[QStringLiteral("model scale")];
        track.clear();
        for (const auto& transform : motion->model_keys) {
            TrackKeyframe key;
            key.scale = transform.scale;
            track[transform.frame] = key;
        }
        state_.max_timeline_frame =
            std::max(state_.max_timeline_frame,
                     static_cast<int>(motion->model_keys.back().frame));
        state_.play_range.second = state_.max_timeline_frame;
        if (ModelInstance* model = active_model()) {
            model->scale = motion->model_keys.front().scale;
            state_.model_scale = model->scale;
        }
    }
    state_.model_scale_dirty = false;
    state_.track_keys_dirty = true;
    emit statusMessage(QStringLiteral("Loaded Hikari motion: %1").arg(path));
    return true;
}

bool D3DViewport::save_project(const QString& path) {
    ProjectData project;
    project.active_model = active_model_;
    project.current_frame = static_cast<int>(state_.current_frame);
    project.max_frame = state_.max_timeline_frame;
    project.play_start = state_.play_range.first;
    project.play_end = state_.play_range.second;
    project.camera_mode = state_.mode == EditorMode::Camera;
    project.scene.camera_pan = state_.camera_pan;
    project.scene.camera_yaw = state_.camera_yaw;
    project.scene.camera_pitch = state_.camera_pitch;
    project.scene.camera_roll = state_.camera_roll;
    project.scene.camera_distance = state_.camera_distance;
    project.scene.camera_fov_deg = state_.camera_fov_deg;
    project.scene.camera_orthographic = state_.camera_orthographic;
    project.scene.camera_target = state_.camera_target;
    project.scene.camera_follow = state_.camera_follow;
    project.scene.camera_parent_model = state_.camera_parent_model;
    project.scene.camera_parent_bone = state_.camera_parent_bone;
    project.scene.camera_follow_offset = state_.camera_follow_offset;
    project.scene.camera_near_clip = state_.camera_near_clip;
    project.scene.camera_far_clip = state_.camera_far_clip;
    project.scene.audio_path = state_.audio_path;
    project.scene.audio_offset_frames = state_.audio_offset_frames;
    project.scene.audio_volume = state_.audio_volume;
    project.scene.light_color = state_.light_color;
    project.scene.light_direction = state_.light_direction;
    project.scene.shadow_mode = state_.shadow_mode;
    project.scene.shadow_range = state_.shadow_range;
    project.scene.shadow_detail = state_.shadow_detail;
    project.scene.gravity = state_.gravity;
    project.scene.gravity_noise = state_.gravity_noise;

    for (const auto& model : models_) {
        ProjectModel entry;
        entry.path = model->path;
        entry.name = model->name;
        entry.offset = model->offset;
        entry.scale = model->scale;
        entry.visible = model->visible;
        project.models.push_back(std::move(entry));
    }

    for (const auto& [track, keys] : state_.track_keyframes) {
        for (const auto& [frame, key] : keys) {
            project.tracks.push_back({track, frame, key});
        }
    }

    if (motion_data_) {
        HikariMotion motion;
        motion.vmd = *motion_data_;
        project.motion = std::move(motion);
    }

    QString error;
    if (!write_project_file(path, hkp_to_json(project), &error)) {
        emit statusMessage(QStringLiteral("Failed to save project: %1").arg(error));
        return false;
    }
    emit statusMessage(QStringLiteral("Saved project: %1").arg(path));
    return true;
}

bool D3DViewport::load_project(const QString& path) {
    QString error;
    const auto bytes = read_project_file(path, &error);
    if (!bytes) {
        emit statusMessage(QStringLiteral("Failed to read project: %1").arg(error));
        return false;
    }
    auto project = hkp_from_json(*bytes, &error);
    if (!project) {
        emit statusMessage(QStringLiteral("Failed to parse project: %1").arg(error));
        return false;
    }

    models_.clear();
    active_model_ = -1;
    motion_player_.reset();
    motion_data_.reset();
    for (const auto& entry : project->models) {
        if (entry.path.isEmpty() || !load_model(entry.path)) {
            emit statusMessage(QStringLiteral("Project model could not be loaded: %1")
                                   .arg(entry.path));
            continue;
        }
        ModelInstance& model = *models_.back();
        model.offset = entry.offset;
        model.scale = entry.scale;
        model.visible = entry.visible;
    }
    if (models_.empty()) {
        sync_active_model_state();
        update_stats();
        emit modelsChanged();
        emit activeModelChanged();
        emit statusMessage(QStringLiteral("Project contains no loadable models: %1").arg(path));
        return false;
    }

    active_model_ = std::clamp(project->active_model, 0, static_cast<int>(models_.size()) - 1);
    if (project->motion) {
        apply_parsed_motion(project->motion->vmd, path);
    } else {
        motion_player_.reset();
        motion_data_.reset();
        rebuild_motion_keyframes();
    }

    state_.track_keyframes.clear();
    for (const auto& track : project->tracks) {
        state_.track_keyframes[track.name][track.frame] = track.key;
    }
    state_.selected_keyframes.clear();
    state_.max_timeline_frame = project->max_frame;
    state_.play_range = {project->play_start, project->play_end};
    state_.current_frame = static_cast<float>(project->current_frame);
    state_.mode = project->camera_mode ? EditorMode::Camera : EditorMode::Model;
    state_.camera_pan = project->scene.camera_pan;
    state_.camera_yaw = project->scene.camera_yaw;
    state_.camera_pitch = project->scene.camera_pitch;
    state_.camera_roll = project->scene.camera_roll;
    state_.camera_distance = project->scene.camera_distance;
    state_.camera_fov_deg = project->scene.camera_fov_deg;
    state_.camera_orthographic = project->scene.camera_orthographic;
    state_.camera_target = project->scene.camera_target;
    state_.camera_follow = project->scene.camera_follow;
    state_.camera_parent_model = std::clamp(project->scene.camera_parent_model, -1,
                                            static_cast<int>(models_.size()) - 1);
    state_.camera_parent_bone = std::max(project->scene.camera_parent_bone, 0);
    state_.camera_follow_offset = project->scene.camera_follow_offset;
    state_.camera_near_clip = std::max(project->scene.camera_near_clip, 0.01f);
    state_.camera_far_clip = std::max(project->scene.camera_far_clip, 1.0f);
    state_.audio_offset_frames = project->scene.audio_offset_frames;
    state_.audio_volume = std::clamp(project->scene.audio_volume, 0.0f, 1.0f);
    unload_audio();
    if (!project->scene.audio_path.isEmpty()) {
        load_audio(project->scene.audio_path);
    }
    state_.light_color = project->scene.light_color;
    state_.light_direction = project->scene.light_direction;
    state_.shadow_mode = project->scene.shadow_mode;
    state_.shadow_range = project->scene.shadow_range;
    state_.shadow_detail = project->scene.shadow_detail;
    state_.gravity = project->scene.gravity;
    state_.gravity_noise = project->scene.gravity_noise;
    last_editor_frame_ = -1.0f;
    state_.track_keys_dirty = true;
    state_.model_scale_dirty = false;
    sync_active_model_state();
    update_stats();
    emit modelsChanged();
    emit activeModelChanged();
    emit statusMessage(QStringLiteral("Loaded project: %1").arg(path));
    return true;
}

void D3DViewport::clear_model() {
    models_.clear();
    active_model_ = -1;
    state_.camera_parent_model = -1;
    state_.camera_parent_bone = 0;
    motion_player_.reset();
    motion_data_.reset();
    rebuild_motion_keyframes();
    last_editor_frame_ = -1.0f;
    sync_active_model_state();
    update_stats();
    emit modelsChanged();
    emit activeModelChanged();
    emit statusMessage(QStringLiteral("Scene cleared"));
}

void D3DViewport::request_physics_reset() {
    for (auto& model : models_) {
        if (model->physics_rig) {
            model->physics_rig->request_reset();
        }
    }
}

void D3DViewport::set_screen_size(int width, int height) {
    state_.screen_size_width = width;
    state_.screen_size_height = height;
    apply_screen_size();
}

QSize D3DViewport::effective_screen_size() const {
    const bool explicit_size = state_.screen_size_width > 0 && state_.screen_size_height > 0;
    if (explicit_size) {
        return {state_.screen_size_width, state_.screen_size_height};
    }
    return {std::max(width(), 1), std::max(height(), 1)};
}

QRect D3DViewport::scene_rect() const {
    const QSize render = effective_screen_size();
    const float scale =
        std::min(static_cast<float>(width()) / static_cast<float>(render.width()),
                 static_cast<float>(height()) / static_cast<float>(render.height()));
    const int scaled_width =
        std::max(1, static_cast<int>(std::lround(static_cast<float>(render.width()) * scale)));
    const int scaled_height =
        std::max(1, static_cast<int>(std::lround(static_cast<float>(render.height()) * scale)));
    return {(width() - scaled_width) / 2, (height() - scaled_height) / 2, scaled_width,
            scaled_height};
}

void D3DViewport::apply_screen_size() {
    applied_screen_width_ = state_.screen_size_width;
    applied_screen_height_ = state_.screen_size_height;
    if (!context_) {
        return;
    }
    try {
        if (state_.screen_size_width > 0 && state_.screen_size_height > 0) {
            context_->set_scene_resolution(static_cast<std::uint32_t>(state_.screen_size_width),
                                           static_cast<std::uint32_t>(state_.screen_size_height));
        } else {
            context_->set_scene_resolution(0, 0);
        }
    } catch (const std::exception& error) {
        emit statusMessage(QString::fromUtf8(error.what()));
    }
    place_gizmo();
    const QSize size = effective_screen_size();
    emit screenSizeChanged(size.width(), size.height());
}

void D3DViewport::place_gizmo() {
    if (gizmo_ == nullptr) {
        return;
    }
    const QRect scene = scene_rect();
    const int margin = 12;
    const int x = std::clamp(scene.right() - gizmo_->width() - margin, 0,
                             std::max(0, width() - gizmo_->width()));
    const int y = std::clamp(scene.bottom() - gizmo_->height() - margin, 0,
                             std::max(0, height() - gizmo_->height()));
    if (gizmo_->pos() != QPoint(x, y)) {
        gizmo_->move(x, y);
    }
    gizmo_->raise();
}

bool D3DViewport::selected_bone_transform(std::size_t& index) const {
    const ModelInstance* model = active_model();
    if (model == nullptr || !state_.selected_bone_idx) {
        return false;
    }
    index = static_cast<std::size_t>(*state_.selected_bone_idx);
    return index < model->skeleton.bones.size();
}

void D3DViewport::rotate_selection(int axis, float degrees) {
    constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
    const float radians = degrees * kDegToRad;
    ModelInstance* model = active_model();
    std::size_t index = 0;
    if (state_.mode == EditorMode::Camera || model == nullptr ||
        !selected_bone_transform(index)) {
        if (axis == 0) {
            state_.camera_pitch += radians;
        } else if (axis == 1) {
            state_.camera_yaw += radians;
        } else {
            state_.camera_roll += radians;
        }
        return;
    }
    const auto& bone = model->skeleton.bones[index];
    const bool rotatable = (bone.flags & 0x0002) != 0 || bone.has_ik || !bone.parent_index;
    if (!rotatable) {
        return;
    }
    apply_editor_bone_override();
    const Vec3 local_axis(axis == 0 ? 1.0f : 0.0f, axis == 1 ? 1.0f : 0.0f,
                          axis == 2 ? 1.0f : 0.0f);
    const Quat delta = Quat::from_axis_angle(local_axis, radians);
    if (state_.is_local_coord) {
        model->skeleton.local_rotations[index] =
            (model->skeleton.local_rotations[index] * delta).normalize();
    } else {
        const Quat world = (delta * model->skeleton.world_rotations[index]).normalize();
        const Quat parent = bone.parent_index
                                ? model->skeleton.world_rotations[*bone.parent_index]
                                : Quat::identity();
        // The authored layer rotates under the IK layer, so peel both frames off.
        const Quat authoring_frame =
            (parent * model->skeleton.ik_rotations[index]).normalize();
        model->skeleton.local_rotations[index] = (authoring_frame.inverse() * world).normalize();
    }
    model->skeleton.update_transforms();
    state_.bone_transform_dirty = false;
}

void D3DViewport::translate_selection(int axis, float amount) {
    ModelInstance* model = active_model();
    std::size_t index = 0;
    if (state_.mode == EditorMode::Camera || model == nullptr ||
        !selected_bone_transform(index)) {
        const Quat orientation = camera_orientation();
        const Vec3 right = orientation.rotate_vec3(Vec3::axis_x());
        const Vec3 up = orientation.rotate_vec3(Vec3::axis_y());
        const Vec3 forward = orientation.rotate_vec3(Vec3::axis_z());
        const Vec3 delta = axis == 0 ? right * amount
                           : axis == 1 ? up * amount
                                       : forward * amount;
        state_.camera_pan[0] += delta.x;
        state_.camera_pan[1] += delta.y;
        state_.camera_pan[2] += delta.z;
        return;
    }
    const auto& bone = model->skeleton.bones[index];
    const bool movable = (bone.flags & 0x0004) != 0 || bone.has_ik || !bone.parent_index;
    if (!movable) {
        return;
    }
    apply_editor_bone_override();
    // local_translations live in parent space. A local-axis move must therefore
    // be rotated by the bone's own local rotation; a world-axis move by the
    // inverse parent world rotation. The gizmo emits world units, so convert
    // them back into the model's authored units.
    const float amount_in_model = amount / std::max(model->scale, 1.0e-3f);
    Vec3 local_delta(axis == 0 ? amount_in_model : 0.0f,
                     axis == 1 ? amount_in_model : 0.0f,
                     axis == 2 ? amount_in_model : 0.0f);
    if (state_.is_local_coord) {
        local_delta = (model->skeleton.ik_rotations[index] *
                       model->skeleton.local_rotations[index])
                          .rotate_vec3(local_delta);
    } else if (bone.parent_index) {
        local_delta =
            model->skeleton.world_rotations[*bone.parent_index].inverse().rotate_vec3(local_delta);
    }
    model->skeleton.local_translations[index] =
        model->skeleton.local_translations[index] + local_delta;
    model->skeleton.update_transforms();
    state_.bone_transform_dirty = false;
}

void D3DViewport::reset_selection_transform() {
    ModelInstance* model = active_model();
    std::size_t index = 0;
    if (model == nullptr || !selected_bone_transform(index)) {
        return;
    }
    model->skeleton.local_translations[index] = Vec3::zero();
    model->skeleton.local_rotations[index] = Quat::identity();
    model->skeleton.update_transforms();
    state_.bone_transform_dirty = false;
}

void D3DViewport::set_selected_bone_ik_enabled(bool enabled) {
    ModelInstance* model = active_model();
    if (model == nullptr || !state_.selected_bone_idx) {
        return;
    }
    const std::size_t index = static_cast<std::size_t>(*state_.selected_bone_idx);
    if (index >= model->skeleton.bones.size() || !model->skeleton.bones[index].has_ik) {
        return;
    }
    if (model->skeleton.ik_enabled.size() != model->skeleton.bones.size()) {
        model->skeleton.ik_enabled.assign(model->skeleton.bones.size(), true);
    }
    model->skeleton.ik_enabled[index] = enabled;
    model->skeleton.update_transforms();
    state_.set_status(QStringLiteral("IK %1 for bone '%2'")
                          .arg(enabled ? QStringLiteral("enabled") : QStringLiteral("disabled"))
                          .arg(to_qstring(model->skeleton.bones[index].name)));
}

QMap<QString, bool> D3DViewport::ik_states() const {
    QMap<QString, bool> states;
    const ModelInstance* model = active_model();
    if (model == nullptr) {
        return states;
    }
    for (std::size_t i = 0; i < model->skeleton.bones.size(); ++i) {
        if (!model->skeleton.bones[i].has_ik) {
            continue;
        }
        const bool enabled = i < model->skeleton.ik_enabled.size()
                                 ? model->skeleton.ik_enabled[i]
                                 : true;
        states.insert(to_qstring(model->skeleton.bones[i].name), enabled);
    }
    return states;
}

ModelInstance* D3DViewport::model_by_name(const QString& name) {
    for (auto& model : models_) {
        if (model->name == name) {
            return model.get();
        }
    }
    return nullptr;
}

namespace {

// Target bone frame in the subject model's units. The target's scene position
// is converted into subject units, but only rotation and translation are kept:
// the subject's own scale must still apply to its geometry. Folding the scale
// ratio into this matrix would render the attached model at the target's scale.
Mat4 op_target_frame(const ModelInstance& subject, const ModelInstance& target,
                     std::size_t target_bone) {
    const float subject_scale = std::max(subject.scale, 1.0e-4f);
    const Vec3 target_scene =
        target.skeleton.world_matrices[target_bone].translation() * target.scale +
        target.offset;
    const Vec3 target_local = (target_scene - subject.offset) / subject_scale;
    const Quat target_rotation =
        target.skeleton.world_matrices[target_bone].rotation();
    return Mat4::from_translation(target_local) * Mat4::from_quat(target_rotation);
}

std::optional<std::size_t> op_attachment_index(const animation::Skeleton& skeleton,
                                               const QString& subject) {
    if (subject == op_root_subject()) {
        return skeleton.attachments.empty() ? std::nullopt
                                            : std::optional<std::size_t>(0);
    }
    const auto bone = skeleton.find_bone_index(subject.toStdString());
    if (!bone) {
        return std::nullopt;
    }
    return skeleton.find_attachment_for_bone(*bone);
}

} // namespace

QMap<QString, OpTarget> D3DViewport::op_states() const {
    QMap<QString, OpTarget> states;
    const ModelInstance* model = active_model();
    if (model == nullptr) {
        return states;
    }
    const auto& skeleton = model->skeleton;
    for (const auto& attachment : skeleton.attachments) {
        const QString subject =
            attachment.is_root
                ? op_root_subject()
                : bone_label(skeleton.bones[attachment.bone_index].name,
                             skeleton.bones[attachment.bone_index].name_en);
        OpTarget target;
        if (attachment.target_model == -2) {
            target.model = op_ground_target();
        } else if (attachment.target_model >= 0 &&
                   attachment.target_model < static_cast<std::int32_t>(models_.size())) {
            const ModelInstance& target_model = *models_[static_cast<std::size_t>(
                attachment.target_model)];
            target.model = target_model.name;
            if (attachment.target_bone < target_model.skeleton.bones.size()) {
                const auto& bone = target_model.skeleton.bones[attachment.target_bone];
                target.bone = bone_label(bone.name, bone.name_en);
            }
        }
        states.insert(subject, target);
    }
    return states;
}

QStringList D3DViewport::op_subject_names() const {
    QStringList names;
    const ModelInstance* model = active_model();
    if (model == nullptr) {
        return names;
    }
    const auto& skeleton = model->skeleton;
    for (const auto& attachment : skeleton.attachments) {
        if (attachment.is_root) {
            names.push_back(op_root_subject());
        } else {
            const auto& bone = skeleton.bones[attachment.bone_index];
            names.push_back(bone_label(bone.name, bone.name_en));
        }
    }
    return names;
}

QStringList D3DViewport::op_target_bone_names(int model_index) const {
    QStringList names;
    if (model_index < 0 || model_index >= static_cast<int>(models_.size())) {
        return names;
    }
    const auto& skeleton = models_[static_cast<std::size_t>(model_index)]->skeleton;
    for (const auto& bone : skeleton.bones) {
        const bool tip = (bone.flags & 0x0008) == 0;
        const bool fixed_axis = (bone.flags & 0x0400) != 0;
        if (!tip || fixed_axis) {
            names.push_back(bone_label(bone.name, bone.name_en));
        }
    }
    return names;
}

QString D3DViewport::op_display_bone_name(int model_index, const QString& name) const {
    if (model_index < 0 || model_index >= static_cast<int>(models_.size())) {
        return name;
    }
    const auto& skeleton = models_[static_cast<std::size_t>(model_index)]->skeleton;
    if (const auto index = skeleton.find_bone_index(name.toStdString())) {
        const auto& bone = skeleton.bones[*index];
        return bone_label(bone.name, bone.name_en);
    }
    return name;
}

QList<int> D3DViewport::op_target_bone_indices(int model_index) const {
    QList<int> indices;
    if (model_index < 0 || model_index >= static_cast<int>(models_.size())) {
        return indices;
    }
    const auto& skeleton = models_[static_cast<std::size_t>(model_index)]->skeleton;
    for (std::size_t i = 0; i < skeleton.bones.size(); ++i) {
        const auto& bone = skeleton.bones[i];
        const bool tip = (bone.flags & 0x0008) == 0;
        const bool fixed_axis = (bone.flags & 0x0400) != 0;
        if (!tip || fixed_axis) {
            indices.push_back(static_cast<int>(i));
        }
    }
    return indices;
}

D3DViewport::OpPreview D3DViewport::op_preview(const QString& subject,
                                               const QString& target_model,
                                               const QString& target_bone) const {
    OpPreview preview;
    const ModelInstance* model = active_model();
    if (model == nullptr || target_model.isEmpty() ||
        target_model == op_ground_target()) {
        return preview;
    }
    const auto index = op_attachment_index(model->skeleton, subject);
    const ModelInstance* target = nullptr;
    for (const auto& candidate : models_) {
        if (candidate->name == target_model) {
            target = candidate.get();
            break;
        }
    }
    if (!index || target == nullptr) {
        return preview;
    }
    const auto target_bone_index = target->skeleton.find_bone_index(target_bone.toStdString());
    if (!target_bone_index ||
        *target_bone_index >= target->skeleton.world_matrices.size()) {
        return preview;
    }
    const auto& attachment = model->skeleton.attachments[*index];
    const std::size_t bone_index =
        attachment.is_root ? 0 : attachment.bone_index;
    if (bone_index >= model->skeleton.world_matrices.size()) {
        return preview;
    }
    const Mat4 target_matrix = op_target_frame(*model, *target, *target_bone_index);
    const Mat4 palette =
        Mat4::from_translation(-model->skeleton.bones[bone_index].initial_position) *
        model->skeleton.world_matrices[bone_index];
    const Mat4 local = palette * target_matrix.inverse_transform();
    const Vec3 rest = attachment.is_root ? Vec3::zero()
                                         : model->skeleton.bones[bone_index].initial_position;
    preview.valid = true;
    preview.position = local.transform_point(rest).to_array();
    const Vec3 euler = local.rotation().to_euler_xyz();
    constexpr float kRadToDeg = 180.0f / 3.14159265358979323846f;
    preview.angles_deg = {euler.x * kRadToDeg, euler.y * kRadToDeg, euler.z * kRadToDeg};
    return preview;
}

void D3DViewport::apply_op_bindings(ModelInstance& model,
                                    const QMap<QString, OpTarget>& bindings) {
    auto& skeleton = model.skeleton;
    for (auto& attachment : skeleton.attachments) {
        attachment.target_model = -1;
        attachment.target_bone = 0;
    }
    for (auto it = bindings.begin(); it != bindings.end(); ++it) {
        const auto index = op_attachment_index(skeleton, it.key());
        if (!index) {
            continue;
        }
        auto& attachment = skeleton.attachments[*index];
        const OpTarget& target = it.value();
        if (target.model == op_ground_target()) {
            attachment.target_model = -2;
        } else if (!target.model.isEmpty()) {
            for (std::size_t i = 0; i < models_.size(); ++i) {
                if (models_[i]->name != target.model) {
                    continue;
                }
                attachment.target_model = static_cast<std::int32_t>(i);
                if (const auto bone =
                        models_[i]->skeleton.find_bone_index(target.bone.toStdString())) {
                    attachment.target_bone = *bone;
                }
                break;
            }
        }
    }
}

void D3DViewport::op_register(const QMap<QString, OpTarget>& bindings) {
    ModelInstance* model = active_model();
    if (model == nullptr) {
        return;
    }
    apply_op_bindings(*model, bindings);
    state_.register_display_keyframe(model->name, ik_states(), op_states(),
                                     static_cast<unsigned int>(
                                         std::max(0.0f, std::floor(state_.current_frame))));
    resolve_op_bindings();
    emit statusMessage(QStringLiteral("OP registration applied to '%1'")
                           .arg(model->name));
}

void D3DViewport::op_frame_register(const QString& subject, const QString& target_model,
                                    const QString& target_bone) {
    ModelInstance* model = active_model();
    if (model == nullptr) {
        return;
    }
    const auto index = op_attachment_index(model->skeleton, subject);
    if (!index) {
        return;
    }
    auto& attachment = model->skeleton.attachments[*index];
    const std::size_t bone_index = attachment.is_root ? 0 : attachment.bone_index;
    if (bone_index >= model->skeleton.bones.size()) {
        return;
    }

    const auto finish = [&](const Vec3& translation, const Quat& rotation) {
        const QString track = to_qstring(model->skeleton.bones[bone_index].name);
        state_.selected_bone_idx = static_cast<int>(bone_index);
        state_.bone_pos_override = translation.to_array();
        const Vec3 euler = rotation.to_euler_xyz();
        constexpr float kRadToDeg = 180.0f / 3.14159265358979323846f;
        state_.bone_rot_euler_deg = {euler.x * kRadToDeg, euler.y * kRadToDeg,
                                     euler.z * kRadToDeg};
        state_.bone_transform_dirty = true;
        state_.register_keyframe(track,
                                 static_cast<unsigned int>(
                                     std::max(0.0f, std::floor(state_.current_frame))));
        emit statusMessage(QStringLiteral("Registered relative pose for '%1'").arg(track));
    };

    if (target_model == op_ground_target()) {
        attachment.target_model = -2;
        attachment.target_bone = 0;
        if (attachment.is_root) {
            finish(Vec3::zero(), Quat::identity());
            return;
        }
        const Vec3 world_position = model->skeleton.world_positions[bone_index];
        const Quat world_rotation = model->skeleton.world_rotations[bone_index];
        const Vec3 translation =
            world_position - model->skeleton.bones[bone_index].initial_position;
        model->skeleton.set_bone_transform(bone_index, translation, world_rotation);
        model->skeleton.update_transforms();
        finish(translation, world_rotation);
        return;
    }

    ModelInstance* target = model_by_name(target_model);
    if (target == nullptr) {
        return;
    }
    const auto target_bone_index =
        target->skeleton.find_bone_index(target_bone.toStdString());
    if (!target_bone_index ||
        *target_bone_index >= target->skeleton.world_matrices.size()) {
        return;
    }
    // Bind the live registration without re-evaluating yet: the bake below
    // must start from the pose the user currently sees.
    attachment.target_model = static_cast<std::int32_t>(
        std::distance(models_.begin(),
                      std::find_if(models_.begin(), models_.end(),
                                   [target](const auto& entry) {
                                       return entry.get() == target;
                                   })));
    attachment.target_bone = *target_bone_index;
    const Vec3 rest = attachment.is_root ? Vec3::zero()
                                         : model->skeleton.bones[bone_index].initial_position;
    const Mat4 target_matrix = op_target_frame(*model, *target, *target_bone_index);
    const Mat4 palette =
        Mat4::from_translation(-rest) * model->skeleton.world_matrices[bone_index];
    const Mat4 local = palette * target_matrix.inverse_transform();
    const Vec3 translation = local.transform_point(rest);
    const Quat rotation = local.rotation();
    model->skeleton.set_bone_transform(bone_index, translation, rotation);
    model->skeleton.update_transforms();
    finish(translation, rotation);
}

void D3DViewport::reset_camera() {
    state_.camera_yaw = 0.0f;
    state_.camera_pitch = 0.0f;
    state_.camera_roll = 0.0f;
    state_.camera_target = CameraTarget::Free;
    state_.camera_follow_offset = {0.0f, 0.0f, 0.0f};
    const ModelInstance* model = active_model();
    if (model == nullptr) {
        state_.camera_distance = 45.0f;
        state_.camera_pan = {0.0f, 10.0f, 0.0f};
        return;
    }
    const Vec3 center =
        (model->bounds_min + model->bounds_max) * 0.5f * model->scale + model->offset;
    const float height =
        std::max((model->bounds_max.y - model->bounds_min.y) * model->scale, 1.0f);
    const float radius = std::max(model->shadow_radius * model->scale, 1.0f);
    state_.camera_pan = center.to_array();
    // Positive studio distance means the eye is away from the focus.
    state_.camera_distance = std::max({height * 1.6f, radius * 3.0f, 10.0f});
}

bool D3DViewport::capture_image(const QString& path) {
    if (!context_) {
        return false;
    }
    try {
        const auto capture = context_->capture_scene();
        core::save_bmp(to_wstring(path), capture.width, capture.height, capture.rgba);
        emit statusMessage(QStringLiteral("Screenshot saved to: %1").arg(path));
        return true;
    } catch (const std::exception& error) {
        emit statusMessage(QStringLiteral("Capture failed: %1").arg(QString::fromUtf8(error.what())));
        return false;
    }
}

bool D3DViewport::render_frames(const QString& directory, int start, int end, int fps) {
    if (!context_ || !renderer_) {
        return false;
    }
    QDir().mkpath(directory);
    const float saved_frame = state_.current_frame;
    const bool saved_playing = state_.is_playing;
    state_.is_playing = false;
    ModelInstance* active = active_model();
    for (auto& model : models_) {
        if (model->physics_rig) {
            model->physics_rig->request_reset();
        }
    }
    for (int frame = start; frame <= end; ++frame) {
        state_.current_frame = static_cast<float>(frame);
        if (!motion_player_ && active != nullptr) {
            active->morphs.reset();
        }
        evaluate_frame(static_cast<float>(frame));
        std::vector<animation::MorphResult> morph_results;
        morph_results.reserve(models_.size());
        for (auto& model : models_) {
            if (model.get() == active) {
                for (auto it = state_.morph_weights.begin();
                     it != state_.morph_weights.end(); ++it) {
                    model->morphs.set_weight(it.key().toStdString(), it.value());
                }
            }
            morph_results.push_back(model->morphs.evaluate());
            apply_bone_morphs(*model, morph_results.back());
            if (model.get() == active) {
                apply_editor_bone_override();
            }
        }
        resolve_op_bindings();
        for (std::size_t i = 0; i < models_.size(); ++i) {
            ModelInstance& model = *models_[i];
            const animation::MorphResult& morph_result = morph_results[i];
            if (model.physics_rig) {
                for (const auto& impulse : morph_result.impulses) {
                    model.physics_rig->apply_impulse(impulse.body_index, impulse.local,
                                                     impulse.velocity, impulse.torque);
                }
            }
            if (physics_active() && model.physics_rig) {
                model.skeleton.apply_physics(*model.physics_rig,
                                             1.0f / static_cast<float>(std::max(fps, 1)));
            }
            apply_material_morphs(model, morph_result);
            model.mesh.apply_morph_offsets(*context_, morph_result.position_offsets,
                                           morph_result.uv_offsets);
        }
        update_camera();
        render_scene();
        try {
            const auto capture = context_->capture_scene();
            const QString file = QDir(directory).filePath(
                QStringLiteral("frame_%1.bmp").arg(frame, 5, 10, QLatin1Char('0')));
            core::save_bmp(to_wstring(file), capture.width, capture.height, capture.rgba);
        } catch (...) {
        }
        context_->present(1);
    }
    state_.current_frame = saved_frame;
    state_.is_playing = saved_playing;
    emit statusMessage(QStringLiteral("Rendered frames %1..%2 to: %3")
                           .arg(start)
                           .arg(end)
                           .arg(directory));
    return true;
}

void D3DViewport::tick() {
    try {
        tick_impl();
    } catch (const std::exception& error) {
        core::log_msg(std::string("[studio] frame failed: ") + error.what());
        timer_->stop();
        emit statusMessage(QStringLiteral("Frame failed: %1").arg(QString::fromUtf8(error.what())));
    } catch (...) {
        core::log_msg("[studio] frame failed with an unknown exception");
        timer_->stop();
        emit statusMessage(QStringLiteral("Frame failed with an unknown error"));
    }
}

void D3DViewport::tick_impl() {
    if (!context_ || !renderer_ || !isVisible()) {
        return;
    }
    if (state_.screen_size_width != applied_screen_width_ ||
        state_.screen_size_height != applied_screen_height_) {
        apply_screen_size();
    }
    const float dt = std::clamp(frame_timer_.restart() / 1000.0f, 0.0001f, 0.1f);
    const int target_interval = state_.save_cpu_power && !state_.is_playing ? 33 : 1;
    if (timer_->interval() != target_interval) {
        timer_->setInterval(target_interval);
    }

    if (state_.is_playing) {
        state_.current_frame += dt * 30.0f;
        if (state_.current_frame > static_cast<float>(state_.play_range.second)) {
            if (state_.loop_playback) {
                state_.current_frame = static_cast<float>(state_.play_range.first);
                for (auto& model : models_) {
                    if (model->physics_rig) {
                        model->physics_rig->request_reset();
                    }
                }
            } else {
                state_.current_frame = static_cast<float>(state_.play_range.second);
                state_.is_playing = false;
            }
        }
        state_.jump_frame_input = QString::number(static_cast<int>(state_.current_frame));
    }

    sync_audio_playback();

    if (state_.model_delete_requested) {
        state_.model_delete_requested = false;
        remove_active_model();
    }

    ModelInstance* active = active_model();
    if (!motion_player_ && active != nullptr) {
        active->morphs.reset();
    }
    evaluate_frame(state_.current_frame);

    if (active != nullptr) {
        for (auto it = state_.morph_weights.begin(); it != state_.morph_weights.end(); ++it) {
            active->morphs.set_weight(it.key().toStdString(), it.value());
        }
        apply_editor_bone_override();
    }
    resolve_op_bindings();

    if (state_.reset_physics_requested) {
        state_.reset_physics_requested = false;
        request_physics_reset();
    }
    const bool physics_active_now = physics_active();
    for (auto& model : models_) {
        if (!physics_active_now && model.get() != active) {
            if (model->jiggle) {
                model->jiggle->reset();
            }
            if (model->physics_rig) {
                model->skeleton.reset_physics_bones(*model->physics_rig);
            }
            model->skeleton.update_transforms();
        }
        const animation::MorphResult morph_result = model->morphs.evaluate();
        apply_bone_morphs(*model, morph_result);
        if (model.get() == active) {
            apply_editor_bone_override();
        }
        if (model->physics_rig) {
            model->physics_rig->set_gravity_vec(state_.gravity);
            model->physics_rig->set_time_scale(state_.physics_speed);
            model->physics_rig->set_ground_enabled(state_.ground_collision);
            if (physics_active_now) {
                for (const auto& impulse : morph_result.impulses) {
                    model->physics_rig->apply_impulse(impulse.body_index, impulse.local,
                                                      impulse.velocity, impulse.torque);
                }
            }
            if (!physics_active_now && physics_was_active_) {
                // Switching physics off parks the bodies at the authored pose.
                model->physics_rig->reset(model->skeleton.get_bone_world_transforms());
            }
        }
        if (physics_active_now && model->physics_rig) {
            model->skeleton.apply_physics(*model->physics_rig, dt);
        }
        if (physics_active_now && state_.jiggle_enabled && model->jiggle) {
            model->skeleton.apply_jiggle(*model->jiggle, dt, model->jiggle_settings);
        }
        apply_material_morphs(*model, morph_result);
        model->mesh.apply_morph_offsets(*context_, morph_result.position_offsets,
                                        morph_result.uv_offsets);
    }
    physics_was_active_ = physics_active_now;

    update_camera();
    render_scene();
    context_->present(state_.vsync || state_.fps_limit == FpsLimit::Fps60 ? 1 : 0);

    ++fps_frames_;
    const qint64 elapsed = fps_timer_.elapsed();
    if (elapsed >= 500) {
        current_fps_ = static_cast<float>(fps_frames_) * 1000.0f / static_cast<float>(elapsed);
        current_frame_ms_ = static_cast<float>(elapsed) / static_cast<float>(fps_frames_);
        fps_frames_ = 0;
        fps_timer_.restart();
        emit statsChanged(current_fps_, current_frame_ms_);
    }

    if (state_.fps_limit == FpsLimit::Fps30) {
        const qint64 budget = 33;
        while (frame_timer_.elapsed() < budget) {
            QThread::msleep(1);
        }
    }
}

void D3DViewport::update_camera() {
    camera_.fov_y = state_.camera_fov_deg * 3.14159265358979323846f / 180.0f;
    camera_.orthographic = state_.camera_orthographic;
    camera_.near_z = std::clamp(state_.camera_near_clip, 0.01f,
                                std::max(state_.camera_far_clip * 0.5f, 0.02f));
    camera_.far_z = std::max(state_.camera_far_clip, camera_.near_z + 0.5f);
    const QSize render_size = effective_screen_size();
    camera_.aspect = static_cast<float>(render_size.width()) /
                     static_cast<float>(std::max(render_size.height(), 1));
    Vec3 focus = Vec3::from_array(state_.camera_pan);
    if (state_.mode == EditorMode::Model && state_.camera_target != CameraTarget::Free) {
        if (const ModelInstance* model = active_model()) {
            bool resolved = false;
            if (state_.camera_target == CameraTarget::Model) {
                focus = (model->bounds_min + model->bounds_max) * 0.5f * model->scale +
                        model->offset;
                resolved = true;
            } else if (state_.camera_target == CameraTarget::SelectedBone &&
                       state_.selected_bone_idx) {
                const std::size_t index =
                    static_cast<std::size_t>(*state_.selected_bone_idx);
                if (index < model->skeleton.world_positions.size()) {
                    focus = model->skeleton.world_positions[index] * model->scale + model->offset;
                    resolved = true;
                }
            }
            if (resolved) {
                state_.camera_pan = focus.to_array();
            }
        }
    }

    // Follow bone (MMD "Track"): the pivot is the bone's world position and the
    // studio keeps an extra focus offset in the bone's local frame.
    Vec3 follow_bone;
    Quat follow_rotation;
    float follow_scale = 1.0f;
    const bool following = camera_follow_state(follow_bone, follow_rotation, follow_scale);
    if (following) {
        const Vec3 local_offset = Vec3::from_array(state_.camera_follow_offset);
        focus = follow_bone + follow_rotation.rotate_vec3(local_offset * follow_scale);
        state_.camera_pan = focus.to_array();
    }

    // MMD rotates the scene by Ry(yaw) * Rx(pitch) * Rz(roll) around the focus
    // and looks down +Z from a fixed eye, so the eye's Z is the signed camera
    // distance. place_mmd() is the equivalent world camera: both the eye offset
    // and the basis come from the inverse of that rotation.
    const Quat orientation = camera_orientation();
    const float distance_scale = following ? follow_scale : 1.0f;
    // Studio convention: positive distance means the eye is farther from the
    // focus, negative means it crossed to the other side. The MMD eye sits on
    // the negative local Z axis, so the sign flips here.
    camera_.place_mmd(focus, orientation, -state_.camera_distance * distance_scale);
    camera_.ortho_height = render::Camera::mmd_ortho_height(state_.camera_distance,
                                                            camera_.aspect, distance_scale);
}

bool D3DViewport::camera_follow_state(Vec3& bone_position, Quat& rotation,
                                      float& scale) const {
    const bool enabled =
        state_.camera_follow && state_.camera_parent_model >= 0 &&
        (state_.mode == EditorMode::Camera || state_.is_playing ||
         active_model_ == state_.camera_parent_model);
    if (!enabled || state_.camera_parent_model >= static_cast<int>(models_.size())) {
        return false;
    }
    const ModelInstance& parent =
        *models_[static_cast<std::size_t>(state_.camera_parent_model)];
    const int bone_index = state_.camera_parent_bone;
    if (bone_index < 0 ||
        static_cast<std::size_t>(bone_index) >= parent.skeleton.world_rotations.size() ||
        static_cast<std::size_t>(bone_index) >= parent.skeleton.world_positions.size()) {
        return false;
    }
    const auto index = static_cast<std::size_t>(bone_index);
    bone_position = parent.skeleton.world_positions[index] * parent.scale + parent.offset;
    rotation = parent.skeleton.world_rotations[index];
    scale = std::max(parent.scale, 1.0e-4f);
    return true;
}

Quat D3DViewport::camera_orientation() const {
    Quat orientation =
        render::mmd_camera_orientation(state_.camera_yaw, state_.camera_pitch,
                                       state_.camera_roll);
    Vec3 bone_position;
    Quat bone_rotation;
    float scale = 1.0f;
    if (!camera_follow_state(bone_position, bone_rotation, scale)) {
        return orientation;
    }
    // The reference attaches the camera to the bone by composing the bone
    // rotation outside the camera rotation.
    return (bone_rotation * orientation).normalize();
}

void D3DViewport::sync_audio_playback() {
    if (!audio_player_ || !audio_player_->has_track()) {
        audio_playing_ = false;
        return;
    }
    state_.audio_volume = std::clamp(state_.audio_volume, 0.0f, 1.0f);
    audio_player_->set_volume(state_.audio_volume);
    const double expected =
        (static_cast<double>(state_.current_frame) - state_.audio_offset_frames) / 30.0;
    const bool in_range =
        expected >= 0.0 && expected < audio_player_->duration_seconds();
    if (state_.is_playing && in_range) {
        // The motion clock is authoritative. Restart the audio voice only when
        // playback starts, wraps, or drifts far enough to be audible.
        if (!audio_playing_ ||
            std::abs(audio_player_->position_seconds() - expected) > 0.2) {
            audio_player_->play(expected);
            audio_playing_ = true;
        }
    } else if (audio_playing_) {
        audio_player_->stop();
        audio_playing_ = false;
    }
}

bool D3DViewport::physics_active() const {
    return state_.physics_mode != PhysicsMode::Disabled &&
           (!state_.playtime_only || state_.is_playing);
}

void D3DViewport::evaluate_frame(float frame) {
    ModelInstance* model = active_model();
    if (model != nullptr && !physics_active()) {
        // MMD freezes physics off at the authored pose, not at the last simulated one.
        // Breast/hip jiggle is a separate spring layer, so it must be cleared too.
        if (model->jiggle) {
            model->jiggle->reset();
        }
        if (model->physics_rig) {
            model->skeleton.reset_physics_bones(*model->physics_rig);
        }
        model->skeleton.update_transforms();
    }
    if (frame != last_editor_frame_) {
        // A pending scale edit belongs to the frame where it was made. Moving the
        // playhead hands control back to the authored scale track.
        state_.model_scale_dirty = false;
    }
    const float evaluated =
        motion_player_ && motion_player_->max_frame > 0
            ? std::fmod(frame, static_cast<float>(motion_player_->max_frame) + 1.0f)
            : frame;
    const bool scene_changed = state_.is_playing || state_.track_keys_dirty ||
                               frame != last_editor_frame_;
    if (scene_changed && motion_player_) {
        if (const auto sample = motion_player_->evaluate_scene(evaluated)) {
            if (sample->camera) {
                state_.camera_pan = sample->camera->position;
                state_.camera_yaw = sample->camera->rotation[1];
                state_.camera_pitch = sample->camera->rotation[0];
                state_.camera_roll = sample->camera->rotation[2];
                // VMD stores MMD's negative eye offset; the studio distance is
                // positive when the eye is away from the focus.
                state_.camera_distance = -sample->camera->distance;
                state_.camera_fov_deg = sample->camera->fov;
                state_.camera_orthographic = !sample->camera->perspective;
            }
            if (sample->light) {
                state_.light_color = sample->light->color;
                state_.light_direction = sample->light->direction;
            }
            if (sample->shadow) {
                state_.shadow_mode = sample->shadow->mode <= 2
                                         ? static_cast<ShadowMode>(sample->shadow->mode)
                                         : ShadowMode::Off;
                state_.shadow_range = sample->shadow->distance;
            }
        }
    }
    if (scene_changed) {
        for (auto& candidate : models_) {
            apply_ik_states(*candidate, frame, evaluated);
        }
    }
    if (model != nullptr && motion_player_) {
        motion_player_->evaluate_pose(evaluated, model->skeleton, model->morphs);
    }
    apply_editor_tracks(model, frame, scene_changed);
    state_.track_keys_dirty = false;
    last_editor_frame_ = frame;
}

void D3DViewport::apply_ik_states(ModelInstance& model, float timeline_frame,
                                  float motion_frame) {
    animation::Skeleton& skeleton = model.skeleton;
    if (skeleton.ik_enabled.size() != skeleton.bones.size()) {
        skeleton.ik_enabled.assign(skeleton.bones.size(), true);
    }
    std::fill(skeleton.ik_enabled.begin(), skeleton.ik_enabled.end(), true);
    const bool active = &model == active_model();
    if (active && motion_player_) {
        motion_player_->apply_ik(motion_frame, skeleton);
    }

    const auto track = state_.track_keyframes.find(QStringLiteral("Disp/IK/OP"));
    if (track == state_.track_keyframes.end() || track->second.empty()) {
        return;
    }
    const auto next = track->second.upper_bound(
        static_cast<unsigned int>(std::max(0.0f, std::floor(timeline_frame))));
    const TrackKeyframe& key =
        next == track->second.begin() ? track->second.begin()->second : std::prev(next)->second;

    const auto apply_ik = [&skeleton](const QMap<QString, bool>& states) {
        for (auto it = states.begin(); it != states.end(); ++it) {
            const auto index = skeleton.find_bone_index(it.key().toStdString());
            if (index && *index < skeleton.ik_enabled.size()) {
                skeleton.ik_enabled[*index] = it.value();
            }
        }
    };

    if (!key.model_ik.isEmpty()) {
        const auto per_model_ik = key.model_ik.find(model.name);
        if (per_model_ik != key.model_ik.end()) {
            apply_ik(*per_model_ik);
        }
    } else if (active) {
        apply_ik(key.ik_enabled);
    }

    if (!key.model_op.isEmpty()) {
        const auto per_model_op = key.model_op.find(model.name);
        if (per_model_op != key.model_op.end()) {
            apply_op_bindings(model, *per_model_op);
        }
    } else if (active && !key.op_bindings.isEmpty()) {
        apply_op_bindings(model, key.op_bindings);
    }
}

QMap<QString, D3DViewport::OpWindow> D3DViewport::op_windows(float timeline_frame) const {
    QMap<QString, OpWindow> windows;
    const ModelInstance* model = active_model();
    if (model == nullptr) {
        return windows;
    }
    const auto track = state_.track_keyframes.find(QStringLiteral("Disp/IK/OP"));
    if (track == state_.track_keyframes.end() || track->second.empty()) {
        return windows;
    }
    std::vector<std::pair<unsigned int, const TrackKeyframe*>> keys;
    keys.reserve(track->second.size());
    for (const auto& [frame, key] : track->second) {
        keys.emplace_back(frame, &key);
    }
    const auto next = track->second.upper_bound(
        static_cast<unsigned int>(std::max(0.0f, std::floor(timeline_frame))));
    std::size_t current = 0;
    if (next != track->second.begin()) {
        current = static_cast<std::size_t>(
            std::distance(track->second.begin(), std::prev(next)));
    }

    const auto pair_for = [&](const TrackKeyframe& key, const QString& subject,
                              const QString& fallback) {
        const auto lookup = [&subject, &fallback](const QMap<QString, OpTarget>& entries) {
            auto entry = entries.find(subject);
            if (entry == entries.end() && !fallback.isEmpty()) {
                entry = entries.find(fallback);
            }
            return entry != entries.end()
                       ? entry->model + QLatin1Char('\n') + entry->bone
                       : QString();
        };
        if (!key.model_op.isEmpty()) {
            const auto per_model = key.model_op.find(model->name);
            return per_model == key.model_op.end() ? QString() : lookup(*per_model);
        }
        return lookup(key.op_bindings);
    };

    for (const auto& attachment : model->skeleton.attachments) {
        const QString subject =
            attachment.is_root
                ? op_root_subject()
                : bone_label(model->skeleton.bones[attachment.bone_index].name,
                             model->skeleton.bones[attachment.bone_index].name_en);
        const QString fallback =
            attachment.is_root
                ? QString()
                : to_qstring(model->skeleton.bones[attachment.bone_index].name);
        const QString active_pair = pair_for(*keys[current].second, subject, fallback);
        OpWindow window;
        std::size_t index = current;
        while (index > 0 &&
               pair_for(*keys[index - 1].second, subject, fallback) == active_pair) {
            --index;
        }
        window.start = index == 0 ? 0 : keys[index].first;
        index = current;
        while (index + 1 < keys.size() &&
               pair_for(*keys[index + 1].second, subject, fallback) == active_pair) {
            ++index;
        }
        window.end = index + 1 < keys.size() ? keys[index + 1].first : 0;
        windows.insert(subject, window);
    }
    return windows;
}

void D3DViewport::resolve_op_bindings() {
    bool any_attachments = false;
    for (auto& model : models_) {
        if (model->skeleton.has_active_attachments()) {
            any_attachments = true;
            break;
        }
    }
    if (!any_attachments) {
        for (auto& model : models_) {
            model->skeleton.clear_external_parents();
        }
        return;
    }

    // Base pose first so every target matrix describes the current frame.
    for (auto& model : models_) {
        model->skeleton.clear_external_parents();
        model->skeleton.update_transforms();
    }

    // Chains settle one link per pass; a cycle simply stops changing when the
    // pass budget runs out, matching MMD's fixed evaluation order fallback.
    const int passes = std::max<int>(1, static_cast<int>(models_.size()));
    for (int pass = 0; pass < passes; ++pass) {
        for (std::size_t subject_index = 0; subject_index < models_.size();
             ++subject_index) {
            ModelInstance& subject = *models_[subject_index];
            auto& skeleton = subject.skeleton;
            skeleton.clear_external_parents();
            if (!skeleton.has_active_attachments()) {
                continue;
            }
            bool changed = false;
            for (const auto& attachment : skeleton.attachments) {
                if (attachment.target_model == -1) {
                    continue;
                }
                if (attachment.target_model == -2) {
                    if (attachment.is_root) {
                        continue; // the model root already sits on the ground
                    }
                    if (attachment.bone_index >= skeleton.external_parents.size()) {
                        continue;
                    }
                    auto& frame = skeleton.external_parents[attachment.bone_index];
                    frame.active = true;
                    frame.ground = true;
                    changed = true;
                    continue;
                }
                if (attachment.target_model < 0 ||
                    attachment.target_model >= static_cast<std::int32_t>(models_.size())) {
                    continue;
                }
                const ModelInstance& target =
                    *models_[static_cast<std::size_t>(attachment.target_model)];
                if (attachment.target_bone >= target.skeleton.world_matrices.size()) {
                    continue;
                }
                const Mat4 target_matrix =
                    op_target_frame(subject, target, attachment.target_bone);
                if (attachment.is_root) {
                    skeleton.root_external_active = true;
                    skeleton.root_external_matrix = target_matrix;
                    changed = true;
                } else if (attachment.bone_index < skeleton.external_parents.size()) {
                    auto& frame = skeleton.external_parents[attachment.bone_index];
                    frame.active = true;
                    frame.ground = false;
                    frame.target_matrix = target_matrix;
                    changed = true;
                }
            }
            if (changed) {
                skeleton.update_transforms();
            }
        }
    }
}

void D3DViewport::apply_editor_tracks(ModelInstance* model, float frame,
                                      bool apply_scene_values) {
    bool bones_changed = false;
    const QMap<QString, OpWindow> windows =
        model != nullptr ? op_windows(frame) : QMap<QString, OpWindow>{};
    for (const auto& [track, keys] : state_.track_keyframes) {
        if (keys.empty()) {
            continue;
        }
        const TrackKeyframe* first = nullptr;
        const TrackKeyframe* second = nullptr;
        float t = 0.0f;
        const auto next = keys.upper_bound(
            static_cast<unsigned int>(std::max(0.0f, std::floor(frame))));
        if (next == keys.begin()) {
            first = &keys.begin()->second;
            second = first;
        } else {
            const auto lower = std::prev(next);
            first = &lower->second;
            if (next == keys.end()) {
                second = first;
            } else {
                second = &next->second;
                const float span = static_cast<float>(next->first - lower->first);
                t = span > 0.0f
                        ? std::clamp((frame - static_cast<float>(lower->first)) / span, 0.0f,
                                     1.0f)
                        : 0.0f;
            }
        }
        const float ease = animation::evaluate_keyframe_ease(second->interpolation, t);
        const auto mix = [ease](float a, float b) { return a + (b - a) * ease; };
        const auto mix3 = [&mix](const std::array<float, 3>& a,
                                 const std::array<float, 3>& b) {
            return std::array<float, 3>{mix(a[0], b[0]), mix(a[1], b[1]), mix(a[2], b[2])};
        };

        if (track == QStringLiteral("model scale")) {
            if (model != nullptr && !state_.model_scale_dirty) {
                model->scale = std::clamp(mix(first->scale, second->scale), 0.05f, 20.0f);
                state_.model_scale = model->scale;
            }
            continue;
        }

        const bool scene_track =
            track == QStringLiteral("camera") || track == QStringLiteral("light") ||
            track == QStringLiteral("shadow") || track == QStringLiteral("gravity") ||
            track == QStringLiteral("accessory");
        if (scene_track) {
            if (!apply_scene_values) {
                continue;
            }
            if (track == QStringLiteral("camera")) {
                state_.camera_pan = mix3(first->position, second->position);
                state_.camera_yaw = mix(first->angles[0], second->angles[0]);
                state_.camera_pitch = mix(first->angles[1], second->angles[1]);
                state_.camera_roll = mix(first->angles[2], second->angles[2]);
                state_.camera_distance = mix(first->distance, second->distance);
                state_.camera_fov_deg = mix(first->fov, second->fov);
                // MMD copies the parent pair from the lower key without
                // interpolating it.
                state_.camera_parent_model = first->parent_model;
                state_.camera_parent_bone = first->parent_bone;
                state_.camera_follow_offset = first->follow_offset;
            } else if (track == QStringLiteral("light")) {
                state_.light_color = mix3(first->position, second->position);
                state_.light_direction = mix3(first->angles, second->angles);
            } else if (track == QStringLiteral("shadow")) {
                state_.shadow_mode =
                    static_cast<ShadowMode>(std::lround(mix(first->weight, second->weight)));
                state_.shadow_range = mix(first->distance, second->distance);
                state_.shadow_detail = mix(first->fov, second->fov);
            } else if (track == QStringLiteral("gravity")) {
                state_.gravity = mix3(first->position, second->position);
                state_.gravity_noise = mix(first->weight, second->weight);
            } else if (track == QStringLiteral("accessory")) {
                state_.accessory_pos = mix3(first->position, second->position);
                state_.accessory_rot = mix3(first->angles, second->angles);
                state_.accessory_alpha = mix(first->weight, second->weight);
                state_.accessory_scale = mix(first->scale, second->scale);
            }
            continue;
        }

        if (track == QStringLiteral("view cnt") || track == QStringLiteral("Disp/IK/OP") ||
            track == QStringLiteral("Facial")) {
            continue;
        }
        if (model == nullptr) {
            continue;
        }

        if (const auto index = model->skeleton.find_bone_index(track.toStdString())) {
            // MMD display-key windows snap the bone at an OP/IK change instead
            // of blending the authored keys across the boundary.
            float bone_ease = ease;
            const auto window = windows.find(track);
            if (window != windows.end()) {
                const unsigned int lower =
                    next == keys.begin() ? 0 : std::prev(next)->first;
                const unsigned int upper =
                    next == keys.end() ? std::numeric_limits<unsigned int>::max()
                                       : next->first;
                const unsigned int current = static_cast<unsigned int>(
                    std::max(0.0f, std::floor(frame)));
                if (window->start > lower && window->start <= upper) {
                    bone_ease = current >= window->start ? 1.0f : 0.0f;
                } else if (window->end > lower && window->end <= upper) {
                    bone_ease = current >= window->end ? 1.0f : 0.0f;
                }
            }
            const auto bone_mix = [bone_ease](float a, float b) {
                return a + (b - a) * bone_ease;
            };
            model->skeleton.local_translations[*index] =
                Vec3(bone_mix(first->position[0], second->position[0]),
                     bone_mix(first->position[1], second->position[1]),
                     bone_mix(first->position[2], second->position[2]));
            model->skeleton.local_rotations[*index] =
                Quat::from_array(first->rotation)
                    .slerp(Quat::from_array(second->rotation), bone_ease);
            bones_changed = true;
        } else {
            model->morphs.set_weight(track.toStdString(), mix(first->weight, second->weight));
        }
    }
    if (bones_changed && model != nullptr) {
        model->skeleton.update_transforms();
    }
}

void D3DViewport::apply_bone_morphs(ModelInstance& model,
                                    const animation::MorphResult& result) {
    bool changed = false;
    const std::size_t count =
        std::min(result.bone_offsets.size(), model.skeleton.bones.size());
    for (std::size_t i = 0; i < count; ++i) {
        const auto& offset = result.bone_offsets[i];
        const bool has_translation = offset.translation.x != 0.0f ||
                                     offset.translation.y != 0.0f ||
                                     offset.translation.z != 0.0f;
        const bool has_rotation = offset.rotation.x != 0.0f || offset.rotation.y != 0.0f ||
                                  offset.rotation.z != 0.0f || offset.rotation.w != 1.0f;
        if (!has_translation && !has_rotation) {
            continue;
        }
        if (has_translation) {
            model.skeleton.local_translations[i] += offset.translation;
        }
        if (has_rotation) {
            model.skeleton.local_rotations[i] =
                (model.skeleton.local_rotations[i] * offset.rotation).normalize();
        }
        changed = true;
    }
    if (changed) {
        model.skeleton.update_transforms();
    }
}

void D3DViewport::apply_material_morphs(ModelInstance& model,
                                        const animation::MorphResult& result) {
    if (!context_) {
        return;
    }
    const std::size_t count = std::min(result.material_offsets.size(), model.materials.size());
    for (std::size_t i = 0; i < count; ++i) {
        const auto& offset = result.material_offsets[i];
        render::MaterialConstants constants;
        constants.diffuse = offset.diffuse;
        constants.specular = offset.specular;
        constants.shininess = offset.shininess;
        constants.ambient = offset.ambient;
        constants.edge_color = offset.edge_color;
        constants.edge_size = offset.edge_size;
        model.materials[i].set_constants(*context_, constants);
    }
}

void D3DViewport::apply_editor_bone_override() {
    ModelInstance* model = active_model();
    if (model == nullptr || !state_.selected_bone_idx) {
        return;
    }
    const std::size_t index = static_cast<std::size_t>(*state_.selected_bone_idx);
    if (index >= model->skeleton.bones.size()) {
        return;
    }
    if (state_.bone_transform_dirty) {
        const auto& bone = model->skeleton.bones[index];
        const bool movable = (bone.flags & 0x0004) != 0 || bone.has_ik || !bone.parent_index;
        if (movable) {
            model->skeleton.local_translations[index] = Vec3::from_array(state_.bone_pos_override);
        }
        const Vec3 euler(state_.bone_rot_euler_deg[0] * 3.14159265358979323846f / 180.0f,
                         state_.bone_rot_euler_deg[1] * 3.14159265358979323846f / 180.0f,
                         state_.bone_rot_euler_deg[2] * 3.14159265358979323846f / 180.0f);
        model->skeleton.local_rotations[index] = Quat::from_euler_xyz(euler);
        model->skeleton.update_transforms();
    } else {
        const Vec3 translation = model->skeleton.local_translations[index];
        state_.bone_pos_override = translation.to_array();
        const Vec3 euler = model->skeleton.local_rotations[index].to_euler_xyz();
        state_.bone_rot_euler_deg = {euler.x * 180.0f / 3.14159265358979323846f,
                                     euler.y * 180.0f / 3.14159265358979323846f,
                                     euler.z * 180.0f / 3.14159265358979323846f};
    }
}

void D3DViewport::render_scene() {
    context_->begin_scene();
    const float clear_color[4] = {0.13f, 0.14f, 0.17f, 1.0f};
    context_->clear(clear_color, 1.0f, 0);
    context_->set_viewport(static_cast<float>(context_->render_width),
                           static_cast<float>(context_->render_height));
    renderer_->set_light(state_.light_color, state_.light_direction);
    renderer_->update_camera(*context_, camera_);
    renderer_->wireframe = state_.wireframe;

    std::vector<Microsoft::WRL::ComPtr<ID3D11CommandList>> lists;
    const auto add_list = [&lists](Microsoft::WRL::ComPtr<ID3D11CommandList> list) {
        if (list) {
            lists.push_back(std::move(list));
        }
    };

    const std::array<float, 4> background =
        state_.viewport_white_bg ? std::array<float, 4>{0.98f, 0.98f, 0.99f, 1.0f}
                                 : std::array<float, 4>{0.19f, 0.22f, 0.27f, 1.0f};
    add_list(renderer_->record_background(*context_, background));
    if (state_.show_grid) {
        add_list(renderer_->record_grid(*context_));
    }

    std::vector<render::GroundShadow> shadows;
    if (state_.ground_shadow) {
        shadows.reserve(models_.size());
        for (const auto& model : models_) {
            if (!model->visible) {
                continue;
            }
            shadows.push_back({Vec3(model->shadow_center.x * model->scale + model->offset.x,
                                    0.0f,
                                    model->shadow_center.z * model->scale + model->offset.z),
                               model->shadow_radius * model->scale,
                               {0.10f, 0.11f, 0.14f, 0.32f}});
        }
    }
    add_list(renderer_->record_ground_shadows(*context_, shadows));

    std::vector<render::SubsetDraw> opaque_draws;
    std::vector<render::SubsetDraw> transparent_draws;
    const Vec3 view_forward = camera_.target - camera_.position;
    for (const auto& model : models_) {
        if (!model->visible || model->mesh.subsets.empty()) {
            continue;
        }
        render_palette_.resize(model->skeleton.skinning_palette.size());
        const Mat4 model_transform =
            Mat4::from_scale(Vec3(model->scale, model->scale, model->scale)) *
            Mat4::from_translation(model->offset);
        for (std::size_t i = 0; i < render_palette_.size(); ++i) {
            render_palette_[i] = model->skeleton.skinning_palette[i] * model_transform;
        }
        model->skinning.update(*context_, render_palette_);

        for (std::size_t i = 0; i < model->mesh.subsets.size(); ++i) {
            const render::MeshSubset& subset = model->mesh.subsets[i];
            const bool transparent =
                subset.material_index < model->materials.size() &&
                model->materials[subset.material_index].constants.diffuse[3] < 0.999f;
            render::SubsetDraw draw;
            draw.mesh = &model->mesh;
            draw.materials = &model->materials;
            draw.textures = &model->textures;
            draw.skinning = &model->skinning;
            draw.subset = i;
            if (transparent) {
                const Vec3 center = subset_world_center(model->pmx.indices,
                                                        model->mesh.current_vertices, subset,
                                                        render_palette_);
                draw.depth = (center - camera_.position).dot(view_forward);
                transparent_draws.push_back(draw);
            } else {
                opaque_draws.push_back(draw);
            }
        }
    }

    const auto opaque_lists =
        renderer_->record_mesh_draws_parallel(*context_, opaque_draws, false);
    lists.insert(lists.end(), opaque_lists.begin(), opaque_lists.end());

    std::stable_sort(transparent_draws.begin(), transparent_draws.end(),
                     [](const render::SubsetDraw& a, const render::SubsetDraw& b) {
                         return a.depth > b.depth;
                     });
    const auto transparent_lists =
        renderer_->record_mesh_draws_parallel(*context_, transparent_draws, true);
    lists.insert(lists.end(), transparent_lists.begin(), transparent_lists.end());

    add_list(renderer_->record_overlay_geometry(*context_, build_overlay_geometry()));

    for (const auto& list : lists) {
        context_->execute_command_list(list.Get(), false);
    }
    context_->end_scene();

    if (context_->scene_target_ready()) {
        const QRect target = scene_rect();
        const float letterbox[4] = {0.05f, 0.05f, 0.06f, 1.0f};
        context_->clear_backbuffer(letterbox);
        context_->blit_scene_to_backbuffer(static_cast<float>(target.x()),
                                           static_cast<float>(target.y()),
                                           static_cast<float>(target.width()),
                                           static_cast<float>(target.height()));
    }
}

render::OverlayGeometry D3DViewport::build_overlay_geometry() const {
    render::OverlayGeometry geometry;
    const ModelInstance* active = active_model();
    const QSize viewport = effective_screen_size();
    const float viewport_height = std::max(1.0f, static_cast<float>(viewport.height()));
    const Mat4 view = camera_.view_matrix();
    const Vec3 view_right(view.m[0][0], view.m[1][0], view.m[2][0]);
    const Vec3 view_up(view.m[0][1], view.m[1][1], view.m[2][1]);
    const Vec3 view_forward(view.m[0][2], view.m[1][2], view.m[2][2]);

    const auto world_per_pixel = [&](const Vec3& point) {
        if (camera_.orthographic) {
            const float height = camera_.ortho_height > 0.0f
                                     ? camera_.ortho_height
                                     : 2.0f * std::tan(camera_.fov_y * 0.5f);
            return height / viewport_height;
        }
        const float depth = std::max((point - camera_.position).dot(view_forward), 0.1f);
        return 2.0f * depth * std::tan(camera_.fov_y * 0.5f) / viewport_height;
    };

    const auto line = [&](const Vec3& a, const Vec3& b, const std::array<float, 4>& color) {
        geometry.lines.push_back({{a.x, a.y, a.z}, color});
        geometry.lines.push_back({{b.x, b.y, b.z}, color});
    };
    const auto triangle = [&](const Vec3& a, const Vec3& b, const Vec3& c,
                              const std::array<float, 4>& color) {
        geometry.triangles.push_back({{a.x, a.y, a.z}, color});
        geometry.triangles.push_back({{b.x, b.y, b.z}, color});
        geometry.triangles.push_back({{c.x, c.y, c.z}, color});
    };
    const auto quad = [&](const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d,
                          const std::array<float, 4>& color) {
        triangle(a, b, c, color);
        triangle(a, c, d, color);
    };
    const auto ribbon = [&](const Vec3& a, const Vec3& b, float width_px,
                            const std::array<float, 4>& color) {
        const Vec3 axis = b - a;
        const float length_sq = axis.length_squared();
        if (length_sq < 1.0e-8f) {
            return;
        }
        const Vec3 direction = axis * (1.0f / std::sqrt(length_sq));
        Vec3 perpendicular = direction.cross(view_forward);
        perpendicular = perpendicular.length_squared() < 1.0e-8f
                            ? view_right
                            : perpendicular.normalize();
        const float half_a = width_px * world_per_pixel(a) * 0.5f;
        const float half_b = width_px * world_per_pixel(b) * 0.5f;
        quad(a - perpendicular * half_a, a + perpendicular * half_a,
             b + perpendicular * half_b, b - perpendicular * half_b, color);
    };
    const std::array<float, 4> outline{0.06f, 0.07f, 0.10f, 0.9f};
    const auto diamond = [&](const Vec3& center, float half_px,
                             const std::array<float, 4>& color) {
        const float step = world_per_pixel(center);
        const float r = half_px * step;
        const float o = r + 1.1f * step;
        quad(center + view_up * o, center + view_right * o, center - view_up * o,
             center - view_right * o, outline);
        quad(center + view_up * r, center + view_right * r, center - view_up * r,
             center - view_right * r, color);
    };
    const auto square_marker = [&](const Vec3& center, float half_px,
                                   const std::array<float, 4>& color) {
        const float step = world_per_pixel(center);
        const float r = half_px * step;
        const float o = r + 1.1f * step;
        quad(center + view_up * o - view_right * o, center + view_up * o + view_right * o,
             center - view_up * o + view_right * o, center - view_up * o - view_right * o,
             outline);
        quad(center + view_up * r - view_right * r, center + view_up * r + view_right * r,
             center - view_up * r + view_right * r, center - view_up * r - view_right * r,
             color);
    };
    const auto arrow = [&](const Vec3& origin, const Vec3& direction, float length_px,
                           const std::array<float, 4>& color) {
        const float length = length_px * world_per_pixel(origin);
        const Vec3 tip = origin + direction * length;
        ribbon(origin, tip, 2.4f, color);
        Vec3 side = direction.cross(view_forward);
        side = side.length_squared() < 1.0e-8f ? view_right : side.normalize();
        const float head = 5.0f * world_per_pixel(tip);
        triangle(tip, tip - direction * head + side * (head * 0.6f),
                 tip - direction * head - side * (head * 0.6f), color);
    };

    // Camera placement marker. The eye sits at the centre of its own view, so
    // the marker is placed just past the near plane and drawn as a pixel-sized
    // diamond with crosshair ticks. At zero or negative distance the eye is at
    // or past the pivot, where the marker would sit on top of the model.
    if ((state_.mode == EditorMode::Camera || state_.camera_follow) &&
        state_.camera_distance > 0.0f) {
        const Vec3 forward = (camera_.target - camera_.position).normalize();
        const Vec3 center =
            camera_.position + forward * std::max(camera_.near_z * 1.5f, 0.05f);
        const std::array<float, 4> color{0.98f, 0.68f, 0.16f, 0.95f};
        diamond(center, 4.5f, color);
        const float step = world_per_pixel(center);
        const float tick = 13.0f * step;
        const float gap = 6.0f * step;
        line(center - view_right * tick, center - view_right * gap, color);
        line(center + view_right * gap, center + view_right * tick, color);
        line(center - view_up * tick, center - view_up * gap, color);
        line(center + view_up * gap, center + view_up * tick, color);
    }

    // Skeleton and physics handles belong to model editing.
    if (active == nullptr || state_.mode == EditorMode::Camera) {
        return geometry;
    }

    const auto append_bones = [&](const ModelInstance& bone_model, bool is_active_model) {
        const Vec3 offset = bone_model.offset;
        const float scale = bone_model.scale;
        const animation::Skeleton& skeleton = bone_model.skeleton;
        const auto types = skeleton.bone_visual_types();
        const auto* rig = bone_model.physics_rig ? &*bone_model.physics_rig : nullptr;
        const auto hidden = [&](std::size_t index) {
            return !state_.show_physics_bones && rig != nullptr && rig->is_bone_driven(index);
        };
        const bool has_selection =
            is_active_model && state_.selected_bone_idx &&
            static_cast<std::size_t>(*state_.selected_bone_idx) < skeleton.bones.size();
        const std::size_t selected =
            has_selection ? static_cast<std::size_t>(*state_.selected_bone_idx) : 0;

        const std::array<float, 4> normal{0.392f, 0.392f, 0.902f, 0.92f};
        const std::array<float, 4> selected_color{1.0f, 0.196f, 0.196f, 1.0f};
        const std::array<float, 4> selected_extra{1.0f, 0.42f, 0.42f, 0.95f};
        const std::array<float, 4> ik_color{1.0f, 0.608f, 0.0f, 1.0f};
        const std::array<float, 4> ik_link_color{0.08f, 0.08f, 0.12f, 0.8f};
        const std::array<float, 4> physics_color{0.392f, 1.0f, 0.392f, 0.95f};

        // IK chains first, so shafts and markers stay on top.
        for (std::size_t i = 0; i < skeleton.bones.size(); ++i) {
            const auto& bone = skeleton.bones[i];
            if (!bone.has_ik || bone.ik_target_bone >= skeleton.bones.size() || hidden(i) ||
                hidden(bone.ik_target_bone)) {
                continue;
            }
            ribbon(skeleton.world_positions[i] * scale + offset,
                   skeleton.world_positions[bone.ik_target_bone] * scale + offset, 1.0f,
                   ik_link_color);
        }

        for (std::size_t i = 0; i < skeleton.bones.size(); ++i) {
            const auto& bone = skeleton.bones[i];
            if ((bone.flags & 0x0008) == 0 || hidden(i)) {
                continue;
            }
            const bool is_selected = has_selection && i == selected;
            const bool physics_driven = rig != nullptr && rig->is_bone_driven(i);
            std::array<float, 4> color = physics_driven ? physics_color : normal;
            if (types[i] == animation::BoneVisualType::IkController) {
                color = ik_color;
            } else if (types[i] == animation::BoneVisualType::UnderIk) {
                color = ik_color;
                color[3] = 0.8f;
            }
            if (is_selected) {
                color = selected_color;
            }

            const Vec3 base = skeleton.world_positions[i] * scale + offset;
            const Vec3 tail = bone_tail_world(skeleton, i) * scale + offset;
            const Vec3 direction = tail - base;
            Vec3 perpendicular = direction.cross(view_forward);
            perpendicular = perpendicular.length_squared() < 1.0e-8f
                                ? view_right
                                : perpendicular.normalize();
            const float width = is_selected ? 3.2f : 1.8f;
            const float spread = 7.0f * world_per_pixel(base);
            ribbon(base + perpendicular * spread, tail, width, color);
            ribbon(base - perpendicular * spread, tail, width, color);

            if (is_selected) {
                ribbon(base + perpendicular * (spread * 1.5f), tail, width + 1.0f,
                       selected_extra);
                ribbon(base - perpendicular * (spread * 1.5f), tail, width + 1.0f,
                       selected_extra);
            }

            const bool translate_only =
                (bone.flags & 0x0004) != 0 && (bone.flags & 0x0002) == 0;
            if (types[i] == animation::BoneVisualType::IkController || translate_only) {
                square_marker(base, is_selected ? 6.0f : 5.0f, color);
            } else {
                diamond(base, is_selected ? 6.0f : 4.5f, color);
            }
        }

        if (has_selection && (skeleton.bones[selected].flags & 0x0008) != 0) {
            const Vec3 origin = skeleton.world_positions[selected] * scale + offset;
            const std::array<std::array<float, 4>, 3> axis_colors{
                std::array<float, 4>{0.86f, 0.30f, 0.33f, 1.0f},
                std::array<float, 4>{0.48f, 0.72f, 0.38f, 1.0f},
                std::array<float, 4>{0.38f, 0.55f, 0.86f, 1.0f}};
            for (int axis = 0; axis < 3; ++axis) {
                Vec3 direction(axis == 0 ? 1.0f : 0.0f, axis == 1 ? 1.0f : 0.0f,
                               axis == 2 ? 1.0f : 0.0f);
                if (state_.is_local_coord) {
                    direction = skeleton.world_rotations[selected].rotate_vec3(direction);
                }
                arrow(origin, direction.normalize(), 34.0f,
                      axis_colors[static_cast<std::size_t>(axis)]);
            }
        }
    };

    if (state_.show_bones) {
        if (state_.show_all_model_bones) {
            for (const auto& bone_model : models_) {
                if (!bone_model->visible) {
                    continue;
                }
                append_bones(*bone_model, bone_model.get() == active);
            }
        } else if (active->visible) {
            append_bones(*active, true);
        }
    }

    const auto debug_cross = [&](const Vec3& point, const std::array<float, 4>& color) {
        const float size = 5.0f * world_per_pixel(point);
        line(point - view_right * size, point + view_right * size, color);
        line(point - view_up * size, point + view_up * size, color);
    };
    if (active->visible && active->physics_rig) {
        const Vec3 offset = active->offset;
        const float scale = active->scale;
        const auto* rig = &*active->physics_rig;
        if (state_.show_rigid_bodies) {
            for (const auto& body : rig->get_debug_rigid_bodies()) {
                const Vec3 position(body.position[0] * scale + offset.x,
                                    body.position[1] * scale + offset.y,
                                    body.position[2] * scale + offset.z);
                const std::array<float, 4> color =
                    body.mode == 0 ? std::array<float, 4>{0.47f, 0.70f, 0.47f, 0.9f}
                    : body.mode == 1 ? std::array<float, 4>{0.84f, 0.75f, 0.41f, 0.9f}
                                     : std::array<float, 4>{0.49f, 0.78f, 0.89f, 0.9f};
                debug_cross(position, color);
            }
        }
        if (state_.show_joints) {
            for (const auto& joint : rig->get_debug_joints()) {
                debug_cross(Vec3(joint.position[0] * scale + offset.x,
                                 joint.position[1] * scale + offset.y,
                                 joint.position[2] * scale + offset.z),
                            {0.62f, 0.53f, 0.77f, 0.9f});
            }
        }
    }

    return geometry;
}

void D3DViewport::mousePressEvent(QMouseEvent* event) {
    last_mouse_ = event->pos();
    if (event->button() == Qt::LeftButton) {
        left_down_ = true;
        if (ModelInstance* model = active_model();
            model != nullptr && state_.mode == EditorMode::Model) {
            const Mat4 view_projection = camera_.view_projection_matrix();
            std::optional<std::size_t> best;
            float best_distance = 10.0f;
            for (std::size_t i = 0; i < model->skeleton.bones.size(); ++i) {
                const auto& bone = model->skeleton.bones[i];
                if ((bone.flags & 0x0008) == 0) {
                    continue;
                }
                if (!state_.show_physics_bones && model->physics_rig &&
                    model->physics_rig->is_bone_driven(i)) {
                    continue;
                }
                const auto projected = project_to_screen(
                    model->skeleton.world_positions[i] * model->scale + model->offset,
                    view_projection, scene_rect());
                if (!projected) {
                    continue;
                }
                const float dx = projected->x - event->pos().x();
                const float dy = projected->y - event->pos().y();
                const float distance = std::sqrt(dx * dx + dy * dy);
                if (distance < best_distance) {
                    best_distance = distance;
                    best = i;
                }
            }
            if (best) {
                state_.selected_bone_idx = static_cast<int>(*best);
                state_.bone_transform_dirty = false;
                const auto& bone = model->skeleton.bones[*best];
                state_.set_status(QStringLiteral("Selected bone [%1]: '%2'")
                                      .arg(*best)
                                      .arg(to_qstring(bone.name)));
            }
        }
    } else if (event->button() == Qt::RightButton) {
        right_down_ = true;
        setCursor(Qt::SizeAllCursor);
    } else if (event->button() == Qt::MiddleButton) {
        middle_down_ = true;
        setCursor(Qt::ClosedHandCursor);
    }
    setFocus();
    event->accept();
}

void D3DViewport::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        left_down_ = false;
    } else if (event->button() == Qt::RightButton) {
        right_down_ = false;
        unsetCursor();
    } else if (event->button() == Qt::MiddleButton) {
        middle_down_ = false;
        unsetCursor();
    }
    event->accept();
}

void D3DViewport::mouseMoveEvent(QMouseEvent* event) {
    const QPoint delta = event->pos() - last_mouse_;
    last_mouse_ = event->pos();
    if (right_down_) {
        // MMD right-drag: orbit with 0.01 rad per pixel and no pitch clamp.
        state_.camera_yaw -= static_cast<float>(delta.x()) * 0.01f;
        state_.camera_pitch -= static_cast<float>(delta.y()) * 0.01f;
    } else if (middle_down_) {
        if (state_.mode == EditorMode::Model && state_.camera_target != CameraTarget::Free) {
            event->accept();
            return;
        }
        const Quat orientation = camera_orientation();
        const Vec3 right = orientation.rotate_vec3(Vec3::axis_x());
        const Vec3 up = orientation.rotate_vec3(Vec3::axis_y());
        const float pan_speed = (event->modifiers() & Qt::ShiftModifier) ? 0.5f
                                : (event->modifiers() & Qt::ControlModifier) ? 0.005f
                                                                            : 0.05f;
        const Vec3 move = right * (-static_cast<float>(delta.x()) * pan_speed) +
                          up * (static_cast<float>(delta.y()) * pan_speed);
        Vec3 bone_position;
        Quat bone_rotation;
        float follow_scale = 1.0f;
        if (camera_follow_state(bone_position, bone_rotation, follow_scale)) {
            const Vec3 local = bone_rotation.inverse().rotate_vec3(move);
            state_.camera_follow_offset[0] += local.x;
            state_.camera_follow_offset[1] += local.y;
            state_.camera_follow_offset[2] += local.z;
        } else {
            state_.camera_pan[0] += move.x;
            state_.camera_pan[1] += move.y;
            state_.camera_pan[2] += move.z;
        }
    }
    event->accept();
}

void D3DViewport::wheelEvent(QWheelEvent* event) {
    // Studio distance grows when the eye moves away from the focus, so wheel up
    // (zoom in) subtracts and wheel down (zoom out) adds.
    state_.camera_distance =
        std::clamp(state_.camera_distance - static_cast<float>(event->angleDelta().y()) * 0.05f,
                   -5000.0f, 5000.0f);
    event->accept();
}

void D3DViewport::keyPressEvent(QKeyEvent* event) {
    switch (event->key()) {
    case Qt::Key_Space:
        state_.toggle_play();
        break;
    case Qt::Key_V:
        state_.vsync = !state_.vsync;
        break;
    case Qt::Key_P:
        state_.physics_mode = state_.physics_mode == PhysicsMode::Disabled
                                  ? PhysicsMode::Anytime
                                  : PhysicsMode::Disabled;
        emit statusMessage(state_.physics_mode == PhysicsMode::Disabled
                               ? QStringLiteral("Physics disabled")
                               : QStringLiteral("Physics enabled"));
        break;
    case Qt::Key_J:
        state_.jiggle_enabled = !state_.jiggle_enabled;
        break;
    case Qt::Key_R:
        request_physics_reset();
        break;
    default:
        QWidget::keyPressEvent(event);
        break;
    }
}

} // namespace hikari::studio
