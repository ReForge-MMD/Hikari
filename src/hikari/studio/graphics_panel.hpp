#pragma once

#include <functional>

#include <QImage>
#include <QWidget>

#include "hikari/studio/app_state.hpp"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QSlider;
class QSpinBox;
class QTimer;

namespace hikari::studio {

class GraphicsPanel : public QWidget {
    Q_OBJECT
public:
    GraphicsPanel(StudioAppState& state, std::function<void()> record_edit,
                  QWidget* parent = nullptr);

    void refresh_from_state();

    // The dock previews the last rendered frame and the state of a custom
    // environment. Both live behind the viewport, which owns the renderer, so the
    // main window installs these providers after construction.
    void set_preview_provider(std::function<QImage(const QSize&)> provider);
    void set_ibl_error_provider(std::function<QString()> provider);

private:
    void push_edit();
    void apply_preset(int index);
    void build_controls();
    QWidget* make_group(const QString& title, QWidget* body);
    void refresh_time_label();
    void refresh_preview();
    void choose_environment();

    StudioAppState& state_;
    std::function<void()> record_edit_;
    std::function<QImage(const QSize&)> preview_provider_;
    std::function<QString()> ibl_error_provider_;
    QComboBox* renderer_ = nullptr;
    QComboBox* preset_ = nullptr;
    QComboBox* sun_mode_ = nullptr;
    QDoubleSpinBox* sun_intensity_ = nullptr;
    QDoubleSpinBox* sun_temperature_ = nullptr;
    QDoubleSpinBox* ambient_intensity_ = nullptr;
    QDoubleSpinBox* exposure_ = nullptr;
    QComboBox* tonemap_ = nullptr;
    QCheckBox* sun_disk_ = nullptr;
    QCheckBox* limb_darkening_ = nullptr;
    QCheckBox* sky_coupling_ = nullptr;
    QComboBox* shadow_quality_ = nullptr;
    QCheckBox* shadow_pcss_ = nullptr;
    QDoubleSpinBox* shadow_light_radius_ = nullptr;
    QCheckBox* shadow_blur_ = nullptr;
    QComboBox* contact_shadow_quality_ = nullptr;
    QComboBox* ssgs_quality_ = nullptr;
    QComboBox* sky_visibility_ = nullptr;
    QComboBox* debug_view_ = nullptr;
    QCheckBox* wireframe_ = nullptr;

    QCheckBox* sky_enabled_ = nullptr;
    QComboBox* sky_quality_ = nullptr;
    QSlider* time_of_day_ = nullptr;
    QLabel* time_of_day_label_ = nullptr;
    QDoubleSpinBox* sky_turbidity_ = nullptr;
    QDoubleSpinBox* sky_ozone_ = nullptr;
    QDoubleSpinBox* sky_sun_radiance_ = nullptr;
    QDoubleSpinBox* sky_mie_anisotropy_ = nullptr;
    QDoubleSpinBox* moon_intensity_ = nullptr;
    QDoubleSpinBox* moon_phase_ = nullptr;
    QDoubleSpinBox* star_brightness_ = nullptr;
    QDoubleSpinBox* milky_way_ = nullptr;
    QDoubleSpinBox* cloud_coverage_ = nullptr;
    QDoubleSpinBox* cloud_density_ = nullptr;
    QDoubleSpinBox* cloud_speed_ = nullptr;
    QDoubleSpinBox* cloud_turbulence_ = nullptr;
    QComboBox* ibl_quality_ = nullptr;
    QDoubleSpinBox* ibl_intensity_ = nullptr;
    QDoubleSpinBox* ibl_rotation_ = nullptr;
    QSpinBox* ibl_update_hz_ = nullptr;
    QLineEdit* ibl_path_ = nullptr;
    QLabel* ibl_status_ = nullptr;
    QLabel* preview_ = nullptr;
    QTimer* preview_timer_ = nullptr;

    QLabel* status_ = nullptr;
};

} // namespace hikari::studio