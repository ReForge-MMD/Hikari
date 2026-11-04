#include "hikari/studio/main_window.hpp"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QStatusBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

#include "hikari/studio/d3d_viewport.hpp"
#include "hikari/studio/timeline_widget.hpp"
#include "hikari/studio/theme.hpp"

namespace hikari::studio {
namespace {

QDoubleSpinBox* make_spin(double minimum, double maximum, double step, int decimals,
                          double value) {
    auto* spin = new QDoubleSpinBox();
    spin->setRange(minimum, maximum);
    spin->setSingleStep(step);
    spin->setDecimals(decimals);
    spin->setValue(value);
    spin->setKeyboardTracking(false);
    return spin;
}

QLabel* make_section(const QString& text) {
    auto* label = new QLabel(text);
    label->setObjectName(QStringLiteral("SectionLabel"));
    return label;
}

QLabel* make_hint(const QString& text) {
    auto* label = new QLabel(text);
    label->setWordWrap(true);
    label->setObjectName(QStringLiteral("HintLabel"));
    return label;
}

void add_row(QGridLayout* layout, int row, const QString& label, QWidget* widget) {
    auto* caption = new QLabel(label);
    caption->setMinimumWidth(70);
    layout->addWidget(caption, row, 0);
    layout->addWidget(widget, row, 1);
}

} // namespace

MainWindow::MainWindow(QString model_path, QString motion_path, QWidget* parent)
    : QMainWindow(parent), model_path_(std::move(model_path)) {
    setWindowTitle(QStringLiteral("Hikari Studio - Direct3D 11 MMD Animation Suite"));
    resize(1440, 900);
    setStyleSheet(NordicTheme::style_sheet());

    viewport_ = new D3DViewport(state_, this);
    setCentralWidget(viewport_);

    build_menus();
    build_docks();

    statusBar()->setSizeGripEnabled(true);
    info_label_ = new QLabel(QStringLiteral("Hikari Studio ready"));
    stats_label_ = new QLabel(QStringLiteral("60.0 FPS (16.67 ms)"));
    statusBar()->addWidget(info_label_, 1);
    statusBar()->addPermanentWidget(stats_label_);

    connect(viewport_, &D3DViewport::statsChanged, this,
            [this](float fps, float frame_ms) {
                stats_label_->setText(QStringLiteral("%1 FPS (%2 ms)")
                                          .arg(fps, 0, 'f', 1)
                                          .arg(frame_ms, 0, 'f', 2));
            });
    connect(viewport_, &D3DViewport::statusMessage, this, [this](const QString& message) {
        statusBar()->showMessage(message, 5000);
        info_label_->setText(message);
    });
    connect(viewport_, &D3DViewport::modelLoaded, this,
            [this](const QString& name, int vertices, int bones) {
                refresh_bone_list();
                refresh_morph_combos();
                info_label_->setText(
                    QStringLiteral("%1 | %2 vertices | %3 bones").arg(name).arg(vertices).arg(bones));
            });
    connect(timeline_, &TimelineWidget::boneSelected, this, [this](int index) {
        state_.selected_bone_idx = index;
        sync_bone_fields();
    });

    auto* sync_timer = new QTimer(this);
    sync_timer->setInterval(120);
    connect(sync_timer, &QTimer::timeout, this, [this] {
        if (jump_frame_ != nullptr && state_.is_playing) {
            jump_frame_->blockSignals(true);
            jump_frame_->setValue(static_cast<int>(state_.current_frame));
            jump_frame_->blockSignals(false);
        }
        if (!state_.bone_transform_dirty) {
            sync_bone_fields();
        }
        timeline_->update();
    });
    sync_timer->start();

    if (!model_path_.isEmpty()) {
        QTimer::singleShot(120, this, [this] {
            viewport_->initialize();
            viewport_->load_model(model_path_);
            refresh_bone_list();
            refresh_morph_combos();
        });
    }
    if (!motion_path.isEmpty()) {
        QTimer::singleShot(400, this, [this, motion_path] {
            viewport_->load_motion(motion_path);
        });
    }
}

void MainWindow::build_menus() {
    auto* file_menu = menuBar()->addMenu(QStringLiteral("&File"));
    file_menu->addAction(QStringLiteral("New Project"), this, [this] {
        viewport_->clear_model();
        state_.set_status(QStringLiteral("Created new project"));
        refresh_bone_list();
    });
    file_menu->addAction(QStringLiteral("Load PMX Model..."), this,
                         [this] { choose_model(); });
    file_menu->addAction(QStringLiteral("Load Motion (.VMD)..."), this,
                         [this] { choose_motion(); });
    file_menu->addSeparator();
    file_menu->addAction(QStringLiteral("Load Pose (.VPD)..."), this,
                         [this] { choose_vpd_load(); });
    file_menu->addAction(QStringLiteral("Save Pose (.VPD)..."), this,
                         [this] { choose_vpd_save(); });
    file_menu->addSeparator();
    file_menu->addAction(QStringLiteral("Render Image (BMP)..."), this,
                         [this] { choose_render_image(); });
    file_menu->addAction(QStringLiteral("Render Frame Sequence..."), this,
                         [this] { show_render_video_dialog(); });
    file_menu->addAction(QStringLiteral("Set Working Directory..."), this, [this] {
        const QString directory = QFileDialog::getExistingDirectory(this, QStringLiteral("Working directory"));
        if (!directory.isEmpty()) {
            state_.default_directory = directory;
            state_.set_status(QStringLiteral("Default directory set: %1").arg(directory));
        }
    });
    file_menu->addSeparator();
    file_menu->addAction(QStringLiteral("Exit"), this, [] { QApplication::quit(); });

    auto* edit_menu = menuBar()->addMenu(QStringLiteral("&Edit"));
    edit_menu->addAction(QStringLiteral("Reset Bone Transform (Z)"), this, [this] {
        state_.bone_pos_override = {0.0f, 0.0f, 0.0f};
        state_.bone_rot_euler_deg = {0.0f, 0.0f, 0.0f};
        state_.bone_transform_dirty = true;
        sync_bone_fields();
        state_.set_status(QStringLiteral("Bone angles and translations initialized"));
    });
    edit_menu->addAction(QStringLiteral("Delete Model"), this, [this] {
        state_.model_delete_requested = true;
    });
    edit_menu->addAction(QStringLiteral("Select All Bone Frames (N)"), this, [this] {
        for (const auto& name : state_.model_bone_names) {
            state_.selected_keyframes.insert({name, static_cast<unsigned int>(state_.current_frame)});
        }
        state_.set_status(QStringLiteral("Selected all bone keyframes at the current frame"));
    });
    edit_menu->addAction(QStringLiteral("Delete Frame Line (K)"), this, [this] {
        const unsigned int frame = static_cast<unsigned int>(state_.current_frame);
        for (auto& [track, frames] : state_.track_keyframes) {
            frames.erase(frame);
        }
        state_.set_status(QStringLiteral("Deleted keyframes at frame %1").arg(frame));
        timeline_->update();
    });

    auto* view_menu = menuBar()->addMenu(QStringLiteral("&View"));
    auto* grid_action = view_menu->addAction(QStringLiteral("Coordinate Grid (G)"));
    grid_action->setCheckable(true);
    grid_action->setChecked(state_.show_grid);
    connect(grid_action, &QAction::toggled, this, [this](bool checked) {
        state_.show_grid = checked;
        state_.show_axes = checked;
    });
    auto* white_action = view_menu->addAction(QStringLiteral("MMD White Canvas"));
    white_action->setCheckable(true);
    white_action->setChecked(state_.viewport_white_bg);
    connect(white_action, &QAction::toggled, this,
            [this](bool checked) { state_.viewport_white_bg = checked; });
    auto* wireframe_action = view_menu->addAction(QStringLiteral("Wireframe (R)"));
    wireframe_action->setCheckable(true);
    connect(wireframe_action, &QAction::toggled, this,
            [this](bool checked) { state_.wireframe = checked; });
    view_menu->addAction(QStringLiteral("Screen Size..."), this,
                         [this] { show_screen_size_dialog(); });
    view_menu->addAction(QStringLiteral("Fullscreen"), this, [this] {
        if (isFullScreen()) {
            showNormal();
        } else {
            showFullScreen();
        }
    });

    auto* physics_menu = menuBar()->addMenu(QStringLiteral("&Physics"));
    for (const auto& [label, mode] : std::array<std::pair<const char*, PhysicsMode>, 4>{
             {{"Always", PhysicsMode::Always},
              {"Anytime", PhysicsMode::Anytime},
              {"Trace", PhysicsMode::Trace},
              {"Disabled", PhysicsMode::Disabled}}}) {
        physics_menu->addAction(QString::fromLatin1(label), this,
                                [this, mode] { state_.physics_mode = mode; });
    }
    physics_menu->addSeparator();
    physics_menu->addAction(QStringLiteral("Gravity Settings..."), this,
                            [this] { show_gravity_dialog(); });
    physics_menu->addAction(QStringLiteral("Reset Physics"), this, [this] {
        state_.reset_physics_requested = true;
        state_.set_status(QStringLiteral("Physics reset requested"));
    });
    physics_menu->addAction(QStringLiteral("About Jolt Physics"), this,
                            [this] { show_physics_dialog(); });

    auto* help_menu = menuBar()->addMenu(QStringLiteral("&Help"));
    help_menu->addAction(QStringLiteral("Keyboard Shortcuts"), this,
                         [this] { show_shortcuts_dialog(); });
    help_menu->addAction(QStringLiteral("Effect Mapping / ReForge..."), this,
                         [this] { show_effect_dialog(); });
    help_menu->addAction(QStringLiteral("About Hikari Studio"), this,
                         [this] { show_about_dialog(); });
}

void MainWindow::build_docks() {
    auto* timeline_dock = new QDockWidget(QStringLiteral("Timeline"), this);
    timeline_dock->setObjectName(QStringLiteral("TimelineDock"));
    auto* timeline_panel = new QWidget();
    auto* timeline_layout = new QVBoxLayout(timeline_panel);
    timeline_layout->setContentsMargins(6, 6, 6, 6);
    timeline_layout->setSpacing(6);
    auto* frame_row = new QHBoxLayout();
    auto* first_button = new QPushButton(QStringLiteral("|<"));
    auto* prev_button = new QPushButton(QStringLiteral("<"));
    jump_frame_ = new QSpinBox();
    jump_frame_->setRange(0, 100000);
    jump_frame_->setAlignment(Qt::AlignCenter);
    auto* next_button = new QPushButton(QStringLiteral(">"));
    auto* last_button = new QPushButton(QStringLiteral(">|"));
    auto* play_button = new QPushButton(QStringLiteral("Play"));
    frame_row->addWidget(first_button);
    frame_row->addWidget(prev_button);
    frame_row->addWidget(jump_frame_, 1);
    frame_row->addWidget(next_button);
    frame_row->addWidget(last_button);
    frame_row->addWidget(play_button);
    timeline_layout->addLayout(frame_row);

    timeline_ = new TimelineWidget(state_);
    auto* scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    scroll->setWidget(timeline_);
    timeline_layout->addWidget(scroll, 1);

    auto* actions_row = new QHBoxLayout();
    auto* copy_button = new QPushButton(QStringLiteral("Copy"));
    auto* paste_button = new QPushButton(QStringLiteral("Paste"));
    auto* delete_button = new QPushButton(QStringLiteral("Delete"));
    auto* register_button = new QPushButton(QStringLiteral("Register"));
    actions_row->addWidget(copy_button);
    actions_row->addWidget(paste_button);
    actions_row->addWidget(delete_button);
    actions_row->addWidget(register_button);
    timeline_layout->addLayout(actions_row);

    auto* zoom_row = new QHBoxLayout();
    zoom_row->addWidget(new QLabel(QStringLiteral("Zoom")));
    auto* zoom = new QSlider(Qt::Horizontal);
    zoom->setRange(50, 300);
    zoom->setValue(100);
    zoom_row->addWidget(zoom, 1);
    timeline_layout->addLayout(zoom_row);

    connect(first_button, &QPushButton::clicked, this, [this] {
        state_.current_frame = 0.0f;
        jump_frame_->setValue(0);
    });
    connect(prev_button, &QPushButton::clicked, this, [this] {
        state_.step_frame(-1);
        jump_frame_->setValue(static_cast<int>(state_.current_frame));
    });
    connect(next_button, &QPushButton::clicked, this, [this] {
        state_.step_frame(1);
        jump_frame_->setValue(static_cast<int>(state_.current_frame));
    });
    connect(last_button, &QPushButton::clicked, this, [this] {
        state_.current_frame = static_cast<float>(state_.play_range.second);
        jump_frame_->setValue(state_.play_range.second);
    });
    connect(jump_frame_, &QSpinBox::valueChanged, this,
            [this](int value) { state_.current_frame = static_cast<float>(value); });
    connect(play_button, &QPushButton::clicked, this, [this, play_button] {
        state_.toggle_play();
        play_button->setText(state_.is_playing ? QStringLiteral("Pause") : QStringLiteral("Play"));
    });
    connect(zoom, &QSlider::valueChanged, this, [this](int value) {
        state_.timeline_zoom = static_cast<float>(value) / 100.0f;
        timeline_->update();
    });
    connect(delete_button, &QPushButton::clicked, this, [this] {
        state_.delete_keyframe(state_.selected_track_name(),
                               static_cast<unsigned int>(state_.current_frame));
        timeline_->update();
    });
    connect(register_button, &QPushButton::clicked, this, [this] {
        state_.register_keyframe(state_.selected_track_name(),
                                 static_cast<unsigned int>(state_.current_frame));
        timeline_->update();
    });
    copy_button->setEnabled(false);
    paste_button->setEnabled(false);
    timeline_dock->setWidget(timeline_panel);
    addDockWidget(Qt::LeftDockWidgetArea, timeline_dock);

    auto* panels_dock = new QDockWidget(QStringLiteral("Editor"), this);
    panels_dock->setObjectName(QStringLiteral("EditorDock"));
    panels_ = new QTabWidget();
    panels_->addTab(build_model_tab(), QStringLiteral("Model"));
    panels_->addTab(build_bone_tab(), QStringLiteral("Bone"));
    panels_->addTab(build_facial_tab(), QStringLiteral("Facial"));
    panels_->addTab(build_camera_tab(), QStringLiteral("Camera"));
    panels_->addTab(build_light_tab(), QStringLiteral("Light"));
    panels_->addTab(build_shadow_tab(), QStringLiteral("Shadow"));
    panels_->addTab(build_accessory_tab(), QStringLiteral("Accessory"));
    panels_->addTab(build_interpolation_tab(), QStringLiteral("Interpolation"));
    panels_->addTab(build_playback_tab(), QStringLiteral("Playback"));
    panels_->addTab(build_view_tab(), QStringLiteral("View"));
    panels_dock->setWidget(panels_);
    addDockWidget(Qt::BottomDockWidgetArea, panels_dock);
    resizeDocks({timeline_dock, panels_dock}, {300, 260}, Qt::Vertical);
}

QWidget* MainWindow::build_model_tab() {
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    auto* row = new QHBoxLayout();
    auto* load = new QPushButton(QStringLiteral("Load PMX..."));
    auto* remove = new QPushButton(QStringLiteral("Remove"));
    row->addWidget(load);
    row->addWidget(remove);
    layout->addLayout(row);

    auto* visible = new QCheckBox(QStringLiteral("Display"));
    visible->setChecked(state_.model_visible);
    auto* shadow = new QCheckBox(QStringLiteral("Shadow"));
    shadow->setChecked(state_.model_shadow_visible);
    layout->addWidget(visible);
    layout->addWidget(shadow);
    auto* register_button = new QPushButton(QStringLiteral("Register Display/IK Keyframe"));
    layout->addWidget(register_button);
    layout->addWidget(make_hint(QStringLiteral("PMX models are loaded with materials, textures, skeleton, "
                                               "Jolt rigid bodies, morphs and jiggle regions.")));
    layout->addStretch(1);

    connect(load, &QPushButton::clicked, this, [this] { choose_model(); });
    connect(remove, &QPushButton::clicked, this,
            [this] { state_.model_delete_requested = true; });
    connect(visible, &QCheckBox::toggled, this,
            [this](bool checked) { state_.model_visible = checked; });
    connect(shadow, &QCheckBox::toggled, this,
            [this](bool checked) { state_.model_shadow_visible = checked; });
    connect(register_button, &QPushButton::clicked, this, [this] {
        state_.register_keyframe(QStringLiteral("Disp/IK/OP"),
                                 static_cast<unsigned int>(state_.current_frame));
        timeline_->update();
    });
    return page;
}

QWidget* MainWindow::build_bone_tab() {
    auto* page = new QWidget();
    auto* layout = new QHBoxLayout(page);
    bone_list_ = new QListWidget();
    bone_list_->setMinimumWidth(220);
    layout->addWidget(bone_list_);

    auto* controls = new QWidget();
    auto* control_layout = new QVBoxLayout(controls);
    auto* mode_row = new QHBoxLayout();
    mode_row->addWidget(new QLabel(QStringLiteral("Mode:")));
    auto* mode = new QComboBox();
    mode->addItems({QStringLiteral("Select"), QStringLiteral("Rotate"), QStringLiteral("Move")});
    mode_row->addWidget(mode);
    control_layout->addLayout(mode_row);

    auto* visibility_row = new QHBoxLayout();
    auto* show_bones = new QCheckBox(QStringLiteral("Bones"));
    show_bones->setChecked(state_.show_bones);
    auto* show_physics = new QCheckBox(QStringLiteral("Physics bones"));
    show_physics->setChecked(state_.show_physics_bones);
    visibility_row->addWidget(show_bones);
    visibility_row->addWidget(show_physics);
    control_layout->addLayout(visibility_row);

    auto* grid = new QGridLayout();
    const char* axis_names = "XYZ";
    for (int i = 0; i < 3; ++i) {
        bone_pos_[static_cast<std::size_t>(i)] =
            make_spin(-1000.0, 1000.0, 0.05, 2, 0.0);
        bone_rot_[static_cast<std::size_t>(i)] =
            make_spin(-720.0, 720.0, 0.5, 1, 0.0);
        add_row(grid, i, QStringLiteral("Move %1").arg(QChar(axis_names[i])),
                bone_pos_[static_cast<std::size_t>(i)]);
        add_row(grid, i + 3, QStringLiteral("Angle %1").arg(QChar(axis_names[i])),
                bone_rot_[static_cast<std::size_t>(i)]);
    }
    control_layout->addLayout(grid);

    auto* buttons = new QHBoxLayout();
    auto* reset = new QPushButton(QStringLiteral("Reset"));
    auto* register_button = new QPushButton(QStringLiteral("Register"));
    auto* physics = new QPushButton(QStringLiteral("Physics"));
    buttons->addWidget(reset);
    buttons->addWidget(register_button);
    buttons->addWidget(physics);
    control_layout->addLayout(buttons);
    control_layout->addStretch(1);
    layout->addWidget(controls, 1);

    connect(bone_list_, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row >= 0) {
            state_.selected_bone_idx = row;
            state_.bone_transform_dirty = false;
            sync_bone_fields();
            state_.set_status(QStringLiteral("Selected bone: %1").arg(bone_list_->item(row)->text()));
        }
    });
    connect(mode, &QComboBox::currentIndexChanged, this, [this](int index) {
        state_.bone_mode = index == 1 ? BoneTransformMode::Rotate
                           : index == 2 ? BoneTransformMode::Move
                                        : BoneTransformMode::Select;
    });
    connect(show_bones, &QCheckBox::toggled, this,
            [this](bool checked) { state_.show_bones = checked; });
    connect(show_physics, &QCheckBox::toggled, this,
            [this](bool checked) { state_.show_physics_bones = checked; });
    for (int i = 0; i < 3; ++i) {
        connect(bone_pos_[static_cast<std::size_t>(i)],
                &QDoubleSpinBox::valueChanged, this, [this, i](double value) {
                    state_.bone_pos_override[static_cast<std::size_t>(i)] =
                        static_cast<float>(value);
                    state_.bone_transform_dirty = true;
                });
        connect(bone_rot_[static_cast<std::size_t>(i)],
                &QDoubleSpinBox::valueChanged, this, [this, i](double value) {
                    state_.bone_rot_euler_deg[static_cast<std::size_t>(i)] =
                        static_cast<float>(value);
                    state_.bone_transform_dirty = true;
                });
    }
    connect(reset, &QPushButton::clicked, this, [this] {
        state_.bone_pos_override = {0.0f, 0.0f, 0.0f};
        state_.bone_rot_euler_deg = {0.0f, 0.0f, 0.0f};
        state_.bone_transform_dirty = true;
        sync_bone_fields();
    });
    connect(register_button, &QPushButton::clicked, this, [this] {
        state_.register_keyframe(state_.selected_track_name(),
                                 static_cast<unsigned int>(state_.current_frame));
        timeline_->update();
    });
    connect(physics, &QPushButton::clicked, this, [this] {
        state_.physics_mode = state_.physics_mode == PhysicsMode::Disabled
                                  ? PhysicsMode::Anytime
                                  : PhysicsMode::Disabled;
    });
    return page;
}

