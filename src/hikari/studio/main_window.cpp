#include "hikari/studio/main_window.hpp"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
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
    spin->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
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
    caption->setMinimumWidth(64);
    layout->addWidget(caption, row, 0);
    layout->addWidget(widget, row, 1);
}

QCheckBox* make_check(const QString& text, bool checked) {
    auto* box = new QCheckBox(text);
    box->setChecked(checked);
    return box;
}

QWidget* make_divider() {
    auto* divider = new QWidget();
    divider->setObjectName(QStringLiteral("ToolbarDivider"));
    divider->setFixedSize(1, 20);
    return divider;
}

enum class Glyph { First, Previous, Next, Last, Add, Remove };

QIcon make_glyph_icon(Glyph glyph) {
    constexpr int logical_size = 16;
    constexpr qreal scale = 2.0;
    QPixmap pixmap(static_cast<int>(logical_size * scale),
                   static_cast<int>(logical_size * scale));
    pixmap.setDevicePixelRatio(scale);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QColor ink(colors::on_surface_variant);
    painter.setPen(Qt::NoPen);
    painter.setBrush(ink);
    const auto triangle = [&painter](const QPointF& a, const QPointF& b, const QPointF& c) {
        QPolygonF shape;
        shape << a << b << c;
        painter.drawPolygon(shape);
    };
    switch (glyph) {
    case Glyph::First:
        painter.drawRect(QRectF(4.0, 3.5, 1.6, 9.0));
        triangle({12.6, 3.5}, {5.8, 8.0}, {12.6, 12.5});
        break;
    case Glyph::Previous:
        triangle({11.6, 3.5}, {4.4, 8.0}, {11.6, 12.5});
        break;
    case Glyph::Next:
        triangle({4.4, 3.5}, {11.6, 8.0}, {4.4, 12.5});
        break;
    case Glyph::Last:
        triangle({3.4, 3.5}, {10.2, 8.0}, {3.4, 12.5});
        painter.drawRect(QRectF(10.4, 3.5, 1.6, 9.0));
        break;
    case Glyph::Add:
        painter.setPen(QPen(ink, 1.6, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(QPointF(8.0, 4.2), QPointF(8.0, 11.8));
        painter.drawLine(QPointF(4.2, 8.0), QPointF(11.8, 8.0));
        break;
    case Glyph::Remove:
        painter.setPen(QPen(ink, 1.6, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(QPointF(4.2, 8.0), QPointF(11.8, 8.0));
        break;
    }
    return QIcon(pixmap);
}

QToolButton* make_transport_button(Glyph glyph, const QString& tooltip, QWidget* parent) {
    auto* button = new QToolButton(parent);
    button->setObjectName(QStringLiteral("TransportButton"));
    button->setIcon(make_glyph_icon(glyph));
    button->setIconSize(QSize(16, 16));
    button->setFixedSize(32, 32);
    button->setAutoRaise(true);
    button->setToolTip(tooltip);
    return button;
}

} // namespace

MainWindow::MainWindow(QString model_path, QString motion_path, QWidget* parent)
    : QMainWindow(parent), model_path_(std::move(model_path)) {
    setWindowTitle(QStringLiteral("Hikari Studio"));
    resize(1440, 900);
    setStyleSheet(NordicTheme::style_sheet());

    setCentralWidget(build_viewport_area());
    build_docks();
    build_menus();
    restore_panel_layout();
    QTimer::singleShot(250, this, [this] { refresh_tab_title_bars(); });

    statusBar()->setSizeGripEnabled(true);
    persistent_status_ = QStringLiteral("Hikari Studio ready");
    info_label_ = new QLabel(persistent_status_);
    info_label_->setObjectName(QStringLiteral("StatusLabel"));
    stats_label_ = new QLabel(QStringLiteral("60.0 fps  ·  16.7 ms"));
    stats_label_->setObjectName(QStringLiteral("StatsLabel"));
    statusBar()->addWidget(info_label_, 1);
    statusBar()->addPermanentWidget(stats_label_);

    connect(viewport_, &D3DViewport::statsChanged, this,
            [this](float fps, float frame_ms) {
                stats_label_->setText(QStringLiteral("%1 fps  ·  %2 ms")
                                          .arg(fps, 0, 'f', 1)
                                          .arg(frame_ms, 0, 'f', 1));
            });
    connect(viewport_, &D3DViewport::statusMessage, this,
            [this](const QString& message) { state_.set_status(message); });
    connect(viewport_, &D3DViewport::modelLoaded, this,
            [this](const QString& name, int vertices, int bones) {
                refresh_targets();
                refresh_morph_combos();
                persistent_status_ =
                    QStringLiteral("%1  ·  %2 vertices  ·  %3 bones").arg(name).arg(vertices).arg(bones);
            });
    connect(viewport_, &D3DViewport::screenSizeChanged, this, [this](int width, int height) {
        state_.screen_size_width = width;
        state_.screen_size_height = height;
        footer_->sync();
    });
    connect(viewport_, &D3DViewport::modelsChanged, this, [this] {
        refresh_model_list();
        refresh_targets();
        refresh_morph_combos();
    });
    connect(viewport_, &D3DViewport::activeModelChanged, this, [this] {
        refresh_model_list();
        refresh_targets();
        refresh_morph_combos();
        sync_bone_fields();
        footer_->sync();
        update_viewport_title();
        timeline_->refresh_layout();
        timeline_->update();
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
        update_edit_actions();
        timeline_->refresh_layout();
        timeline_->update();
        if (stats_label_ != nullptr) {
            stats_label_->setVisible(state_.info_display);
        }
        if (info_label_ != nullptr) {
            info_label_->setText(state_.current_status().value_or(persistent_status_));
            info_label_->setVisible(state_.info_display);
        }
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
            refresh_targets();
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
    auto* open_project = file_menu->addAction(QStringLiteral("Open Project (.hkp)..."), this, [this] {
        const QString path = QFileDialog::getOpenFileName(
            this, QStringLiteral("Open Hikari project"), state_.default_directory,
            QStringLiteral("Hikari Project (*.hkp)"));
        if (path.isEmpty()) {
            return;
        }
        if (viewport_->load_project(path)) {
            refresh_targets();
            refresh_model_list();
            refresh_morph_combos();
            timeline_->refresh_layout();
            timeline_->update();
            header_->set_frame(static_cast<int>(state_.current_frame));
            footer_->sync();
            update_viewport_title();
        }
    });
    open_project->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+O")));
    auto* save_project = file_menu->addAction(QStringLiteral("Save Project (.hkp)..."), this, [this] {
        const QString path = QFileDialog::getSaveFileName(
            this, QStringLiteral("Save Hikari project"), state_.default_directory,
            QStringLiteral("Hikari Project (*.hkp)"));
        if (!path.isEmpty()) {
            viewport_->save_project(path);
        }
    });
    save_project->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+S")));
    file_menu->addSeparator();
    file_menu->addAction(QStringLiteral("Load PMX Model..."), this,
                         [this] { choose_model(); });
    file_menu->addAction(QStringLiteral("Load Motion (.VMD)..."), this,
                         [this] { choose_motion(); });
    auto* load_hkm = file_menu->addAction(QStringLiteral("Load Hikari Motion (.hkm)..."), this, [this] {
        const QString path = QFileDialog::getOpenFileName(
            this, QStringLiteral("Load Hikari motion"), state_.default_directory,
            QStringLiteral("Hikari Motion (*.hkm)"));
        if (path.isEmpty()) {
            return;
        }
        if (viewport_->load_motion_hkm(path)) {
            refresh_targets();
            timeline_->refresh_layout();
            timeline_->update();
            header_->set_frame(static_cast<int>(state_.current_frame));
            footer_->sync();
        }
    });
    load_hkm->setShortcut(QKeySequence(QStringLiteral("Ctrl+Alt+O")));
    file_menu->addSeparator();
    file_menu->addAction(QStringLiteral("Load Pose (.VPD)..."), this,
                         [this] { choose_vpd_load(); });
    file_menu->addAction(QStringLiteral("Save Pose (.VPD)..."), this,
                         [this] { choose_vpd_save(); });
    file_menu->addAction(QStringLiteral("Save Motion (.VMD)..."), this,
                         [this] { choose_motion_save(); });
    auto* save_hkm = file_menu->addAction(QStringLiteral("Save Hikari Motion (.hkm)..."), this, [this] {
        const QString path = QFileDialog::getSaveFileName(
            this, QStringLiteral("Save Hikari motion"), state_.default_directory,
            QStringLiteral("Hikari Motion (*.hkm)"));
        if (!path.isEmpty()) {
            viewport_->save_motion_hkm(path);
        }
    });
    save_hkm->setShortcut(QKeySequence(QStringLiteral("Ctrl+Alt+S")));
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
    undo_action_ = edit_menu->addAction(QStringLiteral("Undo"));
    undo_action_->setShortcut(QKeySequence::Undo);
    undo_action_->setEnabled(false);
    connect(undo_action_, &QAction::triggered, this, [this] {
        state_.undo_keyframes();
        timeline_->refresh_layout();
        timeline_->update();
        update_edit_actions();
    });
    redo_action_ = edit_menu->addAction(QStringLiteral("Redo"));
    redo_action_->setShortcuts(
        {QKeySequence(QStringLiteral("Ctrl+Shift+Z")), QKeySequence(QStringLiteral("Ctrl+Y"))});
    redo_action_->setEnabled(false);
    connect(redo_action_, &QAction::triggered, this, [this] {
        state_.redo_keyframes();
        timeline_->refresh_layout();
        timeline_->update();
        update_edit_actions();
    });
    edit_menu->addSeparator();
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
        state_.delete_frame_line(static_cast<unsigned int>(state_.current_frame));
        timeline_->refresh_layout();
        timeline_->update();
        update_edit_actions();
    });

    auto* view_menu = menuBar()->addMenu(QStringLiteral("&View"));
    auto* grid_action = view_menu->addAction(QStringLiteral("Coordinate Grid (G)"));
    grid_action->setCheckable(true);
    grid_action->setChecked(state_.show_grid);
    connect(grid_action, &QAction::toggled, this,
            [this](bool checked) { state_.show_grid = checked; });
    auto* bones_action = view_menu->addAction(QStringLiteral("Skeleton Overlay"));
    bones_action->setCheckable(true);
    bones_action->setChecked(state_.show_bones);
    connect(bones_action, &QAction::toggled, this,
            [this](bool checked) { state_.show_bones = checked; });
    auto* all_bones_action = view_menu->addAction(QStringLiteral("Skeleton On All Models"));
    all_bones_action->setCheckable(true);
    all_bones_action->setChecked(state_.show_all_model_bones);
    all_bones_action->setToolTip(
        QStringLiteral("Show skeleton bones for every visible model, not only the active one"));
    connect(all_bones_action, &QAction::toggled, this,
            [this](bool checked) { state_.show_all_model_bones = checked; });
    auto* physics_bones_action = view_menu->addAction(QStringLiteral("Physics Bone Overlay"));
    physics_bones_action->setCheckable(true);
    physics_bones_action->setChecked(state_.show_physics_bones);
    connect(physics_bones_action, &QAction::toggled, this,
            [this](bool checked) { state_.show_physics_bones = checked; });
    auto* ground_shadow_action = view_menu->addAction(QStringLiteral("Ground Shadow"));
    ground_shadow_action->setCheckable(true);
    ground_shadow_action->setChecked(state_.ground_shadow);
    connect(ground_shadow_action, &QAction::toggled, this,
            [this](bool checked) { state_.ground_shadow = checked; });
    view_menu->addSeparator();
    auto* white_action = view_menu->addAction(QStringLiteral("MMD White Canvas"));
    white_action->setCheckable(true);
    white_action->setChecked(state_.viewport_white_bg);
    connect(white_action, &QAction::toggled, this,
            [this](bool checked) { state_.viewport_white_bg = checked; });
    auto* wireframe_action = view_menu->addAction(QStringLiteral("Wireframe (R)"));
    wireframe_action->setCheckable(true);
    wireframe_action->setChecked(state_.wireframe);
    connect(wireframe_action, &QAction::toggled, this,
            [this](bool checked) { state_.wireframe = checked; });
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
    view_menu->addSeparator();

    panels_menu_ = view_menu->addMenu(QStringLiteral("Panels"));
    if (timeline_dock_ != nullptr) {
        panels_menu_->addAction(timeline_dock_->toggleViewAction());
        panels_menu_->addSeparator();
    }
    for (auto* dock : panel_docks_) {
        panels_menu_->addAction(dock->toggleViewAction());
    }
    panels_menu_->addSeparator();
    panels_menu_->addAction(QStringLiteral("Reset Panel Layout"), this,
                            [this] { reset_panel_layout(); });

    auto* facial_menu = menuBar()->addMenu(QStringLiteral("&Facial"));
    facial_menu->addAction(QStringLiteral("Reset All Facial Values"), this, [this] {
        state_.morph_weights.clear();
        state_.morph_slider_values = {0.0f, 0.0f, 0.0f, 0.0f};
        sync_morph_fields();
        state_.set_status(QStringLiteral("Reset all facial morphs"));
    });
    facial_menu->addAction(QStringLiteral("Register All Facial Frames"), this, [this] {
        state_.push_undo_snapshot();
        for (const auto& names : state_.category_morph_names) {
            for (const auto& name : names) {
                state_.register_keyframe(name,
                                         static_cast<unsigned int>(state_.current_frame), false);
            }
        }
        timeline_->update();
    });

    auto* physics_menu = menuBar()->addMenu(QStringLiteral("&Physics"));
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
    auto* ground_action = physics_menu->addAction(QStringLiteral("Ground Collision"));
    ground_action->setCheckable(true);
    ground_action->setChecked(state_.ground_collision);
    connect(ground_action, &QAction::toggled, this,
            [this](bool checked) { state_.ground_collision = checked; });
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

    auto* help_menu = menuBar()->addMenu(QStringLiteral("&Help"));
    help_menu->addAction(QStringLiteral("Keyboard Shortcuts"), this,
                         [this] { show_shortcuts_dialog(); });
    help_menu->addAction(QStringLiteral("About Hikari Studio"), this,
                         [this] { show_about_dialog(); });
}

void MainWindow::build_docks() {
    setDockNestingEnabled(true);
    setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowNestedDocks |
                   QMainWindow::AllowTabbedDocks | QMainWindow::GroupedDragging);
    setTabPosition(Qt::AllDockWidgetAreas, QTabWidget::North);
    setCorner(Qt::TopLeftCorner, Qt::LeftDockWidgetArea);
    setCorner(Qt::BottomLeftCorner, Qt::BottomDockWidgetArea);
    setCorner(Qt::TopRightCorner, Qt::RightDockWidgetArea);
    setCorner(Qt::BottomRightCorner, Qt::BottomDockWidgetArea);

    timeline_dock_ = new QDockWidget(QStringLiteral("Timeline"), this);
    timeline_dock_->setObjectName(QStringLiteral("TimelineDock"));
    timeline_dock_->setAllowedAreas(Qt::AllDockWidgetAreas);
    timeline_dock_->setWidget(build_timeline_panel());
    addDockWidget(Qt::BottomDockWidgetArea, timeline_dock_);

    add_panel_dock(QStringLiteral("Model"), QStringLiteral("ModelDock"), build_model_tab(),
                   Qt::LeftDockWidgetArea);
    add_panel_dock(QStringLiteral("Bone"), QStringLiteral("BoneDock"), build_bone_tab(),
                   Qt::LeftDockWidgetArea);

    auto* camera = add_panel_dock(QStringLiteral("Camera"), QStringLiteral("CameraDock"),
                                  build_camera_tab(), Qt::RightDockWidgetArea);
    auto* light = add_panel_dock(QStringLiteral("Light"), QStringLiteral("LightDock"),
                                 build_light_tab(), Qt::RightDockWidgetArea);
    auto* shadow = add_panel_dock(QStringLiteral("Shadow"), QStringLiteral("ShadowDock"),
                                  build_shadow_tab(), Qt::RightDockWidgetArea);
    auto* accessory = add_panel_dock(QStringLiteral("Accessory"),
                                     QStringLiteral("AccessoryDock"), build_accessory_tab(),
                                     Qt::RightDockWidgetArea);
    auto* facial = add_panel_dock(QStringLiteral("Facial"), QStringLiteral("FacialDock"),
                                  build_facial_tab(), Qt::RightDockWidgetArea);
    auto* view = add_panel_dock(QStringLiteral("View"), QStringLiteral("ViewDock"),
                                build_view_tab(), Qt::RightDockWidgetArea);
    auto* playback = add_panel_dock(QStringLiteral("Playback"), QStringLiteral("PlaybackDock"),
                                    build_playback_tab(), Qt::RightDockWidgetArea);
    auto* interpolation =
        add_panel_dock(QStringLiteral("Interpolation"), QStringLiteral("InterpolationDock"),
                       build_interpolation_tab(), Qt::RightDockWidgetArea);

    tabifyDockWidget(camera, light);
    tabifyDockWidget(light, shadow);
    tabifyDockWidget(shadow, accessory);
    tabifyDockWidget(facial, view);
    tabifyDockWidget(view, playback);
    tabifyDockWidget(playback, interpolation);

    resizeDocks({timeline_dock_}, {320}, Qt::Vertical);
    if (auto* model = findChild<QDockWidget*>(QStringLiteral("ModelDock"))) {
        resizeDocks({model}, {288}, Qt::Horizontal);
        if (auto* bone = findChild<QDockWidget*>(QStringLiteral("BoneDock"))) {
            resizeDocks({model, bone}, {255, 265}, Qt::Vertical);
        }
    }
    resizeDocks({camera}, {340}, Qt::Horizontal);
    resizeDocks({camera, facial}, {305, 215}, Qt::Vertical);
    camera->raise();
    facial->raise();
    refresh_tab_title_bars();
}

void MainWindow::refresh_tab_title_bars() {
    std::vector<QDockWidget*> docks = panel_docks_;
    if (timeline_dock_ != nullptr) {
        docks.push_back(timeline_dock_);
    }
    // Dock tab bars elide long names by default, which cuts panel titles down
    // to a few letters in a narrow dock area. Keep the full labels and let the
    // scroller arrows take over when the tabs no longer fit.
    for (auto* bar : findChildren<QTabBar*>()) {
        bar->installEventFilter(this);
        if (bar->elideMode() != Qt::ElideNone) {
            bar->setElideMode(Qt::ElideNone);
        }
        if (!bar->usesScrollButtons()) {
            bar->setUsesScrollButtons(true);
        }
        if (bar->expanding()) {
            bar->setExpanding(false);
        }
        if (bar->drawBase()) {
            bar->setDrawBase(false);
        }
        // Qt draws no close button on dock tabs, so place one inside each tab.
        // It is a plain child of the bar rather than a tab button: QTabBar
        // reserves a wide, non-styleable gap for tab buttons, which would keep
        // the cross far from its label.
        const int last = bar->count() - 1;
        const bool overflowing =
            last >= 0 && bar->tabRect(last).right() > bar->width();
        const int visible_right = overflowing ? bar->width() - 36 : bar->width();
        QStringList titles;
        for (int i = 0; i < bar->count(); ++i) {
            titles.append(bar->tabText(i));
        }
        const auto buttons =
            bar->findChildren<QToolButton*>(QStringLiteral("TabCloseButton"));
        for (auto* button : buttons) {
            if (!titles.contains(button->property("dockTitle").toString())) {
                button->hide();
                button->deleteLater();
            }
        }
        for (int i = 0; i < bar->count(); ++i) {
            const QString title = bar->tabText(i);
            QDockWidget* target = nullptr;
            for (auto* dock : docks) {
                if (dock->windowTitle() == title) {
                    target = dock;
                    break;
                }
            }
            if (target == nullptr) {
                continue;
            }
            QToolButton* close = nullptr;
            for (auto* button : buttons) {
                if (button->property("dockTitle").toString() == title) {
                    close = button;
                    break;
                }
            }
            if (close == nullptr) {
                close = new QToolButton(bar);
                close->setObjectName(QStringLiteral("TabCloseButton"));
                close->setProperty("dockTitle", title);
                close->setIcon(QIcon(QStringLiteral(":/icons/close.svg")));
                close->setIconSize(QSize(16, 16));
                close->setFixedSize(16, 16);
                close->setAutoRaise(true);
                close->setCursor(Qt::PointingHandCursor);
                close->setToolTip(QStringLiteral("Close %1").arg(title));
                connect(close, &QToolButton::clicked, this, [target, close] {
                    close->hide();
                    target->close();
                });
            } else {
                close->setIconSize(QSize(16, 16));
                close->setFixedSize(16, 16);
            }
            const QRect tab = bar->tabRect(i);
            const int button_w = close->width();
            const int button_h = close->height();
            const int x = tab.right() - button_w - 4;
            const int y = tab.top() + (tab.height() - 2 - button_h) / 2;
            close->move(x, y);
            const bool tab_visible = tab.left() >= 0 && tab.right() <= visible_right;
            close->setVisible(tab_visible);
            close->raise();
        }
    }
    // Qt keeps the active dock's title bar below the tab bar in a vertical
    // dock area, which shows every panel name twice. Hide the title bar while
    // the dock shares a tab group, and restore it as soon as it is pulled out.
    for (auto* dock : docks) {
        bool tabbed = false;
        for (auto* sibling : tabifiedDockWidgets(dock)) {
            if (!sibling->isHidden()) {
                tabbed = true;
                break;
            }
        }
        if (tabbed) {
            if (dock->titleBarWidget() == nullptr) {
                auto* spacer = new QWidget(dock);
                spacer->setObjectName(QStringLiteral("TabbedDockSpacer"));
                spacer->setFixedHeight(0);
                dock->setTitleBarWidget(spacer);
            }
        } else if (dock->titleBarWidget() != nullptr) {
            dock->setTitleBarWidget(nullptr);
        }
    }
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    if ((event->type() == QEvent::LayoutRequest || event->type() == QEvent::Resize) &&
        qobject_cast<QTabBar*>(watched) != nullptr) {
        QTimer::singleShot(0, this, [this] { refresh_tab_title_bars(); });
    }
    return QMainWindow::eventFilter(watched, event);
}

QDockWidget* MainWindow::add_panel_dock(const QString& title, const QString& object_name,
                                        QWidget* body, Qt::DockWidgetArea area) {
    auto* dock = new QDockWidget(title, this);
    dock->setObjectName(object_name);
    dock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable |
                      QDockWidget::DockWidgetClosable);
    dock->setAllowedAreas(Qt::AllDockWidgetAreas);

    auto* scroll = new QScrollArea();
    scroll->setObjectName(QStringLiteral("PanelScroll"));
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setBackgroundRole(QPalette::Window);
    scroll->viewport()->setBackgroundRole(QPalette::Window);
    scroll->setMinimumSize(1, 1);
    body->setObjectName(QStringLiteral("PanelBody"));
    scroll->setWidget(body);
    dock->setWidget(scroll);
    dock->setMinimumWidth(240);
    addDockWidget(area, dock);
    connect(dock, &QDockWidget::topLevelChanged, this,
            [this](bool) { refresh_tab_title_bars(); });
    connect(dock, &QDockWidget::dockLocationChanged, this,
            [this](Qt::DockWidgetArea) { refresh_tab_title_bars(); });
    connect(dock, &QDockWidget::visibilityChanged, this, [this](bool) {
        QTimer::singleShot(0, this, [this] { refresh_tab_title_bars(); });
    });
    panel_docks_.push_back(dock);
    return dock;
}

void MainWindow::reset_panel_layout() {
    if (timeline_dock_ == nullptr) {
        return;
    }
    timeline_dock_->setFloating(false);
    timeline_dock_->show();
    addDockWidget(Qt::BottomDockWidgetArea, timeline_dock_);

    const auto dock = [this](const QString& name) {
        return findChild<QDockWidget*>(name);
    };
    auto* model = dock(QStringLiteral("ModelDock"));
    auto* bone = dock(QStringLiteral("BoneDock"));
    auto* camera = dock(QStringLiteral("CameraDock"));
    auto* light = dock(QStringLiteral("LightDock"));
    auto* shadow = dock(QStringLiteral("ShadowDock"));
    auto* accessory = dock(QStringLiteral("AccessoryDock"));
    auto* facial = dock(QStringLiteral("FacialDock"));
    auto* view = dock(QStringLiteral("ViewDock"));
    auto* playback = dock(QStringLiteral("PlaybackDock"));
    auto* interpolation = dock(QStringLiteral("InterpolationDock"));
    if (model == nullptr || bone == nullptr || camera == nullptr || light == nullptr ||
        shadow == nullptr || accessory == nullptr || facial == nullptr || view == nullptr ||
        playback == nullptr || interpolation == nullptr) {
        return;
    }

    for (auto* panel : panel_docks_) {
        panel->setFloating(false);
        panel->show();
    }
    addDockWidget(Qt::LeftDockWidgetArea, model);
    addDockWidget(Qt::LeftDockWidgetArea, bone);
    addDockWidget(Qt::RightDockWidgetArea, camera);
    addDockWidget(Qt::RightDockWidgetArea, light);
    addDockWidget(Qt::RightDockWidgetArea, shadow);
    addDockWidget(Qt::RightDockWidgetArea, accessory);
    addDockWidget(Qt::RightDockWidgetArea, facial);
    addDockWidget(Qt::RightDockWidgetArea, view);
    addDockWidget(Qt::RightDockWidgetArea, playback);
    addDockWidget(Qt::RightDockWidgetArea, interpolation);
    tabifyDockWidget(camera, light);
    tabifyDockWidget(light, shadow);
    tabifyDockWidget(shadow, accessory);
    tabifyDockWidget(facial, view);
    tabifyDockWidget(view, playback);
    tabifyDockWidget(playback, interpolation);

    resizeDocks({timeline_dock_}, {320}, Qt::Vertical);
    resizeDocks({model}, {288}, Qt::Horizontal);
    resizeDocks({model, bone}, {255, 265}, Qt::Vertical);
    resizeDocks({camera}, {340}, Qt::Horizontal);
    resizeDocks({camera, facial}, {305, 215}, Qt::Vertical);
    camera->raise();
    facial->raise();
    refresh_tab_title_bars();
}

void MainWindow::restore_panel_layout() {
    QSettings settings;
    const QByteArray geometry =
        settings.value(QStringLiteral("studio/window_geometry_v2")).toByteArray();
    if (!geometry.isEmpty()) {
        restoreGeometry(geometry);
    }
    const QByteArray layout =
        settings.value(QStringLiteral("studio/panel_layout_v2")).toByteArray();
    if (layout.isEmpty() || !restoreState(layout, 2)) {
        reset_panel_layout();
    }
    refresh_tab_title_bars();
}

void MainWindow::save_panel_layout() {
    QSettings settings;
    settings.setValue(QStringLiteral("studio/window_geometry_v2"), saveGeometry());
    settings.setValue(QStringLiteral("studio/panel_layout_v2"), saveState(2));
}

void MainWindow::closeEvent(QCloseEvent* event) {
    save_panel_layout();
    QMainWindow::closeEvent(event);
}

QWidget* MainWindow::build_timeline_panel() {
    auto* panel = new QWidget();
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(12, 10, 12, 10);
    layout->setSpacing(8);

    auto* transport = new QHBoxLayout();
    transport->setSpacing(4);
    auto* first_button = make_transport_button(Glyph::First, QStringLiteral("First frame"), panel);
    auto* prev_button = make_transport_button(Glyph::Previous, QStringLiteral("Previous frame"),
                                              panel);
    auto* next_button = make_transport_button(Glyph::Next, QStringLiteral("Next frame"), panel);
    auto* last_button = make_transport_button(Glyph::Last, QStringLiteral("Last frame"), panel);
    jump_frame_ = new QSpinBox();
    jump_frame_->setRange(0, 100000);
    jump_frame_->setAlignment(Qt::AlignCenter);
    jump_frame_->setFixedWidth(88);
    jump_frame_->setToolTip(QStringLiteral("Current frame"));
    auto* add_button = make_transport_button(Glyph::Add, QStringLiteral("Register keyframe"),
                                             panel);
    auto* remove_button = make_transport_button(Glyph::Remove, QStringLiteral("Delete keyframe"),
                                                panel);

    transport->addWidget(first_button);
    transport->addWidget(prev_button);
    transport->addWidget(next_button);
    transport->addWidget(last_button);
    transport->addSpacing(8);
    transport->addWidget(jump_frame_);
    transport->addSpacing(4);
    transport->addWidget(add_button);
    transport->addWidget(remove_button);
    transport->addWidget(make_divider());
    transport->addStretch(1);

    auto* zoom_label = new QLabel(QStringLiteral("Zoom"));
    zoom_label->setObjectName(QStringLiteral("FieldLabel"));
    auto* zoom = new QSlider(Qt::Horizontal);
    zoom->setRange(50, 300);
    zoom->setValue(100);
    zoom->setFixedWidth(140);
    auto* zoom_value = new QLabel(QStringLiteral("100%"));
    zoom_value->setObjectName(QStringLiteral("FieldLabel"));
    zoom_value->setFixedWidth(44);
    zoom_value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    transport->addWidget(zoom_label);
    transport->addWidget(zoom);
    transport->addWidget(zoom_value);
    layout->addLayout(transport);

    auto* actions_row = new QHBoxLayout();
    actions_row->setSpacing(4);
    auto* select_all = new QPushButton(QStringLiteral("Select all"));
    select_all->setToolTip(QStringLiteral("Select every bone keyframe at the current frame"));
    auto* vsel_button = new QPushButton(QStringLiteral("Track keys"));
    vsel_button->setToolTip(QStringLiteral("Select every keyframe on the active track"));
    auto* delete_line = new QPushButton(QStringLiteral("Delete frame"));
    delete_line->setToolTip(QStringLiteral("Delete every keyframe at the current frame"));
    copy_button_ = new QPushButton(QStringLiteral("Copy"));
    paste_button_ = new QPushButton(QStringLiteral("Paste"));
    auto* delete_button = new QPushButton(QStringLiteral("Delete keys"));
    delete_button->setObjectName(QStringLiteral("DangerButton"));
    delete_button->setToolTip(QStringLiteral("Delete the selected keyframes"));
    actions_row->addWidget(select_all);
    actions_row->addWidget(vsel_button);
    actions_row->addWidget(delete_line);
    actions_row->addWidget(make_divider());
    actions_row->addWidget(copy_button_);
    actions_row->addWidget(paste_button_);
    actions_row->addWidget(delete_button);
    actions_row->addStretch(1);
    layout->addLayout(actions_row);

    timeline_ = new TimelineWidget(state_);
    auto* scroll = new QScrollArea();
    scroll->setObjectName(QStringLiteral("TimelineScrollArea"));
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setBackgroundRole(QPalette::Window);
    scroll->viewport()->setBackgroundRole(QPalette::Window);
    scroll->setWidget(timeline_);
    layout->addWidget(scroll, 1);

    auto* timeline_scroll = new QScrollBar(Qt::Horizontal);
    timeline_scroll->setObjectName(QStringLiteral("TimelineScroll"));
    layout->addWidget(timeline_scroll);
    connect(timeline_scroll, &QScrollBar::valueChanged, timeline_, &TimelineWidget::set_scroll);
    connect(timeline_, &TimelineWidget::scrollRangeChanged, timeline_scroll,
            [timeline_scroll](int maximum) { timeline_scroll->setRange(0, maximum); });
    connect(timeline_, &TimelineWidget::scrollChanged, timeline_scroll, &QScrollBar::setValue);
    connect(timeline_scroll, &QScrollBar::sliderPressed, timeline_,
            [this] { timeline_->set_follow_playhead(false); });
    connect(timeline_scroll, &QScrollBar::sliderReleased, timeline_,
            [this] { timeline_->set_follow_playhead(true); });
    connect(timeline_, &TimelineWidget::deleteSelectedRequested, this,
            [this] { delete_selected_keyframes(); });
    connect(timeline_, &TimelineWidget::copyRequested, this,
            [this] { copy_keyframes(); });
    connect(timeline_, &TimelineWidget::pasteRequested, this,
            [this] { paste_keyframes(); });

    connect(first_button, &QToolButton::clicked, this, [this] {
        state_.current_frame = 0.0f;
        jump_frame_->setValue(0);
        footer_->sync();
    });
    connect(prev_button, &QToolButton::clicked, this, [this] {
        state_.step_frame(-1);
        jump_frame_->setValue(static_cast<int>(state_.current_frame));
        footer_->sync();
    });
    connect(next_button, &QToolButton::clicked, this, [this] {
        state_.step_frame(1);
        jump_frame_->setValue(static_cast<int>(state_.current_frame));
        footer_->sync();
    });
    connect(last_button, &QToolButton::clicked, this, [this] {
        state_.current_frame = static_cast<float>(state_.play_range.second);
        jump_frame_->setValue(state_.play_range.second);
        footer_->sync();
    });
    connect(jump_frame_, &QSpinBox::valueChanged, this,
            [this](int value) { state_.current_frame = static_cast<float>(value); });
    connect(add_button, &QToolButton::clicked, this, [this] {
        state_.register_keyframe(state_.selected_track_name(),
                                 static_cast<unsigned int>(state_.current_frame));
        timeline_->update();
    });
    connect(remove_button, &QToolButton::clicked, this, [this] {
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
        state_.delete_frame_line(static_cast<unsigned int>(state_.current_frame));
        timeline_->refresh_layout();
        timeline_->update();
        update_edit_actions();
    });
    connect(vsel_button, &QPushButton::clicked, this, [this] {
        const QString track = state_.selected_track_name();
        const auto it = state_.track_keyframes.find(track);
        if (it != state_.track_keyframes.end()) {
            for (const auto& entry : it->second) {
                state_.selected_keyframes.insert({track, entry.first});
            }
        }
        timeline_->update();
    });
    connect(delete_button, &QPushButton::clicked, this,
            [this] { delete_selected_keyframes(); });
    connect(zoom, &QSlider::valueChanged, this, [this, zoom_value](int value) {
        state_.timeline_zoom = static_cast<float>(value) / 100.0f;
        zoom_value->setText(QStringLiteral("%1%").arg(value));
        timeline_->refresh_layout();
        timeline_->update();
    });
    copy_button_->setEnabled(false);
    paste_button_->setEnabled(false);
    connect(copy_button_, &QPushButton::clicked, this, [this] { copy_keyframes(); });
    connect(paste_button_, &QPushButton::clicked, this, [this] { paste_keyframes(); });
    return panel;
}

QWidget* MainWindow::build_model_tab() {
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);
    model_combo_ = new QComboBox();
    model_combo_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    model_combo_->setMinimumContentsLength(8);
    model_combo_->setToolTip(QStringLiteral("Active model. Editing applies to this model."));
    layout->addWidget(model_combo_);
    auto* row = new QHBoxLayout();
    auto* load = new QPushButton(QStringLiteral("Load PMX..."));
    auto* remove = new QPushButton(QStringLiteral("Remove"));
    row->addWidget(load);
    row->addWidget(remove);
    layout->addLayout(row);

    model_visible_ = make_check(QStringLiteral("Display"), state_.model_visible);
    layout->addWidget(model_visible_);
    auto* scale_row = new QHBoxLayout();
    scale_row->addWidget(new QLabel(QStringLiteral("Scale")));
    model_scale_ = make_spin(0.05, 20.0, 0.01, 3, state_.model_scale);
    model_scale_->setToolTip(
        QStringLiteral("Uniform model scale. Poses, motions and physics keep their authored "
                       "model-space units and scale together with the model."));
    scale_row->addWidget(model_scale_, 1);
    auto* key_scale = new QPushButton(QStringLiteral("Key"));
    key_scale->setToolTip(QStringLiteral("Register the model scale at the current frame"));
    scale_row->addWidget(key_scale);
    layout->addLayout(scale_row);
    auto* register_button = new QPushButton(QStringLiteral("Register Disp/IK"));
    register_button->setToolTip(
        QStringLiteral("Register IK on/off state for every IK bone at the current frame. "
                       "Bone display is not stored in VMD."));
    layout->addWidget(register_button);
    layout->addStretch(1);

    connect(model_combo_, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index >= 0 && index != viewport_->active_model_index()) {
            viewport_->set_active_model(index);
        }
    });
    connect(load, &QPushButton::clicked, this, [this] { choose_model(); });
    connect(remove, &QPushButton::clicked, this,
            [this] { state_.model_delete_requested = true; });
    connect(model_visible_, &QCheckBox::toggled, this, [this](bool checked) {
        state_.model_visible = checked;
        viewport_->set_model_visible(viewport_->active_model_index(), checked);
    });
    connect(model_scale_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        const float scale = static_cast<float>(value);
        state_.model_scale = scale;
        viewport_->set_model_scale(viewport_->active_model_index(), scale);
    });
    connect(key_scale, &QPushButton::clicked, this, [this] {
        state_.register_keyframe(QStringLiteral("model scale"),
                                 static_cast<unsigned int>(state_.current_frame));
        timeline_->update();
    });
    connect(register_button, &QPushButton::clicked, this, [this] {
        state_.register_ik_keyframe(viewport_->ik_states(),
                                    static_cast<unsigned int>(state_.current_frame));
        timeline_->update();
    });
    return page;
}

