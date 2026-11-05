#pragma once

#include <QWidget>

namespace hikari::studio {

struct StudioAppState;

class TimelineWidget : public QWidget {
    Q_OBJECT
public:
    explicit TimelineWidget(StudioAppState& state, QWidget* parent = nullptr);

    QSize sizeHint() const override;
    void refresh_layout();

signals:
    void boneSelected(int index);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    QStringList channels() const;
    void scrub_to(const QPoint& position);
    StudioAppState& state_;
    bool dragging_ = false;
};

} // namespace hikari::studio