QWidget* MainWindow::build_facial_tab() {
    auto* page = new QWidget();
    auto* layout = new QHBoxLayout(page);
    const QStringList titles = {QStringLiteral("Eyes"), QStringLiteral("Mouth"),
                                QStringLiteral("Brow"), QStringLiteral("Other")};
    for (int i = 0; i < 4; ++i) {
        auto* group = new QGroupBox(titles[i]);
        auto* group_layout = new QVBoxLayout(group);
        morph_combo_[static_cast<std::size_t>(i)] = new QComboBox();
        morph_slider_[static_cast<std::size_t>(i)] = new QSlider(Qt::Horizontal);
        morph_slider_[static_cast<std::size_t>(i)]->setRange(0, 100);
        morph_value_[static_cast<std::size_t>(i)] = new QLabel(QStringLiteral("0.000"));
        group_layout->addWidget(morph_combo_[static_cast<std::size_t>(i)]);
        group_layout->addWidget(morph_slider_[static_cast<std::size_t>(i)]);
        group_layout->addWidget(morph_value_[static_cast<std::size_t>(i)]);
        layout->addWidget(group);

        connect(morph_combo_[static_cast<std::size_t>(i)], &QComboBox::currentTextChanged,
                this, [this, i](const QString& text) {
                    state_.selected_morph_names[static_cast<std::size_t>(i)] = text;
                    const float weight = state_.morph_weights.value(text, 0.0f);
                    morph_slider_[static_cast<std::size_t>(i)]->blockSignals(true);
                    morph_slider_[static_cast<std::size_t>(i)]->setValue(
                        static_cast<int>(std::round(weight * 100.0f)));
                    morph_slider_[static_cast<std::size_t>(i)]->blockSignals(false);
                });
        connect(morph_slider_[static_cast<std::size_t>(i)], &QSlider::valueChanged, this,
                [this, i](int value) {
                    const float weight = static_cast<float>(value) / 100.0f;
                    state_.morph_slider_values[static_cast<std::size_t>(i)] = weight;
                    const QString name =
                        state_.selected_morph_names[static_cast<std::size_t>(i)];
                    if (!name.isEmpty()) {
                        state_.morph_weights[name] = weight;
                    }
                    morph_value_[static_cast<std::size_t>(i)]->setText(
                        QString::number(weight, 'f', 3));
                });
    }
    return page;
}