QWidget* MainWindow::build_bone_tab() {
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

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

    bone_ik_ = make_check(QStringLiteral("IK"), false);
    bone_ik_->setToolTip(
        QStringLiteral("Enable or disable IK for the selected IK controller bone."));
    layout->addWidget(bone_ik_);

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
    connect(bone_ik_, &QCheckBox::toggled, this,
            [this](bool checked) { viewport_->set_selected_bone_ik_enabled(checked); });
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
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);
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
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);
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
    camera_target_ = new QComboBox();
    camera_target_->addItems({QStringLiteral("Free"), QStringLiteral("Model"),
                              QStringLiteral("Selected bone")});
    camera_target_->setCurrentIndex(static_cast<int>(state_.camera_target));
    camera_target_->setToolTip(
        QStringLiteral("Camera rotation pivot in model mode: free point, model center, "
                       "or the selected bone."));
    add_row(grid, 5, QStringLiteral("Target"), camera_target_);
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
    connect(camera_target_, &QComboBox::currentIndexChanged, this, [this](int index) {
        state_.camera_target = static_cast<CameraTarget>(std::clamp(index, 0, 2));
        state_.set_status(index == 1 ? QStringLiteral("Camera target: model")
                                     : index == 2 ? QStringLiteral("Camera target: selected bone")
                                                  : QStringLiteral("Camera target: free"));
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
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);
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
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);
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
    const QString tooltip = QStringLiteral(
        "Self-shadow mapping is not rendered in this pass. These values are authored and "
        "exported to VMD self-shadow frames.");
    mode->setToolTip(tooltip);
    range->setToolTip(tooltip);
    detail->setToolTip(tooltip);
    register_button->setToolTip(tooltip);
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
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);
    auto* load = new QPushButton(QStringLiteral("Load .x..."));
    auto* clear = new QPushButton(QStringLiteral("Remove"));
    auto* row = new QHBoxLayout();
    row->addWidget(load);
    row->addWidget(clear);
    layout->addLayout(row);
    auto* scale = make_spin(0.01, 100.0, 0.01, 2, state_.accessory_scale);
    auto* alpha = make_spin(0.0, 1.0, 0.01, 2, state_.accessory_alpha);
    const QString accessory_tooltip =
        QStringLiteral("Accessory rendering is not implemented in this pass.");
    load->setEnabled(false);
    clear->setEnabled(false);
    scale->setEnabled(false);
    alpha->setEnabled(false);
    load->setToolTip(accessory_tooltip);
    clear->setToolTip(accessory_tooltip);
    scale->setToolTip(accessory_tooltip);
    alpha->setToolTip(accessory_tooltip);
    auto* grid = new QGridLayout();
    grid->setHorizontalSpacing(4);
    grid->setVerticalSpacing(2);
    add_row(grid, 0, QStringLiteral("Scale"), scale);
    add_row(grid, 1, QStringLiteral("Alpha"), alpha);
    layout->addLayout(grid);
    auto* register_button = new QPushButton(QStringLiteral("Register Accessory Keyframe"));
    register_button->setEnabled(false);
    register_button->setToolTip(accessory_tooltip);
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
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);
    auto* grid = new QGridLayout();
    grid->setHorizontalSpacing(6);
    grid->setVerticalSpacing(2);
    auto* ground = make_check(QStringLiteral("Ground grid"), state_.show_grid);
    auto* white = make_check(QStringLiteral("White canvas"), state_.viewport_white_bg);
    auto* wireframe = make_check(QStringLiteral("Wireframe"), state_.wireframe);
    auto* info = make_check(QStringLiteral("Info"), state_.info_display);
    auto* ground_shadow = make_check(QStringLiteral("Ground shadow"), state_.ground_shadow);
    grid->addWidget(ground, 0, 0);
    grid->addWidget(white, 0, 1);
    grid->addWidget(wireframe, 1, 0);
    grid->addWidget(info, 1, 1);
    grid->addWidget(ground_shadow, 2, 0);
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
    connect(white, &QCheckBox::toggled, this,
            [this](bool checked) { state_.viewport_white_bg = checked; });
    connect(wireframe, &QCheckBox::toggled, this,
            [this](bool checked) { state_.wireframe = checked; });
    connect(info, &QCheckBox::toggled, this,
            [this](bool checked) { state_.info_display = checked; });
    connect(ground_shadow, &QCheckBox::toggled, this,
            [this](bool checked) { state_.ground_shadow = checked; });
    connect(fps, &QComboBox::currentIndexChanged, this,
            [this](int index) { state_.fps_limit = static_cast<FpsLimit>(index); });
    connect(screen_size, &QPushButton::clicked, this,
            [this] { show_screen_size_dialog(); });
    return page;
}

