#pragma once

#include <QElapsedTimer>
#include <QList>
#include <QRect>
#include <QStringList>
#include <QWidget>

#include <memory>
#include <optional>
#include <vector>

#include "hikari/animation/timeline.hpp"
#include "hikari/core/d3d11.hpp"
#include "hikari/render/camera.hpp"
#include "hikari/render/scene_renderer.hpp"
#include "hikari/studio/app_state.hpp"
#include "hikari/studio/model_instance.hpp"
#include "hikari/studio/project_io.hpp"

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
    bool save_motion(const QString& path);
    bool load_motion_hkm(const QString& path);
    bool save_motion_hkm(const QString& path);
    bool load_project(const QString& path);
    bool save_project(const QString& path);
    void clear_model();
    void remove_active_model();
    void set_active_model(int index);
    void set_model_visible(int index, bool visible);
    void set_model_scale(int index, float scale);
    void request_physics_reset();
    bool capture_image(const QString& path);
    bool render_frames(const QString& directory, int start, int end, int fps);

    void set_screen_size(int width, int height);
    QSize effective_screen_size() const;
    QRect scene_rect() const;

    void rotate_selection(int axis, float degrees);
    void translate_selection(int axis, float amount);
    void reset_selection_transform();
    void set_selected_bone_ik_enabled(bool enabled);
    QMap<QString, bool> ik_states() const;
    QMap<QString, OpTarget> op_states() const;
    QStringList op_subject_names() const;
    QStringList op_target_bone_names(int model_index) const;
    QList<int> op_target_bone_indices(int model_index) const;
    QString op_display_bone_name(int model_index, const QString& name) const;
    struct OpPreview {
        bool valid = false;
        std::array<float, 3> position{0.0f, 0.0f, 0.0f};
        std::array<float, 3> angles_deg{0.0f, 0.0f, 0.0f};
    };
    OpPreview op_preview(const QString& subject, const QString& target_model,
                         const QString& target_bone) const;
    void op_register(const QMap<QString, OpTarget>& bindings);
    void op_frame_register(const QString& subject, const QString& target_model,
                           const QString& target_bone);
    void reset_camera();

    int model_count() const { return static_cast<int>(models_.size()); }
    int active_model_index() const { return active_model_; }
    QStringList model_names() const;
    const animation::Skeleton* skeleton() const;
    const physics::PhysicsRig* physics_rig() const;
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
    void modelsChanged();
    void activeModelChanged();
    void screenSizeChanged(int width, int height);

protected:
    QPaintEngine* paintEngine() const override { return nullptr; }
    void paintEvent(QPaintEvent* event) override;
    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;
    void showEvent(QShowEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    ModelInstance* active_model();
    const ModelInstance* active_model() const;
    void sync_active_model_state();
    void update_stats();
    void tick();
    void tick_impl();
    bool physics_active() const;
    void render_scene();
    render::OverlayGeometry build_overlay_geometry() const;
    void update_camera();
    bool camera_follow_state(Vec3& bone_position, Quat& rotation, float& scale) const;
    Quat camera_orientation() const;
    void evaluate_frame(float frame);
    formats::VmdMotion build_authored_vmd() const;
    HikariMotion build_hikari_motion() const;
    bool apply_parsed_motion(const formats::VmdMotion& motion, const QString& label);
    void apply_ik_states(ModelInstance& model, float timeline_frame, float motion_frame);
    struct OpWindow {
        unsigned int start = 0;
        unsigned int end = 0;
    };
    QMap<QString, OpWindow> op_windows(float timeline_frame) const;
    void apply_op_bindings(ModelInstance& model, const QMap<QString, OpTarget>& bindings);
    void resolve_op_bindings();
    ModelInstance* model_by_name(const QString& name);
    void apply_editor_tracks(ModelInstance* model, float frame, bool apply_scene_values);
    void apply_editor_bone_override();
    void apply_bone_morphs(ModelInstance& model, const animation::MorphResult& result);
    void apply_material_morphs(ModelInstance& model, const animation::MorphResult& result);
    void apply_screen_size();
    void place_gizmo();
    bool selected_bone_transform(std::size_t& index) const;

    StudioAppState& state_;
    std::unique_ptr<core::D3D11Context> context_;
    std::unique_ptr<render::SceneRenderer> renderer_;
    render::Camera camera_;
    ViewGizmo* gizmo_ = nullptr;

    std::vector<std::unique_ptr<ModelInstance>> models_;
    int active_model_ = -1;
    std::optional<animation::MotionPlayer> motion_player_;
    std::optional<formats::VmdMotion> motion_data_;
    std::vector<Mat4> render_palette_;

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
    bool physics_was_active_ = false;
    float last_editor_frame_ = -1.0f;
};

} // namespace hikari::studio
