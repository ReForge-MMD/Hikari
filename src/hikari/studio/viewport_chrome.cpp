#include "hikari/studio/viewport_chrome.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <QDoubleSpinBox>
#include <QCursor>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

#include "hikari/studio/app_state.hpp"
#include "hikari/studio/theme.hpp"

namespace hikari::studio {
namespace {

constexpr int kCell = 32;
constexpr int kGap = 4;
constexpr int kMargin = 8;
constexpr int kHeader = 20;
constexpr int kColumns = 3;
constexpr int kRows = 2;

// MMD uses 0.005 rad per pixel as the half angle, which reads as 0.01 rad
// (0.573 degrees) per pixel after the quaternion construction.
constexpr float kRotateDegreesPerPixel = 0.5729578f;
constexpr float kTranslateUnitsPerPixel = 0.05f;

const QColor kAxisColor[3] = {QColor(0xe0, 0xa3, 0x9e), QColor(0xa6, 0xc9, 0xa0),
                              QColor(0x93, 0xb8, 0xdb)};

QRect cell_rect(int cell) {
    const int column = cell % kColumns;
    const int row = cell / kColumns;
    return {kMargin + column * (kCell + kGap), kMargin + kHeader + row * (kCell + kGap), kCell,
            kCell};
}

void draw_arrow(QPainter& painter, const QPointF& from, const QPointF& to, const QColor& color) {
    painter.setPen(QPen(color, 2.0, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(from, to);
    const QPointF delta = to - from;
    const float length = static_cast<float>(std::hypot(delta.x(), delta.y()));
    if (length < 0.5f) {
        return;
    }
    const QPointF direction = delta / length;
    const QPointF normal(-direction.y(), direction.x());
    const QPointF tip = to;
    const QPointF base = to - direction * 6.0;
    QPolygonF head;
    head << tip << base + normal * 3.5 << base - normal * 3.5;
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawPolygon(head);
}

void draw_rotation_icon(QPainter& painter, const QPointF& center, const QColor& color) {
    const QRectF circle(center.x() - 9.0, center.y() - 9.0, 18.0, 18.0);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(color, 2.0, Qt::SolidLine, Qt::RoundCap));
    painter.drawArc(circle, 50 * 16, 270 * 16);
    const float angle = 50.0f * 3.14159265358979323846f / 180.0f;
    const QPointF tip(center.x() + 9.0f * std::cos(angle), center.y() - 9.0f * std::sin(angle));
    const QPointF tangent(-std::sin(angle), -std::cos(angle));
    const QPointF normal(-tangent.y(), tangent.x());
    QPolygonF head;
    head << tip << tip - tangent * 6.0 + normal * 3.5 << tip - tangent * 6.0 - normal * 3.5;
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawPolygon(head);
}

QDoubleSpinBox* make_field(double minimum, double maximum, double step, int decimals) {
    auto* field = new QDoubleSpinBox();
    field->setObjectName(QStringLiteral("ViewportField"));
    field->setRange(minimum, maximum);
    field->setSingleStep(step);
    field->setDecimals(decimals);
    field->setKeyboardTracking(false);
    field->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    field->setMinimumWidth(64);
    field->setMaximumWidth(92);
    return field;
}

QLabel* make_caption(const QString& text) {
    auto* label = new QLabel(text);
    label->setObjectName(QStringLiteral("ViewportCaption"));
    return label;
}

QWidget* make_divider() {
    auto* divider = new QWidget();
    divider->setObjectName(QStringLiteral("ToolbarDivider"));
    divider->setFixedSize(1, 20);
    return divider;
}

} // namespace

ViewGizmo::ViewGizmo(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("ViewGizmo"));
    setFixedSize(kMargin * 2 + kColumns * kCell + (kColumns - 1) * kGap,
                 kMargin * 2 + kHeader + kRows * kCell + (kRows - 1) * kGap);
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);
    setToolTip(QStringLiteral(
        "Row 1 rotates, row 2 moves the camera or the selected bone\n"
        "Drag up or down to adjust"));
}

