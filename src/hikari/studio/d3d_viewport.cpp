#include "hikari/studio/d3d_viewport.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QThread>
#include <QTimer>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

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
        formats::PmxModel pmx = formats::PmxModel::parse(
            std::vector<std::uint8_t>(bytes.begin(), bytes.end()));
        const QFileInfo info(path);
        const QDir directory = info.absoluteDir();
        std::vector<std::optional<render::GpuTexture>> textures;
        textures.reserve(pmx.textures.size());
        for (const auto& name : pmx.textures) {
            QString normalized = to_qstring(name);
            normalized.replace('\\', '/');
            QString target = directory.filePath(normalized);
            if (!QFileInfo::exists(target)) {
                target = directory.filePath(QFileInfo(normalized).fileName());
            }
            if (!QFileInfo::exists(target)) {
                textures.push_back(std::nullopt);
                continue;
            }
            try {
                textures.push_back(render::GpuTexture::from_file(*context_, to_wstring(target)));
            } catch (...) {
                textures.push_back(std::nullopt);
            }
        }

        auto mesh = render::GpuMesh::from_pmx(*context_, pmx);
        std::vector<render::GpuMaterial> materials;
        materials.reserve(pmx.materials.size());
        for (const auto& material : pmx.materials) {
            try {
                materials.push_back(render::GpuMaterial::from_pmx_material(*context_, material));
            } catch (...) {
                materials.push_back(render::GpuMaterial::fallback(*context_, material.name_local,
                                                                  material.texture_index));
            }
        }
        auto skeleton = animation::Skeleton::from_pmx(pmx);
        std::vector<std::array<float, 3>> rest_positions;
        rest_positions.reserve(pmx.bones.size());
        for (const auto& bone : pmx.bones) {
            rest_positions.push_back(bone.position);
        }
        auto physics_rig = physics::PhysicsRig::create(pmx, rest_positions,
                                                       physics::PhysicsSettings{});
        if (physics_rig) {
            physics_rig->set_time_scale(state_.physics_speed);
        }
        auto jiggle = animation::Jiggle(pmx);
        auto morphs = animation::MorphEvaluator::from_pmx(pmx);

        pmx_ = std::move(pmx);
        mesh_ = std::move(mesh);
        materials_ = std::move(materials);
        textures_ = std::move(textures);
        skeleton_ = std::move(skeleton);
        physics_rig_ = std::move(physics_rig);
        jiggle_ = jiggle.body_count() > 0 ? std::optional<animation::Jiggle>(std::move(jiggle))
                                          : std::nullopt;
        morphs_ = std::move(morphs);
        motion_player_.reset();

        vertex_count_ = static_cast<int>(mesh_->vertex_count);
        index_count_ = static_cast<int>(mesh_->index_count);
        bone_count_ = static_cast<int>(skeleton_->bones.size());
        {
            float minimum[3] = {1.0e30f, 1.0e30f, 1.0e30f};
            float maximum[3] = {-1.0e30f, -1.0e30f, -1.0e30f};
            for (const auto& vertex : pmx_->vertices) {
                for (int axis = 0; axis < 3; ++axis) {
                    minimum[axis] = std::min(minimum[axis], vertex.position[static_cast<std::size_t>(axis)]);
                    maximum[axis] = std::max(maximum[axis], vertex.position[static_cast<std::size_t>(axis)]);
                }
            }
            core::log_msg("[studio] PMX counts: vertices=" + std::to_string(vertex_count_) +
                          " indices=" + std::to_string(index_count_) + " materials=" +
                          std::to_string(pmx_->materials.size()) + " bones=" +
                          std::to_string(bone_count_) + " morphs=" +
                          std::to_string(pmx_->morphs.size()) + " rigid=" +
                          std::to_string(pmx_->rigid_bodies.size()) + " joints=" +
                          std::to_string(pmx_->joints.size()));
            core::log_msg("[studio] PMX bounds: min=(" + std::to_string(minimum[0]) + ", " +
                          std::to_string(minimum[1]) + ", " + std::to_string(minimum[2]) +
                          ") max=(" + std::to_string(maximum[0]) + ", " +
                          std::to_string(maximum[1]) + ", " + std::to_string(maximum[2]) + ")");
        }
        state_.model_name = to_qstring(pmx_->info.name_local);
        state_.model_bone_names.clear();
        for (const auto& bone : pmx_->bones) {
            state_.model_bone_names.push_back(to_qstring(
                bone.name_local.empty() ? bone.name_universal : bone.name_local));
        }
        state_.selected_bone_idx.reset();
        state_.morph_weights.clear();
        state_.category_morph_names[0].clear();
        state_.category_morph_names[1].clear();
        state_.category_morph_names[2].clear();
        state_.category_morph_names[3].clear();
        for (const auto& morph : pmx_->morphs) {
            const QString name = to_qstring(morph.name_local);
            const int category = std::clamp(static_cast<int>(morph.category), 0, 3);
            state_.category_morph_names[static_cast<std::size_t>(category)].push_back(name);
        }
        const QStringList defaults = {QStringLiteral("まばたき"), QStringLiteral("あ"),
                                      QStringLiteral("真面目"), QStringLiteral("涙")};
        for (int i = 0; i < 4; ++i) {
            if (state_.category_morph_names[static_cast<std::size_t>(i)].isEmpty()) {
                state_.category_morph_names[static_cast<std::size_t>(i)].push_back(defaults[i]);
            }
            state_.selected_morph_names[static_cast<std::size_t>(i)] =
                state_.category_morph_names[static_cast<std::size_t>(i)].first();
        }
        emit modelLoaded(state_.model_name, vertex_count_, bone_count_);
        emit statusMessage(QStringLiteral("Loaded model: %1").arg(path));
        core::log_msg("[studio] Loaded PMX: " + path.toStdString());
        return true;
    } catch (const std::exception& error) {
        emit statusMessage(QStringLiteral("Failed to load model: %1")
                               .arg(QString::fromUtf8(error.what())));
        return false;
    }
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
        motion_player_.emplace(motion);
        state_.max_timeline_frame = static_cast<int>(motion_player_->max_frame);
        state_.play_range = {0, state_.max_timeline_frame};
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
        if (skeleton_) {
            for (const auto& bone : pose.bones) {
                const auto index = skeleton_->find_bone_index(bone.name);
                if (!index) {
                    continue;
                }
                skeleton_->local_translations[*index] = Vec3::from_array(bone.translation);
                skeleton_->local_rotations[*index] = Quat::from_array(bone.rotation);
            }
            skeleton_->update_transforms();
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
    if (!skeleton_) {
        return false;
    }
    formats::VpdPose pose;
    pose.model_name = state_.model_name.toStdString();
    for (std::size_t i = 0; i < skeleton_->bones.size(); ++i) {
        const auto& bone = skeleton_->bones[i];
        const Vec3 translation = skeleton_->local_translations[i];
        const Quat rotation = skeleton_->local_rotations[i];
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

void D3DViewport::clear_model() {
    pmx_.reset();
    mesh_.reset();
    materials_.clear();
    textures_.clear();
    skeleton_.reset();
    physics_rig_.reset();
    jiggle_.reset();
    motion_player_.reset();
    vertex_count_ = 0;
    index_count_ = 0;
    bone_count_ = 0;
    state_.model_name.clear();
    state_.model_bone_names.clear();
    state_.selected_bone_idx.reset();
    emit statusMessage(QStringLiteral("Active model removed from scene"));
}

void D3DViewport::request_physics_reset() {
    if (physics_rig_) {
        physics_rig_->request_reset();
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
    if (!skeleton_ || !state_.selected_bone_idx) {
        return false;
    }
    index = static_cast<std::size_t>(*state_.selected_bone_idx);
    return index < skeleton_->bones.size();
}

void D3DViewport::rotate_selection(int axis, float degrees) {
    constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
    const float radians = degrees * kDegToRad;
    std::size_t index = 0;
    if (state_.mode == EditorMode::Camera || !selected_bone_transform(index)) {
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
        skeleton_->local_rotations[index] =
            (skeleton_->local_rotations[index] * delta).normalize();
    } else {
        const Quat world = (delta * skeleton_->world_rotations[index]).normalize();
        const auto& bone = skeleton_->bones[index];
        const Quat parent = bone.parent_index
                                ? skeleton_->world_rotations[*bone.parent_index]
                                : Quat::identity();
        skeleton_->local_rotations[index] = (parent.inverse() * world).normalize();
    }
    skeleton_->update_transforms();
    state_.bone_transform_dirty = false;
}

void D3DViewport::translate_selection(int axis, float amount) {
    std::size_t index = 0;
    if (state_.mode == EditorMode::Camera || !selected_bone_transform(index)) {
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
    const auto& bone = skeleton_->bones[index];
    const bool movable = (bone.flags & 0x0004) != 0 || bone.has_ik || !bone.parent_index;
    if (!movable) {
        return;
    }
    apply_editor_bone_override();
    Vec3 local_delta(axis == 0 ? amount : 0.0f, axis == 1 ? amount : 0.0f,
                     axis == 2 ? amount : 0.0f);
    if (!state_.is_local_coord && bone.parent_index) {
        local_delta = skeleton_->world_rotations[*bone.parent_index].inverse().rotate_vec3(
            local_delta);
    }
    skeleton_->local_translations[index] = skeleton_->local_translations[index] + local_delta;
    skeleton_->update_transforms();
    state_.bone_transform_dirty = false;
}

void D3DViewport::reset_selection_transform() {
    std::size_t index = 0;
    if (!selected_bone_transform(index)) {
        return;
    }
    skeleton_->local_translations[index] = Vec3::zero();
    skeleton_->local_rotations[index] = Quat::identity();
    skeleton_->update_transforms();
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
    if (physics_rig_) {
        physics_rig_->request_reset();
    }
    for (int frame = start; frame <= end; ++frame) {
        state_.current_frame = static_cast<float>(frame);
        evaluate_frame(static_cast<float>(frame));
        apply_editor_bone_override();
        if (state_.physics_mode != PhysicsMode::Disabled && skeleton_ && physics_rig_) {
            skeleton_->apply_physics(*physics_rig_, 1.0f / static_cast<float>(std::max(fps, 1)));
        }
        if (mesh_) {
            const auto [position_offsets, uv_offsets] = morphs_.compute_offsets();
            mesh_->apply_morph_offsets(*context_, position_offsets, uv_offsets);
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

    if (state_.is_playing) {
        state_.current_frame += dt * 30.0f;
        if (state_.current_frame > static_cast<float>(state_.play_range.second)) {
            if (state_.loop_playback) {
                state_.current_frame = static_cast<float>(state_.play_range.first);
                if (physics_rig_) {
                    physics_rig_->request_reset();
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
        clear_model();
    }

    update_camera();
    evaluate_frame(state_.current_frame);
    apply_editor_bone_override();

    if (state_.reset_physics_requested) {
        state_.reset_physics_requested = false;
        request_physics_reset();
    }
    if (physics_rig_) {
        physics_rig_->set_gravity_vec(state_.gravity);
        physics_rig_->set_time_scale(state_.physics_speed);
    }
    const bool physics_active =
        state_.physics_mode != PhysicsMode::Disabled &&
        (!state_.playtime_only || state_.is_playing);
    if (physics_active && skeleton_ && physics_rig_) {
        skeleton_->apply_physics(*physics_rig_, dt);
    }
    if (state_.jiggle_enabled && skeleton_ && jiggle_) {
        skeleton_->apply_jiggle(*jiggle_, dt, jiggle_settings_);
    }

    for (auto it = state_.morph_weights.begin(); it != state_.morph_weights.end(); ++it) {
        morphs_.set_weight(it.key().toStdString(), it.value());
    }
    if (mesh_) {
        const auto [position_offsets, uv_offsets] = morphs_.compute_offsets();
        mesh_->apply_morph_offsets(*context_, position_offsets, uv_offsets);
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
    if (!skeleton_) {
        return;
    }
    if (motion_player_) {
        const float evaluated =
            motion_player_->max_frame > 0
                ? std::fmod(frame, static_cast<float>(motion_player_->max_frame) + 1.0f)
                : frame;
        motion_player_->evaluate_pose(evaluated, *skeleton_, morphs_);
    }
}

void D3DViewport::apply_editor_bone_override() {
    if (!skeleton_ || !state_.selected_bone_idx) {
        return;
    }
    const std::size_t index = static_cast<std::size_t>(*state_.selected_bone_idx);
    if (index >= skeleton_->bones.size()) {
        return;
    }
    if (state_.bone_transform_dirty) {
        const auto& bone = skeleton_->bones[index];
        const bool movable = (bone.flags & 0x0004) != 0 || bone.has_ik || !bone.parent_index;
        if (movable) {
            skeleton_->local_translations[index] = Vec3::from_array(state_.bone_pos_override);
        }
        const Vec3 euler(state_.bone_rot_euler_deg[0] * 3.14159265358979323846f / 180.0f,
                         state_.bone_rot_euler_deg[1] * 3.14159265358979323846f / 180.0f,
                         state_.bone_rot_euler_deg[2] * 3.14159265358979323846f / 180.0f);
        skeleton_->local_rotations[index] = Quat::from_euler_xyz(euler);
        skeleton_->update_transforms();
    } else {
        const Vec3 translation = skeleton_->local_translations[index];
        state_.bone_pos_override = translation.to_array();
        const Vec3 euler = skeleton_->local_rotations[index].to_euler_xyz();
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
    renderer_->update_camera(*context_, camera_);
    if (state_.show_grid) {
        renderer_->render_grid_to(*context_, context_->context.Get());
    }
    if (state_.model_visible && mesh_ && skeleton_) {
        renderer_->wireframe = state_.wireframe;
        if (state_.multithreaded) {
            renderer_->skinning_buffer.update(*context_, skeleton_->skinning_palette);
            const auto lists = renderer_->record_mesh_subsets_parallel(
                *context_, *mesh_, materials_, textures_, renderer_->skinning_buffer, 4);
            for (const auto& list : lists) {
                context_->execute_command_list(list.Get(), false);
            }
        } else {
            renderer_->render_mesh(*context_, *mesh_, materials_, textures_,
                                   skeleton_->skinning_palette, camera_);
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

    if (skeleton_ && state_.show_bones) {
        const auto types = skeleton_->bone_visual_types();
        const auto* rig = physics_rig_ ? &*physics_rig_ : nullptr;
        const auto hidden = [&](std::size_t index) {
            return !state_.show_physics_bones && rig != nullptr && rig->is_bone_driven(index);
        };
        const std::array<float, 4> standard{0.55f, 0.68f, 0.80f, 0.85f};
        const std::array<float, 4> ik_link{0.84f, 0.75f, 0.41f, 0.95f};
        const std::array<float, 4> active{0.49f, 0.78f, 0.89f, 1.0f};
        const std::array<float, 4> selected{0.95f, 0.42f, 0.42f, 1.0f};

        for (std::size_t i = 0; i < skeleton_->bones.size(); ++i) {
            const auto& bone = skeleton_->bones[i];
            if ((bone.flags & 0x0008) == 0 || hidden(i)) {
                continue;
            }
            const Vec3 position = skeleton_->world_positions[i];
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
                line(skeleton_->world_positions[*bone.parent_index], position, color);
            }
        }
    }

    if (physics_rig_) {
        const auto* rig = &*physics_rig_;
        if (state_.show_rigid_bodies) {
            for (const auto& body : rig->get_debug_rigid_bodies()) {
                const Vec3 position(body.position[0], body.position[1], body.position[2]);
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
                cross(Vec3(joint.position[0], joint.position[1], joint.position[2]), 0.2f,
                      {0.62f, 0.53f, 0.77f, 0.9f});
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
        if (skeleton_) {
            const Mat4 view_projection = camera_.view_projection_matrix();
            std::optional<std::size_t> best;
            float best_distance = 10.0f;
            for (std::size_t i = 0; i < skeleton_->bones.size(); ++i) {
                const auto& bone = skeleton_->bones[i];
                if ((bone.flags & 0x0008) == 0) {
                    continue;
                }
                if (!state_.show_physics_bones && physics_rig_ &&
                    physics_rig_->is_bone_driven(i)) {
                    continue;
                }
                const auto projected = project_to_screen(skeleton_->world_positions[i],
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
                const auto& bone = skeleton_->bones[*best];
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
