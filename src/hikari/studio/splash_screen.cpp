#include "hikari/studio/splash_screen.hpp"

#include <QGuiApplication>
#include <QLinearGradient>
#include <QPainter>
#include <QPropertyAnimation>
#include <QScreen>
#include <QSvgRenderer>
#include <QTimer>
#include <QVariantAnimation>

#include <algorithm>
#include <cmath>

namespace hikari::studio {
namespace {

constexpr int kWidth = 520;
constexpr int kHeight = 300;
constexpr int kCardRadius = 18;
constexpr int kMinimumVisibleMs = 900;
constexpr qreal kMarkSize = 84.0;
constexpr qreal kMarkSupersample = 3.0;

const QColor kInk(0xe3, 0xe9, 0xef);
const QColor kOutline(0x2b, 0x33, 0x3d);
const QColor kMuted(0x7c, 0x87, 0x94);
const QColor kSecondary(0xa9, 0xb5, 0xc1);
const QColor kPrimary(0x9e, 0xc6, 0xe2);

QPixmap render_mark() {
    QSvgRenderer renderer(QStringLiteral(":/brand/hikari-mark.svg"));
    const int pixels = static_cast<int>(std::lround(kMarkSize * kMarkSupersample));
    QPixmap pixmap(pixels, pixels);
    pixmap.setDevicePixelRatio(kMarkSupersample);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    renderer.render(&painter, QRectF(0.0, 0.0, kMarkSize, kMarkSize));
    return pixmap;
}

QFont spaced_font(const QString& family, int pixel_size, QFont::Weight weight, qreal spacing) {
    QFont font(family);
    font.setPixelSize(pixel_size);
    font.setWeight(weight);
    font.setLetterSpacing(QFont::AbsoluteSpacing, spacing);
    return font;
}

} // namespace

SplashScreen::SplashScreen(QWidget* parent) : QWidget(parent) {
    setWindowFlags(Qt::SplashScreen | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setFixedSize(kWidth, kHeight);
    setWindowTitle(QStringLiteral("Hikari Studio"));

    mark_ = render_mark();
    status_ = QStringLiteral("Preparing the workspace");

    sweep_ = new QVariantAnimation(this);
    sweep_->setStartValue(0.0);
    sweep_->setEndValue(1.0);
    sweep_->setDuration(1300);
    sweep_->setLoopCount(-1);
    sweep_->setEasingCurve(QEasingCurve::InOutQuad);
    connect(sweep_, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        sweep_phase_ = value.toReal();
        update();
    });
    sweep_->start();

    if (const QScreen* screen = QGuiApplication::primaryScreen(); screen != nullptr) {
        const QRect available = screen->availableGeometry();
        move(available.center() - rect().center());
    }
}

void SplashScreen::set_status(const QString& text) {
    status_ = text;
    update();
}

void SplashScreen::finish(QWidget* target) {
    if (finishing_) {
        return;
    }
    finishing_ = true;
    const qint64 shown_for = shown_at_.isValid() ? shown_at_.elapsed() : 0;
    const int remaining = static_cast<int>(std::max<qint64>(0, kMinimumVisibleMs - shown_for));
    if (remaining > 0) {
        QTimer::singleShot(remaining, this, [this, target] { fade_out(target); });
    } else {
        fade_out(target);
    }
}

void SplashScreen::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    if (!shown_at_.isValid()) {
        shown_at_.start();
    }
}
void SplashScreen::fade_out(QWidget* target) {
    auto* fade = new QPropertyAnimation(this, "windowOpacity", this);
    fade->setStartValue(windowOpacity());
    fade->setEndValue(0.0);
    fade->setDuration(240);
    fade->setEasingCurve(QEasingCurve::InCubic);
    connect(fade, &QPropertyAnimation::finished, this, [this, target] {
        if (target != nullptr) {
            target->raise();
            target->activateWindow();
        }
        close();
    });
    fade->start(QAbstractAnimation::DeleteWhenStopped);
}

void SplashScreen::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);

    const QRectF card(20.0, 16.0, kWidth - 40.0, kHeight - 38.0);

    painter.setPen(Qt::NoPen);
    for (int spread = 12; spread >= 1; --spread) {
        const qreal falloff = 1.0 - static_cast<qreal>(spread) / 13.0;
        painter.setBrush(QColor(0, 0, 0, static_cast<int>(4.0 + 20.0 * falloff * falloff)));
        const QRectF shadow = card.adjusted(-spread, -spread + 6.0, spread, spread + 6.0);
        painter.drawRoundedRect(shadow, kCardRadius + spread, kCardRadius + spread);
    }

    QLinearGradient background(card.topLeft(), card.bottomRight());
    background.setColorAt(0.0, QColor(0x1b, 0x22, 0x2b));
    background.setColorAt(1.0, QColor(0x0e, 0x12, 0x17));
    painter.setBrush(background);
    painter.setPen(QPen(kOutline, 1.0));
    painter.drawRoundedRect(card, kCardRadius, kCardRadius);

    const QRectF mark_rect((kWidth - kMarkSize) / 2.0, card.top() + 40.0, kMarkSize, kMarkSize);
    painter.drawPixmap(mark_rect, mark_, QRectF(mark_.rect()));

    painter.setPen(kInk);
    painter.setFont(spaced_font(QStringLiteral("Segoe UI"), 29, QFont::DemiBold, 9.0));
    painter.drawText(QRectF(card.left(), 154.0, card.width(), 36.0), Qt::AlignHCenter | Qt::AlignVCenter,
                     QStringLiteral("HIKARI"));

    painter.setPen(kSecondary);
    painter.setFont(spaced_font(QStringLiteral("Segoe UI"), 11, QFont::Medium, 7.0));
    painter.drawText(QRectF(card.left(), 192.0, card.width(), 16.0), Qt::AlignHCenter | Qt::AlignVCenter,
                     QStringLiteral("STUDIO"));

    painter.setPen(kMuted);
    painter.setFont(spaced_font(QStringLiteral("Segoe UI"), 12, QFont::Normal, 0.0));
    painter.drawText(QRectF(card.left(), 224.0, card.width(), 16.0), Qt::AlignHCenter | Qt::AlignVCenter,
                     status_);

    constexpr qreal track_width = 188.0;
    const qreal track_x = (kWidth - track_width) / 2.0;
    const QRectF track(track_x, 252.0, track_width, 3.0);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0x26, 0x2e, 0x38));
    painter.drawRoundedRect(track, 1.5, 1.5);

    constexpr qreal segment_width = 64.0;
    const qreal segment_x = -segment_width + sweep_phase_ * (track_width + segment_width);
    painter.save();
    painter.setClipRect(track);
    QLinearGradient segment(segment_x, 0.0, segment_x + segment_width, 0.0);
    QColor leading = kPrimary;
    leading.setAlpha(0);
    QColor trailing = kPrimary;
    trailing.setAlpha(0);
    segment.setColorAt(0.0, leading);
    segment.setColorAt(0.5, kPrimary);
    segment.setColorAt(1.0, trailing);
    painter.setBrush(segment);
    painter.drawRoundedRect(QRectF(segment_x, track.y(), segment_width, track.height()), 1.5, 1.5);
    painter.restore();
}

} // namespace hikari::studio