QSize ViewGizmo::sizeHint() const { return size(); }

void ViewGizmo::set_local_mode(bool local) {
    if (local_ == local) {
        return;
    }
    local_ = local;
    update();
}

int ViewGizmo::cell_at(const QPoint& point) const {
    if (point.y() < kMargin + kHeader) {
        return -1;
    }
    for (int cell = 0; cell < kColumns * kRows; ++cell) {
        if (cell_rect(cell).contains(point)) {
            return cell;
        }
    }
    return -1;
}

void ViewGizmo::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
#ifdef _WIN32
    if (testAttribute(Qt::WA_NativeWindow)) {
        const HWND hwnd = reinterpret_cast<HWND>(winId());
        SetWindowLongPtr(hwnd, GWL_STYLE, GetWindowLongPtr(hwnd, GWL_STYLE) | WS_CLIPSIBLINGS);
        const HRGN rgn = CreateRoundRectRgn(0, 0, width() + 1, height() + 1, 16, 16);
        SetWindowRgn(hwnd, rgn, TRUE);
    }
#endif
}

bool ViewGizmo::nativeEvent(const QByteArray& eventType, void* message, qintptr* result) {
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

void ViewGizmo::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(QColor(colors::outline), 1.0));
    painter.setBrush(QColor(colors::surface_high));
    painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 12.0, 12.0);

    painter.setPen(local_ ? QColor(colors::primary) : QColor(colors::on_surface_variant));
    QFont header_font = font();
    header_font.setPointSizeF(8.0);
    header_font.setWeight(QFont::DemiBold);
    painter.setFont(header_font);
    painter.drawText(QRect(kMargin, 0, width() - kMargin * 2, kHeader),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     local_ ? QStringLiteral("Local") : QStringLiteral("Global"));

    for (int cell = 0; cell < kColumns * kRows; ++cell) {
        const QRect box = cell_rect(cell);
        const bool active = cell == pressed_ || (pressed_ < 0 && cell == hovered_);
        painter.setPen(Qt::NoPen);
        painter.setBrush(active ? QColor(colors::primary_container)
                                : QColor(colors::surface_highest));
        painter.drawRoundedRect(QRectF(box), 8.0, 8.0);
        const QColor color = kAxisColor[cell % kColumns];
        const QPointF center(box.center().x() + 0.5, box.center().y() + 0.5);
        if (cell < kColumns) {
            draw_rotation_icon(painter, center, color);
        } else if (cell % kColumns == 0) {
            draw_arrow(painter, center - QPointF(9.0, 0.0), center + QPointF(9.0, 0.0), color);
        } else if (cell % kColumns == 1) {
            draw_arrow(painter, center + QPointF(0.0, 9.0), center - QPointF(0.0, 9.0), color);
        } else {
            draw_arrow(painter, center + QPointF(7.0, -7.0), center - QPointF(7.0, -7.0), color);
        }
    }
}

void ViewGizmo::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        return;
    }
    if (event->pos().y() < kMargin + kHeader) {
        local_ = !local_;
        update();
        emit localModeChanged(local_);
        event->accept();
        return;
    }
    const int cell = cell_at(event->pos());
    if (cell < 0) {
        return;
    }
    pressed_ = cell;
    drag_anchor_ = event->globalPosition().toPoint();
    dragging_ = true;
    grabMouse();
    update();
    event->accept();
}

void ViewGizmo::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        end_drag();
        pressed_ = -1;
        update();
        event->accept();
    }
}