QWidget* MainWindow::build_camera_tab() {
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    auto* distance = make_spin(1.0, 500.0, 0.2, 2, state_.camera_distance);
    auto* fov = make_spin(10.0, 120.0, 0.5, 1, state_.camera_fov_deg);
    auto* yaw = make_spin(-180.0, 180.0, 1.0, 1,
                          state_.camera_yaw * 180.0 / 3.14159265358979323846);
    auto* pitch = make_spin(-88.0, 88.0, 1.0, 1,
                            state_.camera_pitch * 180.0 / 3.14159265358979323846);
    auto* grid = new QGridLayout();
    add_row(grid, 0, QStringLiteral("Distance"), distance);
    add_row(grid, 1, QStringLiteral("FOV"), fov);
    add_row(grid, 2, QStringLiteral("Yaw"), yaw);
    add_row(grid, 3, QStringLiteral("Pitch"), pitch);
    layout->addLayout(grid);

    auto* row = new QHBoxLayout();
    auto* reset = new QPushButton(QStringLiteral("Reset Camera"));
    auto* ortho = new QCheckBox(QStringLiteral("Orthographic"));
    row->addWidget(reset);
    row->addWidget(ortho);
    layout->addLayout(row);
    auto* register_button = new QPushButton(QStringLiteral("Register Camera Keyframe"));
    layout->addWidget(register_button);
    layout->addWidget(make_hint(QStringLiteral("Right mouse drag orbits, middle mouse drag pans, "
                                               "wheel zooms the viewport.")));
    layout->addStretch(1);

    connect(distance, &QDoubleSpinBox::valueChanged, this,
            [this](double value) { state_.camera_distance = static_cast<float>(value); });
    connect(fov, &QDoubleSpinBox::valueChanged, this,
            [this](double value) { state_.camera_fov_deg = static_cast<float>(value); });
    connect(yaw, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        state_.camera_yaw = static_cast<float>(value) * 3.14159265358979323846f / 180.0f;
    });
    connect(pitch, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        state_.camera_pitch = static_cast<float>(value) * 3.14159265358979323846f / 180.0f;
    });
    connect(reset, &QPushButton::clicked, this, [this, distance, fov, yaw, pitch] {
        state_.camera_yaw = 0.0f;
        state_.camera_pitch = 0.15f;
        state_.camera_distance = 35.0f;
        state_.camera_pan = {0.0f, 10.0f, 0.0f};
        distance->setValue(35.0);
        fov->setValue(30.0);
        yaw->setValue(0.0);
        pitch->setValue(8.6);
    });
    connect(ortho, &QCheckBox::toggled, this,
            [this](bool checked) { state_.camera_orthographic = checked; });
    connect(register_button, &QPushButton::clicked, this, [this] {
        state_.register_keyframe(QStringLiteral("camera"),
                                 static_cast<unsigned int>(state_.current_frame));
        timeline_->update();
    });
    return page;
}

