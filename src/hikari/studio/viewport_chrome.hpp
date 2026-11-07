#pragma once

#include <QWidget>

class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSpinBox;

namespace hikari::studio {

struct StudioAppState;

// MMD-style local/global axis pad: the first row rotates the active target,
// the second row translates it. Dragging up or down adjusts the value
// continuously with a pointer-locked cursor; a plain press does nothing.
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
    void showEvent(QShowEvent* event) override;
    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    int cell_at(const QPoint& point) const;
    void drag_by(int delta_y, Qt::KeyboardModifiers modifiers);
    void end_drag();

    bool local_ = true;
    int hovered_ = -1;
    int pressed_ = -1;
    QPoint drag_anchor_;
    bool dragging_ = false;
    bool cursor_hidden_ = false;
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
    void modelLoadRequested();

private:
    void apply_camera_fields();
    void apply_bone_fields();

    StudioAppState& state_;
    QPushButton* mode_ = nullptr;
    QDoubleSpinBox* position_[3] = {};
    QDoubleSpinBox* angle_[3] = {};
    QDoubleSpinBox* distance_ = nullptr;
    bool syncing_ = false;
};

} // namespace hikari::studio