void ViewGizmo::mouseMoveEvent(QMouseEvent* event) {
    if (dragging_) {
        if (!(event->buttons() & Qt::LeftButton)) {
            end_drag();
            pressed_ = -1;
            update();
            event->accept();
            return;
        }
        const int delta_y = event->globalPosition().toPoint().y() - drag_anchor_.y();
        if (delta_y != 0) {
            if (!cursor_hidden_) {
                setCursor(Qt::BlankCursor);
                cursor_hidden_ = true;
            }
            drag_by(delta_y, event->modifiers());
            QCursor::setPos(drag_anchor_);
        }
        event->accept();
        return;
    }
    const int cell = cell_at(event->pos());
    if (cell != hovered_) {
        hovered_ = cell;
        update();
    }
    event->accept();
}

void ViewGizmo::leaveEvent(QEvent* event) {
    if (!dragging_) {
        hovered_ = -1;
        update();
    }
    QWidget::leaveEvent(event);
}

void ViewGizmo::end_drag() {
    if (!dragging_) {
        return;
    }
    dragging_ = false;
    if (cursor_hidden_) {
        cursor_hidden_ = false;
        setCursor(Qt::PointingHandCursor);
    }
    releaseMouse();
}

void ViewGizmo::drag_by(int delta_y, Qt::KeyboardModifiers modifiers) {
    const float pixels = -static_cast<float>(delta_y);
    const int axis = pressed_ % kColumns;
    const bool coarse = modifiers.testFlag(Qt::ShiftModifier);
    const bool fine = modifiers.testFlag(Qt::ControlModifier);
    if (pressed_ < kColumns) {
        const float scale = coarse ? 5.0f : (fine ? 0.1f : 1.0f);
        emit rotateRequested(axis, pixels * kRotateDegreesPerPixel * scale);
    } else {
        const float scale = coarse ? 10.0f : (fine ? 0.1f : 1.0f);
        emit translateRequested(axis, pixels * kTranslateUnitsPerPixel * scale);
    }
}

ViewportHeader::ViewportHeader(StudioAppState& state, QWidget* parent)
    : QWidget(parent), state_(state) {
    setObjectName(QStringLiteral("ViewportHeader"));
    setFixedHeight(44);
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(12, 7, 12, 7);
    layout->setSpacing(6);

    title_ = new QLabel(QStringLiteral("Camera  ·  Light  ·  Accessory"));
    title_->setObjectName(QStringLiteral("ViewportTitle"));
    title_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    layout->addWidget(title_);

    auto* set_button = new QPushButton(QStringLiteral("Key"));
    set_button->setObjectName(QStringLiteral("ViewportChip"));
    set_button->setToolTip(QStringLiteral("Register the current target at this frame"));
    layout->addWidget(set_button);

    frame_ = new QSpinBox();
    frame_->setObjectName(QStringLiteral("ViewportField"));
    frame_->setRange(0, 100000);
    frame_->setAlignment(Qt::AlignCenter);
    frame_->setFixedWidth(76);
    frame_->setToolTip(QStringLiteral("Current frame"));
    layout->addWidget(frame_);
    layout->addWidget(make_divider());

    auto* shadow = new QPushButton(QStringLiteral("Ground shadow"));
    shadow->setObjectName(QStringLiteral("ViewportChip"));
    shadow->setCheckable(true);
    shadow->setChecked(state_.ground_shadow);
    shadow->setToolTip(QStringLiteral("Toggle the ground shadow"));
    layout->addWidget(shadow);

    auto* info = new QPushButton(QStringLiteral("Info"));
    info->setObjectName(QStringLiteral("ViewportChip"));
    info->setCheckable(true);
    info->setChecked(state_.info_display);
    info->setToolTip(QStringLiteral("Show frame statistics in the status bar"));
    layout->addWidget(info);

    auto* low_power = new QPushButton(QStringLiteral("Low power"));
    low_power->setObjectName(QStringLiteral("ViewportChip"));
    low_power->setCheckable(true);
    low_power->setChecked(state_.save_cpu_power);
    low_power->setToolTip(QStringLiteral("Reduce CPU work while editing"));
    layout->addWidget(low_power);

    auto* axis = new QPushButton(QStringLiteral("Grid"));
    axis->setObjectName(QStringLiteral("ViewportChip"));
    axis->setCheckable(true);
    axis->setChecked(state_.show_grid);
    axis->setToolTip(QStringLiteral("Coordinate grid"));
    layout->addWidget(axis);
    layout->addWidget(make_divider());

    auto* home = new QPushButton(QStringLiteral("Reset view"));
    home->setObjectName(QStringLiteral("ViewportChip"));
    home->setToolTip(QStringLiteral("Frame the model with the default camera"));
    layout->addWidget(home);

    connect(frame_, &QSpinBox::valueChanged, this,
            [this](int value) { emit frameRequested(value); });
    connect(set_button, &QPushButton::clicked, this, [this] {
        state_.register_keyframe(state_.selected_track_name(),
                                 static_cast<unsigned int>(state_.current_frame));
    });
    connect(shadow, &QPushButton::toggled, this,
            [this](bool checked) { state_.ground_shadow = checked; });
    connect(info, &QPushButton::toggled, this,
            [this](bool checked) { state_.info_display = checked; });
    connect(low_power, &QPushButton::toggled, this,
            [this](bool checked) { state_.save_cpu_power = checked; });
    connect(axis, &QPushButton::toggled, this,
            [this](bool checked) { state_.show_grid = checked; });
    connect(home, &QPushButton::clicked, this, [this] { emit resetViewRequested(); });
}