QWidget* MainWindow::build_light_tab() {
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    auto* grid = new QGridLayout();
    std::array<QSlider*, 3> sliders{};
    const QStringList names = {QStringLiteral("R"), QStringLiteral("G"), QStringLiteral("B")};
    for (int i = 0; i < 3; ++i) {
        sliders[static_cast<std::size_t>(i)] = new QSlider(Qt::Horizontal);
        sliders[static_cast<std::size_t>(i)]->setRange(0, 100);
        sliders[static_cast<std::size_t>(i)]->setValue(
            static_cast<int>(state_.light_color[static_cast<std::size_t>(i)] * 100.0f));
        add_row(grid, i, names[i], sliders[static_cast<std::size_t>(i)]);
    }
    layout->addLayout(grid);
    auto* direction = new QHBoxLayout();
    std::array<QDoubleSpinBox*, 3> dir_spins{};
    const char* axis_names = "XYZ";
    for (int i = 0; i < 3; ++i) {
        dir_spins[static_cast<std::size_t>(i)] =
            make_spin(-1.0, 1.0, 0.02, 2, state_.light_direction[static_cast<std::size_t>(i)]);
        direction->addWidget(new QLabel(QStringLiteral("Dir %1").arg(QChar(axis_names[i]))));
        direction->addWidget(dir_spins[static_cast<std::size_t>(i)]);
    }
    layout->addLayout(direction);
    auto* row = new QHBoxLayout();
    auto* reset = new QPushButton(QStringLiteral("Reset"));
    auto* register_button = new QPushButton(QStringLiteral("Register"));
    row->addWidget(reset);
    row->addWidget(register_button);
    layout->addLayout(row);
    layout->addStretch(1);

    for (int i = 0; i < 3; ++i) {
        connect(sliders[static_cast<std::size_t>(i)], &QSlider::valueChanged, this,
                [this, i](int value) {
                    state_.light_color[static_cast<std::size_t>(i)] =
                        static_cast<float>(value) / 100.0f;
                });
        connect(dir_spins[static_cast<std::size_t>(i)], &QDoubleSpinBox::valueChanged, this,
                [this, i](double value) {
                    state_.light_direction[static_cast<std::size_t>(i)] =
                        static_cast<float>(value);
                });
    }
    connect(reset, &QPushButton::clicked, this, [this, sliders, dir_spins] {
        state_.light_color = {0.6f, 0.6f, 0.6f};
        state_.light_direction = {-0.5f, -1.0f, -0.5f};
        for (int i = 0; i < 3; ++i) {
            sliders[static_cast<std::size_t>(i)]->setValue(60);
            dir_spins[static_cast<std::size_t>(i)]->setValue(
                state_.light_direction[static_cast<std::size_t>(i)]);
        }
    });
    connect(register_button, &QPushButton::clicked, this, [this] {
        state_.register_keyframe(QStringLiteral("light"),
                                 static_cast<unsigned int>(state_.current_frame));
        timeline_->update();
    });
    return page;
}

