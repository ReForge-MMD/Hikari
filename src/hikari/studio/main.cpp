#include <QApplication>
#include <QSurfaceFormat>

#include "hikari/studio/main_window.hpp"

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    application.setApplicationName(QStringLiteral("Hikari Studio"));
    application.setOrganizationName(QStringLiteral("Hikari"));
    application.setApplicationVersion(QStringLiteral("1.0.0"));

    QString model_path;
    QString motion_path;
    const QStringList arguments = application.arguments();
    if (arguments.size() > 1) {
        model_path = arguments[1];
    }
    if (arguments.size() > 2) {
        motion_path = arguments[2];
    }

    hikari::studio::MainWindow window(model_path, motion_path);
    window.show();
    return application.exec();
}