void ViewportHeader::set_title(const QString& title) { title_->setText(title); }

void ViewportHeader::set_frame(int frame) {
    if (frame_->value() == frame) {
        return;
    }
    frame_->blockSignals(true);
    frame_->setValue(frame);
    frame_->blockSignals(false);
}

ViewportFooter::ViewportFooter(StudioAppState& state, QWidget* parent)
    : QWidget(parent), state_(state) {
    setObjectName(QStringLiteral("ViewportFooter"));
    setFixedHeight(44);
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(12, 7, 12, 7);
    layout->setSpacing(4);

    mode_ = new QPushButton(QStringLiteral("To Model"));
    mode_->setObjectName(QStringLiteral("ViewportMode"));
    mode_->setFixedWidth(100);
    layout->addWidget(mode_);
    layout->addWidget(make_divider());

    const QStringList position_labels = {QStringLiteral("X"), QStringLiteral("Y"),
                                         QStringLiteral("Z")};
    for (int axis = 0; axis < 3; ++axis) {
        layout->addWidget(make_caption(position_labels[axis]));
        position_[axis] = make_field(-100000.0, 100000.0, 0.1, 2);
        position_[axis]->setToolTip(
            QStringLiteral("Position %1").arg(position_labels[axis]));
        layout->addWidget(position_[axis]);
    }
    layout->addWidget(make_divider());
    const QStringList angle_labels = {QStringLiteral("RX"), QStringLiteral("RY"),
                                      QStringLiteral("RZ")};
    for (int axis = 0; axis < 3; ++axis) {
        layout->addWidget(make_caption(angle_labels[axis]));
        angle_[axis] = make_field(-3600.0, 3600.0, 0.5, 1);
        angle_[axis]->setToolTip(
            QStringLiteral("Rotation %1").arg(QChar(static_cast<char>('X' + axis))));
        layout->addWidget(angle_[axis]);
    }
    layout->addWidget(make_divider());
    layout->addWidget(make_caption(QStringLiteral("Dist")));
    distance_ = make_field(0.0, 5000.0, 0.5, 2);
    distance_->setSpecialValueText(QStringLiteral("-"));
    distance_->setToolTip(QStringLiteral("Camera distance"));
    layout->addWidget(distance_);
    layout->addStretch(1);

    for (int axis = 0; axis < 3; ++axis) {
        connect(position_[axis], &QDoubleSpinBox::valueChanged, this,
                [this](double) { apply_camera_fields(); });
        connect(angle_[axis], &QDoubleSpinBox::valueChanged, this,
                [this](double) { apply_camera_fields(); });
    }
    connect(distance_, &QDoubleSpinBox::valueChanged, this,
            [this](double) { apply_camera_fields(); });
    connect(mode_, &QPushButton::clicked, this, [this] {
        if (state_.mode == EditorMode::Camera && !state_.model_loaded) {
            emit modelLoadRequested();
            return;
        }
        state_.mode = state_.mode == EditorMode::Camera ? EditorMode::Model : EditorMode::Camera;
        sync();
        emit modeChanged();
    });
    sync();
}