QWidget* MainWindow::build_shadow_tab() {
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    auto* mode = new QComboBox();
    mode->addItems({QStringLiteral("Off"), QStringLiteral("Mode 1"), QStringLiteral("Mode 2")});
    mode->setCurrentIndex(static_cast<int>(state_.shadow_mode));
    auto* range = make_spin(100.0, 15000.0, 10.0, 1, state_.shadow_range);
    auto* detail = make_spin(0.0, 1.0, 0.01, 2, state_.shadow_detail);
    auto* grid = new QGridLayout();
    add_row(grid, 0, QStringLiteral("Mode"), mode);
    add_row(grid, 1, QStringLiteral("Range"), range);
    add_row(grid, 2, QStringLiteral("Detail"), detail);
    layout->addLayout(grid);
    auto* register_button = new QPushButton(QStringLiteral("Register Shadow Keyframe"));
    layout->addWidget(register_button);
    layout->addStretch(1);

    connect(mode, &QComboBox::currentIndexChanged, this, [this](int index) {
        state_.shadow_mode = static_cast<ShadowMode>(index);
    });
    connect(range, &QDoubleSpinBox::valueChanged, this,
            [this](double value) { state_.shadow_range = static_cast<float>(value); });
    connect(detail, &QDoubleSpinBox::valueChanged, this,
            [this](double value) { state_.shadow_detail = static_cast<float>(value); });
    connect(register_button, &QPushButton::clicked, this, [this] {
        state_.register_keyframe(QStringLiteral("shadow"),
                                 static_cast<unsigned int>(state_.current_frame));
        timeline_->update();
    });
    return page;
}

QWidget* MainWindow::build_accessory_tab() {
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    auto* load = new QPushButton(QStringLiteral("Load Accessory (.x)..."));
    auto* clear = new QPushButton(QStringLiteral("Remove Accessory"));
    auto* row = new QHBoxLayout();
    row->addWidget(load);
    row->addWidget(clear);
    layout->addLayout(row);
    auto* scale = make_spin(0.01, 100.0, 0.01, 2, state_.accessory_scale);
    auto* alpha = make_spin(0.0, 1.0, 0.01, 2, state_.accessory_alpha);
    auto* grid = new QGridLayout();
    add_row(grid, 0, QStringLiteral("Scale"), scale);
    add_row(grid, 1, QStringLiteral("Alpha"), alpha);
    layout->addLayout(grid);
    auto* register_button = new QPushButton(QStringLiteral("Register Accessory Keyframe"));
    layout->addWidget(register_button);
    layout->addWidget(make_hint(QStringLiteral("Accessories are resolved by the MMD host bridge; the "
                                               "studio keeps their transform state.")));
    layout->addStretch(1);

    connect(load, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Accessory"),
                                                          state_.default_directory,
                                                          QStringLiteral("DirectX Model (*.x)"));
        if (!path.isEmpty()) {
            state_.accessory_name = QFileInfo(path).completeBaseName();
            state_.set_status(QStringLiteral("Loaded accessory: %1").arg(path));
        }
    });
    connect(clear, &QPushButton::clicked, this, [this] { state_.accessory_name.clear(); });
    connect(scale, &QDoubleSpinBox::valueChanged, this,
            [this](double value) { state_.accessory_scale = static_cast<float>(value); });
    connect(alpha, &QDoubleSpinBox::valueChanged, this,
            [this](double value) { state_.accessory_alpha = static_cast<float>(value); });
    connect(register_button, &QPushButton::clicked, this, [this] {
        state_.register_keyframe(QStringLiteral("accessory"),
                                 static_cast<unsigned int>(state_.current_frame));
        timeline_->update();
    });
    return page;
}

QWidget* MainWindow::build_view_tab() {
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    auto* grid = new QCheckBox(QStringLiteral("Ground grid"));
    grid->setChecked(state_.show_grid);
    auto* axes = new QCheckBox(QStringLiteral("Coordinate axes"));
    axes->setChecked(state_.show_axes);
    auto* white = new QCheckBox(QStringLiteral("MMD white canvas"));
    white->setChecked(state_.viewport_white_bg);
    auto* wireframe = new QCheckBox(QStringLiteral("Wireframe"));
    wireframe->setChecked(state_.wireframe);
    auto* info = new QCheckBox(QStringLiteral("Information display"));
    info->setChecked(state_.info_display);
    auto* aa = new QCheckBox(QStringLiteral("Anti-aliasing"));
    aa->setChecked(state_.antialiasing);
    layout->addWidget(grid);
    layout->addWidget(axes);
    layout->addWidget(white);
    layout->addWidget(wireframe);
    layout->addWidget(info);
    layout->addWidget(aa);

    auto* fps = new QComboBox();
    fps->addItems({QStringLiteral("30 FPS"), QStringLiteral("60 FPS"),
                   QStringLiteral("Unlimited")});
    fps->setCurrentIndex(static_cast<int>(state_.fps_limit));
    layout->addWidget(new QLabel(QStringLiteral("FPS limit")));
    layout->addWidget(fps);
    layout->addStretch(1);

    connect(grid, &QCheckBox::toggled, this,
            [this](bool checked) { state_.show_grid = checked; });
    connect(axes, &QCheckBox::toggled, this,
            [this](bool checked) { state_.show_axes = checked; });
    connect(white, &QCheckBox::toggled, this,
            [this](bool checked) { state_.viewport_white_bg = checked; });
    connect(wireframe, &QCheckBox::toggled, this,
            [this](bool checked) { state_.wireframe = checked; });
    connect(info, &QCheckBox::toggled, this,
            [this](bool checked) { state_.info_display = checked; });
    connect(aa, &QCheckBox::toggled, this,
            [this](bool checked) { state_.antialiasing = checked; });
    connect(fps, &QComboBox::currentIndexChanged, this,
            [this](int index) { state_.fps_limit = static_cast<FpsLimit>(index); });
    return page;
}

