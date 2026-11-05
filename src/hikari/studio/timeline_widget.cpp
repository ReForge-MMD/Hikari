#include "hikari/studio/timeline_widget.hpp"

#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

#include "hikari/studio/app_state.hpp"

namespace hikari::studio {
namespace {
constexpr int kRowHeight = 20;
constexpr int kHeaderWidth = 96;
constexpr int kRulerHeight = 22;
const QColor kBase(24, 27, 34);
const QColor kPanel(32, 37, 46);
const QColor kLine(56, 63, 76);
const QColor kSnow(220, 224, 230);
const QColor kCyan(126, 200, 227);
const QColor kYellow(214, 190, 104);
const QColor kRed(196, 92, 92);
} // namespace

TimelineWidget::TimelineWidget(StudioAppState& state, QWidget* parent)
    : QWidget(parent), state_(state) {
    setMinimumWidth(260);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
}

QSize TimelineWidget::sizeHint() const {
    return {300, 520};
}

void TimelineWidget::refresh_layout() {
    const int rows = channels().size();
    setMinimumHeight(kRulerHeight + rows * kRowHeight + 4);
    updateGeometry();
}

QStringList TimelineWidget::channels() const {
    QStringList list;
    if (state_.mode == EditorMode::Camera) {
        list << "camera" << "light" << "shadow" << "gravity" << "accessory";
        return list;
    }
    list << "view cnt" << "Disp/IK/OP" << "Facial";
    if (state_.model_bone_names.isEmpty()) {
        list << "center" << "Root" << "Waist" << "Spine" << "Neck" << "Head";
    } else {
        list << state_.model_bone_names;
    }
    return list;
}

void TimelineWidget::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.fillRect(rect(), kBase);
    const QStringList channel_list = channels();
    const float spacing = 14.0f * std::max(state_.timeline_zoom, 0.2f);
    const int visible_frames = std::max(state_.max_timeline_frame, 40);
    const int content_width = kHeaderWidth + static_cast<int>(visible_frames * spacing) + 20;
    const int content_height = kRulerHeight + static_cast<int>(channel_list.size()) * kRowHeight;

    painter.fillRect(QRect(0, 0, width(), kRulerHeight), kPanel);
    painter.setPen(QPen(kLine, 1));
    painter.drawLine(0, kRulerHeight, width(), kRulerHeight);

    for (int frame = 0; frame <= visible_frames; frame += 5) {
        const int x = kHeaderWidth + static_cast<int>(frame * spacing);
        if (x > width()) {
            break;
        }
        painter.setPen(QPen(QColor(kLine.red(), kLine.green(), kLine.blue(), 120), 1));
        painter.drawLine(x, 0, x, height());
        painter.setPen(kSnow);
        painter.drawText(QRect(x - 20, 2, 40, 18), Qt::AlignCenter, QString::number(frame));
    }

    for (int row = 0; row < channel_list.size(); ++row) {
        const int y = kRulerHeight + row * kRowHeight;
        if (y > height()) {
            break;
        }
        const QString name = channel_list[row];
        bool selected = false;
        if (state_.mode == EditorMode::Model && row >= 3 && state_.selected_bone_idx) {
            selected = *state_.selected_bone_idx == row - 3;
        }
        painter.fillRect(QRect(0, y, width(), kRowHeight),
                         selected ? QColor(45, 62, 72) : (row % 2 ? QColor(29, 33, 41) : kBase));
        painter.fillRect(QRect(0, y, kHeaderWidth, kRowHeight), selected ? kLine : kPanel);
        painter.setPen(kLine);
        painter.drawLine(0, y + kRowHeight - 1, width(), y + kRowHeight - 1);
        painter.setPen(selected ? kCyan : kSnow);
        painter.drawText(QRect(6, y, kHeaderWidth - 8, kRowHeight), Qt::AlignVCenter | Qt::AlignLeft,
                         name);

        const auto track = state_.track_keyframes.find(name);
        if (track != state_.track_keyframes.end()) {
            for (unsigned int frame : track->second) {
                const int x = kHeaderWidth + static_cast<int>(frame * spacing);
                if (x > width()) {
                    continue;
                }
                const QPointF center(x, y + kRowHeight / 2.0);
                const bool key_selected =
                    state_.selected_keyframes.count({name, frame}) != 0;
                QPolygonF diamond;
                diamond << QPointF(center.x(), center.y() - 4.5)
                        << QPointF(center.x() + 4.5, center.y())
                        << QPointF(center.x(), center.y() + 4.5)
                        << QPointF(center.x() - 4.5, center.y());
                painter.setPen(QPen(kBase, 1));
                painter.setBrush(key_selected ? kCyan : kYellow);
                painter.drawPolygon(diamond);
            }
        }
    }

    const int playhead_x = kHeaderWidth + static_cast<int>(state_.current_frame * spacing);
    if (playhead_x >= kHeaderWidth && playhead_x <= width()) {
        painter.setPen(QPen(kRed, 1.8));
        painter.drawLine(playhead_x, 0, playhead_x, std::min(height(), content_height));
        painter.setBrush(kRed);
        painter.drawEllipse(QPointF(playhead_x, 3.0), 4.0, 4.0);
    }
    (void)content_width;
}

void TimelineWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        return;
    }
    const int x = event->pos().x();
    const int y = event->pos().y();
    if (x < kHeaderWidth) {
        if (y >= kRulerHeight && state_.mode == EditorMode::Model) {
            const int row = (y - kRulerHeight) / kRowHeight;
            if (row >= 3) {
                const int bone = row - 3;
                if (bone < state_.model_bone_names.size()) {
                    state_.selected_bone_idx = bone;
                    state_.bone_transform_dirty = false;
                    state_.set_status(QStringLiteral("Selected bone: %1")
                                          .arg(state_.model_bone_names[bone]));
                    emit boneSelected(bone);
                    update();
                }
            }
        }
        return;
    }
    dragging_ = true;
    scrub_to(event->pos());
    update();
}

void TimelineWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        dragging_ = false;
    }
}

void TimelineWidget::mouseMoveEvent(QMouseEvent* event) {
    if (!dragging_) {
        return;
    }
    scrub_to(event->pos());
    update();
}

void TimelineWidget::scrub_to(const QPoint& position) {
    const float spacing = 14.0f * std::max(state_.timeline_zoom, 0.2f);
    if (position.x() < kHeaderWidth) {
        return;
    }
    const float frame = (position.x() - kHeaderWidth) / spacing;
    state_.current_frame = std::clamp(frame, 0.0f, static_cast<float>(state_.max_timeline_frame));
    state_.jump_frame_input = QString::number(static_cast<int>(state_.current_frame));
}

void TimelineWidget::wheelEvent(QWheelEvent* event) {
    if (event->modifiers() & Qt::ControlModifier) {
        state_.timeline_zoom =
            std::clamp(state_.timeline_zoom + event->angleDelta().y() / 1200.0f, 0.5f, 3.0f);
        update();
        event->accept();
        return;
    }
    QWidget::wheelEvent(event);
}

} // namespace hikari::studio
