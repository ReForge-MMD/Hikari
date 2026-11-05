#pragma once

#include <QElapsedTimer>
#include <QRect>
#include <QWidget>

#include <memory>
#include <optional>

#include "hikari/animation/jiggle.hpp"
#include "hikari/animation/morph.hpp"
#include "hikari/animation/skeleton.hpp"
#include "hikari/animation/timeline.hpp"
#include "hikari/core/d3d11.hpp"
#include "hikari/formats/pmx.hpp"
#include "hikari/physics/rig.hpp"
#include "hikari/render/camera.hpp"
#include "hikari/render/material.hpp"
#include "hikari/render/mesh.hpp"
#include "hikari/render/scene_renderer.hpp"
#include "hikari/render/texture.hpp"
#include "hikari/studio/app_state.hpp"

class QTimer;

namespace hikari::studio {

class ViewGizmo;

class D3DViewport : public QWidget {
    Q_OBJECT
public:
    explicit D3DViewport(StudioAppState& state, QWidget* parent = nullptr);
    ~D3DViewport() override;

    bool initialize();
    bool load_model(const QString& path);
    bool load_motion(const QString& path);
    bool load_vpd(const QString& path);
    bool save_vpd(const QString& path);
    void clear_model();
    void request_physics_reset();
    bool capture_image(const QString& path);
    bool render_frames(const QString& directory, int start, int end, int fps);

    void set_screen_size(int width, int height);
    QSize effective_screen_size() const;
    QRect scene_rect() const;

    void rotate_selection(int axis, float degrees);
    void translate_selection(int axis, float amount);
    void reset_selection_transform();
    void reset_camera();

    const animation::Skeleton* skeleton() const { return skeleton_ ? &*skeleton_ : nullptr; }
    const physics::PhysicsRig* physics_rig() const { return physics_rig_ ? &*physics_rig_ : nullptr; }
    int vertex_count() const { return vertex_count_; }
    int index_count() const { return index_count_; }
    int bone_count() const { return bone_count_; }
    float current_fps() const { return current_fps_; }
    float current_frame_ms() const { return current_frame_ms_; }
    StudioAppState& state() { return state_; }

signals:
    void statsChanged(float fps, float frame_ms);
    void statusMessage(const QString& message);
    void modelLoaded(const QString& name, int vertices, int bones);
    void screenSizeChanged(int width, int height);

protected:
    void showEvent(QShowEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void tick();
    void tick_impl();
    void render_scene();
    void draw_overlay();
    void update_camera();
    void evaluate_frame(float frame);
    void apply_editor_bone_override();
    void apply_bone_morphs(const animation::MorphResult& result);
    void apply_material_morphs(const animation::MorphResult& result);
    void apply_screen_size();
    void place_gizmo();
    bool selected_bone_transform(std::size_t& index) const;

    StudioAppState& state_;
    std::unique_ptr<core::D3D11Context> context_;
    std::unique_ptr<render::SceneRenderer> renderer_;
    render::Camera camera_;
    ViewGizmo* gizmo_ = nullptr;

    std::optional<formats::PmxModel> pmx_;
    std::optional<render::GpuMesh> mesh_;
    std::vector<render::GpuMaterial> materials_;
    std::vector<std::optional<render::GpuTexture>> textures_;
    std::optional<animation::Skeleton> skeleton_;
    std::optional<physics::PhysicsRig> physics_rig_;
    std::optional<animation::Jiggle> jiggle_;
    animation::JiggleSettings jiggle_settings_;
    animation::MorphEvaluator morphs_;
    std::optional<animation::MotionPlayer> motion_player_;

    QTimer* timer_ = nullptr;
    QElapsedTimer frame_timer_;
    QElapsedTimer fps_timer_;
    int fps_frames_ = 0;
    float current_fps_ = 60.0f;
    float current_frame_ms_ = 16.6f;
    int vertex_count_ = 0;
    int index_count_ = 0;
    int bone_count_ = 0;
    int applied_screen_width_ = -1;
    int applied_screen_height_ = -1;

    QPoint last_mouse_;
    bool left_down_ = false;
    bool right_down_ = false;
    bool middle_down_ = false;
    bool initialized_ = false;
};

} // namespace hikari::studio
