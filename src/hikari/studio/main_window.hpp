#pragma once

#include <QMainWindow>

#include <array>
#include <vector>

#include "hikari/studio/app_state.hpp"

class QAction;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QKeyEvent;
class QLabel;
class QPushButton;
class QSlider;
class QSpinBox;
class QActionGroup;

namespace hikari::studio {

class D3DViewport;
class TimelineWidget;
class ViewportFooter;
class ViewportHeader;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QString model_path, QString motion_path, QWidget* parent = nullptr);

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    void build_menus();
    void build_docks();
    QWidget* build_viewport_area();
    QWidget* build_timeline_panel();
    QWidget* build_panel_strip();
    QWidget* make_panel(const QString& title, QWidget* body, int width);
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
    void refresh_morph_combos();
    void sync_bone_fields();
    void sync_morph_fields();
    void delete_selected_keyframes();
    void update_edit_actions();
    void update_viewport_title();

    StudioAppState state_;
    D3DViewport* viewport_ = nullptr;
    ViewportHeader* header_ = nullptr;
    ViewportFooter* footer_ = nullptr;
    TimelineWidget* timeline_ = nullptr;
    QWidget* panel_strip_ = nullptr;
    std::array<QDoubleSpinBox*, 3> bone_pos_{};
    std::array<QDoubleSpinBox*, 3> bone_rot_{};
    std::array<QComboBox*, 4> morph_combo_{};
    std::array<QSlider*, 4> morph_slider_{};
    std::array<QLabel*, 4> morph_value_{};
    QComboBox* physics_speed_ = nullptr;
    QActionGroup* physics_speed_group_ = nullptr;
    QComboBox* model_combo_ = nullptr;
    QComboBox* camera_target_ = nullptr;
    QCheckBox* model_visible_ = nullptr;
    QDoubleSpinBox* model_scale_ = nullptr;
    QCheckBox* bone_ik_ = nullptr;
    QPushButton* copy_button_ = nullptr;
    QPushButton* paste_button_ = nullptr;
    QAction* undo_action_ = nullptr;
    QAction* redo_action_ = nullptr;
    QSpinBox* jump_frame_ = nullptr;
    QLabel* stats_label_ = nullptr;
    QLabel* info_label_ = nullptr;
    std::vector<ClipboardKey> clipboard_;
    unsigned int clipboard_anchor_ = 0;
    QString model_path_;
};

} // namespace hikari::studio
