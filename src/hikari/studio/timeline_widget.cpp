#include "hikari/studio/timeline_widget.hpp"

#include <QKeyEvent>
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
constexpr int kRightMargin = 20;
constexpr int kGridPadding = 6;
constexpr int kMinVisibleFrames = 40;
constexpr float kFrameSpacing = 14.0f;
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
    refresh_layout();
}

QSize TimelineWidget::sizeHint() const {
    return {300, 520};
}

int TimelineWidget::content_width() const {
    const int frames = std::max(state_.max_timeline_frame, kMinVisibleFrames);
    const float spacing = kFrameSpacing * std::max(state_.timeline_zoom, 0.2f);
    return kHeaderWidth + kGridPadding + static_cast<int>(frames * spacing) + kRightMargin;
}

int TimelineWidget::max_scroll() const {
    return std::max(0, content_width() - width());
}

void TimelineWidget::refresh_layout() {
    const int rows = channels().size();
    const int frames = std::max(state_.max_timeline_frame, kMinVisibleFrames);
    const float spacing = kFrameSpacing * std::max(state_.timeline_zoom, 0.2f);
    const bool changed =
        frames != layout_frames_ || rows != layout_rows_ || spacing != layout_spacing_;
    layout_frames_ = frames;
    layout_rows_ = rows;
    layout_spacing_ = spacing;
    setMinimumHeight(kRulerHeight + rows * kRowHeight + 4);
    if (changed) {
        updateGeometry();
    }
    update_scroll_range();
}

void TimelineWidget::set_scroll(int offset) {
    const int clamped = std::clamp(offset, 0, max_scroll());
    if (static_cast<int>(state_.timeline_scroll) == clamped) {
        return;
    }
    state_.timeline_scroll = static_cast<float>(clamped);
    update();
}

void TimelineWidget::update_scroll_range() {
    const int maximum = max_scroll();
    const int current = static_cast<int>(state_.timeline_scroll);
    const int clamped = std::clamp(current, 0, maximum);
    state_.timeline_scroll = static_cast<float>(clamped);
    if (maximum != scroll_max_) {
        scroll_max_ = maximum;
        emit scrollRangeChanged(maximum);
    }
    if (clamped != current) {
        emit scrollChanged(clamped);
    }
}

QStringList TimelineWidget::channels() const {
    QStringList list;
    if (state_.mode == EditorMode::Camera) {
        list << "camera" << "light" << "shadow" << "gravity" << "accessory";
        return list;
    }
    list << "view cnt" << "Disp/IK/OP" << "Facial";
    list << state_.model_bone_names;
    return list;
}