QWidget* MainWindow::build_playback_tab() {
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);
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
    auto* jiggle = make_check(QStringLiteral("Jiggle dynamics"), state_.jiggle_enabled);
    auto* vsync = make_check(QStringLiteral("VSync"), state_.vsync);
    auto* physics = make_check(QStringLiteral("Physics"), state_.physics_mode != PhysicsMode::Disabled);
    auto* playtime = make_check(QStringLiteral("Only while playing"), state_.playtime_only);
    layout->addWidget(loop);
    layout->addWidget(jiggle);
    layout->addWidget(vsync);
    layout->addWidget(physics);
    layout->addWidget(playtime);
    layout->addStretch(1);

    connect(play, &QPushButton::clicked, this, [this] {
        if (!state_.is_playing) {
            timeline_->set_follow_playhead(true);
        }
        state_.toggle_play();
    });
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
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);
    auto* automatic = make_check(QStringLiteral("Automatic curve"), state_.curve_auto);
    auto* row = new QHBoxLayout();
    auto* linear = new QPushButton(QStringLiteral("Linear"));
    auto* scurve = new QPushButton(QStringLiteral("S-Curve"));
    row->addWidget(linear);
    row->addWidget(scurve);
    layout->addWidget(automatic);
    layout->addLayout(row);
    layout->addWidget(make_hint(QStringLiteral("Applies to selected keyframes, or sets the "
                                               "default for new keys when nothing is selected.")));
    layout->addStretch(1);

    connect(automatic, &QCheckBox::toggled, this,
            [this](bool checked) { state_.curve_auto = checked; });
    connect(linear, &QPushButton::clicked, this, [this] {
        state_.curve_auto = true;
        state_.curve_p1 = {0.33f, 0.33f};
        state_.curve_p2 = {0.66f, 0.66f};
        state_.apply_curve_to_selection();
        timeline_->update();
        update_edit_actions();
    });
    connect(scurve, &QPushButton::clicked, this, [this] {
        state_.curve_auto = false;
        state_.curve_p1 = {0.1f, 0.9f};
        state_.curve_p2 = {0.9f, 0.1f};
        state_.apply_curve_to_selection();
        timeline_->update();
        update_edit_actions();
    });
    return page;
}

