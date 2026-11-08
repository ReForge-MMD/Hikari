#include "hikari/studio/curve_editor.hpp"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

#include "hikari/studio/app_state.hpp"
#include "hikari/studio/localization.hpp"
#include "hikari/studio/theme.hpp"

namespace hikari::studio {
namespace {

constexpr int kMargin = 14;
const QColor kGrid(0x3a, 0x44, 0x50);
const QColor kAxis(0x62, 0x70, 0x80);
const QColor kCurve(colors::primary);
const QColor kControl(colors::tertiary);
const QColor kLinear(0x7e, 0x9d, 0xb4, 160);

std::array<float, 2> bytes_to_points(const std::array<std::uint8_t, 4>& bytes) {
    return {static_cast<float>(bytes[0]) / 127.0f,
            static_cast<float>(bytes[1]) / 127.0f};
}

} // namespace

CurvePlot::CurvePlot(QWidget* parent) : QWidget(parent) {
    setMinimumSize(320, 260);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
}

QSize CurvePlot::sizeHint() const {
    return {380, 300};
}

void CurvePlot::set_points(const std::array<float, 2>& p1,
                           const std::array<float, 2>& p2) {
    p1_ = p1;
    p2_ = p2;
    update();
}

void CurvePlot::set_mixed(bool mixed) {
    mixed_ = mixed;
    update();
}

QRectF CurvePlot::plot_rect() const {
    const int size = std::max(40, std::min(width(), height()) - kMargin * 2);
    return QRectF(kMargin, kMargin, size, size);
}

QPointF CurvePlot::plot_point(const std::array<float, 2>& point) const {
    const QRectF rect = plot_rect();
    return QPointF(rect.left() + point[0] * rect.width(),
                   rect.bottom() - point[1] * rect.height());
}

std::array<float, 2> CurvePlot::normalize_point(const QPointF& position) const {
    const QRectF rect = plot_rect();
    return {static_cast<float>(std::clamp((position.x() - rect.left()) / rect.width(), 0.0, 1.0)),
            static_cast<float>(std::clamp((rect.bottom() - position.y()) / rect.height(), 0.0, 1.0))};
}

void CurvePlot::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), QColor(colors::surface_lowest));
    const QRectF rect = plot_rect();

    painter.setPen(QPen(kGrid, 1));
    for (int i = 1; i < 4; ++i) {
        const qreal x = rect.left() + rect.width() * i / 4.0;
        const qreal y = rect.top() + rect.height() * i / 4.0;
        painter.drawLine(QPointF(x, rect.top()), QPointF(x, rect.bottom()));
        painter.drawLine(QPointF(rect.left(), y), QPointF(rect.right(), y));
    }
    painter.setPen(QPen(kAxis, 1));
    painter.drawRect(rect);

    painter.setPen(QPen(kLinear, 1, Qt::DashLine));
    painter.drawLine(QPointF(rect.left(), rect.bottom()), QPointF(rect.right(), rect.top()));

    const QPointF start(rect.left(), rect.bottom());
    const QPointF end(rect.right(), rect.top());
    const QPointF c1 = plot_point(p1_);
    const QPointF c2 = plot_point(p2_);
    painter.setPen(QPen(kAxis, 1, Qt::DotLine));
    painter.drawLine(start, c1);
    painter.drawLine(end, c2);

    QPainterPath path(start);
    path.cubicTo(c1, c2, end);
    painter.setPen(QPen(kCurve, 2.2));
    painter.drawPath(path);

    painter.setPen(QPen(QColor(colors::on_surface), 1));
    painter.setBrush(kControl);
    painter.drawEllipse(c1, 6.0, 6.0);
    painter.drawEllipse(c2, 6.0, 6.0);

    if (mixed_) {
        painter.setPen(colors::tertiary);
        painter.drawText(rect.adjusted(6, 4, -6, -4), Qt::AlignTop | Qt::AlignRight,
                         localization::text(QStringLiteral("mixed")));
    }
}

void CurvePlot::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        return;
    }
    const QPointF position = event->position();
    const float d1 = static_cast<float>((position - plot_point(p1_)).manhattanLength());
    const float d2 = static_cast<float>((position - plot_point(p2_)).manhattanLength());
    active_point_ = d1 <= d2 ? 0 : 1;
    if (std::min(d1, d2) > 24.0f) {
        active_point_ = -1;
    }
    if (active_point_ >= 0) {
        mouseMoveEvent(event);
    }
}

void CurvePlot::mouseMoveEvent(QMouseEvent* event) {
    if (active_point_ < 0) {
        return;
    }
    const std::array<float, 2> point = normalize_point(event->position());
    if (active_point_ == 0) {
        p1_ = point;
        p1_[0] = std::min(p1_[0], p2_[0]);
    } else {
        p2_ = point;
        p2_[0] = std::max(p2_[0], p1_[0]);
    }
    update();
    emit curveChanged();
}

