#include "hikari/studio/timeline_widget.hpp"

#include <QFontMetrics>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMouseEvent>
#include <QPainter>
#include <QSet>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

#include "hikari/studio/theme.hpp"
#include "hikari/studio/localization.hpp"

namespace hikari::studio {
namespace {
constexpr int kRowHeight = 22;
constexpr int kHeaderWidth = 148;
constexpr int kRulerHeight = 26;
constexpr int kAudioStripHeight = 34;
constexpr int kRightMargin = 24;
constexpr int kGridPadding = 8;
constexpr int kMinVisibleFrames = 40;
constexpr int kPlayheadMargin = 18;
constexpr float kFrameSpacing = 14.0f;
const QColor kBase(colors::surface_lowest);
const QColor kPanel(colors::surface_container);
const QColor kLine(colors::outline_variant);
const QColor kSnow(colors::on_surface);
const QColor kMuted(colors::on_surface_muted);
const QColor kCyan(colors::primary);
const QColor kKey(0x7e, 0x9d, 0xb4);
const QColor kMotionKey(0x55, 0x6b, 0x7d);
const QColor kKeySelected(colors::tertiary);
const QColor kMotionSelected(colors::primary);
const QColor kSelection(colors::primary_container);
const QColor kPlayhead(colors::on_surface);
const QColor kAudioWave(0x6f, 0x9f, 0xc9, 210);

int rows_top(const StudioAppState& state) {
    return kRulerHeight + (state.audio_peaks.empty() ? 0 : kAudioStripHeight);
}

bool reserved_channel(const QString& name) {
    return name == QStringLiteral("view cnt") || name == QStringLiteral("Disp/IK/OP") ||
           name == QStringLiteral("Facial");
}

bool channel_track(const QString& name) {
    return name == QStringLiteral("camera") || name == QStringLiteral("light") ||
           name == QStringLiteral("shadow") || name == QStringLiteral("gravity") ||
           name == QStringLiteral("accessory") || name == QStringLiteral("model scale") ||
           name == QStringLiteral("view cnt") || name == QStringLiteral("Facial") ||
           name == QStringLiteral("Disp/IK/OP");
}

QPolygonF diamond_at(const QPointF& center, float radius) {
    QPolygonF diamond;
    diamond << QPointF(center.x(), center.y() - radius)
            << QPointF(center.x() + radius, center.y())
            << QPointF(center.x(), center.y() + radius)
            << QPointF(center.x() - radius, center.y());
    return diamond;
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
    setMinimumHeight(rows_top(state_) + row_count * kRowHeight + 4);
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
    rows_cache_.push_back({QStringLiteral("model scale"), TimelineRowKind::Channel, 0});
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
        if (track != state_.track_keyframes.end() && !track->second.empty()) {
            return true;
        }
        const auto motion = state_.motion_keyframes.find(name);
        return motion != state_.motion_keyframes.end() && !motion.value().isEmpty();
    };
    for (const auto& category : state_.category_morph_names) {
        for (const QString& name : category) {
            if (has_keys(name) && !morph_names.contains(name)) {
                morph_names.insert(name);
                rows_cache_.push_back({name, TimelineRowKind::Morph, 0});
            }
        }
    }
    const auto append_extra_row = [&](const QString& track) {
        if (track.isEmpty() || reserved_channel(track) || bone_names.contains(track) ||
            morph_names.contains(track)) {
            return;
        }
        if (track == QStringLiteral("camera") || track == QStringLiteral("light") ||
            track == QStringLiteral("shadow") || track == QStringLiteral("gravity") ||
            track == QStringLiteral("accessory")) {
            return;
        }
        morph_names.insert(track);
        rows_cache_.push_back({track, TimelineRowKind::Morph, 0});
    };
    for (const auto& [track, keys] : state_.track_keyframes) {
        if (!keys.empty()) {
            append_extra_row(track);
        }
    }
    for (auto it = state_.motion_keyframes.begin(); it != state_.motion_keyframes.end(); ++it) {
        if (!it.value().isEmpty()) {
            append_extra_row(it.key());
        }
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
    const int content_height = rows_top(state_) + static_cast<int>(row_list.size()) * kRowHeight;
    const QRect grid_rect(kHeaderWidth, 0, std::max(0, width() - kHeaderWidth), height());

    QFont row_font = font();
    row_font.setPixelSize(12);
    QFont ruler_font = font();
    ruler_font.setFamilies({QStringLiteral("Cascadia Mono"), QStringLiteral("Consolas")});
    ruler_font.setPixelSize(11);

    painter.fillRect(QRect(0, 0, width(), kRulerHeight), kPanel);
    painter.setPen(QPen(kLine, 1));
    painter.drawLine(0, kRulerHeight, width(), kRulerHeight);

    const int strip_top = kRulerHeight;
    const int strip_height = rows_top(state_) - kRulerHeight;
    if (strip_height > 0) {
        const int strip_bottom = strip_top + strip_height;
        painter.fillRect(QRect(0, strip_top, width(), strip_height), kPanel);
        painter.setPen(QPen(kLine, 1));
        painter.drawLine(0, strip_bottom, width(), strip_bottom);

        painter.setFont(row_font);
        painter.setPen(kMuted);
        const QString name =
            state_.audio_name.isEmpty()
                ? localization::text(QStringLiteral("Audio"))
                : QFontMetrics(row_font).elidedText(state_.audio_name, Qt::ElideRight,
                                                    kHeaderWidth - 24);
        painter.drawText(QRect(12, strip_top, kHeaderWidth - 20, strip_height),
                         Qt::AlignVCenter | Qt::AlignLeft, name);

        if (!state_.audio_peaks.empty() && state_.audio_peaks_per_second > 0.0) {
            const int center = strip_top + strip_height / 2;
            const int half = std::max(1, strip_height / 2 - 4);
            const QRect strip_grid(kHeaderWidth, strip_top,
                                   std::max(0, width() - kHeaderWidth), strip_height);
            painter.setClipRect(strip_grid);
            painter.setPen(QPen(kAudioWave, 1));
            for (int x = kHeaderWidth; x < width(); ++x) {
                const float frame =
                    (static_cast<float>(x - kHeaderWidth - kGridPadding) +
                     state_.timeline_scroll) /
                    spacing;
                const double seconds =
                    (static_cast<double>(frame) - state_.audio_offset_frames) / 30.0;
                if (seconds < 0.0 || seconds >= state_.audio_duration) {
                    continue;
                }
                const auto index =
                    static_cast<std::size_t>(seconds * state_.audio_peaks_per_second);
                if (index >= state_.audio_peaks.size()) {
                    continue;
                }
                const int amplitude =
                    static_cast<int>(std::lround(state_.audio_peaks[index] * half));
                painter.drawLine(x, center - amplitude, x, center + amplitude);
            }
            painter.setClipping(false);
        }
    }

    for (int row = 0; row < static_cast<int>(row_list.size()); ++row) {
        const int y = rows_top(state_) + row * kRowHeight;
        if (y > height()) {
            break;
        }
        const TimelineRow& descriptor = row_list[static_cast<std::size_t>(row)];
        bool selected = false;
        if (descriptor.kind == TimelineRowKind::Bone && state_.selected_bone_idx) {
            selected = *state_.selected_bone_idx == descriptor.index;
        }
        painter.fillRect(QRect(0, y, width(), kRowHeight),
                         selected ? kSelection
                                  : (row % 2 ? QColor(colors::surface_low) : kBase));
        painter.fillRect(QRect(0, y, kHeaderWidth, kRowHeight), selected ? kSelection : kPanel);
        painter.setPen(QColor(kLine.red(), kLine.green(), kLine.blue(), 110));
        painter.drawLine(0, y + kRowHeight - 1, width(), y + kRowHeight - 1);
        painter.setFont(row_font);
        painter.setPen(selected ? kCyan : kSnow);
        const QString source_label =
            descriptor.kind == TimelineRowKind::Channel ? localization::text(descriptor.name)
                                                        : descriptor.name;
        const QString label = QFontMetrics(row_font).elidedText(
            source_label, Qt::ElideRight, kHeaderWidth - 24);
        painter.drawText(QRect(12, y, kHeaderWidth - 20, kRowHeight),
                         Qt::AlignVCenter | Qt::AlignLeft, label);
    }

    painter.setClipRect(grid_rect);
    painter.setFont(ruler_font);
    for (int frame = 0; frame <= visible_frames; frame += 5) {
        const int x =
            kHeaderWidth + kGridPadding + static_cast<int>(frame * spacing) - offset;
        if (x > width()) {
            break;
        }
        if (x < kHeaderWidth - 20) {
            continue;
        }
        painter.setPen(QPen(QColor(kLine.red(), kLine.green(), kLine.blue(), 90), 1));
        painter.drawLine(x, kRulerHeight, x, height());
        painter.fillRect(QRect(x, kRulerHeight - 5, 1, 4), kLine);
        painter.setPen(kMuted);
        painter.drawText(QRect(x - 24, 3, 48, 16), Qt::AlignCenter, QString::number(frame));
    }

    for (int row = 0; row < static_cast<int>(row_list.size()); ++row) {
        const int y = rows_top(state_) + row * kRowHeight;
        if (y > height()) {
            break;
        }
        const TimelineRow& descriptor = row_list[static_cast<std::size_t>(row)];
        const auto motion_keys = state_.motion_keyframes.find(descriptor.name);
        if (motion_keys != state_.motion_keyframes.end()) {
            painter.setPen(Qt::NoPen);
            for (const unsigned int frame : motion_keys.value()) {
                const bool selected =
                    state_.selected_motion_keyframes.count({descriptor.name, frame}) != 0;
                int x = kHeaderWidth + kGridPadding + static_cast<int>(frame * spacing) - offset;
                if (selected && dragging_motion_) {
                    x += static_cast<int>(motion_drag_delta_ * spacing);
                }
                if (x < kHeaderWidth || x > width()) {
                    continue;
                }
                const QPointF center(x, y + kRowHeight / 2.0);
                painter.setPen(selected ? QPen(kSnow, 1) : Qt::NoPen);
                painter.setBrush(selected ? kMotionSelected : kMotionKey);
                painter.drawPolygon(diamond_at(center, selected ? 5.0f : 3.0f));
            }
        }
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
            painter.setPen(QPen(key_selected ? kSnow : kBase, 1));
            painter.setBrush(key_selected ? kKeySelected : kKey);
            painter.drawPolygon(diamond_at(center, 5.0f));
        }
    }
    painter.setClipping(false);

