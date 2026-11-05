#pragma once

#include <QString>
#include <QStringList>
#include <QWidget>

#include <optional>

namespace hikari::studio {

struct StudioAppState;

struct TimelineKeyframeHit {
    QString track;
    unsigned int frame = 0;
    int row = 0;
};

class TimelineWidget : public QWidget {
    Q_OBJECT
public:
    explicit TimelineWidget(StudioAppState& state, QWidget* parent = nullptr);

    QSize sizeHint() const override;
    void refresh_layout();

public slots:
    void set_scroll(int offset);

signals:
    void boneSelected(int index);
    void scrollRangeChanged(int maximum);
    void scrollChanged(int value);
    void deleteSelectedRequested();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    QStringList channels() const;
    int content_width() const;
    int max_scroll() const;
    void update_scroll_range();
    void scrub_to(const QPoint& position);
    std::optional<TimelineKeyframeHit> keyframe_at(const QPoint& position) const;
    StudioAppState& state_;
    bool dragging_ = false;
    int layout_frames_ = -1;
    int layout_rows_ = -1;
    float layout_spacing_ = -1.0f;
    int scroll_max_ = -1;
};

} // namespace hikari::studio