QWidget* MainWindow::build_playback_tab() {
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    auto* play = new QPushButton(QStringLiteral("Play / Pause"));
    auto* loop = new QCheckBox(QStringLiteral("Loop"));
    loop->setChecked(state_.loop_playback);
    auto* multithreaded = new QCheckBox(QStringLiteral("Multithreaded deferred rendering"));
    multithreaded->setChecked(state_.multithreaded);
    auto* jiggle = new QCheckBox(QStringLiteral("Jiggle secondary dynamics"));
    jiggle->setChecked(state_.jiggle_enabled);
    auto* vsync = new QCheckBox(QStringLiteral("VSync"));
    vsync->setChecked(state_.vsync);
    auto* physics = new QCheckBox(QStringLiteral("Physics enabled"));
    physics->setChecked(state_.physics_mode != PhysicsMode::Disabled);
    auto* playtime = new QCheckBox(QStringLiteral("Physics only while playing"));
    playtime->setChecked(state_.playtime_only);
    layout->addWidget(play);
    layout->addWidget(loop);
    layout->addWidget(multithreaded);
    layout->addWidget(jiggle);
    layout->addWidget(vsync);
    layout->addWidget(physics);
    layout->addWidget(playtime);
    layout->addStretch(1);

    connect(play, &QPushButton::clicked, this, [this] { state_.toggle_play(); });
    connect(loop, &QCheckBox::toggled, this,
            [this](bool checked) { state_.loop_playback = checked; });
    connect(multithreaded, &QCheckBox::toggled, this,
            [this](bool checked) { state_.multithreaded = checked; });
    connect(jiggle, &QCheckBox::toggled, this,
            [this](bool checked) { state_.jiggle_enabled = checked; });
    connect(vsync, &QCheckBox::toggled, this,
            [this](bool checked) { state_.vsync = checked; });
    connect(physics, &QCheckBox::toggled, this, [this](bool checked) {
        state_.physics_mode = checked ? PhysicsMode::Anytime : PhysicsMode::Disabled;
    });
    connect(playtime, &QCheckBox::toggled, this,
            [this](bool checked) { state_.playtime_only = checked; });
    return page;
}

QWidget* MainWindow::build_interpolation_tab() {
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    auto* target = new QComboBox();
    target->addItems({QStringLiteral("Rotation"), QStringLiteral("Move X"),
                      QStringLiteral("Move Y"), QStringLiteral("Move Z")});
    target->setCurrentIndex(static_cast<int>(state_.curve_target));
    auto* automatic = new QCheckBox(QStringLiteral("Automatic curve"));
    automatic->setChecked(state_.curve_auto);
    auto* row = new QHBoxLayout();
    auto* linear = new QPushButton(QStringLiteral("Linear"));
    auto* scurve = new QPushButton(QStringLiteral("S-Curve"));
    row->addWidget(linear);
    row->addWidget(scurve);
    layout->addWidget(target);
    layout->addWidget(automatic);
    layout->addLayout(row);
    layout->addWidget(make_hint(QStringLiteral("Bezier control points are stored in the VMD "
                                               "interpolation channel of each keyframe.")));
    layout->addStretch(1);

    connect(target, &QComboBox::currentIndexChanged, this,
            [this](int index) { state_.curve_target = static_cast<InterpolationTarget>(index); });
    connect(automatic, &QCheckBox::toggled, this,
            [this](bool checked) { state_.curve_auto = checked; });
    connect(linear, &QPushButton::clicked, this, [this] {
        state_.curve_p1 = {0.33f, 0.33f};
        state_.curve_p2 = {0.66f, 0.66f};
    });
    connect(scurve, &QPushButton::clicked, this, [this] {
        state_.curve_p1 = {0.1f, 0.9f};
        state_.curve_p2 = {0.9f, 0.1f};
    });
    return page;
}

void MainWindow::refresh_bone_list() {
    if (bone_list_ == nullptr) {
        return;
    }
    bone_list_->blockSignals(true);
    bone_list_->clear();
    bone_list_->addItems(state_.model_bone_names);
    bone_list_->blockSignals(false);
    if (state_.selected_bone_idx) {
        bone_list_->setCurrentRow(*state_.selected_bone_idx);
    }
}

void MainWindow::refresh_morph_combos() {
    for (int i = 0; i < 4; ++i) {
        auto* combo = morph_combo_[static_cast<std::size_t>(i)];
        if (combo == nullptr) {
            continue;
        }
        combo->blockSignals(true);
        combo->clear();
        combo->addItems(state_.category_morph_names[static_cast<std::size_t>(i)]);
        const QString selected = state_.selected_morph_names[static_cast<std::size_t>(i)];
        const int index = combo->findText(selected);
        combo->setCurrentIndex(index >= 0 ? index : 0);
        combo->blockSignals(false);
    }
}

void MainWindow::sync_bone_fields() {
    const auto* skeleton = viewport_->skeleton();
    if (skeleton == nullptr || !state_.selected_bone_idx) {
        return;
    }
    const std::size_t index = static_cast<std::size_t>(*state_.selected_bone_idx);
    if (index >= skeleton->bones.size()) {
        return;
    }
    const Vec3 translation = skeleton->local_translations[index];
    state_.bone_pos_override = translation.to_array();
    const Vec3 euler = skeleton->local_rotations[index].to_euler_xyz();
    state_.bone_rot_euler_deg = {euler.x * 180.0f / 3.14159265358979323846f,
                                 euler.y * 180.0f / 3.14159265358979323846f,
                                 euler.z * 180.0f / 3.14159265358979323846f};
    for (int i = 0; i < 3; ++i) {
        if (bone_pos_[static_cast<std::size_t>(i)] != nullptr) {
            bone_pos_[static_cast<std::size_t>(i)]->blockSignals(true);
            bone_pos_[static_cast<std::size_t>(i)]->setValue(
                state_.bone_pos_override[static_cast<std::size_t>(i)]);
            bone_pos_[static_cast<std::size_t>(i)]->blockSignals(false);
        }
        if (bone_rot_[static_cast<std::size_t>(i)] != nullptr) {
            bone_rot_[static_cast<std::size_t>(i)]->blockSignals(true);
            bone_rot_[static_cast<std::size_t>(i)]->setValue(
                state_.bone_rot_euler_deg[static_cast<std::size_t>(i)]);
            bone_rot_[static_cast<std::size_t>(i)]->blockSignals(false);
        }
    }
}