    if (box_selecting_) {
        const QRect selection = QRect(box_start_, box_current_).normalized() & grid_rect;
        QColor fill = kCyan;
        fill.setAlpha(42);
        painter.fillRect(selection, fill);
        QColor border = kCyan;
        border.setAlpha(220);
        painter.setPen(QPen(border, 1.0, Qt::DashLine));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(selection.adjusted(0, 0, -1, -1));
    }

    const int playhead_x = kHeaderWidth + kGridPadding +
                           static_cast<int>(state_.current_frame * spacing) - offset;
    if (playhead_x >= kHeaderWidth && playhead_x <= width()) {
        painter.setPen(QPen(kPlayhead, 1.6));
        painter.drawLine(playhead_x, 0, playhead_x, std::min(height(), content_height));
        QPolygonF head;
        head << QPointF(playhead_x - 4.0, 0.0) << QPointF(playhead_x + 4.0, 0.0)
             << QPointF(playhead_x, 7.0);
        painter.setPen(Qt::NoPen);
        painter.setBrush(kPlayhead);
        painter.drawPolygon(head);
    }

    painter.setPen(QPen(kLine, 1));
    painter.drawLine(kHeaderWidth, 0, kHeaderWidth, height());
}

int TimelineWidget::frame_at(const QPoint& position) const {
    const float spacing = kFrameSpacing * std::max(state_.timeline_zoom, 0.2f);
    const float frame =
        (static_cast<float>(position.x() - kHeaderWidth - kGridPadding) +
         state_.timeline_scroll) /
        spacing;
    return static_cast<int>(std::lround(frame));
}

std::optional<TimelineKeyframeHit> TimelineWidget::motion_keyframe_at(
    const QPoint& position) const {
    const int top = rows_top(state_);
    if (position.x() < kHeaderWidth || position.y() < top) {
        return std::nullopt;
    }
    const int row = (position.y() - top) / kRowHeight;
    const auto& row_list = rows();
    if (row < 0 || row >= static_cast<int>(row_list.size())) {
        return std::nullopt;
    }
    const int row_y = top + row * kRowHeight + kRowHeight / 2;
    if (std::abs(position.y() - row_y) > 6) {
        return std::nullopt;
    }
    const TimelineRow& descriptor = row_list[static_cast<std::size_t>(row)];
    const auto motion = state_.motion_keyframes.find(descriptor.name);
    if (motion == state_.motion_keyframes.end()) {
        return std::nullopt;
    }
    const float spacing = kFrameSpacing * std::max(state_.timeline_zoom, 0.2f);
    const int offset = static_cast<int>(state_.timeline_scroll);
    for (const unsigned int frame : motion.value()) {
        const int x = kHeaderWidth + kGridPadding + static_cast<int>(frame * spacing) - offset;
        if (x < kHeaderWidth || x > width()) {
            continue;
        }
        if (std::abs(position.x() - x) <= 6) {
            return TimelineKeyframeHit{descriptor.name, frame, row, true};
        }
    }
    return std::nullopt;
}

std::optional<TimelineKeyframeHit> TimelineWidget::keyframe_at(const QPoint& position) const {
    const int top = rows_top(state_);
    if (position.x() < kHeaderWidth || position.y() < top) {
        return std::nullopt;
    }
    const int row = (position.y() - top) / kRowHeight;
    const auto& row_list = rows();
    if (row < 0 || row >= static_cast<int>(row_list.size())) {
        return std::nullopt;
    }
    const int row_y = top + row * kRowHeight + kRowHeight / 2;
    if (std::abs(position.y() - row_y) > 6) {
        return std::nullopt;
    }
    const TimelineRow& descriptor = row_list[static_cast<std::size_t>(row)];
    const auto track = state_.track_keyframes.find(descriptor.name);
    if (track != state_.track_keyframes.end()) {
        const float spacing = kFrameSpacing * std::max(state_.timeline_zoom, 0.2f);
        const int offset = static_cast<int>(state_.timeline_scroll);
        for (const auto& entry : track->second) {
            const unsigned int frame = entry.first;
            const int x = kHeaderWidth + kGridPadding + static_cast<int>(frame * spacing) - offset;
            if (x < kHeaderWidth || x > width()) {
                continue;
            }
            if (std::abs(position.x() - x) <= 6) {
                return TimelineKeyframeHit{descriptor.name, frame, row, false};
            }
        }
    }
    return motion_keyframe_at(position);
}

void TimelineWidget::begin_key_drag(const QPoint& position) {
    drag_keys_.clear();
    drag_selection_before_ = state_.selected_keyframes;
    for (const auto& key : state_.selected_keyframes) {
        const auto track = state_.track_keyframes.find(key.first);
        if (track == state_.track_keyframes.end()) {
            continue;
        }
        const auto entry = track->second.find(key.second);
        if (entry == track->second.end()) {
            continue;
        }
        drag_keys_.push_back({key.first, key.second, key.second, entry->second});
    }
    if (drag_keys_.empty()) {
        return;
    }
    drag_origin_ = position;
    drag_undo_pushed_ = false;
    drag_moved_ = false;
    drag_last_delta_ = 0;
}

void TimelineWidget::update_key_drag(const QPoint& position) {
    if (drag_keys_.empty()) {
        return;
    }
    const int delta = frame_at(position) - frame_at(drag_origin_);
    if (delta == drag_last_delta_) {
        return;
    }
    drag_last_delta_ = delta;
    if (!drag_undo_pushed_) {
        state_.push_undo_snapshot();
        drag_undo_pushed_ = true;
    }
    // Erase the previous positions first so a key can move onto a frame that
    // another selected key is leaving in the same step.
    for (const DragKey& key : drag_keys_) {
        const auto track = state_.track_keyframes.find(key.track);
        if (track != state_.track_keyframes.end()) {
            track->second.erase(key.current_frame);
        }
    }
    std::set<std::pair<QString, unsigned int>> selection;
    for (DragKey& key : drag_keys_) {
        const int frame = std::clamp(static_cast<int>(key.original_frame) + delta, 0,
                                     std::max(0, state_.max_timeline_frame));
        key.current_frame = static_cast<unsigned int>(frame);
        state_.track_keyframes[key.track][key.current_frame] = key.key;
        selection.insert({key.track, key.current_frame});
    }
    state_.selected_keyframes = std::move(selection);
    state_.track_keys_dirty = true;
    drag_moved_ = drag_moved_ || delta != 0;
    update();
}

void TimelineWidget::finish_key_drag() {
    if (drag_moved_) {
        ++state_.keyframe_revision;
        state_.set_status(QStringLiteral("Moved %1 keyframe(s)")
                              .arg(static_cast<int>(drag_keys_.size())));
    }
    drag_keys_.clear();
    drag_undo_pushed_ = false;
    drag_moved_ = false;
    drag_last_delta_ = 0;
}

void TimelineWidget::cancel_interaction() {
    if (drag_undo_pushed_) {
        state_.discard_last_undo_snapshot();
        for (const DragKey& key : drag_keys_) {
            const auto track = state_.track_keyframes.find(key.track);
            if (track != state_.track_keyframes.end()) {
                track->second.erase(key.current_frame);
            }
        }
        for (const DragKey& key : drag_keys_) {
            state_.track_keyframes[key.track][key.original_frame] = key.key;
        }
        state_.selected_keyframes = drag_selection_before_;
        state_.track_keys_dirty = true;
        ++state_.keyframe_revision;
    } else if (!drag_keys_.empty()) {
        state_.selected_keyframes = drag_selection_before_;
    }
    drag_keys_.clear();
    drag_undo_pushed_ = false;
    drag_moved_ = false;
    drag_last_delta_ = 0;
    dragging_motion_ = false;
    motion_drag_delta_ = 0;
    box_selecting_ = false;
    update();
}

void TimelineWidget::apply_box_selection(const QRect& rect, bool additive) {
    if (!additive) {
        state_.selected_keyframes.clear();
        state_.selected_motion_keyframes.clear();
    }
    const int top = rows_top(state_);
    const auto& row_list = rows();
    for (int row = 0; row < static_cast<int>(row_list.size()); ++row) {
        const int y = top + row * kRowHeight;
        if (y + kRowHeight < rect.top() || y > rect.bottom()) {
            continue;
        }
        const TimelineRow& descriptor = row_list[static_cast<std::size_t>(row)];
        const auto hit_x = [&](unsigned int frame) {
            const float spacing = kFrameSpacing * std::max(state_.timeline_zoom, 0.2f);
            const int offset = static_cast<int>(state_.timeline_scroll);
            const int x =
                kHeaderWidth + kGridPadding + static_cast<int>(frame * spacing) - offset;
            return rect.contains(x, y + kRowHeight / 2);
        };
        if (const auto track = state_.track_keyframes.find(descriptor.name);
            track != state_.track_keyframes.end()) {
            for (const auto& entry : track->second) {
                if (hit_x(entry.first)) {
                    state_.selected_keyframes.insert({descriptor.name, entry.first});
                }
            }
        }
        if (const auto motion = state_.motion_keyframes.find(descriptor.name);
            motion != state_.motion_keyframes.end()) {
            for (const unsigned int frame : motion.value()) {
                if (hit_x(frame)) {
                    state_.selected_motion_keyframes.insert({descriptor.name, frame});
                }
            }
        }
    }
    const std::size_t total = state_.selected_keyframes.size() +
                              state_.selected_motion_keyframes.size();
    state_.set_status(QStringLiteral("Selected %1 keyframe(s)").arg(static_cast<int>(total)));
    update();
}

void TimelineWidget::update_hover_cursor(const QPoint& position) {
    if (keyframe_at(position)) {
        setCursor(Qt::SizeHorCursor);
    } else if (position.x() > kHeaderWidth && position.y() > rows_top(state_)) {
        setCursor(Qt::CrossCursor);
    } else {
        unsetCursor();
    }
}

void TimelineWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        return;
    }
    setFocus();
    const QPoint position = event->pos();
    const bool ctrl = (event->modifiers() & Qt::ControlModifier) != 0;
    const bool shift = (event->modifiers() & Qt::ShiftModifier) != 0;

    if (position.x() < kHeaderWidth) {
        if (position.y() >= rows_top(state_)) {
            const int row = (position.y() - rows_top(state_)) / kRowHeight;
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
                    state_.set_status(
                        QStringLiteral("Selected morph track: %1").arg(descriptor.name));
                    update();
                }
            }
        }
        event->accept();
        return;
    }

    if (position.y() < rows_top(state_)) {
        // The ruler and audio strip are the scrub zone and never move keys.
        state_.selected_keyframes.clear();
        state_.selected_motion_keyframes.clear();
        dragging_ = true;
        scrub_to(position);
        update();
        event->accept();
        return;
    }

    if (const auto hit = keyframe_at(position)) {
        const auto key = std::make_pair(hit->track, hit->frame);
        if (hit->motion) {
            if (ctrl) {
                const auto existing = state_.selected_motion_keyframes.find(key);
                if (existing != state_.selected_motion_keyframes.end()) {
                    state_.selected_motion_keyframes.erase(existing);
                } else {
                    state_.selected_motion_keyframes.insert(key);
                }
            } else {
                if (!shift && state_.selected_motion_keyframes.count(key) == 0) {
                    state_.selected_motion_keyframes.clear();
                    state_.selected_keyframes.clear();
                }
                state_.selected_motion_keyframes.insert(key);
            }
            if (!ctrl && state_.selected_motion_keyframes.count(key) != 0) {
                dragging_motion_ = true;
                motion_drag_anchor_frame_ = static_cast<int>(hit->frame);
                motion_drag_delta_ = 0;
                drag_origin_ = position;
            }
        } else {
            if (ctrl) {
                const auto existing = state_.selected_keyframes.find(key);
                if (existing != state_.selected_keyframes.end()) {
                    state_.selected_keyframes.erase(existing);
                } else {
                    state_.selected_keyframes.insert(key);
                }
            } else {
                if (!shift && state_.selected_keyframes.count(key) == 0) {
                    state_.selected_keyframes.clear();
                    state_.selected_motion_keyframes.clear();
                }
                state_.selected_keyframes.insert(key);
            }
            if (!ctrl && state_.selected_keyframes.count(key) != 0) {
                begin_key_drag(position);
            }
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
        state_.set_status(QStringLiteral("Selected %1 keyframe %2 at frame %3")
                              .arg(hit->motion ? QStringLiteral("VMD")
                                               : localization::text(QStringLiteral("authored")))
                              .arg(channel_track(hit->track)
                                       ? localization::text(hit->track)
                                       : hit->track)
                              .arg(hit->frame));
        update();
        event->accept();
        return;
    }

    if (!ctrl && !shift) {
        state_.selected_keyframes.clear();
        state_.selected_motion_keyframes.clear();
    }
    box_selecting_ = true;
    box_additive_ = ctrl || shift;
    box_start_ = position;
    box_current_ = position;
    update();
    event->accept();
}

void TimelineWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        return;
    }
    if (dragging_) {
        dragging_ = false;
    } else if (dragging_motion_) {
        const int delta = motion_drag_delta_;
        dragging_motion_ = false;
        motion_drag_delta_ = 0;
        if (delta != 0) {
            emit motionKeysMoveRequested(delta);
        }
    } else if (!drag_keys_.empty()) {
        finish_key_drag();
    } else if (box_selecting_) {
        const QRect rect = QRect(box_start_, box_current_).normalized();
        box_selecting_ = false;
        apply_box_selection(rect, box_additive_);
    }
    update_hover_cursor(event->pos());
    update();
}

void TimelineWidget::mouseMoveEvent(QMouseEvent* event) {
    const QPoint position = event->pos();
    if (dragging_) {
        scrub_to(position);
        update();
        return;
    }
    if (!drag_keys_.empty()) {
        update_key_drag(position);
        return;
    }
    if (dragging_motion_) {
        motion_drag_delta_ = frame_at(position) - frame_at(drag_origin_);
        state_.set_status(QStringLiteral("Moving %1 VMD keyframe(s) by %2 frame(s)")
                              .arg(static_cast<int>(state_.selected_motion_keyframes.size()))
                              .arg(motion_drag_delta_));
        update();
        return;
    }
    if (box_selecting_) {
        box_current_ = position;
        update();
        return;
    }
    update_hover_cursor(position);
}

void TimelineWidget::scrub_to(const QPoint& position) {
    if (position.x() < kHeaderWidth) {
        return;
    }
    const float spacing = kFrameSpacing * std::max(state_.timeline_zoom, 0.2f);
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
    if (event->key() == Qt::Key_Escape) {
        cancel_interaction();
        event->accept();
        return;
    }
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
