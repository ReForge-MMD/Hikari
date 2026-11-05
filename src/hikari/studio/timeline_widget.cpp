#include "hikari/studio/timeline_widget.hpp"

#include <QKeyEvent>
#include <QKeySequence>
#include <QMouseEvent>
#include <QPainter>
#include <QSet>
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
constexpr int kPlayheadMargin = 18;
constexpr float kFrameSpacing = 14.0f;
const QColor kBase(24, 27, 34);
const QColor kPanel(32, 37, 46);
const QColor kLine(56, 63, 76);
const QColor kSnow(220, 224, 230);
const QColor kCyan(126, 200, 227);
const QColor kYellow(214, 190, 104);
const QColor kRed(196, 92, 92);

bool reserved_channel(const QString& name) {
    return name == QStringLiteral("view cnt") || name == QStringLiteral("Disp/IK/OP") ||
           name == QStringLiteral("Facial");
}
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
    const auto& row_list = rows();
    const int row_count = static_cast<int>(row_list.size());
    const int frames = std::max(state_.max_timeline_frame, kMinVisibleFrames);
    const float spacing = kFrameSpacing * std::max(state_.timeline_zoom, 0.2f);
    const bool changed =
        frames != layout_frames_ || row_count != layout_rows_ || spacing != layout_spacing_;
    layout_frames_ = frames;
    layout_rows_ = row_count;
    layout_spacing_ = spacing;
    setMinimumHeight(kRulerHeight + row_count * kRowHeight + 4);
    if (changed) {
        updateGeometry();
    }
    update_scroll_range();
}

void TimelineWidget::set_follow_playhead(bool follow) {
    follow_playhead_ = follow;
}

void TimelineWidget::set_scroll(int offset) {
    const int clamped = std::clamp(offset, 0, max_scroll());
    if (static_cast<int>(state_.timeline_scroll) == clamped) {
        return;
    }
    state_.timeline_scroll = static_cast<float>(clamped);
    update();
}

