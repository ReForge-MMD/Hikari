#pragma once

#include <QWidget>

class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QTimer;

namespace hikari::studio {

struct StudioAppState;

// MMD-style local/global axis pad: the first row rotates the active target,
// the second row translates it.
class ViewGizmo : public QWidget {
    Q_OBJECT
public:
    explicit ViewGizmo(QWidget* parent = nullptr);

    QSize sizeHint() const override;
    void set_local_mode(bool local);

signals:
    void rotateRequested(int axis, float degrees);
    void translateRequested(int axis, float amount);
    void localModeChanged(bool local);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    int cell_at(const QPoint& point) const;
    void trigger(int cell);
    void stop_repeat();

    bool local_ = true;
    int hovered_ = -1;
    int pressed_ = -1;
    QTimer* repeat_ = nullptr;
    int repeat_ticks_ = 0;
};

// Top strip of the viewport: active target title and frame scrubber.
class ViewportHeader : public QWidget {
    Q_OBJECT
public:
    explicit ViewportHeader(StudioAppState& state, QWidget* parent = nullptr);

    void set_title(const QString& title);
    void set_frame(int frame);

signals:
    void frameRequested(int frame);
    void resetViewRequested();

private:
    StudioAppState& state_;
    QLabel* title_ = nullptr;
    QSpinBox* frame_ = nullptr;
};

// Bottom strip of the viewport: To Model / To Camera switch and numeric
// transform fields, mirroring the MMD camera panel.
class ViewportFooter : public QWidget {
    Q_OBJECT
public:
    explicit ViewportFooter(StudioAppState& state, QWidget* parent = nullptr);

    void sync();

signals:
    void modeChanged();
    void valueEdited();

private:
    void apply_camera_fields();
    void apply_bone_fields();

    StudioAppState& state_;
    QPushButton* mode_ = nullptr;
    QLabel* target_ = nullptr;
    QDoubleSpinBox* position_[3] = {};
    QDoubleSpinBox* angle_[3] = {};
    QDoubleSpinBox* distance_ = nullptr;
    bool syncing_ = false;
};

} // namespace hikari::studio
