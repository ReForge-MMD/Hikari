#include <QApplication>
#include <QIcon>
#include <QSurfaceFormat>
#include <QTimer>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <dwmapi.h>
#include <windows.h>
#endif

#include "hikari/studio/localization.hpp"
#include "hikari/studio/main_window.hpp"
#include "hikari/studio/splash_screen.hpp"
#include "hikari/studio/theme.hpp"

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    application.setStyle(QStringLiteral("Fusion"));
    application.setPalette(hikari::studio::NordicTheme::palette());
    application.setApplicationName(QStringLiteral("Hikari Studio"));
    application.setOrganizationName(QStringLiteral("Hikari"));
    application.setApplicationVersion(QStringLiteral("1.0.0"));
    application.setWindowIcon(QIcon(QStringLiteral(":/brand/hikari-app-icon.svg")));

    QString model_path;
    QString motion_path;
    QString audio_path;
    const QStringList arguments = application.arguments();
    if (arguments.size() > 1) {
        model_path = arguments[1];
    }
    if (arguments.size() > 2) {
        motion_path = arguments[2];
    }
    if (arguments.size() > 3) {
        audio_path = arguments[3];
    }

    hikari::studio::localization::load_language();
    hikari::studio::SplashScreen splash;
    splash.set_status(model_path.isEmpty()
                          ? hikari::studio::localization::message(
                                QStringLiteral("Preparing the workspace"))
                          : hikari::studio::localization::message(
                                QStringLiteral("Loading the startup scene")));
    splash.show();
    application.processEvents();

    hikari::studio::MainWindow window(model_path, motion_path, audio_path);
    QObject::connect(&window, &hikari::studio::MainWindow::startupFinished, &splash,
                     [&splash, &window] { splash.finish(&window); });
    window.show();
    QTimer::singleShot(15000, &splash, [&splash, &window] { splash.finish(&window); });
#ifdef _WIN32
    // DWM attribute ids used by Windows 10 20H1 and Windows 11; the SDK
    // constants are not present in every toolchain.
    constexpr DWORD kUseImmersiveDarkMode = 20;
    constexpr DWORD kBorderColor = 34;
    constexpr DWORD kCaptionColor = 35;
    const HWND handle = reinterpret_cast<HWND>(window.winId());
    const BOOL dark = TRUE;
    DwmSetWindowAttribute(handle, kUseImmersiveDarkMode, &dark, sizeof(dark));
    const COLORREF caption = RGB(0x10, 0x14, 0x19);
    DwmSetWindowAttribute(handle, kCaptionColor, &caption, sizeof(caption));
    const COLORREF border = RGB(0x2b, 0x33, 0x3d);
    DwmSetWindowAttribute(handle, kBorderColor, &border, sizeof(border));
#endif
    return application.exec();
}
