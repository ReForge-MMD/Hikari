#pragma once

#include <functional>

#include <QWidget>

#include "hikari/studio/app_state.hpp"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;

namespace hikari::studio {

class GraphicsPanel : public QWidget {
    Q_OBJECT
public:
    GraphicsPanel(StudioAppState& state, std::function<void()> record_edit,
                  QWidget* parent = nullptr);

    void refresh_from_state();

private:
    void push_edit();
    void apply_preset(int index);
    void build_controls();
    QWidget* make_group(const QString& title, QWidget* body);

    StudioAppState& state_;
    std::function<void()> record_edit_;
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
    QComboBox* shadow_quality_ = nullptr;
    QCheckBox* shadow_pcss_ = nullptr;
    QDoubleSpinBox* shadow_light_radius_ = nullptr;
    QCheckBox* shadow_blur_ = nullptr;
    QComboBox* contact_shadow_quality_ = nullptr;
    QComboBox* ssgs_quality_ = nullptr;
    QComboBox* sky_visibility_ = nullptr;
    QComboBox* debug_view_ = nullptr;
    QCheckBox* wireframe_ = nullptr;
    QLabel* status_ = nullptr;
};

} // namespace hikari::studio