void MainWindow::sync_morph_fields() {
    for (int i = 0; i < 4; ++i) {
        if (morph_slider_[static_cast<std::size_t>(i)] == nullptr) {
            continue;
        }
        const QString name = state_.selected_morph_names[static_cast<std::size_t>(i)];
        const float weight = state_.morph_weights.value(name, 0.0f);
        morph_slider_[static_cast<std::size_t>(i)]->blockSignals(true);
        morph_slider_[static_cast<std::size_t>(i)]->setValue(
            static_cast<int>(std::round(weight * 100.0f)));
        morph_slider_[static_cast<std::size_t>(i)]->blockSignals(false);
        morph_value_[static_cast<std::size_t>(i)]->setText(QString::number(weight, 'f', 3));
    }
}

void MainWindow::choose_model() {
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Load PMX Model"), state_.default_directory,
        QStringLiteral("Polygon Model eXtended (*.pmx);;All files (*.*)"));
    if (path.isEmpty()) {
        return;
    }
    viewport_->initialize();
    if (viewport_->load_model(path)) {
        state_.default_directory = QFileInfo(path).absolutePath();
        refresh_bone_list();
        refresh_morph_combos();
    }
}

void MainWindow::choose_motion() {
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Load VMD Motion"), state_.default_directory,
        QStringLiteral("Vocaloid Motion Data (*.vmd);;All files (*.*)"));
    if (!path.isEmpty()) {
        viewport_->load_motion(path);
        state_.default_directory = QFileInfo(path).absolutePath();
    }
}

void MainWindow::choose_vpd_load() {
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Load VPD Pose"), state_.default_directory,
        QStringLiteral("Vocaloid Pose Data (*.vpd);;All files (*.*)"));
    if (!path.isEmpty()) {
        viewport_->load_vpd(path);
        sync_morph_fields();
    }
}

void MainWindow::choose_vpd_save() {
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Save VPD Pose"), state_.default_directory,
        QStringLiteral("Vocaloid Pose Data (*.vpd)"));
    if (!path.isEmpty()) {
        viewport_->save_vpd(path);
    }
}

void MainWindow::choose_render_image() {
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Render Image"), state_.default_directory,
        QStringLiteral("Bitmap Image (*.bmp)"));
    if (!path.isEmpty()) {
        viewport_->capture_image(path);
    }
}

void MainWindow::show_effect_dialog() {
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Effect Mapping & ReForge DX11 Pipeline"));
    dialog.resize(620, 520);
    auto* layout = new QVBoxLayout(&dialog);
    auto* tabs = new QTabWidget();
    const QStringList names = {QStringLiteral("Main"), QStringLiteral("MaterialMap"),
                               QStringLiteral("DepthBuffer"), QStringLiteral("NormalMap"),
                               QStringLiteral("SSAO"), QStringLiteral("ReForge PBR")};
    for (int i = 0; i < 5; ++i) {
        auto* page = new QWidget();
        auto* page_layout = new QVBoxLayout(page);
        page_layout->addWidget(make_section(QStringLiteral("Object Effect Assignment (.hmm / .fx)")));
        auto* target = new QLabel(state_.model_name.isEmpty()
                                      ? QStringLiteral("Current target: No model loaded")
                                      : QStringLiteral("Current target: %1").arg(state_.model_name));
        page_layout->addWidget(target);
        auto* load = new QPushButton(QStringLiteral("Load Effect (.fx / .hlsl)..."));
        page_layout->addWidget(load);
        page_layout->addWidget(make_hint(QStringLiteral(
            "Effect assignment is stored in the Hikari mapping registry and applied by "
            "the MMD host bridge at draw time.")));
        page_layout->addStretch(1);
        connect(load, &QPushButton::clicked, &dialog, [this] {
            const QString path = QFileDialog::getOpenFileName(
                this, QStringLiteral("Effect"), state_.default_directory,
                QStringLiteral("DirectX Effect (*.fx *.fxsub *.hlsl)"));
            if (!path.isEmpty()) {
                state_.set_status(QStringLiteral("Assigned effect: %1").arg(path));
            }
        });
        tabs->addTab(page, names[i]);
    }

    auto* pbr_page = new QWidget();
    auto* pbr_layout = new QGridLayout(pbr_page);
    auto* exposure = make_spin(0.1, 5.0, 0.05, 2, state_.effect_state.pbr_exposure);
    auto* bloom = make_spin(0.0, 2.0, 0.05, 2, state_.effect_state.pbr_bloom_intensity);
    auto* ssao = make_spin(0.1, 2.0, 0.05, 2, state_.effect_state.pbr_ssao_radius);
    auto* ssr = make_spin(0.0, 1.0, 0.02, 2, state_.effect_state.pbr_ssr_roughness_cutoff);
    auto* fog = make_spin(0.0, 0.1, 0.001, 3, state_.effect_state.pbr_volumetric_fog_density);
    auto* tonemapper = new QComboBox();
    tonemapper->addItems({QStringLiteral("Reinhard Standard"),
                          QStringLiteral("ACES Filmic High-Contrast"),
                          QStringLiteral("AgX Neutral Modern")});
    tonemapper->setCurrentIndex(state_.effect_state.pbr_tonemapper);
    add_row(pbr_layout, 0, QStringLiteral("Exposure / ISO"), exposure);
    add_row(pbr_layout, 1, QStringLiteral("Tone mapping"), tonemapper);
    add_row(pbr_layout, 2, QStringLiteral("HDR Bloom"), bloom);
    add_row(pbr_layout, 3, QStringLiteral("SSAO Radius"), ssao);
    add_row(pbr_layout, 4, QStringLiteral("SSR Cutoff"), ssr);
    add_row(pbr_layout, 5, QStringLiteral("Fog Density"), fog);
    pbr_layout->setRowStretch(6, 1);
    tabs->addTab(pbr_page, QStringLiteral("ReForge PBR"));

    layout->addWidget(tabs);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    auto* reset = new QPushButton(QStringLiteral("Reset ReForge"));
    buttons->addButton(reset, QDialogButtonBox::ResetRole);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(reset, &QPushButton::clicked, &dialog, [this] {
        state_.effect_state = EffectDialogState{};
        state_.set_status(QStringLiteral("Reset ReForge parameters to defaults"));
    });
    connect(exposure, &QDoubleSpinBox::valueChanged, &dialog,
            [this](double value) { state_.effect_state.pbr_exposure = static_cast<float>(value); });
    connect(bloom, &QDoubleSpinBox::valueChanged, &dialog,
            [this](double value) { state_.effect_state.pbr_bloom_intensity = static_cast<float>(value); });
    connect(ssao, &QDoubleSpinBox::valueChanged, &dialog,
            [this](double value) { state_.effect_state.pbr_ssao_radius = static_cast<float>(value); });
    connect(ssr, &QDoubleSpinBox::valueChanged, &dialog,
            [this](double value) { state_.effect_state.pbr_ssr_roughness_cutoff = static_cast<float>(value); });
    connect(fog, &QDoubleSpinBox::valueChanged, &dialog,
            [this](double value) { state_.effect_state.pbr_volumetric_fog_density = static_cast<float>(value); });
    connect(tonemapper, &QComboBox::currentIndexChanged, &dialog,
            [this](int index) { state_.effect_state.pbr_tonemapper = index; });
    dialog.exec();
}

