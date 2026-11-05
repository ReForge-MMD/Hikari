#include "hikari/studio/main_window.hpp"

#include <QAction>
#include <QActionGroup>
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
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QStatusBar>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <numeric>

#include "hikari/studio/d3d_viewport.hpp"
#include "hikari/studio/timeline_widget.hpp"
#include "hikari/studio/theme.hpp"
#include "hikari/studio/viewport_chrome.hpp"

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
    caption->setObjectName(QStringLiteral("FieldLabel"));
    caption->setMinimumWidth(52);
    layout->addWidget(caption, row, 0);
    layout->addWidget(widget, row, 1);
}

QCheckBox* make_check(const QString& text, bool checked) {
    auto* box = new QCheckBox(text);
    box->setChecked(checked);
    return box;
}

} // namespace

MainWindow::MainWindow(QString model_path, QString motion_path, QWidget* parent)
    : QMainWindow(parent), model_path_(std::move(model_path)) {
    setWindowTitle(QStringLiteral("Hikari Studio"));
    resize(1440, 900);
    setStyleSheet(NordicTheme::style_sheet());

    setCentralWidget(build_viewport_area());
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
                refresh_targets();
                refresh_morph_combos();
                info_label_->setText(
                    QStringLiteral("%1 | %2 vertices | %3 bones").arg(name).arg(vertices).arg(bones));
            });
    connect(viewport_, &D3DViewport::screenSizeChanged, this, [this](int width, int height) {
        state_.screen_size_width = width;
        state_.screen_size_height = height;
        footer_->sync();
    });
    connect(timeline_, &TimelineWidget::boneSelected, this, [this](int index) {
        state_.selected_bone_idx = index;
        state_.mode = EditorMode::Model;
        sync_bone_fields();
        update_viewport_title();
        footer_->sync();
    });
    connect(header_, &ViewportHeader::frameRequested, this, [this](int frame) {
        state_.current_frame = static_cast<float>(frame);
        state_.is_playing = false;
        jump_frame_->blockSignals(true);
        jump_frame_->setValue(frame);
        jump_frame_->blockSignals(false);
        footer_->sync();
    });
    connect(header_, &ViewportHeader::resetViewRequested, this, [this] {
        viewport_->reset_camera();
        footer_->sync();
        state_.set_status(QStringLiteral("Camera view reset"));
    });
    connect(footer_, &ViewportFooter::modeChanged, this, [this] {
        update_viewport_title();
        timeline_->update();
        sync_bone_fields();
        state_.set_status(state_.mode == EditorMode::Camera
                              ? QStringLiteral("Editing the camera")
                              : QStringLiteral("Editing the model"));
    });
    connect(footer_, &ViewportFooter::modelLoadRequested, this, [this] {
        choose_model();
        if (!state_.model_loaded) {
            return;
        }
        state_.mode = EditorMode::Model;
        update_viewport_title();
        timeline_->update();
        sync_bone_fields();
        footer_->sync();
        state_.set_status(QStringLiteral("Editing the model"));
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
        header_->set_frame(static_cast<int>(state_.current_frame));
        footer_->sync();
        update_viewport_title();
        timeline_->update();
    });
    sync_timer->start();

    if (!model_path_.isEmpty()) {
        QTimer::singleShot(120, this, [this] {
            viewport_->initialize();
            viewport_->load_model(model_path_);
            refresh_targets();
            refresh_morph_combos();
        });
    }
    if (!motion_path.isEmpty()) {
        QTimer::singleShot(400, this, [this, motion_path] {
            viewport_->load_motion(motion_path);
        });
    }
}

QWidget* MainWindow::build_viewport_area() {
    auto* area = new QWidget();
    area->setObjectName(QStringLiteral("ViewportArea"));
    auto* layout = new QVBoxLayout(area);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    header_ = new ViewportHeader(state_, area);
    viewport_ = new D3DViewport(state_, area);
    footer_ = new ViewportFooter(state_, area);

    layout->addWidget(header_);
    layout->addWidget(viewport_, 1);
    layout->addWidget(footer_);
    return area;
}

