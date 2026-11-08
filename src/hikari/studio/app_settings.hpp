#pragma once

#include <QJsonObject>
#include <QString>

namespace hikari::studio {

struct StudioAppState;

// User-facing application settings stored in a JSON file in the user's
// configuration directory. Window geometry, dock layout and interface language
// stay in QSettings because they describe the interface rather than the
// application configuration.
class AppSettings {
public:
    AppSettings();
    explicit AppSettings(QString path);

    const QString& path() const { return path_; }

    bool load(QString* error = nullptr);
    bool save(QString* error = nullptr) const;

    void apply_to(StudioAppState& state) const;
    void capture_from(const StudioAppState& state);

    QJsonObject camera_presets() const { return camera_presets_; }
    void set_camera_presets(const QJsonObject& presets);

private:
    QString path_;
    QJsonObject root_;
    QJsonObject camera_presets_;
};

// Default location for the settings file. Uses the per-user configuration
// directory and falls back to the application directory when unavailable.
QString default_app_settings_path();

} // namespace hikari::studio