void TimelineWidget::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.fillRect(rect(), kBase);
    const QStringList channel_list = channels();
    const float spacing = kFrameSpacing * std::max(state_.timeline_zoom, 0.2f);
    const int visible_frames = std::max(state_.max_timeline_frame, kMinVisibleFrames);
    const int offset = static_cast<int>(state_.timeline_scroll);
    const int content_height = kRulerHeight + static_cast<int>(channel_list.size()) * kRowHeight;
    const QRect grid_rect(kHeaderWidth, 0, std::max(0, width() - kHeaderWidth), height());

    painter.fillRect(QRect(0, 0, width(), kRulerHeight), kPanel);
    painter.setPen(QPen(kLine, 1));
    painter.drawLine(0, kRulerHeight, width(), kRulerHeight);

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
    }

    painter.setClipRect(grid_rect);
    for (int frame = 0; frame <= visible_frames; frame += 5) {
        const int x =
            kHeaderWidth + kGridPadding + static_cast<int>(frame * spacing) - offset;
        if (x > width()) {
            break;
        }
        if (x < kHeaderWidth - 20) {
            continue;
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
        const auto track = state_.track_keyframes.find(channel_list[row]);
        if (track == state_.track_keyframes.end()) {
            continue;
        }
        for (unsigned int frame : track->second) {
            const int x =
                kHeaderWidth + kGridPadding + static_cast<int>(frame * spacing) - offset;
            if (x < kHeaderWidth || x > width()) {
                continue;
            }
            const QPointF center(x, y + kRowHeight / 2.0);
            const bool key_selected =
                state_.selected_keyframes.count({channel_list[row], frame}) != 0;
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

    const int playhead_x = kHeaderWidth + kGridPadding +
                           static_cast<int>(state_.current_frame * spacing) - offset;
    if (playhead_x >= kHeaderWidth && playhead_x <= width()) {
        painter.setPen(QPen(kRed, 1.8));
        painter.drawLine(playhead_x, 0, playhead_x, std::min(height(), content_height));
        painter.setBrush(kRed);
        painter.drawEllipse(QPointF(playhead_x, 3.0), 4.0, 4.0);
    }
    painter.setClipping(false);

    painter.setPen(QPen(kLine, 1));
    painter.drawLine(kHeaderWidth, 0, kHeaderWidth, height());
}

std::optional<TimelineKeyframeHit> TimelineWidget::keyframe_at(const QPoint& position) const {
    if (position.x() < kHeaderWidth || position.y() < kRulerHeight) {
        return std::nullopt;
    }
    const int row = (position.y() - kRulerHeight) / kRowHeight;
    const QStringList channel_list = channels();
    if (row < 0 || row >= channel_list.size()) {
        return std::nullopt;
    }
    const int row_y = kRulerHeight + row * kRowHeight + kRowHeight / 2;
    if (std::abs(position.y() - row_y) > 6) {
        return std::nullopt;
    }
    const auto track = state_.track_keyframes.find(channel_list[row]);
    if (track == state_.track_keyframes.end()) {
        return std::nullopt;
    }
    const float spacing = kFrameSpacing * std::max(state_.timeline_zoom, 0.2f);
    const int offset = static_cast<int>(state_.timeline_scroll);
    for (unsigned int frame : track->second) {
        const int x = kHeaderWidth + kGridPadding + static_cast<int>(frame * spacing) - offset;
        if (x < kHeaderWidth || x > width()) {
            continue;
        }
        if (std::abs(position.x() - x) <= 6) {
            return TimelineKeyframeHit{channel_list[row], frame, row};
        }
    }
    return std::nullopt;
}

void TimelineWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        return;
    }
    setFocus();
    const QPoint position = event->pos();
    if (position.x() < kHeaderWidth) {
        if (position.y() >= kRulerHeight && state_.mode == EditorMode::Model) {
            const int row = (position.y() - kRulerHeight) / kRowHeight;
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
        event->accept();
        return;
    }

    if (const auto hit = keyframe_at(position)) {
        const auto key = std::make_pair(hit->track, hit->frame);
        if (event->modifiers() & Qt::ControlModifier) {
            const auto existing = state_.selected_keyframes.find(key);
            if (existing != state_.selected_keyframes.end()) {
                state_.selected_keyframes.erase(existing);
            } else {
                state_.selected_keyframes.insert(key);
            }
        } else {
            state_.selected_keyframes.clear();
            state_.selected_keyframes.insert(key);
        }
        state_.current_frame = static_cast<float>(hit->frame);
        state_.jump_frame_input = QString::number(hit->frame);
        if (hit->row >= 3 && state_.mode == EditorMode::Model) {
            const int bone = hit->row - 3;
            if (bone < state_.model_bone_names.size()) {
                state_.selected_bone_idx = bone;
                state_.bone_transform_dirty = false;
                emit boneSelected(bone);
            }
        }
        state_.set_status(QStringLiteral("Selected keyframe %1 at frame %2")
                              .arg(hit->track)
                              .arg(hit->frame));
        dragging_ = false;
        update();
        event->accept();
        return;
    }

    state_.selected_keyframes.clear();
    dragging_ = true;
    scrub_to(position);
    update();
    event->accept();
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
    const float spacing = kFrameSpacing * std::max(state_.timeline_zoom, 0.2f);
    if (position.x() < kHeaderWidth) {
        return;
    }
    const float frame = (static_cast<float>(position.x() - kHeaderWidth - kGridPadding) +
                         state_.timeline_scroll) /
                        spacing;
    state_.current_frame = std::clamp(frame, 0.0f, static_cast<float>(state_.max_timeline_frame));
    state_.jump_frame_input = QString::number(static_cast<int>(state_.current_frame));
}

void TimelineWidget::wheelEvent(QWheelEvent* event) {
    if (event->modifiers() & Qt::ControlModifier) {
        const float old_spacing = kFrameSpacing * std::max(state_.timeline_zoom, 0.2f);
        const int anchor_x = static_cast<int>(event->position().x());
        const float anchor_frame =
            anchor_x > kHeaderWidth
                ? (static_cast<float>(anchor_x - kHeaderWidth - kGridPadding) +
                   state_.timeline_scroll) /
                      old_spacing
                : 0.0f;
        state_.timeline_zoom =
            std::clamp(state_.timeline_zoom + event->angleDelta().y() / 1200.0f, 0.5f, 3.0f);
        const float new_spacing = kFrameSpacing * std::max(state_.timeline_zoom, 0.2f);
        state_.timeline_scroll =
            static_cast<float>(anchor_x - kHeaderWidth - kGridPadding) > 0.0f
                ? anchor_frame * new_spacing -
                      static_cast<float>(anchor_x - kHeaderWidth - kGridPadding)
                : 0.0f;
        refresh_layout();
        emit scrollChanged(static_cast<int>(state_.timeline_scroll));
        update();
        event->accept();
        return;
    }
    if ((event->modifiers() & Qt::ShiftModifier) || event->angleDelta().x() != 0) {
        const int step = std::max(
            1, static_cast<int>(kFrameSpacing * std::max(state_.timeline_zoom, 0.2f) * 2.0f));
        const int notches = event->angleDelta().x() / 120 - event->angleDelta().y() / 120;
        set_scroll(static_cast<int>(state_.timeline_scroll) + notches * step);
        event->accept();
        return;
    }
    QWidget::wheelEvent(event);
}

void TimelineWidget::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Delete) {
        emit deleteSelectedRequested();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

void TimelineWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    update_scroll_range();
}

} // namespace hikari::studio
