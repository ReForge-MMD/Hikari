#pragma once

#include <functional>

#include <QImage>
#include <QWidget>

#include "hikari/studio/app_state.hpp"
#include "hikari/studio/material_section.hpp"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
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
    // The viewport owns the models, so the Materials section reads and writes
    // their per-subset responses through these callbacks.
    void set_material_callbacks(MaterialSectionCallbacks callbacks);
    void refresh_materials();

private:
    void push_edit();
    void apply_preset(int index);
    void build_controls();
    QWidget* make_group(const QString& title, QWidget* body);
    void refresh_preview();
    void choose_environment();
    void refresh_material_models();
    void refresh_material_subsets();
    void refresh_material_entry();
    void apply_material_shading(int shading_model);
    void import_ray_mmd_material();
    void open_material_library();
    void open_material_allocator();
    void open_material_node_editor();

    StudioAppState& state_;
    std::function<void()> record_edit_;
    std::function<QImage(const QSize&)> preview_provider_;
    std::function<QString()> ibl_error_provider_;
    QComboBox* renderer_ = nullptr;
    QComboBox* preset_ = nullptr;
    QDoubleSpinBox* sun_intensity_ = nullptr;
    QDoubleSpinBox* sun_temperature_ = nullptr;
    QDoubleSpinBox* sun_size_ = nullptr;
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
    QComboBox* ao_type_ = nullptr;
    QComboBox* ao_quality_ = nullptr;
    QDoubleSpinBox* ao_radius_ = nullptr;
    QDoubleSpinBox* ao_intensity_ = nullptr;
    QComboBox* ssr_quality_ = nullptr;
    QCheckBox* ssr_debug_ = nullptr;
    QComboBox* ssr_debug_mode_ = nullptr;
    QComboBox* gi_enable_ = nullptr;
    QComboBox* gi_quality_ = nullptr;
    QSpinBox* gi_filter_radius_ = nullptr;
    QSpinBox* gi_prefilter_radius_ = nullptr;
    QDoubleSpinBox* gi_sample_radius_ = nullptr;
    QDoubleSpinBox* gi_thickness_ = nullptr;
    QDoubleSpinBox* gi_bounce_weight_ = nullptr;
    QDoubleSpinBox* gi_il_amount_ = nullptr;
    QDoubleSpinBox* gi_ao_amount_ = nullptr;
    QDoubleSpinBox* gi_sensitivity_ = nullptr;
    QCheckBox* gi_bounces_ = nullptr;
    QCheckBox* gi_specular_ = nullptr;
    QComboBox* debug_view_ = nullptr;
    QCheckBox* wireframe_ = nullptr;

    MaterialSectionCallbacks material_callbacks_;
    QComboBox* material_model_ = nullptr;
    QComboBox* material_subset_ = nullptr;
    QComboBox* material_shading_ = nullptr;
    QComboBox* material_preset_ = nullptr;
    QPushButton* material_browse_preset_ = nullptr;
    QDoubleSpinBox* material_roughness_ = nullptr;
    QDoubleSpinBox* material_metallic_ = nullptr;
    QDoubleSpinBox* material_custom_a_ = nullptr;
    QDoubleSpinBox* material_custom_b_ = nullptr;
    QCheckBox* material_hashed_alpha_ = nullptr;
    QDoubleSpinBox* material_hex_tiling_ = nullptr;
    QPushButton* material_toggle_toon_ = nullptr;
    QPushButton* material_load_hkmat_ = nullptr;
    QPushButton* material_save_hkmat_ = nullptr;
    QPushButton* material_clear_ = nullptr;
    QPushButton* material_import_ = nullptr;
    QPushButton* material_allocator_btn_ = nullptr;
    QPushButton* material_node_editor_btn_ = nullptr;
    QLineEdit* material_albedo_map_ = nullptr;
    QLineEdit* material_normal_map_ = nullptr;
    QLineEdit* material_roughness_map_ = nullptr;
    QLineEdit* material_metallic_map_ = nullptr;
    QLineEdit* material_emissive_map_ = nullptr;
    QLineEdit* material_occlusion_map_ = nullptr;
    QCheckBox* material_procedural_hair_ = nullptr;
    QCheckBox* material_glitter_ = nullptr;
    QCheckBox* material_eye_parallax_ = nullptr;
    QLabel* material_status_ = nullptr;
    QStringList material_model_cache_;

    QCheckBox* sky_enabled_ = nullptr;
    QComboBox* sky_quality_ = nullptr;
    QDoubleSpinBox* moon_size_ = nullptr;
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
    QComboBox* fog_enable_ = nullptr;
    QComboBox* fog_quality_ = nullptr;
    QComboBox* fog_debug_ = nullptr;
    QCheckBox* fog_cloud_shadow_ = nullptr;
    QDoubleSpinBox* fog_density_ = nullptr;
    QDoubleSpinBox* fog_height_ = nullptr;
    QDoubleSpinBox* fog_falloff_ = nullptr;
    QDoubleSpinBox* fog_ground_density_ = nullptr;
    QDoubleSpinBox* fog_ground_height_ = nullptr;
    QDoubleSpinBox* fog_ground_falloff_ = nullptr;
    QDoubleSpinBox* fog_start_ = nullptr;
    QDoubleSpinBox* fog_cutoff_ = nullptr;
    QDoubleSpinBox* fog_max_opacity_ = nullptr;
    QDoubleSpinBox* fog_color_r_ = nullptr;
    QDoubleSpinBox* fog_color_g_ = nullptr;
    QDoubleSpinBox* fog_color_b_ = nullptr;
    QDoubleSpinBox* fog_sun_color_r_ = nullptr;
    QDoubleSpinBox* fog_sun_color_g_ = nullptr;
    QDoubleSpinBox* fog_sun_color_b_ = nullptr;
    QDoubleSpinBox* fog_sun_intensity_ = nullptr;
    QDoubleSpinBox* fog_mie_phase_ = nullptr;
    QDoubleSpinBox* fog_mist_noise_ = nullptr;
    QDoubleSpinBox* fog_wind_speed_ = nullptr;
    QDoubleSpinBox* fog_godray_intensity_ = nullptr;
    QDoubleSpinBox* fog_ray_density_ = nullptr;
    QLineEdit* ibl_path_ = nullptr;
    QLabel* ibl_status_ = nullptr;
    QLabel* preview_ = nullptr;
    QTimer* preview_timer_ = nullptr;

    QLabel* status_ = nullptr;
};

} // namespace hikari::studio