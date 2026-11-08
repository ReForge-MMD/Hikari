#pragma once

#include <QMainWindow>

#include <array>
#include <vector>

#include "hikari/studio/app_state.hpp"

class QAction;
class QActionGroup;
class QCheckBox;
class QCloseEvent;
class QComboBox;
class QDockWidget;
class QDoubleSpinBox;
class QEvent;
class QKeyEvent;
class QLabel;
class QMenu;
class QPushButton;
class QSlider;
class QSpinBox;
class QTimer;

namespace hikari::studio {

class D3DViewport;
class TimelineWidget;
class ViewportFooter;
class ViewportHeader;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QString model_path, QString motion_path, QString audio_path = {},
                        QWidget* parent = nullptr);

signals:
    void startupFinished();

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void build_menus();
    void build_docks();
    QDockWidget* add_panel_dock(const QString& title, const QString& object_name, QWidget* body,
                                Qt::DockWidgetArea area);
    void reset_panel_layout();
    void restore_panel_layout();
    void save_panel_layout();
    void refresh_tab_title_bars();
    void refresh_camera_presets();
    QWidget* build_viewport_area();
    QWidget* build_timeline_panel();
    QWidget* build_model_tab();
    QWidget* build_bone_tab();
    QWidget* build_facial_tab();
    QWidget* build_camera_tab();
    QWidget* build_light_tab();
    QWidget* build_shadow_tab();
    QWidget* build_accessory_tab();
    QWidget* build_view_tab();
    QWidget* build_playback_tab();
    QWidget* build_interpolation_tab();
    void show_gravity_dialog();
    void show_op_dialog();
    void show_physics_dialog();
    void show_shortcuts_dialog();
    void show_about_dialog();
    void show_screen_size_dialog();
    void show_render_video_dialog();
    void choose_model();
    void choose_motion();
    void choose_motion_save();
    void choose_vpd_load();
    void choose_vpd_save();
    void choose_render_image();
    void copy_keyframes();
    void paste_keyframes();
    void refresh_targets();
    void refresh_model_list();
    void refresh_camera_follow_combos();
    void refresh_morph_combos();
    void sync_bone_fields();
    void sync_morph_fields();
    void delete_selected_keyframes();
    void bake_selected_motion_keys();
    void update_edit_actions();
    void update_viewport_title();
    void record_scene_edit();
    void apply_scene_values_to_fields();
    void retranslate_interface();

    StudioAppState state_;
    D3DViewport* viewport_ = nullptr;
    ViewportHeader* header_ = nullptr;
    ViewportFooter* footer_ = nullptr;
    TimelineWidget* timeline_ = nullptr;
    QDockWidget* timeline_dock_ = nullptr;
    std::vector<QDockWidget*> panel_docks_;
    QMenu* panels_menu_ = nullptr;
    std::array<QDoubleSpinBox*, 3> bone_pos_{};
    std::array<QDoubleSpinBox*, 3> bone_rot_{};
    std::array<QComboBox*, 4> morph_combo_{};
    std::array<QSlider*, 4> morph_slider_{};
    std::array<QLabel*, 4> morph_value_{};
    QComboBox* physics_speed_ = nullptr;
    QActionGroup* physics_speed_group_ = nullptr;
    QComboBox* model_combo_ = nullptr;
    QComboBox* camera_target_ = nullptr;
    QDoubleSpinBox* camera_distance_ = nullptr;
    QDoubleSpinBox* camera_near_clip_ = nullptr;
    QDoubleSpinBox* camera_far_clip_ = nullptr;
    QDoubleSpinBox* camera_fov_ = nullptr;
    QDoubleSpinBox* camera_yaw_ = nullptr;
    QDoubleSpinBox* camera_pitch_ = nullptr;
    QDoubleSpinBox* camera_roll_ = nullptr;
    QComboBox* camera_preset_combo_ = nullptr;
    QCheckBox* camera_ortho_ = nullptr;
    QCheckBox* camera_follow_ = nullptr;
    QComboBox* camera_parent_model_ = nullptr;
    QComboBox* camera_parent_bone_ = nullptr;
    std::array<QDoubleSpinBox*, 3> camera_follow_offset_{};
    QLabel* audio_label_ = nullptr;
    QDoubleSpinBox* audio_offset_ = nullptr;
    QSlider* audio_volume_ = nullptr;
    QCheckBox* model_visible_ = nullptr;
    QDoubleSpinBox* model_scale_ = nullptr;
    std::array<QDoubleSpinBox*, 3> model_pos_{};
    std::array<QDoubleSpinBox*, 3> model_rot_{};
    QTimer* scene_edit_timer_ = nullptr;
    QCheckBox* bone_ik_ = nullptr;
    QPushButton* copy_button_ = nullptr;
    QPushButton* paste_button_ = nullptr;
    QAction* undo_action_ = nullptr;
    QAction* redo_action_ = nullptr;
    QSpinBox* jump_frame_ = nullptr;
    QLabel* stats_label_ = nullptr;
    QLabel* info_label_ = nullptr;
    QString persistent_status_;
    QString persistent_model_name_;
    int persistent_vertices_ = 0;
    int persistent_bones_ = 0;
    std::vector<ClipboardKey> clipboard_;
    unsigned int clipboard_anchor_ = 0;
    QString model_path_;
    QString motion_path_;
    QString audio_path_;
};

} // namespace hikari::studio