void MainWindow::show_gravity_dialog() {
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Gravity & Dynamics Configuration"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* grid = new QGridLayout();
    std::array<QDoubleSpinBox*, 3> spins{};
    const char* axis_names = "XYZ";
    for (int i = 0; i < 3; ++i) {
        spins[static_cast<std::size_t>(i)] =
            make_spin(-100.0, 100.0, 0.1, 2, state_.gravity[static_cast<std::size_t>(i)]);
        add_row(grid, i, QStringLiteral("Gravity %1").arg(QChar(axis_names[i])),
                spins[static_cast<std::size_t>(i)]);
    }
    layout->addLayout(grid);
    auto* noise = make_spin(0.0, 10.0, 0.1, 2, state_.gravity_noise);
    add_row(grid, 3, QStringLiteral("Noise"), noise);
    layout->addWidget(make_hint(QStringLiteral("Joint springs preserve PMX stiffness; bodies use "
                                               "Jolt 6DOF limits, CCD and chain projection.")));
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    auto* earth = new QPushButton(QStringLiteral("Earth (-9.8 Y)"));
    auto* zero = new QPushButton(QStringLiteral("Zero-G"));
    buttons->addButton(earth, QDialogButtonBox::ActionRole);
    buttons->addButton(zero, QDialogButtonBox::ActionRole);
    layout->addWidget(buttons);
    connect(earth, &QPushButton::clicked, &dialog, [this, spins] {
        state_.gravity = {0.0f, -9.8f, 0.0f};
        for (int i = 0; i < 3; ++i) {
            spins[static_cast<std::size_t>(i)]->setValue(
                state_.gravity[static_cast<std::size_t>(i)]);
        }
    });
    connect(zero, &QPushButton::clicked, &dialog, [this, spins] {
        state_.gravity = {0.0f, 0.0f, 0.0f};
        for (int i = 0; i < 3; ++i) {
            spins[static_cast<std::size_t>(i)]->setValue(0.0);
        }
    });
    connect(noise, &QDoubleSpinBox::valueChanged, &dialog,
            [this](double value) { state_.gravity_noise = static_cast<float>(value); });
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.exec();
}

void MainWindow::show_physics_dialog() {
    QMessageBox::information(this, QStringLiteral("About Physics"),
                             QStringLiteral("Jolt Physics 5.x\n\n"
                                            "• 60/120 Hz substepping with distance constraint chains\n"
                                            "• 6DOF spring joints matching PMX rest frames\n"
                                            "• Rest-pose overlap protection and kinematic bone driving\n"
                                            "• SIMD rigid bodies: spheres, boxes and capsules\n"
                                            "• Multithreaded solver and CCD on kinematic limbs"));
}

void MainWindow::show_shortcuts_dialog() {
    QMessageBox::information(
        this, QStringLiteral("Keyboard & Mouse Shortcuts"),
        QStringLiteral("Right mouse drag - orbit camera\n"
                       "Middle mouse drag - pan camera\n"
                       "Wheel - zoom\n"
                       "Left click - select bone\n\n"
                       "Space - play / pause\n"
                       "V - VSync\n"
                       "P - physics simulation\n"
                       "J - jiggle dynamics\n"
                       "M - multithreaded rendering\n"
                       "R - reset physics"));
}

void MainWindow::show_about_dialog() {
    QMessageBox::about(this, QStringLiteral("About Hikari Studio"),
                       QStringLiteral("HIKARI STUDIO\n\n"
                                      "Direct3D 11 MMD/MME runtime and animation studio.\n"
                                      "C++20, Qt 6, Jolt Physics, vcpkg and ccache.\n\n"
                                      "Shader Model 5.0, shader reflection, MRT, compute "
                                      "shaders and a 64 KB MME constant buffer."));
}

void MainWindow::show_screen_size_dialog() {
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Screen & Render Resolution"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* presets = new QHBoxLayout();
    for (const auto& [label, size] :
         std::array<std::pair<const char*, QSize>, 4>{{{"720p", {1280, 720}},
                                                       {"1080p", {1920, 1080}},
                                                       {"2K", {2560, 1440}},
                                                       {"4K", {3840, 2160}}}}) {
        auto* button = new QPushButton(QString::fromLatin1(label));
        presets->addWidget(button);
        connect(button, &QPushButton::clicked, &dialog, [this, size] {
            state_.screen_size_width = size.width();
            state_.screen_size_height = size.height();
            resize(size);
        });
    }
    layout->addLayout(presets);
    auto* width = new QSpinBox();
    width->setRange(320, 7680);
    width->setValue(state_.screen_size_width);
    auto* height = new QSpinBox();
    height->setRange(240, 4320);
    height->setValue(state_.screen_size_height);
    auto* grid = new QGridLayout();
    add_row(grid, 0, QStringLiteral("Width"), width);
    add_row(grid, 1, QStringLiteral("Height"), height);
    layout->addLayout(grid);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [this, &dialog, width, height] {
        state_.screen_size_width = width->value();
        state_.screen_size_height = height->value();
        resize(state_.screen_size_width, state_.screen_size_height);
        dialog.accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.exec();
}

void MainWindow::show_render_video_dialog() {
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Render Animation Frames"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* start = new QSpinBox();
    start->setRange(0, 100000);
    start->setValue(state_.video_render_start_frame);
    auto* end = new QSpinBox();
    end->setRange(0, 100000);
    end->setValue(state_.video_render_end_frame);
    auto* fps = new QComboBox();
    fps->addItems({QStringLiteral("30 FPS"), QStringLiteral("60 FPS")});
    fps->setCurrentIndex(state_.video_render_fps == 60 ? 1 : 0);
    auto* grid = new QGridLayout();
    add_row(grid, 0, QStringLiteral("Start frame"), start);
    add_row(grid, 1, QStringLiteral("End frame"), end);
    add_row(grid, 2, QStringLiteral("FPS"), fps);
    layout->addLayout(grid);
    auto* directory_row = new QHBoxLayout();
    auto* directory = new QLabel(state_.default_directory + QStringLiteral("/renders"));
    auto* choose = new QPushButton(QStringLiteral("Choose..."));
    directory_row->addWidget(directory, 1);
    directory_row->addWidget(choose);
    layout->addLayout(directory_row);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);

    QString render_directory = state_.default_directory + QStringLiteral("/renders");
    connect(choose, &QPushButton::clicked, &dialog, [this, &render_directory, directory] {
        const QString path =
            QFileDialog::getExistingDirectory(this, QStringLiteral("Render directory"),
                                              state_.default_directory);
        if (!path.isEmpty()) {
            render_directory = path;
            directory->setText(path);
        }
    });
    connect(buttons, &QDialogButtonBox::accepted, &dialog,
            [this, &dialog, &render_directory, start, end, fps] {
                state_.video_render_start_frame = start->value();
                state_.video_render_end_frame = end->value();
                state_.video_render_fps = fps->currentIndex() == 1 ? 60 : 30;
                viewport_->render_frames(render_directory, start->value(), end->value(),
                                         state_.video_render_fps);
                dialog.accept();
            });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.exec();
}

} // namespace hikari::studio