void ViewportFooter::sync() {
    if (!state_.model_loaded && state_.mode == EditorMode::Model) {
        state_.mode = EditorMode::Camera;
    }
    syncing_ = true;
    const bool camera = state_.mode == EditorMode::Camera;
    mode_->setToolTip(state_.model_loaded
                          ? QString()
                          : QStringLiteral("To model mode: load a model first"));
    mode_->setText(camera ? QStringLiteral("To Model") : QStringLiteral("To Camera"));
    if (camera) {
        for (int axis = 0; axis < 3; ++axis) {
            if (!position_[axis]->hasFocus()) {
                position_[axis]->setValue(state_.camera_pan[static_cast<std::size_t>(axis)]);
            }
        }
        if (!angle_[0]->hasFocus()) {
            angle_[0]->setValue(state_.camera_pitch * 180.0 / 3.14159265358979323846);
        }
        if (!angle_[1]->hasFocus()) {
            angle_[1]->setValue(state_.camera_yaw * 180.0 / 3.14159265358979323846);
        }
        if (!angle_[2]->hasFocus()) {
            angle_[2]->setValue(state_.camera_roll * 180.0 / 3.14159265358979323846);
        }
        if (!distance_->hasFocus()) {
            distance_->setValue(state_.camera_distance);
        }
        distance_->setEnabled(true);
    } else {
        for (int axis = 0; axis < 3; ++axis) {
            if (!position_[axis]->hasFocus()) {
                position_[axis]->setValue(
                    state_.bone_pos_override[static_cast<std::size_t>(axis)]);
            }
            if (!angle_[axis]->hasFocus()) {
                angle_[axis]->setValue(
                    state_.bone_rot_euler_deg[static_cast<std::size_t>(axis)]);
            }
        }
        distance_->setValue(0.0);
        distance_->setEnabled(false);
    }
    syncing_ = false;
}

void ViewportFooter::apply_camera_fields() {
    if (syncing_) {
        return;
    }
    if (state_.mode == EditorMode::Camera) {
        for (int axis = 0; axis < 3; ++axis) {
            state_.camera_pan[static_cast<std::size_t>(axis)] =
                static_cast<float>(position_[axis]->value());
        }
        state_.camera_pitch =
            static_cast<float>(angle_[0]->value() * 3.14159265358979323846 / 180.0);
        state_.camera_yaw =
            static_cast<float>(angle_[1]->value() * 3.14159265358979323846 / 180.0);
        state_.camera_roll =
            static_cast<float>(angle_[2]->value() * 3.14159265358979323846 / 180.0);
        state_.camera_distance = static_cast<float>(distance_->value());
    } else {
        apply_bone_fields();
    }
    emit valueEdited();
}

void ViewportFooter::apply_bone_fields() {
    for (int axis = 0; axis < 3; ++axis) {
        state_.bone_pos_override[static_cast<std::size_t>(axis)] =
            static_cast<float>(position_[axis]->value());
        state_.bone_rot_euler_deg[static_cast<std::size_t>(axis)] =
            static_cast<float>(angle_[axis]->value());
    }
    state_.bone_transform_dirty = true;
}

} // namespace hikari::studio