void MainWindow::refresh_targets() {
    if (!state_.model_loaded && state_.mode == EditorMode::Model) {
        state_.mode = EditorMode::Camera;
    }
    refresh_model_list();
    if (timeline_ != nullptr) {
        timeline_->refresh_layout();
        timeline_->update();
    }
    update_edit_actions();
    update_viewport_title();
    if (footer_ != nullptr) {
        footer_->sync();
    }
}

void MainWindow::refresh_model_list() {
    if (model_combo_ == nullptr || viewport_ == nullptr) {
        return;
    }
    model_combo_->blockSignals(true);
    model_combo_->clear();
    model_combo_->addItems(viewport_->model_names());
    const int active = viewport_->active_model_index();
    if (active >= 0 && active < model_combo_->count()) {
        model_combo_->setCurrentIndex(active);
    }
    model_combo_->blockSignals(false);
    const bool loaded = model_combo_->count() > 0;
    model_combo_->setEnabled(loaded);
    if (model_visible_ != nullptr) {
        model_visible_->setEnabled(loaded);
        model_visible_->blockSignals(true);
        model_visible_->setChecked(state_.model_visible);
        model_visible_->blockSignals(false);
    }
    if (model_scale_ != nullptr) {
        model_scale_->setEnabled(loaded);
        model_scale_->blockSignals(true);
        model_scale_->setValue(state_.model_scale);
        model_scale_->blockSignals(false);
    }
}