void TimelineWidget::ensure_playhead_visible() {
    if (!follow_playhead_) {
        return;
    }
    const float spacing = kFrameSpacing * std::max(state_.timeline_zoom, 0.2f);
    const float content_x =
        static_cast<float>(kHeaderWidth + kGridPadding) + state_.current_frame * spacing;
    const int offset = static_cast<int>(state_.timeline_scroll);
    const float playhead_x = content_x - static_cast<float>(offset);
    const float left = static_cast<float>(kHeaderWidth + kPlayheadMargin);
    const float right = std::max(left, static_cast<float>(width() - kPlayheadMargin));
    int target = offset;
    if (playhead_x < left) {
        target = static_cast<int>(std::floor(content_x - left));
    } else if (playhead_x > right) {
        target = static_cast<int>(std::ceil(content_x - right));
    }
    const int clamped = std::clamp(target, 0, max_scroll());
    if (clamped != offset) {
        state_.timeline_scroll = static_cast<float>(clamped);
        emit scrollChanged(clamped);
    }
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

const std::vector<TimelineRow>& TimelineWidget::rows() const {
    const int bone_count = state_.model_bone_names.size();
    const int mode = static_cast<int>(state_.mode);
    if (rows_mode_ == mode && rows_bone_count_ == bone_count &&
        rows_revision_ == state_.keyframe_revision && !rows_cache_.empty()) {
        return rows_cache_;
    }
    rebuild_rows();
    rows_mode_ = mode;
    rows_bone_count_ = bone_count;
    rows_revision_ = state_.keyframe_revision;
    return rows_cache_;
}

void TimelineWidget::rebuild_rows() const {
    rows_cache_.clear();
    if (state_.mode == EditorMode::Camera) {
        for (const QString& name :
             {QStringLiteral("camera"), QStringLiteral("light"), QStringLiteral("shadow"),
              QStringLiteral("gravity"), QStringLiteral("accessory")}) {
            rows_cache_.push_back({name, TimelineRowKind::Channel, 0});
        }
        return;
    }

    rows_cache_.push_back({QStringLiteral("view cnt"), TimelineRowKind::Channel, 0});
    rows_cache_.push_back({QStringLiteral("Disp/IK/OP"), TimelineRowKind::Channel, 0});
    rows_cache_.push_back({QStringLiteral("Facial"), TimelineRowKind::Channel, 0});

    QSet<QString> bone_names;
    for (int i = 0; i < state_.model_bone_names.size(); ++i) {
        const QString& name = state_.model_bone_names[i];
        bone_names.insert(name);
        rows_cache_.push_back({name, TimelineRowKind::Bone, i});
    }

    QSet<QString> morph_names;
    const auto has_keys = [this](const QString& name) {
        const auto track = state_.track_keyframes.find(name);
        return track != state_.track_keyframes.end() && !track->second.empty();
    };
    for (const auto& category : state_.category_morph_names) {
        for (const QString& name : category) {
            if (has_keys(name) && !morph_names.contains(name)) {
                morph_names.insert(name);
                rows_cache_.push_back({name, TimelineRowKind::Morph, 0});
            }
        }
    }
    for (const auto& [track, keys] : state_.track_keyframes) {
        if (keys.empty() || track.isEmpty() || reserved_channel(track) ||
            bone_names.contains(track) || morph_names.contains(track)) {
            continue;
        }
        if (track == QStringLiteral("camera") || track == QStringLiteral("light") ||
            track == QStringLiteral("shadow") || track == QStringLiteral("gravity") ||
            track == QStringLiteral("accessory")) {
            continue;
        }
        morph_names.insert(track);
        rows_cache_.push_back({track, TimelineRowKind::Morph, 0});
    }
}

void TimelineWidget::paintEvent(QPaintEvent*) {
    ensure_playhead_visible();
    QPainter painter(this);
    painter.fillRect(rect(), kBase);
    const auto& row_list = rows();
    const float spacing = kFrameSpacing * std::max(state_.timeline_zoom, 0.2f);
    const int visible_frames = std::max(state_.max_timeline_frame, kMinVisibleFrames);
    const int offset = static_cast<int>(state_.timeline_scroll);
    const int content_height = kRulerHeight + static_cast<int>(row_list.size()) * kRowHeight;
    const QRect grid_rect(kHeaderWidth, 0, std::max(0, width() - kHeaderWidth), height());

    painter.fillRect(QRect(0, 0, width(), kRulerHeight), kPanel);
    painter.setPen(QPen(kLine, 1));
    painter.drawLine(0, kRulerHeight, width(), kRulerHeight);

    for (int row = 0; row < static_cast<int>(row_list.size()); ++row) {
        const int y = kRulerHeight + row * kRowHeight;
        if (y > height()) {
            break;
        }
        const TimelineRow& descriptor = row_list[static_cast<std::size_t>(row)];
        bool selected = false;
        if (descriptor.kind == TimelineRowKind::Bone && state_.selected_bone_idx) {
            selected = *state_.selected_bone_idx == descriptor.index;
        }
        painter.fillRect(QRect(0, y, width(), kRowHeight),
                         selected ? QColor(45, 62, 72) : (row % 2 ? QColor(29, 33, 41) : kBase));
        painter.fillRect(QRect(0, y, kHeaderWidth, kRowHeight), selected ? kLine : kPanel);
        painter.setPen(kLine);
        painter.drawLine(0, y + kRowHeight - 1, width(), y + kRowHeight - 1);
        painter.setPen(selected ? kCyan : kSnow);
        painter.drawText(QRect(6, y, kHeaderWidth - 8, kRowHeight), Qt::AlignVCenter | Qt::AlignLeft,
                         descriptor.name);
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

    for (int row = 0; row < static_cast<int>(row_list.size()); ++row) {
        const int y = kRulerHeight + row * kRowHeight;
        if (y > height()) {
            break;
        }
        const TimelineRow& descriptor = row_list[static_cast<std::size_t>(row)];
        const auto track = state_.track_keyframes.find(descriptor.name);
        if (track == state_.track_keyframes.end()) {
            continue;
        }
        for (const auto& entry : track->second) {
            const unsigned int frame = entry.first;
            const int x =
                kHeaderWidth + kGridPadding + static_cast<int>(frame * spacing) - offset;
            if (x < kHeaderWidth || x > width()) {
                continue;
            }
            const QPointF center(x, y + kRowHeight / 2.0);
            const bool key_selected =
                state_.selected_keyframes.count({descriptor.name, frame}) != 0;
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
    const auto& row_list = rows();
    if (row < 0 || row >= static_cast<int>(row_list.size())) {
        return std::nullopt;
    }
    const int row_y = kRulerHeight + row * kRowHeight + kRowHeight / 2;
    if (std::abs(position.y() - row_y) > 6) {
        return std::nullopt;
    }
    const TimelineRow& descriptor = row_list[static_cast<std::size_t>(row)];
    const auto track = state_.track_keyframes.find(descriptor.name);
    if (track == state_.track_keyframes.end()) {
        return std::nullopt;
    }
    const float spacing = kFrameSpacing * std::max(state_.timeline_zoom, 0.2f);
    const int offset = static_cast<int>(state_.timeline_scroll);
    for (const auto& entry : track->second) {
        const unsigned int frame = entry.first;
        const int x = kHeaderWidth + kGridPadding + static_cast<int>(frame * spacing) - offset;
        if (x < kHeaderWidth || x > width()) {
            continue;
        }
        if (std::abs(position.x() - x) <= 6) {
            return TimelineKeyframeHit{descriptor.name, frame, row};
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
        if (position.y() >= kRulerHeight) {
            const int row = (position.y() - kRulerHeight) / kRowHeight;
            const auto& row_list = rows();
            if (row >= 0 && row < static_cast<int>(row_list.size())) {
                const TimelineRow& descriptor = row_list[static_cast<std::size_t>(row)];
                if (descriptor.kind == TimelineRowKind::Bone) {
                    state_.selected_bone_idx = descriptor.index;
                    state_.bone_transform_dirty = false;
                    state_.set_status(QStringLiteral("Selected bone: %1").arg(descriptor.name));
                    emit boneSelected(descriptor.index);
                    update();
                } else if (descriptor.kind == TimelineRowKind::Morph) {
                    state_.set_status(QStringLiteral("Selected morph track: %1").arg(descriptor.name));
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
        const auto& row_list = rows();
        if (hit->row >= 0 && hit->row < static_cast<int>(row_list.size())) {
            const TimelineRow& descriptor = row_list[static_cast<std::size_t>(hit->row)];
            if (descriptor.kind == TimelineRowKind::Bone) {
                state_.selected_bone_idx = descriptor.index;
                state_.bone_transform_dirty = false;
                emit boneSelected(descriptor.index);
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
    if (event->matches(QKeySequence::Copy)) {
        emit copyRequested();
        event->accept();
        return;
    }
    if (event->matches(QKeySequence::Paste)) {
        emit pasteRequested();
        event->accept();
        return;
    }
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