void MainWindow::build_menus() {
    auto* file_menu = menuBar()->addMenu(QStringLiteral("&File"));
    file_menu->addAction(QStringLiteral("New Project"), this, [this] {
        viewport_->clear_model();
        state_.set_status(QStringLiteral("Created new project"));
        refresh_targets();
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
        const QString directory =
            QFileDialog::getExistingDirectory(this, QStringLiteral("Working directory"));
        if (!directory.isEmpty()) {
            state_.default_directory = directory;
            state_.set_status(QStringLiteral("Default directory set: %1").arg(directory));
        }
    });
    file_menu->addSeparator();
    file_menu->addAction(QStringLiteral("Exit"), this, [] { QApplication::quit(); });

    auto* edit_menu = menuBar()->addMenu(QStringLiteral("&Edit"));
    edit_menu->addAction(QStringLiteral("Reset Bone Transform (Z)"), this, [this] {
        viewport_->reset_selection_transform();
        state_.bone_pos_override = {0.0f, 0.0f, 0.0f};
        state_.bone_rot_euler_deg = {0.0f, 0.0f, 0.0f};
        state_.bone_transform_dirty = true;
        sync_bone_fields();
        footer_->sync();
        state_.set_status(QStringLiteral("Bone angles and translations initialized"));
    });
    edit_menu->addAction(QStringLiteral("Delete Model"), this, [this] {
        state_.model_delete_requested = true;
    });
    edit_menu->addAction(QStringLiteral("Select All Bone Frames (N)"), this, [this] {
        for (const auto& name : state_.model_bone_names) {
            state_.selected_keyframes.insert(
                {name, static_cast<unsigned int>(state_.current_frame)});
        }
        state_.set_status(QStringLiteral("Selected all bone keyframes at the current frame"));
        timeline_->update();
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
    auto* bones_action = view_menu->addAction(QStringLiteral("Skeleton Overlay"));
    bones_action->setCheckable(true);
    bones_action->setChecked(state_.show_bones);
    connect(bones_action, &QAction::toggled, this,
            [this](bool checked) { state_.show_bones = checked; });
    auto* physics_bones_action = view_menu->addAction(QStringLiteral("Physics Bone Overlay"));
    physics_bones_action->setCheckable(true);
    physics_bones_action->setChecked(state_.show_physics_bones);
    connect(physics_bones_action, &QAction::toggled, this,
            [this](bool checked) { state_.show_physics_bones = checked; });
    view_menu->addSeparator();
    view_menu->addAction(QStringLiteral("Reset View"), this, [this] {
        viewport_->reset_camera();
        footer_->sync();
    });
    view_menu->addAction(QStringLiteral("Screen Size..."), this,
                         [this] { show_screen_size_dialog(); });
    view_menu->addAction(QStringLiteral("Fullscreen"), this, [this] {
        if (isFullScreen()) {
            showNormal();
        } else {
            showFullScreen();
        }
    });

    auto* background_menu = menuBar()->addMenu(QStringLiteral("&Background"));
    auto* white_action = background_menu->addAction(QStringLiteral("MMD White Canvas"));
    white_action->setCheckable(true);
    white_action->setChecked(state_.viewport_white_bg);
    connect(white_action, &QAction::toggled, this,
            [this](bool checked) { state_.viewport_white_bg = checked; });
    auto* wireframe_action = background_menu->addAction(QStringLiteral("Wireframe (R)"));
    wireframe_action->setCheckable(true);
    wireframe_action->setChecked(state_.wireframe);
    connect(wireframe_action, &QAction::toggled, this,
            [this](bool checked) { state_.wireframe = checked; });
    auto* aa_action = background_menu->addAction(QStringLiteral("Anti-aliasing"));
    aa_action->setCheckable(true);
    aa_action->setChecked(state_.antialiasing);
    connect(aa_action, &QAction::toggled, this,
            [this](bool checked) { state_.antialiasing = checked; });
    background_menu->addSeparator();
    background_menu->addAction(QStringLiteral("Screen Size..."), this,
                               [this] { show_screen_size_dialog(); });

    auto* facial_menu = menuBar()->addMenu(QStringLiteral("&Facial"));
    facial_menu->addAction(QStringLiteral("Reset All Facial Values"), this, [this] {
        state_.morph_weights.clear();
        state_.morph_slider_values = {0.0f, 0.0f, 0.0f, 0.0f};
        sync_morph_fields();
        state_.set_status(QStringLiteral("Reset all facial morphs"));
    });
    facial_menu->addAction(QStringLiteral("Register All Facial Frames"), this, [this] {
        for (const auto& names : state_.category_morph_names) {
            for (const auto& name : names) {
                state_.register_keyframe(name,
                                         static_cast<unsigned int>(state_.current_frame));
            }
        }
        timeline_->update();
    });

    auto* physics_menu = menuBar()->addMenu(QStringLiteral("&Physical Operation"));
    auto* physics_group = new QActionGroup(this);
    physics_group->setExclusive(true);
    for (const auto& [label, mode] :
         std::array<std::pair<const char*, PhysicsMode>, 4>{
             {{"Always", PhysicsMode::Always},
              {"Anytime", PhysicsMode::Anytime},
              {"Trace", PhysicsMode::Trace},
              {"Disabled", PhysicsMode::Disabled}}}) {
        auto* action = physics_menu->addAction(QString::fromLatin1(label));
        action->setCheckable(true);
        action->setChecked(state_.physics_mode == mode);
        physics_group->addAction(action);
        connect(action, &QAction::triggered, this,
                [this, mode] { state_.physics_mode = mode; });
    }
    auto* playtime_action = physics_menu->addAction(QStringLiteral("Simulate Only While Playing"));
    playtime_action->setCheckable(true);
    playtime_action->setChecked(state_.playtime_only);
    connect(playtime_action, &QAction::toggled, this,
            [this](bool checked) { state_.playtime_only = checked; });
    auto* speed_menu = physics_menu->addMenu(QStringLiteral("Simulation Speed"));
    auto* speed_group = new QActionGroup(this);
    speed_group->setExclusive(true);
    physics_speed_group_ = speed_group;
    constexpr std::array<float, 6> speed_values{1.0f, 1.5f, 2.0f, 2.5f, 3.0f, 3.5f};
    for (std::size_t i = 0; i < speed_values.size(); ++i) {
        auto* action =
            speed_menu->addAction(QStringLiteral("%1x").arg(speed_values[i], 0, 'f', 1));
        action->setCheckable(true);
        action->setChecked(std::abs(state_.physics_speed - speed_values[i]) < 0.01f);
        speed_group->addAction(action);
        connect(action, &QAction::triggered, this, [this, i] {
            constexpr std::array<float, 6> values{1.0f, 1.5f, 2.0f, 2.5f, 3.0f, 3.5f};
            state_.physics_speed = values[i];
            if (physics_speed_ != nullptr) {
                physics_speed_->blockSignals(true);
                physics_speed_->setCurrentIndex(static_cast<int>(i));
                physics_speed_->blockSignals(false);
            }
        });
    }
    physics_menu->addSeparator();
    physics_menu->addAction(QStringLiteral("Gravity Settings..."), this,
                            [this] { show_gravity_dialog(); });
    physics_menu->addAction(QStringLiteral("Reset Physics"), this, [this] {
        state_.reset_physics_requested = true;
        state_.set_status(QStringLiteral("Physics reset requested"));
    });
    auto* rigid_action = physics_menu->addAction(QStringLiteral("Show Rigid Bodies"));
    rigid_action->setCheckable(true);
    rigid_action->setChecked(state_.show_rigid_bodies);
    connect(rigid_action, &QAction::toggled, this,
            [this](bool checked) { state_.show_rigid_bodies = checked; });
    auto* joint_action = physics_menu->addAction(QStringLiteral("Show Joints"));
    joint_action->setCheckable(true);
    joint_action->setChecked(state_.show_joints);
    connect(joint_action, &QAction::toggled, this,
            [this](bool checked) { state_.show_joints = checked; });
    physics_menu->addAction(QStringLiteral("About Jolt Physics"), this,
                            [this] { show_physics_dialog(); });

    menuBar()->addAction(QStringLiteral("MMEffect"), this, [this] { show_effect_dialog(); });

    auto* help_menu = menuBar()->addMenu(QStringLiteral("&Help"));
    help_menu->addAction(QStringLiteral("Keyboard Shortcuts"), this,
                         [this] { show_shortcuts_dialog(); });
    help_menu->addAction(QStringLiteral("About Hikari Studio"), this,
                         [this] { show_about_dialog(); });
}

void MainWindow::build_docks() {
    setCorner(Qt::BottomLeftCorner, Qt::BottomDockWidgetArea);
    setCorner(Qt::BottomRightCorner, Qt::BottomDockWidgetArea);

    auto* timeline_dock = new QDockWidget(QStringLiteral("Timeline"), this);
    timeline_dock->setObjectName(QStringLiteral("TimelineDock"));
    timeline_dock->setWidget(build_timeline_panel());
    addDockWidget(Qt::LeftDockWidgetArea, timeline_dock);

    auto* panels_dock = new QDockWidget(QStringLiteral("Manipulation"), this);
    panels_dock->setObjectName(QStringLiteral("EditorDock"));
    panel_strip_ = build_panel_strip();
    panels_dock->setWidget(panel_strip_);
    addDockWidget(Qt::BottomDockWidgetArea, panels_dock);
    resizeDocks({timeline_dock}, {300}, Qt::Horizontal);
    resizeDocks({panels_dock}, {306}, Qt::Vertical);
}

QWidget* MainWindow::build_timeline_panel() {
    auto* panel = new QWidget();
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    layout->addWidget(make_section(QStringLiteral("Bone / Frame manipulation")));
    auto* select_row = new QHBoxLayout();
    auto* select_all = new QPushButton(QStringLiteral("Select All"));
    auto* delete_line = new QPushButton(QStringLiteral("Delete Line"));
    select_row->addWidget(select_all);
    select_row->addWidget(delete_line);
    layout->addLayout(select_row);

    layout->addWidget(make_section(QStringLiteral("Frame manipulation")));
    auto* frame_row = new QHBoxLayout();
    auto* first_button = new QPushButton(QStringLiteral("|<"));
    auto* prev_button = new QPushButton(QStringLiteral("<"));
    jump_frame_ = new QSpinBox();
    jump_frame_->setRange(0, 100000);
    jump_frame_->setAlignment(Qt::AlignCenter);
    auto* next_button = new QPushButton(QStringLiteral(">"));
    auto* last_button = new QPushButton(QStringLiteral(">|"));
    auto* add_button = new QPushButton(QStringLiteral("+"));
    auto* remove_button = new QPushButton(QStringLiteral("-"));
    frame_row->addWidget(first_button);
    frame_row->addWidget(prev_button);
    frame_row->addWidget(jump_frame_, 1);
    frame_row->addWidget(next_button);
    frame_row->addWidget(last_button);
    frame_row->addWidget(add_button);
    frame_row->addWidget(remove_button);
    layout->addLayout(frame_row);

    timeline_ = new TimelineWidget(state_);
    auto* scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    scroll->setWidget(timeline_);
    layout->addWidget(scroll, 1);

    auto* actions_row = new QHBoxLayout();
    auto* copy_button = new QPushButton(QStringLiteral("Copy"));
    auto* paste_button = new QPushButton(QStringLiteral("Paste"));
    auto* vsel_button = new QPushButton(QStringLiteral("V-Sel"));
    auto* delete_button = new QPushButton(QStringLiteral("Delete"));
    actions_row->addWidget(copy_button);
    actions_row->addWidget(paste_button);
    actions_row->addWidget(vsel_button);
    actions_row->addWidget(delete_button);
    layout->addLayout(actions_row);

    auto* zoom_row = new QHBoxLayout();
    auto* register_button = new QPushButton(QStringLiteral("Register"));
    zoom_row->addWidget(register_button);
    zoom_row->addWidget(new QLabel(QStringLiteral("Zoom")));
    auto* zoom = new QSlider(Qt::Horizontal);
    zoom->setRange(50, 300);
    zoom->setValue(100);
    zoom_row->addWidget(zoom, 1);
    layout->addLayout(zoom_row);

    connect(first_button, &QPushButton::clicked, this, [this] {
        state_.current_frame = 0.0f;
        jump_frame_->setValue(0);
        footer_->sync();
    });
    connect(prev_button, &QPushButton::clicked, this, [this] {
        state_.step_frame(-1);
        jump_frame_->setValue(static_cast<int>(state_.current_frame));
        footer_->sync();
    });
    connect(next_button, &QPushButton::clicked, this, [this] {
        state_.step_frame(1);
        jump_frame_->setValue(static_cast<int>(state_.current_frame));
        footer_->sync();
    });
    connect(last_button, &QPushButton::clicked, this, [this] {
        state_.current_frame = static_cast<float>(state_.play_range.second);
        jump_frame_->setValue(state_.play_range.second);
        footer_->sync();
    });
    connect(jump_frame_, &QSpinBox::valueChanged, this,
            [this](int value) { state_.current_frame = static_cast<float>(value); });
    connect(add_button, &QPushButton::clicked, this, [this] {
        state_.register_keyframe(state_.selected_track_name(),
                                 static_cast<unsigned int>(state_.current_frame));
        timeline_->update();
    });
    connect(remove_button, &QPushButton::clicked, this, [this] {
        state_.delete_keyframe(state_.selected_track_name(),
                               static_cast<unsigned int>(state_.current_frame));
        timeline_->update();
    });
    connect(select_all, &QPushButton::clicked, this, [this] {
        for (const auto& name : state_.model_bone_names) {
            state_.selected_keyframes.insert(
                {name, static_cast<unsigned int>(state_.current_frame)});
        }
        timeline_->update();
    });
    connect(delete_line, &QPushButton::clicked, this, [this] {
        const unsigned int frame = static_cast<unsigned int>(state_.current_frame);
        for (auto& [track, frames] : state_.track_keyframes) {
            frames.erase(frame);
        }
        timeline_->update();
    });
    connect(vsel_button, &QPushButton::clicked, this, [this] {
        const QString track = state_.selected_track_name();
        const auto it = state_.track_keyframes.find(track);
        if (it != state_.track_keyframes.end()) {
            for (unsigned int frame : it->second) {
                state_.selected_keyframes.insert({track, frame});
            }
        }
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
    connect(zoom, &QSlider::valueChanged, this, [this](int value) {
        state_.timeline_zoom = static_cast<float>(value) / 100.0f;
        timeline_->update();
    });
    copy_button->setEnabled(false);
    paste_button->setEnabled(false);
    return panel;
}

QWidget* MainWindow::build_panel_strip() {
    auto* scroll = new QScrollArea();
    scroll->setObjectName(QStringLiteral("PanelStrip"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    auto* content = new QWidget();
    auto* row = new QHBoxLayout(content);
    row->setContentsMargins(4, 4, 4, 4);
    row->setSpacing(4);
    row->setSizeConstraint(QLayout::SetMinAndMaxSize);
    row->addWidget(make_panel(QStringLiteral("Interpolation curve"),
                              build_interpolation_tab(), 176));
    row->addWidget(make_panel(QStringLiteral("Model manipulation"), build_model_tab(), 168));
    row->addWidget(make_panel(QStringLiteral("Bone manipulation"), build_bone_tab(), 232));
    row->addWidget(make_panel(QStringLiteral("Facial manipulation"), build_facial_tab(), 214));
    row->addWidget(make_panel(QStringLiteral("Camera"), build_camera_tab(), 196));
    row->addWidget(make_panel(QStringLiteral("Light manipulation"), build_light_tab(), 200));
    row->addWidget(make_panel(QStringLiteral("Self-shadow manipulation"),
                              build_shadow_tab(), 186));
    row->addWidget(make_panel(QStringLiteral("Accessory manipulation"),
                              build_accessory_tab(), 196));
    row->addWidget(make_panel(QStringLiteral("View"), build_view_tab(), 210));
    row->addWidget(make_panel(QStringLiteral("Playback"), build_playback_tab(), 196));
    row->addStretch(1);
    scroll->setWidget(content);
    return scroll;
}

QWidget* MainWindow::make_panel(const QString& title, QWidget* body, int width) {
    auto* panel = new QWidget();
    panel->setObjectName(QStringLiteral("MmdPanel"));
    panel->setFixedWidth(width);
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto* caption = new QLabel(title);
    caption->setObjectName(QStringLiteral("PanelTitle"));
    caption->setFixedHeight(20);
    layout->addWidget(caption);
    body->setParent(panel);
    layout->addWidget(body, 1);
    return panel;
}

QWidget* MainWindow::build_model_tab() {
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(4);
    auto* row = new QHBoxLayout();
    auto* load = new QPushButton(QStringLiteral("Load PMX..."));
    auto* remove = new QPushButton(QStringLiteral("Remove"));
    row->addWidget(load);
    row->addWidget(remove);
    layout->addLayout(row);

    auto* visible = make_check(QStringLiteral("Display"), state_.model_visible);
    auto* shadow = make_check(QStringLiteral("Shadow"), state_.model_shadow_visible);
    layout->addWidget(visible);
    layout->addWidget(shadow);
    auto* register_button = new QPushButton(QStringLiteral("Register Disp/IK"));
    layout->addWidget(register_button);
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
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(4);

    auto* mode_row = new QHBoxLayout();
    mode_row->addWidget(new QLabel(QStringLiteral("Mode")));
    auto* mode = new QComboBox();
    mode->addItems({QStringLiteral("Select"), QStringLiteral("Rotate"), QStringLiteral("Move")});
    mode->setCurrentIndex(static_cast<int>(state_.bone_mode));
    mode_row->addWidget(mode, 1);
    layout->addLayout(mode_row);

    auto* visibility_row = new QHBoxLayout();
    auto* show_bones = make_check(QStringLiteral("Bones"), state_.show_bones);
    auto* show_physics = make_check(QStringLiteral("Physics"), state_.show_physics_bones);
    visibility_row->addWidget(show_bones);
    visibility_row->addWidget(show_physics);
    layout->addLayout(visibility_row);

    auto* grid = new QGridLayout();
    grid->setHorizontalSpacing(4);
    grid->setVerticalSpacing(2);
    const char* axis_names = "XYZ";
    for (int i = 0; i < 3; ++i) {
        bone_pos_[static_cast<std::size_t>(i)] = make_spin(-1000.0, 1000.0, 0.05, 2, 0.0);
        bone_rot_[static_cast<std::size_t>(i)] = make_spin(-720.0, 720.0, 0.5, 1, 0.0);
        add_row(grid, i, QStringLiteral("Move %1").arg(QChar(axis_names[i])),
                bone_pos_[static_cast<std::size_t>(i)]);
        add_row(grid, i + 3, QStringLiteral("Angle %1").arg(QChar(axis_names[i])),
                bone_rot_[static_cast<std::size_t>(i)]);
    }
    layout->addLayout(grid);

    auto* buttons = new QHBoxLayout();
    auto* reset = new QPushButton(QStringLiteral("Reset"));
    auto* register_button = new QPushButton(QStringLiteral("Register"));
    buttons->addWidget(reset);
    buttons->addWidget(register_button);
    layout->addLayout(buttons);
    layout->addStretch(1);

    connect(mode, &QComboBox::currentIndexChanged, this, [this](int index) {
        state_.bone_mode = index == 1 ? BoneTransformMode::Rotate
                           : index == 2 ? BoneTransformMode::Move
                                        : BoneTransformMode::Select;
        if (state_.model_loaded) {
            state_.mode = EditorMode::Model;
        }
        update_viewport_title();
    });
    connect(show_bones, &QCheckBox::toggled, this,
            [this](bool checked) { state_.show_bones = checked; });
    connect(show_physics, &QCheckBox::toggled, this,
            [this](bool checked) { state_.show_physics_bones = checked; });
    for (int i = 0; i < 3; ++i) {
        connect(bone_pos_[static_cast<std::size_t>(i)], &QDoubleSpinBox::valueChanged, this,
                [this, i](double value) {
                    state_.bone_pos_override[static_cast<std::size_t>(i)] =
                        static_cast<float>(value);
                    state_.bone_transform_dirty = true;
                    if (state_.model_loaded) {
                        state_.mode = EditorMode::Model;
                    }
                });
        connect(bone_rot_[static_cast<std::size_t>(i)], &QDoubleSpinBox::valueChanged, this,
                [this, i](double value) {
                    state_.bone_rot_euler_deg[static_cast<std::size_t>(i)] =
                        static_cast<float>(value);
                    state_.bone_transform_dirty = true;
                    if (state_.model_loaded) {
                        state_.mode = EditorMode::Model;
                    }
                });
    }
    connect(reset, &QPushButton::clicked, this, [this] {
        viewport_->reset_selection_transform();
        state_.bone_pos_override = {0.0f, 0.0f, 0.0f};
        state_.bone_rot_euler_deg = {0.0f, 0.0f, 0.0f};
        state_.bone_transform_dirty = true;
        sync_bone_fields();
        footer_->sync();
    });
    connect(register_button, &QPushButton::clicked, this, [this] {
        state_.register_keyframe(state_.selected_track_name(),
                                 static_cast<unsigned int>(state_.current_frame));
        timeline_->update();
    });
    return page;
}

QWidget* MainWindow::build_facial_tab() {
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(2);
    const QStringList titles = {QStringLiteral("Eyes"), QStringLiteral("Mouth"),
                                QStringLiteral("Brow"), QStringLiteral("Other")};
    for (int i = 0; i < 4; ++i) {
        auto* caption_row = new QHBoxLayout();
        auto* caption = new QLabel(titles[i]);
        caption->setObjectName(QStringLiteral("FieldLabel"));
        caption->setFixedWidth(42);
        caption_row->addWidget(caption);
        morph_combo_[static_cast<std::size_t>(i)] = new QComboBox();
        morph_combo_[static_cast<std::size_t>(i)]->setSizeAdjustPolicy(
            QComboBox::AdjustToMinimumContentsLengthWithIcon);
        morph_combo_[static_cast<std::size_t>(i)]->setMinimumContentsLength(8);
        caption_row->addWidget(morph_combo_[static_cast<std::size_t>(i)], 1);
        layout->addLayout(caption_row);

        auto* slider_row = new QHBoxLayout();
        morph_slider_[static_cast<std::size_t>(i)] = new QSlider(Qt::Horizontal);
        morph_slider_[static_cast<std::size_t>(i)]->setRange(0, 100);
        morph_value_[static_cast<std::size_t>(i)] = new QLabel(QStringLiteral("0.000"));
        morph_value_[static_cast<std::size_t>(i)]->setFixedWidth(38);
        morph_value_[static_cast<std::size_t>(i)]->setAlignment(Qt::AlignRight |
                                                                Qt::AlignVCenter);
        slider_row->addWidget(morph_slider_[static_cast<std::size_t>(i)], 1);
        slider_row->addWidget(morph_value_[static_cast<std::size_t>(i)]);
        layout->addLayout(slider_row);

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
    layout->addStretch(1);
    return page;
}

QWidget* MainWindow::build_camera_tab() {
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(4);
    auto* distance = make_spin(1.0, 500.0, 0.2, 2, state_.camera_distance);
    auto* fov = make_spin(10.0, 120.0, 0.5, 1, state_.camera_fov_deg);
    auto* yaw = make_spin(-180.0, 180.0, 1.0, 1,
                          state_.camera_yaw * 180.0 / 3.14159265358979323846);
    auto* pitch = make_spin(-88.0, 88.0, 1.0, 1,
                            state_.camera_pitch * 180.0 / 3.14159265358979323846);
    auto* roll = make_spin(-180.0, 180.0, 1.0, 1,
                           state_.camera_roll * 180.0 / 3.14159265358979323846);
    auto* grid = new QGridLayout();
    grid->setHorizontalSpacing(4);
    grid->setVerticalSpacing(2);
    add_row(grid, 0, QStringLiteral("Distance"), distance);
    add_row(grid, 1, QStringLiteral("FOV"), fov);
    add_row(grid, 2, QStringLiteral("Yaw"), yaw);
    add_row(grid, 3, QStringLiteral("Pitch"), pitch);
    add_row(grid, 4, QStringLiteral("Roll"), roll);
    layout->addLayout(grid);

    auto* row = new QHBoxLayout();
    auto* reset = new QPushButton(QStringLiteral("Reset Camera"));
    auto* ortho = make_check(QStringLiteral("Ortho"), state_.camera_orthographic);
    row->addWidget(reset);
    row->addWidget(ortho);
    layout->addLayout(row);
    auto* register_button = new QPushButton(QStringLiteral("Register Camera Keyframe"));
    layout->addWidget(register_button);
    layout->addStretch(1);

    connect(distance, &QDoubleSpinBox::valueChanged, this, [this, distance](double value) {
        state_.camera_distance = static_cast<float>(value);
        if (distance->hasFocus()) {
            state_.mode = EditorMode::Camera;
        }
    });
    connect(fov, &QDoubleSpinBox::valueChanged, this, [this, fov](double value) {
        state_.camera_fov_deg = static_cast<float>(value);
        if (fov->hasFocus()) {
            state_.mode = EditorMode::Camera;
        }
    });
    connect(yaw, &QDoubleSpinBox::valueChanged, this, [this, yaw](double value) {
        state_.camera_yaw = static_cast<float>(value) * 3.14159265358979323846 / 180.0;
        if (yaw->hasFocus()) {
            state_.mode = EditorMode::Camera;
        }
    });
    connect(pitch, &QDoubleSpinBox::valueChanged, this, [this, pitch](double value) {
        state_.camera_pitch = static_cast<float>(value) * 3.14159265358979323846 / 180.0;
        if (pitch->hasFocus()) {
            state_.mode = EditorMode::Camera;
        }
    });
    connect(roll, &QDoubleSpinBox::valueChanged, this, [this, roll](double value) {
        state_.camera_roll = static_cast<float>(value) * 3.14159265358979323846 / 180.0;
        if (roll->hasFocus()) {
            state_.mode = EditorMode::Camera;
        }
    });
    connect(reset, &QPushButton::clicked, this, [this, distance, fov, yaw, pitch, roll] {
        viewport_->reset_camera();
        state_.mode = EditorMode::Camera;
        distance->setValue(50.0);
        fov->setValue(30.0);
        yaw->setValue(0.0);
        pitch->setValue(8.6);
        roll->setValue(0.0);
        update_viewport_title();
        footer_->sync();
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
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(3);
    auto* grid = new QGridLayout();
    grid->setHorizontalSpacing(4);
    grid->setVerticalSpacing(2);
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

    auto* direction = new QGridLayout();
    direction->setHorizontalSpacing(4);
    direction->setVerticalSpacing(2);
    std::array<QDoubleSpinBox*, 3> dir_spins{};
    const char* axis_names = "XYZ";
    for (int i = 0; i < 3; ++i) {
        dir_spins[static_cast<std::size_t>(i)] =
            make_spin(-1.0, 1.0, 0.02, 2, state_.light_direction[static_cast<std::size_t>(i)]);
        add_row(direction, i, QStringLiteral("Dir %1").arg(QChar(axis_names[i])),
                dir_spins[static_cast<std::size_t>(i)]);
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
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(4);
    auto* mode = new QComboBox();
    mode->addItems({QStringLiteral("Off"), QStringLiteral("Mode 1"), QStringLiteral("Mode 2")});
    mode->setCurrentIndex(static_cast<int>(state_.shadow_mode));
    auto* range = make_spin(100.0, 15000.0, 10.0, 1, state_.shadow_range);
    auto* detail = make_spin(0.0, 1.0, 0.01, 2, state_.shadow_detail);
    auto* grid = new QGridLayout();
    grid->setHorizontalSpacing(4);
    grid->setVerticalSpacing(2);
    add_row(grid, 0, QStringLiteral("Mode"), mode);
    add_row(grid, 1, QStringLiteral("Range"), range);
    add_row(grid, 2, QStringLiteral("Detail"), detail);
    layout->addLayout(grid);
    auto* register_button = new QPushButton(QStringLiteral("Register Shadow Keyframe"));
    layout->addWidget(register_button);
    layout->addStretch(1);

    connect(mode, &QComboBox::currentIndexChanged, this,
            [this](int index) { state_.shadow_mode = static_cast<ShadowMode>(index); });
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
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(4);
    auto* load = new QPushButton(QStringLiteral("Load .x..."));
    auto* clear = new QPushButton(QStringLiteral("Remove"));
    auto* row = new QHBoxLayout();
    row->addWidget(load);
    row->addWidget(clear);
    layout->addLayout(row);
    auto* scale = make_spin(0.01, 100.0, 0.01, 2, state_.accessory_scale);
    auto* alpha = make_spin(0.0, 1.0, 0.01, 2, state_.accessory_alpha);
    auto* grid = new QGridLayout();
    grid->setHorizontalSpacing(4);
    grid->setVerticalSpacing(2);
    add_row(grid, 0, QStringLiteral("Scale"), scale);
    add_row(grid, 1, QStringLiteral("Alpha"), alpha);
    layout->addLayout(grid);
    auto* register_button = new QPushButton(QStringLiteral("Register Accessory Keyframe"));
    layout->addWidget(register_button);
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
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(3);
    auto* grid = new QGridLayout();
    grid->setHorizontalSpacing(6);
    grid->setVerticalSpacing(2);
    auto* ground = make_check(QStringLiteral("Ground grid"), state_.show_grid);
    auto* axes = make_check(QStringLiteral("Axes"), state_.show_axes);
    auto* white = make_check(QStringLiteral("White canvas"), state_.viewport_white_bg);
    auto* wireframe = make_check(QStringLiteral("Wireframe"), state_.wireframe);
    auto* info = make_check(QStringLiteral("Info"), state_.info_display);
    auto* aa = make_check(QStringLiteral("Anti-alias"), state_.antialiasing);
    grid->addWidget(ground, 0, 0);
    grid->addWidget(axes, 0, 1);
    grid->addWidget(white, 1, 0);
    grid->addWidget(wireframe, 1, 1);
    grid->addWidget(info, 2, 0);
    grid->addWidget(aa, 2, 1);
    layout->addLayout(grid);

    auto* fps = new QComboBox();
    fps->addItems({QStringLiteral("30 FPS"), QStringLiteral("60 FPS"),
                   QStringLiteral("Unlimited")});
    fps->setCurrentIndex(static_cast<int>(state_.fps_limit));
    auto* fps_row = new QHBoxLayout();
    fps_row->addWidget(new QLabel(QStringLiteral("FPS")));
    fps_row->addWidget(fps, 1);
    layout->addLayout(fps_row);
    auto* screen_size = new QPushButton(QStringLiteral("Screen Size..."));
    layout->addWidget(screen_size);
    layout->addStretch(1);

    connect(ground, &QCheckBox::toggled, this,
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
    connect(screen_size, &QPushButton::clicked, this,
            [this] { show_screen_size_dialog(); });
    return page;
}

QWidget* MainWindow::build_playback_tab() {
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(3);
    auto* play = new QPushButton(QStringLiteral("Play / Pause"));
    layout->addWidget(play);
    auto* speed_row = new QHBoxLayout();
    speed_row->addWidget(new QLabel(QStringLiteral("Speed")));
    auto* speed = new QComboBox();
    speed->addItems({QStringLiteral("1.0x"), QStringLiteral("1.5x"), QStringLiteral("2.0x"),
                     QStringLiteral("2.5x"), QStringLiteral("3.0x"), QStringLiteral("3.5x")});
    const std::array<float, 6> speed_values{1.0f, 1.5f, 2.0f, 2.5f, 3.0f, 3.5f};
    int speed_index = 4;
    for (int i = 0; i < 6; ++i) {
        if (std::abs(state_.physics_speed - speed_values[static_cast<std::size_t>(i)]) < 0.01f) {
            speed_index = i;
        }
    }
    speed->setCurrentIndex(speed_index);
    speed->setToolTip(QStringLiteral("Simulation rate: faster response without raising gravity"));
    speed_row->addWidget(speed, 1);
    physics_speed_ = speed;
    layout->addLayout(speed_row);
    auto* loop = make_check(QStringLiteral("Loop"), state_.loop_playback);
    auto* multithreaded = make_check(QStringLiteral("Deferred rendering"), state_.multithreaded);
    auto* jiggle = make_check(QStringLiteral("Jiggle dynamics"), state_.jiggle_enabled);
    auto* vsync = make_check(QStringLiteral("VSync"), state_.vsync);
    auto* physics = make_check(QStringLiteral("Physics"), state_.physics_mode != PhysicsMode::Disabled);
    auto* playtime = make_check(QStringLiteral("Only while playing"), state_.playtime_only);
    layout->addWidget(loop);
    layout->addWidget(multithreaded);
    layout->addWidget(jiggle);
    layout->addWidget(vsync);
    layout->addWidget(physics);
    layout->addWidget(playtime);
    layout->addStretch(1);

    connect(play, &QPushButton::clicked, this, [this] { state_.toggle_play(); });
    connect(speed, &QComboBox::currentIndexChanged, this, [this](int index) {
        constexpr std::array<float, 6> values{1.0f, 1.5f, 2.0f, 2.5f, 3.0f, 3.5f};
        state_.physics_speed = values[static_cast<std::size_t>(std::clamp(index, 0, 5))];
        if (physics_speed_group_ != nullptr) {
            const auto actions = physics_speed_group_->actions();
            if (index >= 0 && index < actions.size()) {
                actions[index]->setChecked(true);
            }
        }
    });
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
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(4);
    auto* target = new QComboBox();
    target->addItems({QStringLiteral("Rotation"), QStringLiteral("Move X"),
                      QStringLiteral("Move Y"), QStringLiteral("Move Z")});
    target->setCurrentIndex(static_cast<int>(state_.curve_target));
    auto* automatic = make_check(QStringLiteral("Automatic curve"), state_.curve_auto);
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

void MainWindow::refresh_targets() {
    if (!state_.model_loaded && state_.mode == EditorMode::Model) {
        state_.mode = EditorMode::Camera;
    }
    if (timeline_ != nullptr) {
        timeline_->refresh_layout();
        timeline_->update();
    }
    update_viewport_title();
    if (footer_ != nullptr) {
        footer_->sync();
    }
}

void MainWindow::update_viewport_title() {
    if (header_ == nullptr) {
        return;
    }
    if (state_.mode == EditorMode::Camera) {
        header_->set_title(QStringLiteral("camera light accessory"));
    } else {
        header_->set_title(state_.model_name.isEmpty() ? QStringLiteral("model")
                                                       : state_.model_name);
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
        const bool available = combo->count() > 0;
        combo->setEnabled(available);
        if (morph_slider_[static_cast<std::size_t>(i)] != nullptr) {
            morph_slider_[static_cast<std::size_t>(i)]->setEnabled(available);
        }
        if (morph_value_[static_cast<std::size_t>(i)] != nullptr) {
            morph_value_[static_cast<std::size_t>(i)]->setEnabled(available);
        }
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
        refresh_targets();
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
                                            "• 60 Hz substepping with distance constraint chains\n"
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
                       "Left click - select bone\n"
                       "Corner gizmo - row 1 rotates, row 2 moves\n"
                       "Corner gizmo title - local / global axes\n\n"
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
    dialog.setWindowTitle(QStringLiteral("Screen Size - Viewport Render Target"));
    dialog.setMinimumWidth(420);
    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(make_section(QStringLiteral("Viewport render size")));
    layout->addWidget(make_hint(QStringLiteral(
        "Sets the offscreen resolution and aspect ratio used to render the 3D viewport. "
        "The application window is not resized; the image is letterboxed to fit.")));

    auto* presets = new QGridLayout();
    presets->setHorizontalSpacing(4);
    presets->setVerticalSpacing(4);
    struct Preset {
        const char* label;
        int width;
        int height;
    };
    const std::array<Preset, 6> preset_list{
        {{"4:3 1024x768", 1024, 768},
         {"720p 1280x720", 1280, 720},
         {"1080p 1920x1080", 1920, 1080},
         {"2K 2560x1440", 2560, 1440},
         {"1:1 1080x1080", 1080, 1080},
         {"9:16 1080x1920", 1080, 1920}}};
    auto* width = new QSpinBox();
    width->setRange(0, 7680);
    width->setSpecialValueText(QStringLiteral("Window"));
    width->setValue(state_.screen_size_width);
    auto* height = new QSpinBox();
    height->setRange(0, 4320);
    height->setSpecialValueText(QStringLiteral("Window"));
    height->setValue(state_.screen_size_height);
    auto* match_window = make_check(
        QStringLiteral("Match the window size"),
        state_.screen_size_width <= 0 || state_.screen_size_height <= 0);
    auto* info = new QLabel();
    info->setObjectName(QStringLiteral("HintLabel"));

    const auto update_info = [width, height, match_window, info] {
        if (match_window->isChecked()) {
            info->setText(QStringLiteral("The viewport follows the window size and aspect."));
            width->setEnabled(false);
            height->setEnabled(false);
            return;
        }
        width->setEnabled(true);
        height->setEnabled(true);
        const int w = std::max(width->value(), 1);
        const int h = std::max(height->value(), 1);
        const int divisor = std::gcd(w, h);
        info->setText(QStringLiteral("Render target %1 x %2  (%3:%4)")
                          .arg(w)
                          .arg(h)
                          .arg(w / divisor)
                          .arg(h / divisor));
    };

    int row = 0;
    for (const auto& preset : preset_list) {
        auto* button = new QPushButton(QString::fromLatin1(preset.label));
        presets->addWidget(button, row / 2, row % 2);
        ++row;
        connect(button, &QPushButton::clicked, &dialog,
                [width, height, match_window, preset, update_info] {
                    match_window->setChecked(false);
                    width->setValue(preset.width);
                    height->setValue(preset.height);
                    update_info();
                });
    }
    layout->addLayout(presets);

    auto* grid = new QGridLayout();
    add_row(grid, 0, QStringLiteral("Width"), width);
    add_row(grid, 1, QStringLiteral("Height"), height);
    layout->addLayout(grid);
    layout->addWidget(match_window);
    layout->addWidget(info);
    connect(match_window, &QCheckBox::toggled, &dialog,
            [update_info](bool) { update_info(); });
    connect(width, &QSpinBox::valueChanged, &dialog,
            [update_info](int) { update_info(); });
    connect(height, &QSpinBox::valueChanged, &dialog,
            [update_info](int) { update_info(); });
    update_info();

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog,
            [this, &dialog, width, height, match_window] {
                if (match_window->isChecked()) {
                    viewport_->set_screen_size(0, 0);
                } else {
                    viewport_->set_screen_size(std::max(width->value(), 1),
                                               std::max(height->value(), 1));
                }
                footer_->sync();
                state_.set_status(QStringLiteral("Viewport render size: %1 x %2")
                                      .arg(viewport_->effective_screen_size().width())
                                      .arg(viewport_->effective_screen_size().height()));
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
    const QSize size = viewport_->effective_screen_size();
    layout->addWidget(make_hint(QStringLiteral("Frames are captured at the viewport render "
                                               "size: %1 x %2.")
                                    .arg(size.width())
                                    .arg(size.height())));
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