void CurvePlot::mouseReleaseEvent(QMouseEvent*) {
    active_point_ = -1;
}

CurveEditorDialog::CurveEditorDialog(StudioAppState& state, QWidget* parent)
    : QDialog(parent), state_(state) {
    setWindowTitle(QStringLiteral("Interpolation Curve"));
    auto* layout = new QVBoxLayout(this);
    plot_ = new CurvePlot(this);
    layout->addWidget(plot_, 1);

    std::array<float, 2> p1 = state_.curve_p1;
    std::array<float, 2> p2 = state_.curve_p2;
    has_selection_ = !state_.selected_keyframes.empty();
    if (has_selection_) {
        bool first = true;
        for (const auto& [track, frame] : state_.selected_keyframes) {
            const auto track_it = state_.track_keyframes.find(track);
            if (track_it == state_.track_keyframes.end()) {
                continue;
            }
            const auto key_it = track_it->second.find(frame);
            if (key_it == track_it->second.end()) {
                continue;
            }
            const std::array<float, 2> key_p1 = bytes_to_points(key_it->second.interpolation);
            std::array<float, 2> key_p2{};
            key_p2[0] = static_cast<float>(key_it->second.interpolation[2]) / 127.0f;
            key_p2[1] = static_cast<float>(key_it->second.interpolation[3]) / 127.0f;
            if (first) {
                p1 = key_p1;
                p2 = key_p2;
                first = false;
            } else if (std::abs(p1[0] - key_p1[0]) > 1.0e-4f ||
                       std::abs(p1[1] - key_p1[1]) > 1.0e-4f ||
                       std::abs(p2[0] - key_p2[0]) > 1.0e-4f ||
                       std::abs(p2[1] - key_p2[1]) > 1.0e-4f) {
                mixed_ = true;
            }
        }
    }
    plot_->set_points(p1, p2);
    plot_->set_mixed(mixed_);

    auto* hint = new QLabel(
        has_selection_
            ? (mixed_ ? QStringLiteral("Selected keys use different curves. Applying replaces "
                                       "all of them with the edited curve.")
                      : QStringLiteral("The curve is applied to the selected keyframes."))
            : QStringLiteral("No keyframes are selected. The curve becomes the default for new "
                             "keyframes."));
    hint->setWordWrap(true);
    hint->setObjectName(QStringLiteral("HintLabel"));
    layout->addWidget(hint);

    auto* value_row = new QHBoxLayout();
    auto* values = new QLabel(this);
    values->setObjectName(QStringLiteral("FieldLabel"));
    value_row->addWidget(values, 1);
    layout->addLayout(value_row);
    const auto update_values = [plot = plot_, values] {
        const int x1 = std::lround(plot->point1()[0] * 127.0f);
        const int y1 = std::lround(plot->point1()[1] * 127.0f);
        const int x2 = std::lround(plot->point2()[0] * 127.0f);
        const int y2 = std::lround(plot->point2()[1] * 127.0f);
        values->setText(localization::text(
                            QStringLiteral("P1 (%1, %2)   P2 (%3, %4)"))
                            .arg(x1)
                            .arg(y1)
                            .arg(x2)
                            .arg(y2));
    };
    connect(plot_, &CurvePlot::curveChanged, this, update_values);
    update_values();

    auto* buttons = new QDialogButtonBox();
    auto* linear = buttons->addButton(QStringLiteral("Linear"), QDialogButtonBox::ResetRole);
    auto* reset = buttons->addButton(QStringLiteral("Reset"), QDialogButtonBox::ResetRole);
    auto* apply_button = buttons->addButton(QStringLiteral("Apply"), QDialogButtonBox::AcceptRole);
    auto* cancel = buttons->addButton(QStringLiteral("Cancel"), QDialogButtonBox::RejectRole);
    layout->addWidget(buttons);

    connect(linear, &QPushButton::clicked, this, [this] {
        plot_->set_points({0.157f, 0.157f}, {0.843f, 0.843f});
        plot_->set_mixed(false);
    });
    connect(reset, &QPushButton::clicked, this, [this] {
        plot_->set_points(state_.curve_p1, state_.curve_p2);
        plot_->set_mixed(mixed_);
    });
    connect(apply_button, &QPushButton::clicked, this, [this] { apply(); });
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
}

void CurveEditorDialog::apply() {
    state_.curve_auto = false;
    state_.curve_p1 = plot_->point1();
    state_.curve_p2 = plot_->point2();
    state_.apply_curve_to_selection();
    accept();
}

} // namespace hikari::studio
