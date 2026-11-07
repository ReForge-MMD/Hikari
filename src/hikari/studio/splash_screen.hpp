#pragma once

#include <QElapsedTimer>
#include <QPixmap>
#include <QString>
#include <QWidget>

class QVariantAnimation;

namespace hikari::studio {

// Frameless startup card shown while the studio builds its workspace. It owns
// its own painting so the splash does not depend on the widget style sheet.
class SplashScreen : public QWidget {
    Q_OBJECT
public:
    explicit SplashScreen(QWidget* parent = nullptr);

    void set_status(const QString& text);

    // Keeps the splash visible for a minimum duration, then fades it out and
    // hands focus to target.
    void finish(QWidget* target);

protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    void fade_out(QWidget* target);

    QPixmap mark_;
    QString status_;
    QVariantAnimation* sweep_ = nullptr;
    QElapsedTimer shown_at_;
    qreal sweep_phase_ = 0.0;
    bool finishing_ = false;
};

} // namespace hikari::studio
