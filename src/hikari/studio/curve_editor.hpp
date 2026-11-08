#pragma once

#include <QDialog>
#include <QWidget>

#include <array>

namespace hikari::studio {

struct StudioAppState;

class CurvePlot : public QWidget {
    Q_OBJECT
public:
    explicit CurvePlot(QWidget* parent = nullptr);

    void set_points(const std::array<float, 2>& p1, const std::array<float, 2>& p2);
    const std::array<float, 2>& point1() const { return p1_; }
    const std::array<float, 2>& point2() const { return p2_; }
    void set_mixed(bool mixed);
    QSize sizeHint() const override;

signals:
    void curveChanged();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    QPointF plot_point(const std::array<float, 2>& point) const;
    std::array<float, 2> normalize_point(const QPointF& position) const;
    QRectF plot_rect() const;

    std::array<float, 2> p1_{0.157f, 0.157f};
    std::array<float, 2> p2_{0.843f, 0.843f};
    int active_point_ = -1;
    bool mixed_ = false;
};

class CurveEditorDialog : public QDialog {
    Q_OBJECT
public:
    explicit CurveEditorDialog(StudioAppState& state, QWidget* parent = nullptr);

private:
    void apply();
    StudioAppState& state_;
    CurvePlot* plot_ = nullptr;
    bool mixed_ = false;
    bool has_selection_ = false;
};

} // namespace hikari::studio
