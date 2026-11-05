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

#include "hikari/animation/bezier.hpp"
#include "hikari/core/log.hpp"
#include "hikari/studio/viewport_chrome.hpp"

namespace hikari::studio {
namespace {

QString to_qstring(const std::string& value) {
    return QString::fromUtf8(value.data(), static_cast<int>(value.size()));
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
        last_editor_frame_ = -1.0f;
        sync_active_model_state();
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
        const std::string& name = morph.name_local.empty() ? morph.name_universal : morph.name_local;
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
    if (index < 0 || index >= static_cast<int>(models_.size()) || index == active_model_) {
        return;
    }
    active_model_ = index;
    last_editor_frame_ = -1.0f;
    sync_active_model_state();
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

void D3DViewport::remove_active_model() {
    if (active_model_ < 0 || active_model_ >= static_cast<int>(models_.size())) {
        return;
    }
    models_.erase(models_.begin() + active_model_);
    if (models_.empty()) {
        active_model_ = -1;
    } else {
        active_model_ = std::min(active_model_, static_cast<int>(models_.size()) - 1);
    }
    motion_player_.reset();
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
        state_.max_timeline_frame = static_cast<int>(motion_player_->max_frame);
        state_.play_range = {0, state_.max_timeline_frame};
        last_editor_frame_ = -1.0f;
        state_.track_keys_dirty = true;
        if (ModelInstance* model = active_model(); model != nullptr && model->physics_rig) {
            model->physics_rig->request_reset();
        }
        emit statusMessage(QStringLiteral("Loaded motion: %1").arg(path));
        return true;
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

bool D3DViewport::save_motion(const QString& path) {
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
            track == QStringLiteral("Disp/IK/OP") || track == QStringLiteral("Facial")) {
            continue;
        }
        if (track == QStringLiteral("camera")) {
            for (const auto& [frame, key] : keys) {
                formats::VmdCameraFrame camera;
                camera.frame = frame;
                camera.distance = key.distance;
                camera.position = key.position;
                camera.rotation = {key.angles[1], key.angles[0], key.angles[2]};
                camera.interpolation.fill(20);
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
        if (track == QStringLiteral("gravity") || track == QStringLiteral("accessory")) {
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

void D3DViewport::clear_model() {
    models_.clear();
    active_model_ = -1;
    motion_player_.reset();
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
    gizmo_->move(x, y);
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
            state_.camera_pitch = std::clamp(state_.camera_pitch + radians, -1.55f, 1.55f);
        } else if (axis == 1) {
            state_.camera_yaw += radians;
        } else {
            state_.camera_roll += radians;
        }
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
        const auto& bone = model->skeleton.bones[index];
        const Quat parent = bone.parent_index
                                ? model->skeleton.world_rotations[*bone.parent_index]
                                : Quat::identity();
        model->skeleton.local_rotations[index] = (parent.inverse() * world).normalize();
    }
    model->skeleton.update_transforms();
    state_.bone_transform_dirty = false;
}

void D3DViewport::translate_selection(int axis, float amount) {
    ModelInstance* model = active_model();
    std::size_t index = 0;
    if (state_.mode == EditorMode::Camera || model == nullptr ||
        !selected_bone_transform(index)) {
        const float cy = std::cos(state_.camera_yaw);
        const float sy = std::sin(state_.camera_yaw);
        const float cp = std::cos(state_.camera_pitch);
        const float sp = std::sin(state_.camera_pitch);
        const Vec3 right(cy, 0.0f, sy);
        const Vec3 up(-sp * sy, cp, sp * cy);
        const Vec3 forward(-cp * sy, -sp, cp * cy);
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
    Vec3 local_delta(axis == 0 ? amount : 0.0f, axis == 1 ? amount : 0.0f,
                     axis == 2 ? amount : 0.0f);
    if (!state_.is_local_coord && bone.parent_index) {
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

void D3DViewport::reset_camera() {
    state_.camera_yaw = 0.0f;
    state_.camera_pitch = 0.15f;
    state_.camera_roll = 0.0f;
    state_.camera_distance = 50.0f;
    state_.camera_pan = {0.0f, 10.0f, 0.0f};
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
        for (auto& model : models_) {
            if (model.get() == active) {
                for (auto it = state_.morph_weights.begin();
                     it != state_.morph_weights.end(); ++it) {
                    model->morphs.set_weight(it.key().toStdString(), it.value());
                }
            }
            const animation::MorphResult morph_result = model->morphs.evaluate();
            apply_bone_morphs(*model, morph_result);
            if (model.get() == active) {
                apply_editor_bone_override();
            }
            if (model->physics_rig) {
                for (const auto& impulse : morph_result.impulses) {
                    model->physics_rig->apply_impulse(impulse.body_index, impulse.local,
                                                      impulse.velocity, impulse.torque);
                }
            }
            if (state_.physics_mode != PhysicsMode::Disabled && model->physics_rig) {
                model->skeleton.apply_physics(*model->physics_rig,
                                              1.0f / static_cast<float>(std::max(fps, 1)));
            }
            apply_material_morphs(*model, morph_result);
            model->mesh.apply_morph_offsets(*context_, morph_result.position_offsets,
                                            morph_result.uv_offsets);
        }
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

    if (state_.model_delete_requested) {
        state_.model_delete_requested = false;
        remove_active_model();
    }

    update_camera();
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

    if (state_.reset_physics_requested) {
        state_.reset_physics_requested = false;
        request_physics_reset();
    }
    const bool physics_active =
        state_.physics_mode != PhysicsMode::Disabled &&
        (!state_.playtime_only || state_.is_playing);
    for (auto& model : models_) {
        const animation::MorphResult morph_result = model->morphs.evaluate();
        apply_bone_morphs(*model, morph_result);
        if (model.get() == active) {
            apply_editor_bone_override();
        }
        if (model->physics_rig) {
            model->physics_rig->set_gravity_vec(state_.gravity);
            model->physics_rig->set_time_scale(state_.physics_speed);
            model->physics_rig->set_ground_enabled(state_.ground_collision);
            for (const auto& impulse : morph_result.impulses) {
                model->physics_rig->apply_impulse(impulse.body_index, impulse.local,
                                                  impulse.velocity, impulse.torque);
            }
        }
        if (physics_active && model->physics_rig) {
            model->skeleton.apply_physics(*model->physics_rig, dt);
        }
        if (state_.jiggle_enabled && model->jiggle) {
            model->skeleton.apply_jiggle(*model->jiggle, dt, model->jiggle_settings);
        }
        apply_material_morphs(*model, morph_result);
        model->mesh.apply_morph_offsets(*context_, morph_result.position_offsets,
                                        morph_result.uv_offsets);
    }

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
    camera_.ortho_height = 2.0f * state_.camera_distance * std::tan(camera_.fov_y * 0.5f);
    const QSize render_size = effective_screen_size();
    camera_.aspect = static_cast<float>(render_size.width()) /
                     static_cast<float>(std::max(render_size.height(), 1));
    const float cy = std::cos(state_.camera_yaw);
    const float sy = std::sin(state_.camera_yaw);
    const float cp = std::cos(state_.camera_pitch);
    const float sp = std::sin(state_.camera_pitch);
    const Vec3 eye_offset(state_.camera_distance * cp * sy,
                          state_.camera_distance * sp,
                          -state_.camera_distance * cp * cy);
    const Vec3 pan = Vec3::from_array(state_.camera_pan);
    camera_.position = pan + eye_offset;
    camera_.target = pan;

    const Vec3 forward = (camera_.target - camera_.position).normalize();
    const Vec3 world_up(0.0f, 1.0f, 0.0f);
    Vec3 right = world_up.cross(forward);
    right = right.length() > 1.0e-4f ? right.normalize() : Vec3(1.0f, 0.0f, 0.0f);
    const Vec3 base_up = forward.cross(right).normalize();
    camera_.up =
        (base_up * std::cos(state_.camera_roll) + right * std::sin(state_.camera_roll)).normalize();
}

void D3DViewport::evaluate_frame(float frame) {
    ModelInstance* model = active_model();
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
                state_.camera_distance = sample->camera->distance;
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
    if (model != nullptr && motion_player_) {
        motion_player_->evaluate_pose(evaluated, model->skeleton, model->morphs);
    }
    apply_editor_tracks(model, frame, scene_changed);
    state_.track_keys_dirty = false;
    last_editor_frame_ = frame;
}

void D3DViewport::apply_editor_tracks(ModelInstance* model, float frame,
                                      bool apply_scene_values) {
    bool bones_changed = false;
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
            model->skeleton.local_translations[*index] =
                Vec3::from_array(mix3(first->position, second->position));
            model->skeleton.local_rotations[*index] =
                Quat::from_array(first->rotation)
                    .slerp(Quat::from_array(second->rotation), ease);
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
    const std::array<float, 4> background =
        state_.viewport_white_bg ? std::array<float, 4>{0.98f, 0.98f, 0.99f, 1.0f}
                                 : std::array<float, 4>{0.19f, 0.22f, 0.27f, 1.0f};
    renderer_->render_viewport_background(*context_, background);
    renderer_->set_light(state_.light_color, state_.light_direction);
    renderer_->update_camera(*context_, camera_);
    if (state_.show_grid) {
        renderer_->render_grid_to(*context_, context_->context.Get());
    }
    if (state_.ground_shadow) {
        for (const auto& model : models_) {
            if (!model->visible) {
                continue;
            }
            const Vec3 center(model->shadow_center.x + model->offset.x, 0.0f,
                              model->shadow_center.z + model->offset.z);
            renderer_->render_ground_shadow(*context_, center, model->shadow_radius,
                                            {0.10f, 0.11f, 0.14f, 0.32f});
        }
    }
    renderer_->wireframe = state_.wireframe;
    for (const auto& model : models_) {
        if (!model->visible) {
            continue;
        }
        render_palette_.resize(model->skeleton.skinning_palette.size());
        const Mat4 offset = Mat4::from_translation(model->offset);
        for (std::size_t i = 0; i < render_palette_.size(); ++i) {
            render_palette_[i] = model->skeleton.skinning_palette[i] * offset;
        }
        if (state_.multithreaded) {
            renderer_->skinning_buffer.update(*context_, render_palette_);
            const auto lists = renderer_->record_mesh_subsets_parallel(
                *context_, model->mesh, model->materials, model->textures,
                renderer_->skinning_buffer, 4);
            for (const auto& list : lists) {
                context_->execute_command_list(list.Get(), false);
            }
        } else {
            renderer_->skinning_buffer.update(*context_, render_palette_);
            renderer_->render_mesh_to(*context_, context_->context.Get(), model->mesh,
                                      model->materials, model->textures,
                                      renderer_->skinning_buffer);
        }
    }
    draw_overlay();
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

void D3DViewport::draw_overlay() {
    if (!context_ || !renderer_) {
        return;
    }
    const ModelInstance* model = active_model();
    if (model == nullptr) {
        return;
    }
    const Vec3 offset = model->offset;
    std::vector<render::OverlayVertex> vertices;
    const auto line = [&](const Vec3& a, const Vec3& b, const std::array<float, 4>& color) {
        vertices.push_back({{a.x, a.y, a.z}, color});
        vertices.push_back({{b.x, b.y, b.z}, color});
    };
    const auto cross = [&](const Vec3& point, float size, const std::array<float, 4>& color) {
        line(point - Vec3(size, 0.0f, 0.0f), point + Vec3(size, 0.0f, 0.0f), color);
        line(point - Vec3(0.0f, size, 0.0f), point + Vec3(0.0f, size, 0.0f), color);
        line(point - Vec3(0.0f, 0.0f, size), point + Vec3(0.0f, 0.0f, size), color);
    };

    if (state_.show_bones) {
        const auto& skeleton = model->skeleton;
        const auto types = skeleton.bone_visual_types();
        const auto* rig = model->physics_rig ? &*model->physics_rig : nullptr;
        const auto hidden = [&](std::size_t index) {
            return !state_.show_physics_bones && rig != nullptr && rig->is_bone_driven(index);
        };
        const std::array<float, 4> standard{0.55f, 0.68f, 0.80f, 0.85f};
        const std::array<float, 4> ik_link{0.84f, 0.75f, 0.41f, 0.95f};
        const std::array<float, 4> active{0.49f, 0.78f, 0.89f, 1.0f};
        const std::array<float, 4> selected{0.95f, 0.42f, 0.42f, 1.0f};

        for (std::size_t i = 0; i < skeleton.bones.size(); ++i) {
            const auto& bone = skeleton.bones[i];
            if ((bone.flags & 0x0008) == 0 || hidden(i)) {
                continue;
            }
            const Vec3 position = skeleton.world_positions[i] + offset;
            const bool is_selected = state_.selected_bone_idx &&
                                     static_cast<std::size_t>(*state_.selected_bone_idx) == i;
            std::array<float, 4> color = standard;
            if (is_selected) {
                color = selected;
            } else if (types[i] == animation::BoneVisualType::UnderIk ||
                       types[i] == animation::BoneVisualType::IkController) {
                color = ik_link;
            }
            if (is_selected) {
                const float axis = 1.4f;
                line(position, position + Vec3(axis, 0.0f, 0.0f), {0.78f, 0.30f, 0.33f, 1.0f});
                line(position, position + Vec3(0.0f, axis, 0.0f), {0.48f, 0.68f, 0.38f, 1.0f});
                line(position, position + Vec3(0.0f, 0.0f, axis), {0.38f, 0.55f, 0.78f, 1.0f});
            }
            cross(position, is_selected ? 0.45f : 0.25f, color);

            if (bone.parent_index && !hidden(*bone.parent_index)) {
                color = active;
                if (!is_selected) {
                    color = (types[i] == animation::BoneVisualType::UnderIk ||
                             types[i] == animation::BoneVisualType::IkController)
                                ? ik_link
                                : standard;
                    color[3] = 0.6f;
                }
                line(skeleton.world_positions[*bone.parent_index] + offset, position, color);
            }
        }
    }

    if (model->physics_rig) {
        const auto* rig = &*model->physics_rig;
        if (state_.show_rigid_bodies) {
            for (const auto& body : rig->get_debug_rigid_bodies()) {
                const Vec3 position(body.position[0] + offset.x, body.position[1] + offset.y,
                                    body.position[2] + offset.z);
                const std::array<float, 4> color =
                    body.mode == 0 ? std::array<float, 4>{0.47f, 0.70f, 0.47f, 0.9f}
                    : body.mode == 1 ? std::array<float, 4>{0.84f, 0.75f, 0.41f, 0.9f}
                                     : std::array<float, 4>{0.49f, 0.78f, 0.89f, 0.9f};
                const float size = std::max({body.size[0], body.size[1], body.size[2], 0.2f}) * 0.5f;
                cross(position, std::min(size, 1.0f), color);
            }
        }
        if (state_.show_joints) {
            for (const auto& joint : rig->get_debug_joints()) {
                cross(Vec3(joint.position[0] + offset.x, joint.position[1] + offset.y,
                           joint.position[2] + offset.z),
                      0.2f, {0.62f, 0.53f, 0.77f, 0.9f});
            }
        }
    }

    if (!vertices.empty()) {
        try {
            renderer_->render_overlay_lines(*context_, vertices);
        } catch (const std::exception& error) {
            core::log_msg(std::string("[studio] overlay failed: ") + error.what());
        }
    }
}

void D3DViewport::mousePressEvent(QMouseEvent* event) {
    last_mouse_ = event->pos();
    if (event->button() == Qt::LeftButton) {
        left_down_ = true;
        if (ModelInstance* model = active_model()) {
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
                    model->skeleton.world_positions[i] + model->offset, view_projection,
                    scene_rect());
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
        state_.camera_yaw += static_cast<float>(delta.x()) * 0.005f;
        state_.camera_pitch =
            std::clamp(state_.camera_pitch + static_cast<float>(delta.y()) * 0.005f, -1.55f, 1.55f);
    } else if (middle_down_) {
        const float cy = std::cos(state_.camera_yaw);
        const float sy = std::sin(state_.camera_yaw);
        const float cp = std::cos(state_.camera_pitch);
        const float sp = std::sin(state_.camera_pitch);
        const Vec3 right(cy, 0.0f, sy);
        const Vec3 up(-sp * sy, cp, sp * cy);
        const float pan_speed = std::clamp(state_.camera_distance * 0.0012f, 0.005f, 1.0f);
        const Vec3 move = right * (-static_cast<float>(delta.x()) * pan_speed) +
                          up * (static_cast<float>(delta.y()) * pan_speed);
        state_.camera_pan[0] += move.x;
        state_.camera_pan[1] += move.y;
        state_.camera_pan[2] += move.z;
    }
    event->accept();
}

void D3DViewport::wheelEvent(QWheelEvent* event) {
    const float steps = event->angleDelta().y() / 120.0f;
    const float zoom_factor = std::clamp(state_.camera_distance * 0.1f, 0.5f, 10.0f);
    state_.camera_distance =
        std::clamp(state_.camera_distance - steps * zoom_factor, 0.5f, 2000.0f);
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
        break;
    case Qt::Key_J:
        state_.jiggle_enabled = !state_.jiggle_enabled;
        break;
    case Qt::Key_M:
        state_.multithreaded = !state_.multithreaded;
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
