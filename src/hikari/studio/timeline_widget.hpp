#pragma once

#include <QPoint>
#include <QRect>
#include <QString>
#include <QWidget>

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <utility>
#include <vector>

#include "hikari/studio/app_state.hpp"

namespace hikari::studio {

struct TimelineKeyframeHit {
    QString track;
    unsigned int frame = 0;
    int row = 0;
    bool motion = false;
};

enum class TimelineRowKind { Channel, Bone, Morph };

struct TimelineRow {
    QString name;
    TimelineRowKind kind = TimelineRowKind::Channel;
    int index = 0;
};

class TimelineWidget : public QWidget {
    Q_OBJECT
public:
    explicit TimelineWidget(StudioAppState& state, QWidget* parent = nullptr);

    QSize sizeHint() const override;
    void refresh_layout();
    void set_follow_playhead(bool follow);

public slots:
    void set_scroll(int offset);

signals:
    void boneSelected(int index);
    void scrollRangeChanged(int maximum);
    void scrollChanged(int value);
    void deleteSelectedRequested();
    void copyRequested();
    void pasteRequested();
    void bakeMotionKeysRequested();
    void deleteMotionKeysRequested();
    void motionKeysMoveRequested(int delta);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    struct DragKey {
        QString track;
        unsigned int original_frame = 0;
        unsigned int current_frame = 0;
        TrackKeyframe key;
    };

    const std::vector<TimelineRow>& rows() const;
    void rebuild_rows() const;
    void ensure_playhead_visible();
    int content_width() const;
    int max_scroll() const;
    void update_scroll_range();
    void scrub_to(const QPoint& position);
    int frame_at(const QPoint& position) const;
    std::optional<TimelineKeyframeHit> keyframe_at(const QPoint& position) const;
    std::optional<TimelineKeyframeHit> motion_keyframe_at(const QPoint& position) const;
    void begin_key_drag(const QPoint& position);
    void update_key_drag(const QPoint& position);
    void finish_key_drag();
    void cancel_interaction();
    void apply_box_selection(const QRect& rect, bool additive);
    void update_hover_cursor(const QPoint& position);

    StudioAppState& state_;
    bool dragging_ = false;
    bool follow_playhead_ = true;
    int layout_frames_ = -1;
    int layout_rows_ = -1;
    float layout_spacing_ = -1.0f;
    int scroll_max_ = -1;
    mutable std::vector<TimelineRow> rows_cache_;
    mutable int rows_mode_ = -1;
    mutable int rows_bone_count_ = -1;
    mutable std::uint64_t rows_revision_ = 0;

    std::vector<DragKey> drag_keys_;
    std::set<std::pair<QString, unsigned int>> drag_selection_before_;
    QPoint drag_origin_;
    bool drag_undo_pushed_ = false;
    bool drag_moved_ = false;
    int drag_last_delta_ = 0;

    bool dragging_motion_ = false;
    int motion_drag_anchor_frame_ = 0;
    int motion_drag_delta_ = 0;

    bool box_selecting_ = false;
    QPoint box_start_;
    QPoint box_current_;
    bool box_additive_ = false;
};

} // namespace hikari::studio