void MainWindow::update_edit_actions() {
    if (undo_action_ != nullptr) {
        undo_action_->setEnabled(state_.can_undo());
    }
    if (redo_action_ != nullptr) {
        redo_action_->setEnabled(state_.can_redo());
    }
    if (copy_button_ != nullptr) {
        copy_button_->setEnabled(!state_.selected_keyframes.empty());
    }
    if (paste_button_ != nullptr) {
        paste_button_->setEnabled(!clipboard_.empty());
    }
}

void MainWindow::copy_keyframes() {
    clipboard_.clear();
    clipboard_anchor_ = 0;
    bool first = true;
    for (const auto& [track, frame] : state_.selected_keyframes) {
        const auto track_it = state_.track_keyframes.find(track);
        if (track_it == state_.track_keyframes.end()) {
            continue;
        }
        const auto key_it = track_it->second.find(frame);
        if (key_it == track_it->second.end()) {
            continue;
        }
        clipboard_.push_back({track, frame, key_it->second});
        if (first) {
            clipboard_anchor_ = frame;
            first = false;
        } else {
            clipboard_anchor_ = std::min(clipboard_anchor_, frame);
        }
    }
    state_.set_status(QStringLiteral("Copied %1 keyframe(s)")
                          .arg(static_cast<int>(clipboard_.size())));
    update_edit_actions();
}

void MainWindow::paste_keyframes() {
    if (clipboard_.empty()) {
        return;
    }
    const std::size_t pasted = state_.paste_keyframes(
        clipboard_, clipboard_anchor_, static_cast<int>(state_.current_frame));
    timeline_->refresh_layout();
    timeline_->update();
    update_edit_actions();
    state_.set_status(QStringLiteral("Pasted %1 keyframe(s)").arg(static_cast<int>(pasted)));
}

void MainWindow::update_viewport_title() {
    if (header_ == nullptr) {
        return;
    }
    if (state_.mode == EditorMode::Camera) {
        header_->set_title(QStringLiteral("Camera  ·  Light  ·  Accessory"));
    } else {
        header_->set_title(state_.model_name.isEmpty() ? QStringLiteral("Model")
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
    if (camera_target_ != nullptr && !camera_target_->hasFocus()) {
        camera_target_->blockSignals(true);
        camera_target_->setCurrentIndex(static_cast<int>(state_.camera_target));
        camera_target_->blockSignals(false);
    }
    if (model_scale_ != nullptr && !model_scale_->hasFocus()) {
        model_scale_->blockSignals(true);
        model_scale_->setValue(state_.model_scale);
        model_scale_->blockSignals(false);
    }
    const auto set_ik_check = [this](bool enabled, bool checked) {
        if (bone_ik_ == nullptr) {
            return;
        }
        bone_ik_->blockSignals(true);
        bone_ik_->setEnabled(enabled);
        bone_ik_->setChecked(checked);
        bone_ik_->blockSignals(false);
    };
    const auto* skeleton = viewport_->skeleton();
    if (skeleton == nullptr || !state_.selected_bone_idx) {
        set_ik_check(false, false);
        return;
    }
    const std::size_t index = static_cast<std::size_t>(*state_.selected_bone_idx);
    if (index >= skeleton->bones.size()) {
        set_ik_check(false, false);
        return;
    }
    const bool has_ik = skeleton->bones[index].has_ik;
    const bool ik_enabled = index < skeleton->ik_enabled.size() && skeleton->ik_enabled[index];
    set_ik_check(has_ik, has_ik && ik_enabled);
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

void MainWindow::delete_selected_keyframes() {
    if (state_.selected_keyframes.empty()) {
        state_.delete_keyframe(state_.selected_track_name(),
                               static_cast<unsigned int>(state_.current_frame));
    } else {
        state_.delete_keyframes(state_.selected_keyframes);
    }
    timeline_->refresh_layout();
    timeline_->update();
    update_edit_actions();
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
        refresh_model_list();
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
        refresh_targets();
    }
}

void MainWindow::choose_motion_save() {
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Save VMD Motion"), state_.default_directory,
        QStringLiteral("Vocaloid Motion Data (*.vmd)"));
    if (!path.isEmpty()) {
        viewport_->save_motion(path);
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

void MainWindow::keyPressEvent(QKeyEvent* event) {
    if (event->matches(QKeySequence::Copy)) {
        copy_keyframes();
        event->accept();
        return;
    }
    if (event->matches(QKeySequence::Paste)) {
        paste_keyframes();
        event->accept();
        return;
    }
    QMainWindow::keyPressEvent(event);
}

} // namespace hikari::studio
