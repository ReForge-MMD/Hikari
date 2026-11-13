#include "hikari/studio/graphics_panel.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <string>

#include "hikari/gfx/settings.hpp"

namespace hikari::studio {
namespace {

constexpr int kPreviewIntervalMs = 500;
constexpr char kTooltipProperty[] = "hikari_original_tooltip";

// Disables a dependency-gated control and explains why, restoring the original
// tooltip when the control becomes editable again.
void gate_control(QWidget* widget, bool enabled, const QString& reason) {
    widget->setEnabled(enabled);
    if (enabled) {
        const QVariant original = widget->property(kTooltipProperty);
        if (original.isValid()) {
            widget->setToolTip(original.toString());
        }
        return;
    }
    if (reason.isEmpty()) {
        return;
    }
    if (!widget->property(kTooltipProperty).isValid()) {
        widget->setProperty(kTooltipProperty, widget->toolTip());
    }
    widget->setToolTip(reason);
}

QDoubleSpinBox* make_spin(double minimum, double maximum, double step, int decimals) {
    auto* spin = new QDoubleSpinBox();
    spin->setRange(minimum, maximum);
    spin->setSingleStep(step);
    spin->setDecimals(decimals);
    spin->setKeyboardTracking(false);
    return spin;
}

} // namespace

GraphicsPanel::GraphicsPanel(StudioAppState& state, std::function<void()> record_edit,
                             QWidget* parent)
    : QWidget(parent), state_(state), record_edit_(std::move(record_edit)) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);
    build_controls();
    layout->addWidget(status_);
    layout->addStretch(1);

    preview_timer_ = new QTimer(this);
    preview_timer_->setInterval(kPreviewIntervalMs);
    connect(preview_timer_, &QTimer::timeout, this, [this] {
        if (isVisible()) {
            refresh_preview();
        }
    });
    preview_timer_->start();

    refresh_from_state();
}

void GraphicsPanel::set_preview_provider(std::function<QImage(const QSize&)> provider) {
    preview_provider_ = std::move(provider);
    refresh_preview();
}

void GraphicsPanel::set_ibl_error_provider(std::function<QString()> provider) {
    ibl_error_provider_ = std::move(provider);
    refresh_preview();
}

QWidget* GraphicsPanel::make_group(const QString& title, QWidget* body) {
    auto* group = new QGroupBox(title);
    auto* layout = new QVBoxLayout(group);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->addWidget(body);
    return group;
}

void GraphicsPanel::build_controls() {
    auto* layout = static_cast<QVBoxLayout*>(this->layout());

    renderer_ = new QComboBox();
    renderer_->addItems({QStringLiteral("Legacy"), QStringLiteral("Native")});
    renderer_->setToolTip(QStringLiteral(
        "Legacy keeps the forward renderer. Native uses the deferred G-buffer path."));
    preset_ = new QComboBox();
    for (int i = 0; i <= static_cast<int>(gfx::QualityPreset::Custom); ++i) {
        preset_->addItem(gfx::quality_preset_name(static_cast<gfx::QualityPreset>(i)));
    }
    preset_->setToolTip(QStringLiteral("Preset values can be edited individually."));

    auto* backend = new QWidget();
    auto* backend_form = new QFormLayout(backend);
    backend_form->setContentsMargins(0, 0, 0, 0);
    backend_form->addRow(QStringLiteral("Renderer"), renderer_);
    backend_form->addRow(QStringLiteral("Preset"), preset_);
    layout->addWidget(make_group(QStringLiteral("Backend"), backend));

    sun_size_ = make_spin(0.1, 10.0, 0.1, 1);
    sun_size_->setToolTip(QStringLiteral("Visual size scaling of the sun disk in the sky."));
    sun_intensity_ = make_spin(0.0, 100.0, 0.1, 2);
    sun_temperature_ = make_spin(1000.0, 20000.0, 100.0, 0);
    sun_temperature_->setSuffix(QStringLiteral(" K"));
    sun_disk_ = new QCheckBox(QStringLiteral("Physical sun disk specular"));
    limb_darkening_ = new QCheckBox(QStringLiteral("Solar limb darkening"));
    sky_coupling_ = new QCheckBox(QStringLiteral("Fade direct sun into the sky"));
    sky_coupling_->setToolTip(QStringLiteral(
        "Hands direct sun energy over to the captured sky as the sun reaches the "
        "horizon, where the capture cannot contain the solar disk."));

    auto* sun = new QWidget();
    auto* sun_form = new QFormLayout(sun);
    sun_form->setContentsMargins(0, 0, 0, 0);
    sun_form->addRow(QStringLiteral("Sun size"), sun_size_);
    sun_form->addRow(QStringLiteral("Intensity"), sun_intensity_);
    sun_form->addRow(QStringLiteral("Temperature"), sun_temperature_);
    sun_form->addRow(sun_disk_);
    sun_form->addRow(limb_darkening_);
    sun_form->addRow(sky_coupling_);
    layout->addWidget(make_group(QStringLiteral("Sun"), sun));

    sky_enabled_ = new QCheckBox(QStringLiteral("Enable analytic sky"));
    sky_enabled_->setToolTip(QStringLiteral(
        "Draws the procedural atmosphere and captures it for lighting. Without it "
        "the scene keeps the flat background unless a custom environment is set."));

    sky_quality_ = new QComboBox();
    sky_quality_->addItems({QStringLiteral("Low (8 steps)"), QStringLiteral("Medium (12 steps)"),
                            QStringLiteral("High (16 steps)"), QStringLiteral("Very high (24 steps)"),
                            QStringLiteral("Cinematic (32 steps)")});
    sky_quality_->setToolTip(QStringLiteral("Raymarch steps per sky pixel."));

    moon_size_ = make_spin(0.1, 10.0, 0.1, 1);
    moon_size_->setToolTip(QStringLiteral("Visual size scaling of the moon disk in the sky."));

    sky_turbidity_ = make_spin(0.0, 10.0, 0.1, 2);
    sky_turbidity_->setToolTip(QStringLiteral("Aerosol density; higher values haze the horizon."));
    sky_ozone_ = make_spin(0.0, 5.0, 0.05, 2);
    sky_ozone_->setToolTip(QStringLiteral("Ozone absorption; deepens the blue zenith."));
    sky_sun_radiance_ = make_spin(0.0, 1000.0, 1.0, 1);
    sky_sun_radiance_->setToolTip(QStringLiteral("Radiance of the sun disk in the captured sky."));
    sky_mie_anisotropy_ = make_spin(-0.99, 0.99, 0.01, 2);
    sky_mie_anisotropy_->setToolTip(
        QStringLiteral("Mie phase anisotropy; 0.76 gives the forward glow around the sun."));

    auto* sky = new QWidget();
    auto* sky_form = new QFormLayout(sky);
    sky_form->setContentsMargins(0, 0, 0, 0);
    sky_form->addRow(sky_enabled_);
    sky_form->addRow(QStringLiteral("Quality"), sky_quality_);
    sky_form->addRow(QStringLiteral("Turbidity"), sky_turbidity_);
    sky_form->addRow(QStringLiteral("Ozone"), sky_ozone_);
    sky_form->addRow(QStringLiteral("Sun radiance"), sky_sun_radiance_);
    sky_form->addRow(QStringLiteral("Mie anisotropy"), sky_mie_anisotropy_);
    layout->addWidget(make_group(QStringLiteral("Sky"), sky));

    preview_ = new QLabel(QStringLiteral("No frame rendered yet"));
    preview_->setAlignment(Qt::AlignCenter);
    preview_->setMinimumHeight(130);
    preview_->setFrameShape(QFrame::StyledPanel);
    preview_->setToolTip(QStringLiteral(
        "Thumbnail of the last rendered viewport frame, refreshed twice a second while "
        "this panel is visible."));
    layout->addWidget(make_group(QStringLiteral("Sky preview"), preview_));

    moon_intensity_ = make_spin(0.0, 10.0, 0.05, 2);
    moon_intensity_->setToolTip(QStringLiteral("Radiance of the moon disk and its reflection."));
    moon_phase_ = make_spin(0.0, 1.0, 0.01, 2);
    moon_phase_->setToolTip(QStringLiteral("Lunar phase: 0 is new, 0.5 is full, 1 is new again."));
    star_brightness_ = make_spin(0.0, 10.0, 0.05, 2);
    star_brightness_->setToolTip(QStringLiteral("Star field brightness after sunset."));
    milky_way_ = make_spin(0.0, 10.0, 0.05, 2);
    milky_way_->setToolTip(QStringLiteral("Milky Way band intensity."));

    auto* moon = new QWidget();
    auto* moon_form = new QFormLayout(moon);
    moon_form->setContentsMargins(0, 0, 0, 0);
    moon_form->addRow(QStringLiteral("Moon size"), moon_size_);
    moon_form->addRow(QStringLiteral("Moon intensity"), moon_intensity_);
    moon_form->addRow(QStringLiteral("Moon phase"), moon_phase_);
    moon_form->addRow(QStringLiteral("Star brightness"), star_brightness_);
    moon_form->addRow(QStringLiteral("Milky Way"), milky_way_);
    layout->addWidget(make_group(QStringLiteral("Moon and night"), moon));

    cloud_coverage_ = make_spin(0.0, 1.0, 0.01, 2);
    cloud_coverage_->setToolTip(QStringLiteral("Fraction of the sky covered by clouds."));
    cloud_density_ = make_spin(0.0, 4.0, 0.05, 2);
    cloud_density_->setToolTip(QStringLiteral("Optical thickness of the cloud layer."));
    cloud_speed_ = make_spin(0.0, 10.0, 0.05, 2);
    cloud_speed_->setToolTip(QStringLiteral("Cloud drift speed in layer widths per minute."));
    cloud_turbulence_ = make_spin(0.0, 2.0, 0.05, 2);
    cloud_turbulence_->setToolTip(QStringLiteral("Erosion detail of the cloud edges."));

    auto* clouds = new QWidget();
    auto* clouds_form = new QFormLayout(clouds);
    clouds_form->setContentsMargins(0, 0, 0, 0);
    clouds_form->addRow(QStringLiteral("Coverage"), cloud_coverage_);
    clouds_form->addRow(QStringLiteral("Density"), cloud_density_);
    clouds_form->addRow(QStringLiteral("Speed"), cloud_speed_);
    clouds_form->addRow(QStringLiteral("Turbulence"), cloud_turbulence_);
    layout->addWidget(make_group(QStringLiteral("Clouds"), clouds));

    shadow_quality_ = new QComboBox();
    shadow_quality_->addItems({QStringLiteral("Disabled"), QStringLiteral("Low (1024)"),
                               QStringLiteral("Medium (2048)"), QStringLiteral("High (4096)"),
                               QStringLiteral("Cinematic (8192)")});
    shadow_quality_->setToolTip(QStringLiteral("Cascaded shadow map quality preset."));

    shadow_pcss_ = new QCheckBox(QStringLiteral("PCSS variable penumbra"));
    shadow_pcss_->setToolTip(QStringLiteral("Percentage Closer Soft Shadows blocker search and adaptive filter."));

    shadow_light_radius_ = make_spin(0.001, 0.1, 0.005, 3);
    shadow_light_radius_->setToolTip(QStringLiteral("Apparent sun angular size used for PCSS penumbra estimation."));

    shadow_blur_ = new QCheckBox(QStringLiteral("Bilateral shadow blur"));
    shadow_blur_->setToolTip(QStringLiteral("Cross-bilateral depth-guided filter reducing shadow map and contact noise."));

    contact_shadow_quality_ = new QComboBox();
    contact_shadow_quality_->addItems({QStringLiteral("Disabled"), QStringLiteral("Low (8 steps)"),
                                       QStringLiteral("Medium (12 steps)"), QStringLiteral("High (16 steps)")});
    contact_shadow_quality_->setToolTip(QStringLiteral("Screen-space raymarched contact shadows for small contact details."));

    ssgs_quality_ = new QComboBox();
    ssgs_quality_->addItems({QStringLiteral("Disabled"), QStringLiteral("Low (16 steps)"),
                            QStringLiteral("Medium (24 steps)"), QStringLiteral("High (32 steps)")});
    ssgs_quality_->setToolTip(QStringLiteral("Screen-Space Global Shadows for distant self-shadowing."));

    sky_visibility_ = new QComboBox();
    sky_visibility_->addItems({QStringLiteral("Disabled"), QStringLiteral("Fast (1 sample)"),
                               QStringLiteral("Accurate (5 samples)")});
    sky_visibility_->setToolTip(QStringLiteral("Top-down sky visibility height map for ambient occlusion under canopies."));

    auto* shadows = new QWidget();
    auto* shadows_form = new QFormLayout(shadows);
    shadows_form->setContentsMargins(0, 0, 0, 0);
    shadows_form->addRow(QStringLiteral("Quality"), shadow_quality_);
    shadows_form->addRow(shadow_pcss_);
    shadows_form->addRow(QStringLiteral("Light radius"), shadow_light_radius_);
    shadows_form->addRow(shadow_blur_);
    shadows_form->addRow(QStringLiteral("Contact shadows"), contact_shadow_quality_);
    shadows_form->addRow(QStringLiteral("SSGS"), ssgs_quality_);
    shadows_form->addRow(QStringLiteral("Sky visibility"), sky_visibility_);
    layout->addWidget(make_group(QStringLiteral("Shadows"), shadows));

    ao_type_ = new QComboBox();
    ao_type_->addItems({QStringLiteral("SSDO (bent normal)"), QStringLiteral("HBAO (horizon)"),
                        QStringLiteral("GTAO / GTSO (multi-bounce)")});
    ao_type_->setToolTip(QStringLiteral(
        "SSDO traces a hemisphere per pixel. HBAO and GTAO integrate the horizon in "
        "screen space; GTAO adds the multi-bounce term."));

    ao_quality_ = new QComboBox();
    ao_quality_->addItem(QStringLiteral("Disabled"));
    for (int level = 1; level <= 6; ++level) {
        ao_quality_->addItem(QString::number(level));
    }
    ao_quality_->setToolTip(QStringLiteral(
        "Sample budget: 8 to 28 hemisphere rays for SSDO, or 4 to 8 slice directions "
        "times 4 to 8 horizon steps for HBAO and GTAO."));

    ao_radius_ = make_spin(0.05, 20.0, 0.05, 2);
    ao_radius_->setToolTip(QStringLiteral("Sampling radius in world units."));
    ao_intensity_ = make_spin(0.0, 10.0, 0.05, 2);
    ao_intensity_->setToolTip(
        QStringLiteral("Intensity and contrast power of ambient occlusion crevices and contact shadows."));

    auto* ao = new QWidget();
    auto* ao_form = new QFormLayout(ao);
    ao_form->setContentsMargins(0, 0, 0, 0);
    ao_form->addRow(QStringLiteral("Algorithm"), ao_type_);
    ao_form->addRow(QStringLiteral("Quality"), ao_quality_);
    ao_form->addRow(QStringLiteral("Radius"), ao_radius_);
    ao_form->addRow(QStringLiteral("Intensity"), ao_intensity_);
    layout->addWidget(make_group(QStringLiteral("Ambient occlusion"), ao));

    ssr_quality_ = new QComboBox();
    ssr_quality_->addItems({QStringLiteral("Disabled"), QStringLiteral("128 taps"),
                            QStringLiteral("256 taps"), QStringLiteral("384 taps"),
                            QStringLiteral("512 taps")});
    ssr_quality_->setToolTip(QStringLiteral(
        "Half-resolution trace budget through the depth hierarchy. Higher budgets "
        "reach further before the ray gives up."));

    ssr_debug_ = new QCheckBox(QStringLiteral("Trace debug output"));
    ssr_debug_->setToolTip(QStringLiteral(
        "Writes the raw trace value into the SSR resolve target and drops the "
        "composite. Inspect it with the SSR radiance debug view."));

    ssr_debug_mode_ = new QComboBox();
    ssr_debug_mode_->addItems({QStringLiteral("Hit mask"), QStringLiteral("Confidence"),
                               QStringLiteral("Travel distance"), QStringLiteral("Hit normal"),
                               QStringLiteral("Roughness")});
    ssr_debug_mode_->setToolTip(QStringLiteral("Value forwarded by the trace debug path."));

    auto* ssr = new QWidget();
    auto* ssr_form = new QFormLayout(ssr);
    ssr_form->setContentsMargins(0, 0, 0, 0);
    ssr_form->addRow(QStringLiteral("Quality"), ssr_quality_);
    ssr_form->addRow(ssr_debug_);
    ssr_form->addRow(QStringLiteral("Debug mode"), ssr_debug_mode_);
    layout->addWidget(make_group(QStringLiteral("Screen-space reflections"), ssr));

    gi_enable_ = new QComboBox();
    gi_enable_->addItems({QStringLiteral("Disabled"), QStringLiteral("SSGI"),
                          QStringLiteral("SSGI (wide)"),
                          QStringLiteral("SSPT / RTGI (multi-bounce)"),
                          QStringLiteral("SSRC (Radiance Cascades)"),
                          QStringLiteral("VXGI (Voxel Cone Tracing)")});
    gi_enable_->setToolTip(QStringLiteral(
        "SSGI traces short screen-space rays. SSPT follows longer paths with "
        "thickness rejection. SSRC uses spherical harmonics cascades. "
        "VXGI builds a 3D voxel volume and traces multi-scale cones."));

    gi_quality_ = new QComboBox();
    gi_quality_->addItems({QStringLiteral("Low (4 rays)"), QStringLiteral("Medium (8 rays)"),
                           QStringLiteral("High (12 rays)"),
                           QStringLiteral("Very high (16 rays)")});
    gi_quality_->setToolTip(QStringLiteral(
        "Rays per pixel. SSPT spends the same budget on longer paths instead."));

    gi_filter_radius_ = new QSpinBox();
    gi_filter_radius_->setRange(1, 8);
    gi_filter_radius_->setKeyboardTracking(false);
    gi_filter_radius_->setToolTip(
        QStringLiteral("Taps per side of the separable GI denoiser."));

    gi_prefilter_radius_ = new QSpinBox();
    gi_prefilter_radius_->setRange(1, 3);
    gi_prefilter_radius_->setKeyboardTracking(false);
    gi_prefilter_radius_->setToolTip(
        QStringLiteral("Neighbourhood radius of the firefly and outlier prefilter."));

    gi_sample_radius_ = make_spin(0.5, 200.0, 1.0, 1);
    gi_sample_radius_->setToolTip(QStringLiteral("Maximum GI ray length in world units."));
    gi_thickness_ = make_spin(0.05, 20.0, 0.05, 2);
    gi_thickness_->setToolTip(
        QStringLiteral("Depth a ray may pass behind a surface before it is rejected."));
    gi_bounce_weight_ = make_spin(0.0, 1.0, 0.05, 2);
    gi_bounce_weight_->setToolTip(QStringLiteral("Weight of the previous-frame feedback."));
    gi_il_amount_ = make_spin(0.0, 10.0, 0.1, 2);
    gi_il_amount_->setToolTip(QStringLiteral("Scales the traced indirect radiance."));
    gi_ao_amount_ = make_spin(0.0, 4.0, 0.05, 2);
    gi_ao_amount_->setToolTip(QStringLiteral("Scales the ambient occlusion applied to GI."));
    gi_sensitivity_ = make_spin(0.05, 8.0, 0.05, 2);
    gi_sensitivity_->setToolTip(
        QStringLiteral("How aggressively the denoiser separates across depth edges."));
    gi_bounces_ = new QCheckBox(QStringLiteral("Previous-frame feedback"));
    gi_bounces_->setToolTip(
        QStringLiteral("Adds the last denoised frame to the trace so light bounces."));
    gi_specular_ = new QCheckBox(QStringLiteral("Specular GI"));
    gi_specular_->setToolTip(
        QStringLiteral("Adds the traced radiance to the specular term of smooth materials."));

    auto* gi = new QWidget();
    auto* gi_form = new QFormLayout(gi);
    gi_form->setContentsMargins(0, 0, 0, 0);
    gi_form->addRow(QStringLiteral("Mode"), gi_enable_);
    gi_form->addRow(QStringLiteral("Quality"), gi_quality_);
    gi_form->addRow(QStringLiteral("Denoise taps"), gi_filter_radius_);
    gi_form->addRow(QStringLiteral("Prefilter radius"), gi_prefilter_radius_);
    gi_form->addRow(QStringLiteral("Ray length"), gi_sample_radius_);
    gi_form->addRow(QStringLiteral("Thickness"), gi_thickness_);
    gi_form->addRow(QStringLiteral("Feedback weight"), gi_bounce_weight_);
    gi_form->addRow(QStringLiteral("Radiance scale"), gi_il_amount_);
    gi_form->addRow(QStringLiteral("AO scale"), gi_ao_amount_);
    gi_form->addRow(QStringLiteral("Edge sensitivity"), gi_sensitivity_);
    gi_form->addRow(gi_bounces_);
    gi_form->addRow(gi_specular_);
    layout->addWidget(make_group(QStringLiteral("Global illumination"), gi));

    ibl_quality_ = new QComboBox();
    ibl_quality_->addItems({QStringLiteral("Off"), QStringLiteral("Low (128 px)"),
                            QStringLiteral("High (256 px)")});
    ibl_quality_->setToolTip(QStringLiteral(
        "Image based lighting resolution. Low uses a 128 pixel cube face with a "
        "single mirror tap, High uses 256 pixels with a mirrored lookup."));

    ibl_intensity_ = make_spin(0.0, 10.0, 0.05, 2);
    ibl_intensity_->setToolTip(QStringLiteral("Scales the prefiltered specular term only."));
    ibl_rotation_ = make_spin(-180.0, 180.0, 1.0, 1);
    ibl_rotation_->setSuffix(QStringLiteral(" deg"));
    ibl_rotation_->setToolTip(QStringLiteral(
        "Turns the environment about the vertical axis. Positive values move content "
        "from +Z toward +X."));
    ibl_update_hz_ = new QSpinBox();
    ibl_update_hz_->setRange(1, 60);
    ibl_update_hz_->setSuffix(QStringLiteral(" Hz"));
    ibl_update_hz_->setKeyboardTracking(false);
    ibl_update_hz_->setToolTip(QStringLiteral(
        "Upper bound on analytic sky recaptures per second. A fresh or resized "
        "environment updates immediately regardless of this value."));

    ibl_path_ = new QLineEdit();
    ibl_path_->setReadOnly(true);
    ibl_path_->setPlaceholderText(QStringLiteral("Procedural sky"));
    ibl_path_->setToolTip(QStringLiteral(
        "Equirectangular HDR or uncompressed DDS cubemap used instead of the captured sky."));
    auto* browse = new QPushButton(QStringLiteral("Browse"));
    auto* clear = new QPushButton(QStringLiteral("Clear"));
    auto* env_row = new QWidget();
    auto* env_layout = new QHBoxLayout(env_row);
    env_layout->setContentsMargins(0, 0, 0, 0);
    env_layout->setSpacing(4);
    env_layout->addWidget(ibl_path_, 1);
    env_layout->addWidget(browse);
    env_layout->addWidget(clear);

    ibl_status_ = new QLabel(QStringLiteral("Environment: procedural sky"));
    ibl_status_->setWordWrap(true);

    auto* ibl = new QWidget();
    auto* ibl_form = new QFormLayout(ibl);
    ibl_form->setContentsMargins(0, 0, 0, 0);
    ibl_form->addRow(QStringLiteral("Quality"), ibl_quality_);
    ibl_form->addRow(QStringLiteral("Intensity"), ibl_intensity_);
    ibl_form->addRow(QStringLiteral("Rotation"), ibl_rotation_);
    ibl_form->addRow(QStringLiteral("Capture rate"), ibl_update_hz_);
    ibl_form->addRow(QStringLiteral("Environment"), env_row);
    ibl_form->addRow(ibl_status_);
    layout->addWidget(make_group(QStringLiteral("Image based lighting"), ibl));

    auto make_color_row = [](QDoubleSpinBox*& r, QDoubleSpinBox*& g, QDoubleSpinBox*& b) {
        r = make_spin(0.0, 10.0, 0.01, 3);
        g = make_spin(0.0, 10.0, 0.01, 3);
        b = make_spin(0.0, 10.0, 0.01, 3);
        auto* row = new QWidget();
        auto* row_layout = new QHBoxLayout(row);
        row_layout->setContentsMargins(0, 0, 0, 0);
        row_layout->setSpacing(4);
        row_layout->addWidget(r);
        row_layout->addWidget(g);
        row_layout->addWidget(b);
        return row;
    };

    fog_enable_ = new QComboBox();
    fog_enable_->addItems({QStringLiteral("Off"), QStringLiteral("Analytical"),
                           QStringLiteral("Volumetric")});
    fog_enable_->setToolTip(QStringLiteral(
        "Off skips the pass, Analytical evaluates the closed form, and Volumetric marches "
        "the medium so it can shadow the shafts and animate the mist."));
    fog_quality_ = new QComboBox();
    fog_quality_->addItems({QStringLiteral("Low (12 steps)"),
                            QStringLiteral("Medium (20 steps)"), QStringLiteral("High (32 steps)"),
                            QStringLiteral("Cinematic (48 steps)")});
    fog_quality_->setToolTip(
        QStringLiteral("Raymarch step budget of the volumetric mode."));
    fog_debug_ = new QComboBox();
    fog_debug_->addItems({QStringLiteral("Off"), QStringLiteral("Transmittance"),
                          QStringLiteral("In-scattering"), QStringLiteral("Optical depth"),
                          QStringLiteral("Sun shafts")});
    fog_debug_->setToolTip(QStringLiteral(
        "Replaces the image with one fog channel. Only the volumetric mode produces these."));
    fog_cloud_shadow_ = new QCheckBox(QStringLiteral("Cloud shadow"));
    fog_cloud_shadow_->setToolTip(
        QStringLiteral("Attenuates the sun shafts with the cloud coverage of the sky layer."));

    fog_density_ = make_spin(0.0, 1.0, 0.0005, 4);
    fog_density_->setToolTip(
        QStringLiteral("Extinction of the upper haze at the reference height."));
    fog_height_ = make_spin(-1000.0, 1000.0, 1.0, 2);
    fog_height_->setToolTip(
        QStringLiteral("Altitude where the upper haze reaches its full density."));
    fog_falloff_ = make_spin(0.0, 10.0, 0.005, 3);
    fog_falloff_->setToolTip(QStringLiteral("How quickly the upper haze thins with altitude."));
    fog_ground_density_ = make_spin(0.0, 1.0, 0.001, 4);
    fog_ground_density_->setToolTip(QStringLiteral("Extinction of the ground mist slab."));
    fog_ground_height_ = make_spin(-1000.0, 1000.0, 0.5, 2);
    fog_ground_height_->setToolTip(QStringLiteral("Altitude where the ground mist starts."));
    fog_ground_falloff_ = make_spin(0.0, 10.0, 0.01, 3);
    fog_ground_falloff_->setToolTip(
        QStringLiteral("How quickly the ground mist thins above its base."));
    fog_start_ = make_spin(0.0, 10000.0, 0.5, 2);
    fog_start_->setToolTip(QStringLiteral("Distance from the camera before the medium starts."));
    fog_cutoff_ = make_spin(0.0, 100000.0, 10.0, 1);
    fog_cutoff_->setToolTip(QStringLiteral("Longest distance the medium is integrated over."));
    fog_max_opacity_ = make_spin(0.0, 1.0, 0.01, 2);
    fog_max_opacity_->setToolTip(
        QStringLiteral("Upper bound on how much of the scene the fog may hide."));
    fog_sun_intensity_ = make_spin(0.0, 100.0, 0.05, 2);
    fog_sun_intensity_->setToolTip(QStringLiteral("Radiance the sun injects into the medium."));
    fog_mie_phase_ = make_spin(-1.0, 1.0, 0.01, 2);
    fog_mie_phase_->setToolTip(
        QStringLiteral("Henyey-Greenstein anisotropy. Positive values tighten the shafts "
                       "around the sun."));
    fog_mist_noise_ = make_spin(0.0, 1.0, 0.01, 2);
    fog_mist_noise_->setToolTip(
        QStringLiteral("How strongly the animated noise breaks up the ground mist."));
    fog_wind_speed_ = make_spin(0.0, 10.0, 0.05, 2);
    fog_wind_speed_->setToolTip(QStringLiteral("Scroll speed of the mist noise."));
    fog_godray_intensity_ = make_spin(0.0, 10.0, 0.05, 2);
    fog_godray_intensity_->setToolTip(QStringLiteral("Gain on the sun in-scattering shafts."));
    fog_ray_density_ = make_spin(0.0, 10.0, 0.05, 2);
    fog_ray_density_->setToolTip(
        QStringLiteral("Gain on the medium that carries the shafts, without thickening the fog."));

    auto* haze_row = make_color_row(fog_color_r_, fog_color_g_, fog_color_b_);
    fog_color_r_->setToolTip(QStringLiteral("Haze colour, red channel."));
    fog_color_g_->setToolTip(QStringLiteral("Haze colour, green channel."));
    fog_color_b_->setToolTip(QStringLiteral("Haze colour, blue channel."));
    auto* fog_sun_row = make_color_row(fog_sun_color_r_, fog_sun_color_g_, fog_sun_color_b_);
    fog_sun_color_r_->setToolTip(QStringLiteral("Sun colour used by the shafts, red channel."));
    fog_sun_color_g_->setToolTip(QStringLiteral("Sun colour used by the shafts, green channel."));
    fog_sun_color_b_->setToolTip(QStringLiteral("Sun colour used by the shafts, blue channel."));

    auto* fog = new QWidget();
    auto* fog_form = new QFormLayout(fog);
    fog_form->setContentsMargins(0, 0, 0, 0);
    fog_form->addRow(QStringLiteral("Mode"), fog_enable_);
    fog_form->addRow(QStringLiteral("Quality"), fog_quality_);
    fog_form->addRow(fog_cloud_shadow_);
    fog_form->addRow(QStringLiteral("Haze density"), fog_density_);
    fog_form->addRow(QStringLiteral("Haze height"), fog_height_);
    fog_form->addRow(QStringLiteral("Haze falloff"), fog_falloff_);
    fog_form->addRow(QStringLiteral("Mist density"), fog_ground_density_);
    fog_form->addRow(QStringLiteral("Mist height"), fog_ground_height_);
    fog_form->addRow(QStringLiteral("Mist falloff"), fog_ground_falloff_);
    fog_form->addRow(QStringLiteral("Start distance"), fog_start_);
    fog_form->addRow(QStringLiteral("Cutoff distance"), fog_cutoff_);
    fog_form->addRow(QStringLiteral("Max opacity"), fog_max_opacity_);
    fog_form->addRow(QStringLiteral("Haze colour"), haze_row);
    fog_form->addRow(QStringLiteral("Sun colour"), fog_sun_row);
    fog_form->addRow(QStringLiteral("Sun intensity"), fog_sun_intensity_);
    fog_form->addRow(QStringLiteral("Phase"), fog_mie_phase_);
    fog_form->addRow(QStringLiteral("Mist noise"), fog_mist_noise_);
    fog_form->addRow(QStringLiteral("Wind speed"), fog_wind_speed_);
    fog_form->addRow(QStringLiteral("Godray gain"), fog_godray_intensity_);
    fog_form->addRow(QStringLiteral("Ray density"), fog_ray_density_);
    fog_form->addRow(QStringLiteral("Debug channel"), fog_debug_);
    layout->addWidget(make_group(QStringLiteral("Fog and atmosphere"), fog));

    ambient_intensity_ = make_spin(0.0, 10.0, 0.05, 2);
    exposure_ = make_spin(0.0, 100.0, 0.05, 2);
    tonemap_ = new QComboBox();
    for (int i = 0; i <= static_cast<int>(gfx::TonemapOperator::AgXGolden); ++i) {
        tonemap_->addItem(
            gfx::tonemap_operator_name(static_cast<gfx::TonemapOperator>(i)));
    }
    wireframe_ = new QCheckBox(QStringLiteral("Wireframe"));

    debug_view_ = new QComboBox();
    debug_view_->addItem(QStringLiteral("Off (Final image)"), static_cast<int>(gfx::DebugView::Off));
    debug_view_->addItem(QStringLiteral("Contact shadows (SSCS)"), static_cast<int>(gfx::DebugView::ContactShadow));
    debug_view_->addItem(QStringLiteral("SSGS (Global shadows)"), static_cast<int>(gfx::DebugView::SSGS));
    debug_view_->addItem(QStringLiteral("Cascade splits (Colors)"), static_cast<int>(gfx::DebugView::ShadowCascades));
    debug_view_->addItem(QStringLiteral("Sun shadows (PCSS)"), static_cast<int>(gfx::DebugView::ShadowPCSS));
    debug_view_->addItem(QStringLiteral("Sky visibility"), static_cast<int>(gfx::DebugView::SkyVisibility));
    debug_view_->addItem(QStringLiteral("Sky cubemap"), static_cast<int>(gfx::DebugView::SkyCube));
    debug_view_->addItem(QStringLiteral("Sky irradiance (SH9)"), static_cast<int>(gfx::DebugView::SkyIrradiance));
    debug_view_->addItem(QStringLiteral("Sky prefilter"), static_cast<int>(gfx::DebugView::SkyPrefilter));
    debug_view_->addItem(QStringLiteral("Sky prefilter (roughness)"), static_cast<int>(gfx::DebugView::SkyPrefilterRoughness));
    debug_view_->addItem(QStringLiteral("IBL specular"), static_cast<int>(gfx::DebugView::IblSpecular));
    debug_view_->addItem(QStringLiteral("Base color"), static_cast<int>(gfx::DebugView::BaseColor));
    debug_view_->addItem(QStringLiteral("Normals"), static_cast<int>(gfx::DebugView::Normal));
    debug_view_->addItem(QStringLiteral("Roughness"), static_cast<int>(gfx::DebugView::Roughness));
    debug_view_->addItem(QStringLiteral("Metalness"), static_cast<int>(gfx::DebugView::Metalness));
    debug_view_->addItem(QStringLiteral("Depth"), static_cast<int>(gfx::DebugView::Depth));
    debug_view_->addItem(QStringLiteral("Ambient occlusion"), static_cast<int>(gfx::DebugView::Ao));
    debug_view_->addItem(QStringLiteral("AO bent normal"),
                         static_cast<int>(gfx::DebugView::AoBentNormal));
    debug_view_->addItem(QStringLiteral("SSR radiance"),
                         static_cast<int>(gfx::DebugView::SsrRadiance));
    debug_view_->addItem(QStringLiteral("SSR confidence"),
                         static_cast<int>(gfx::DebugView::SsrConfidence));
    debug_view_->addItem(QStringLiteral("GI radiance"),
                         static_cast<int>(gfx::DebugView::GiRadiance));
    debug_view_->addItem(QStringLiteral("VXGI voxels (Raymarch)"),
                         static_cast<int>(gfx::DebugView::VxgiVoxels));
    debug_view_->setToolTip(QStringLiteral("Select a diagnostic pass to inspect isolated buffers or shadow masks."));

    auto* image = new QWidget();
    auto* image_form = new QFormLayout(image);
    image_form->setContentsMargins(0, 0, 0, 0);
    image_form->addRow(QStringLiteral("Ambient"), ambient_intensity_);
    image_form->addRow(QStringLiteral("Exposure"), exposure_);
    image_form->addRow(QStringLiteral("Tonemap"), tonemap_);
    image_form->addRow(QStringLiteral("Debug view"), debug_view_);
    image_form->addRow(wireframe_);
    layout->addWidget(make_group(QStringLiteral("Image"), image));

    auto* reset = new QPushButton(QStringLiteral("Reset to Medium"));
    layout->addWidget(reset);
    status_ = new QLabel(QStringLiteral(
        "Phase 6: atmospheric fog with dual-layer height profiles, raymarched sun "
        "shafts, cloud shadows, and ground mist on top of the phase 5 lighting."));
    status_->setWordWrap(true);

    auto bind_sky_spin = [this](QDoubleSpinBox* spin, float gfx::SkySettings::*member) {
        connect(spin, &QDoubleSpinBox::valueChanged, this, [this, member](double value) {
            push_edit();
            state_.graphics.sky.*member = static_cast<float>(value);
        });
    };
    auto bind_ibl_spin = [this](QDoubleSpinBox* spin, float gfx::IblSettings::*member) {
        connect(spin, &QDoubleSpinBox::valueChanged, this, [this, member](double value) {
            push_edit();
            state_.graphics.ibl.*member = static_cast<float>(value);
        });
    };
    auto bind_fog_spin = [this](QDoubleSpinBox* spin, float gfx::FogSettings::*member) {
        connect(spin, &QDoubleSpinBox::valueChanged, this, [this, member](double value) {
            push_edit();
            state_.graphics.fog.*member = static_cast<float>(value);
        });
    };
    auto bind_fog_color = [this](QDoubleSpinBox* spin, Vec3 gfx::FogSettings::*member,
                                 float Vec3::*channel) {
        connect(spin, &QDoubleSpinBox::valueChanged, this, [this, member, channel](double value) {
            push_edit();
            (state_.graphics.fog.*member).*channel = static_cast<float>(value);
        });
    };

    connect(renderer_, &QComboBox::currentIndexChanged, this, [this](int index) {
        push_edit();
        state_.graphics.renderer = index == 1 ? gfx::RendererMode::Native
                                              : gfx::RendererMode::Legacy;
    });
    connect(preset_, &QComboBox::currentIndexChanged, this,
            [this](int index) { apply_preset(index); });
    connect(sun_size_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        push_edit();
        state_.graphics.sky.sun_radius = static_cast<float>(value);
    });
    connect(sun_intensity_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        push_edit();
        state_.graphics.sun.intensity = static_cast<float>(value);
    });
    connect(sun_temperature_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        push_edit();
        state_.graphics.sun.color_temperature = static_cast<float>(value);
    });
    connect(sun_disk_, &QCheckBox::toggled, this, [this](bool checked) {
        push_edit();
        state_.graphics.sun.specular_disk = checked;
    });
    connect(limb_darkening_, &QCheckBox::toggled, this, [this](bool checked) {
        push_edit();
        state_.graphics.sun.limb_darkening = checked;
    });
    connect(sky_coupling_, &QCheckBox::toggled, this, [this](bool checked) {
        push_edit();
        state_.graphics.sun.sky_coupling = checked;
    });
    connect(sky_enabled_, &QCheckBox::toggled, this, [this](bool checked) {
        push_edit();
        state_.graphics.sky.enabled = checked;
        refresh_from_state();
    });
    connect(sky_quality_, &QComboBox::currentIndexChanged, this, [this](int index) {
        push_edit();
        state_.graphics.sky.quality = index;
    });
    bind_sky_spin(moon_size_, &gfx::SkySettings::moon_radius);
    bind_sky_spin(sky_turbidity_, &gfx::SkySettings::turbidity);
    bind_sky_spin(sky_ozone_, &gfx::SkySettings::ozone);
    bind_sky_spin(sky_sun_radiance_, &gfx::SkySettings::sun_radiance);
    bind_sky_spin(sky_mie_anisotropy_, &gfx::SkySettings::mie_anisotropy);
    bind_sky_spin(moon_intensity_, &gfx::SkySettings::moon_intensity);
    bind_sky_spin(moon_phase_, &gfx::SkySettings::moon_phase);
    bind_sky_spin(star_brightness_, &gfx::SkySettings::star_brightness);
    bind_sky_spin(milky_way_, &gfx::SkySettings::milky_way_intensity);
    bind_sky_spin(cloud_coverage_, &gfx::SkySettings::cloud_coverage);
    bind_sky_spin(cloud_density_, &gfx::SkySettings::cloud_density);
    bind_sky_spin(cloud_speed_, &gfx::SkySettings::cloud_speed);
    bind_sky_spin(cloud_turbulence_, &gfx::SkySettings::cloud_turbulence);
    connect(ibl_quality_, &QComboBox::currentIndexChanged, this, [this](int index) {
        push_edit();
        state_.graphics.ibl.quality = index;
        refresh_from_state();
    });
    bind_ibl_spin(ibl_intensity_, &gfx::IblSettings::intensity);
    bind_ibl_spin(ibl_rotation_, &gfx::IblSettings::rotation_degrees);
    connect(ibl_update_hz_, &QSpinBox::valueChanged, this, [this](int value) {
        push_edit();
        state_.graphics.sky.ibl_update_hz = value;
    });
    connect(browse, &QPushButton::clicked, this, [this] { choose_environment(); });
    connect(clear, &QPushButton::clicked, this, [this] {
        if (state_.graphics.ibl.custom_path.empty()) {
            return;
        }
        push_edit();
        state_.graphics.ibl.custom_path.clear();
        refresh_from_state();
    });
    connect(shadow_quality_, &QComboBox::currentIndexChanged, this, [this](int index) {
        push_edit();
        state_.graphics.shadows.quality = index;
        refresh_from_state();
    });
    connect(shadow_pcss_, &QCheckBox::toggled, this, [this](bool checked) {
        push_edit();
        state_.graphics.shadows.pcss = checked;
        refresh_from_state();
    });
    connect(shadow_light_radius_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        push_edit();
        state_.graphics.shadows.light_radius = static_cast<float>(value);
    });
    connect(shadow_blur_, &QCheckBox::toggled, this, [this](bool checked) {
        push_edit();
        state_.graphics.shadows.blur = checked;
    });
    connect(contact_shadow_quality_, &QComboBox::currentIndexChanged, this, [this](int index) {
        push_edit();
        state_.graphics.shadows.contact_quality = index;
    });
    connect(ssgs_quality_, &QComboBox::currentIndexChanged, this, [this](int index) {
        push_edit();
        state_.graphics.shadows.ssgs_quality = index;
    });
    connect(sky_visibility_, &QComboBox::currentIndexChanged, this, [this](int index) {
        push_edit();
        state_.graphics.ao.sky_visibility = index;
    });
    connect(ao_type_, &QComboBox::currentIndexChanged, this, [this](int index) {
        push_edit();
        state_.graphics.ao.type = index;
        refresh_from_state();
    });
    connect(ao_quality_, &QComboBox::currentIndexChanged, this, [this](int index) {
        push_edit();
        state_.graphics.ao.quality = index;
        refresh_from_state();
    });
    connect(ao_radius_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        push_edit();
        state_.graphics.ao.radius = static_cast<float>(value);
    });
    connect(ao_intensity_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        push_edit();
        state_.graphics.ao.intensity = static_cast<float>(value);
    });
    connect(ssr_quality_, &QComboBox::currentIndexChanged, this, [this](int index) {
        push_edit();
        state_.graphics.ssr.quality = index;
        refresh_from_state();
    });
    connect(ssr_debug_, &QCheckBox::toggled, this, [this](bool checked) {
        push_edit();
        state_.graphics.ssr.debug = checked;
        refresh_from_state();
    });
    connect(ssr_debug_mode_, &QComboBox::currentIndexChanged, this, [this](int index) {
        push_edit();
        state_.graphics.ssr.debug_mode = index;
    });
    connect(gi_enable_, &QComboBox::currentIndexChanged, this, [this](int index) {
        push_edit();
        state_.graphics.gi.enable = index;
        refresh_from_state();
    });
    connect(gi_quality_, &QComboBox::currentIndexChanged, this, [this](int index) {
        push_edit();
        state_.graphics.gi.quality = index + 1;
    });
    connect(gi_filter_radius_, &QSpinBox::valueChanged, this, [this](int value) {
        push_edit();
        state_.graphics.gi.filter_radius = value;
    });
    connect(gi_prefilter_radius_, &QSpinBox::valueChanged, this, [this](int value) {
        push_edit();
        state_.graphics.gi.prefilter_radius = value;
    });
    connect(gi_sample_radius_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        push_edit();
        state_.graphics.gi.sample_radius = static_cast<float>(value);
    });
    connect(gi_thickness_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        push_edit();
        state_.graphics.gi.z_thickness = static_cast<float>(value);
    });
    connect(gi_bounce_weight_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        push_edit();
        state_.graphics.gi.bounce_weight = static_cast<float>(value);
    });
    connect(gi_il_amount_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        push_edit();
        state_.graphics.gi.il_amount = static_cast<float>(value);
    });
    connect(gi_ao_amount_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        push_edit();
        state_.graphics.gi.ao_amount = static_cast<float>(value);
    });
    connect(gi_sensitivity_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        push_edit();
        state_.graphics.gi.filter_sensitivity = static_cast<float>(value);
    });
    connect(gi_bounces_, &QCheckBox::toggled, this, [this](bool checked) {
        push_edit();
        state_.graphics.gi.bounces = checked;
    });
    connect(gi_specular_, &QCheckBox::toggled, this, [this](bool checked) {
        push_edit();
        state_.graphics.gi.specular_gi = checked;
    });
    connect(fog_enable_, &QComboBox::currentIndexChanged, this, [this](int index) {
        push_edit();
        state_.graphics.fog.enable = index;
        refresh_from_state();
    });
    connect(fog_quality_, &QComboBox::currentIndexChanged, this, [this](int index) {
        push_edit();
        state_.graphics.fog.quality = index;
    });
    connect(fog_debug_, &QComboBox::currentIndexChanged, this, [this](int index) {
        push_edit();
        state_.graphics.fog.debug = index;
    });
    connect(fog_cloud_shadow_, &QCheckBox::toggled, this, [this](bool checked) {
        push_edit();
        state_.graphics.fog.cloud_shadow = checked;
    });
    bind_fog_spin(fog_density_, &gfx::FogSettings::density);
    bind_fog_spin(fog_height_, &gfx::FogSettings::height);
    bind_fog_spin(fog_falloff_, &gfx::FogSettings::falloff);
    bind_fog_spin(fog_ground_density_, &gfx::FogSettings::ground_density);
    bind_fog_spin(fog_ground_height_, &gfx::FogSettings::ground_height);
    bind_fog_spin(fog_ground_falloff_, &gfx::FogSettings::ground_falloff);
    bind_fog_spin(fog_start_, &gfx::FogSettings::start_distance);
    bind_fog_spin(fog_cutoff_, &gfx::FogSettings::cutoff_distance);
    bind_fog_spin(fog_max_opacity_, &gfx::FogSettings::max_opacity);
    bind_fog_spin(fog_sun_intensity_, &gfx::FogSettings::sun_intensity);
    bind_fog_spin(fog_mie_phase_, &gfx::FogSettings::mie_phase);
    bind_fog_spin(fog_mist_noise_, &gfx::FogSettings::mist_noise);
    bind_fog_spin(fog_wind_speed_, &gfx::FogSettings::wind_speed);
    bind_fog_spin(fog_godray_intensity_, &gfx::FogSettings::godray_intensity);
    bind_fog_spin(fog_ray_density_, &gfx::FogSettings::ray_density);
    bind_fog_color(fog_color_r_, &gfx::FogSettings::color, &Vec3::x);
    bind_fog_color(fog_color_g_, &gfx::FogSettings::color, &Vec3::y);
    bind_fog_color(fog_color_b_, &gfx::FogSettings::color, &Vec3::z);
    bind_fog_color(fog_sun_color_r_, &gfx::FogSettings::sun_color, &Vec3::x);
    bind_fog_color(fog_sun_color_g_, &gfx::FogSettings::sun_color, &Vec3::y);
    bind_fog_color(fog_sun_color_b_, &gfx::FogSettings::sun_color, &Vec3::z);
    connect(ambient_intensity_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        push_edit();
        state_.graphics.sky.ambient_intensity = static_cast<float>(value);
    });
    connect(exposure_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        push_edit();
        state_.graphics.post.exposure = static_cast<float>(value);
    });
    connect(tonemap_, &QComboBox::currentIndexChanged, this, [this](int index) {
        push_edit();
        state_.graphics.post.tonemap = static_cast<gfx::TonemapOperator>(index);
    });
    connect(wireframe_, &QCheckBox::toggled, this, [this](bool checked) {
        push_edit();
        state_.graphics.wireframe = checked;
    });
    connect(debug_view_, &QComboBox::currentIndexChanged, this, [this](int index) {
        push_edit();
        state_.graphics.debug.view = static_cast<gfx::DebugView>(debug_view_->itemData(index).toInt());
    });
    connect(reset, &QPushButton::clicked, this, [this] {
        push_edit();
        const gfx::RendererMode renderer = state_.graphics.renderer;
        state_.graphics = gfx::GraphicsSettings::preset_values(gfx::QualityPreset::Medium);
        state_.graphics.renderer = renderer;
        refresh_from_state();
    });
}

void GraphicsPanel::push_edit() {
    if (record_edit_) {
        record_edit_();
    }
}

void GraphicsPanel::apply_preset(int index) {
    if (index < 0 || index > static_cast<int>(gfx::QualityPreset::Custom)) {
        return;
    }
    const auto preset = static_cast<gfx::QualityPreset>(index);
    if (preset == gfx::QualityPreset::Custom) {
        push_edit();
        state_.graphics.preset = preset;
        return;
    }
    push_edit();
    const gfx::RendererMode renderer = state_.graphics.renderer;
    state_.graphics = gfx::GraphicsSettings::preset_values(preset);
    state_.graphics.renderer = renderer;
    refresh_from_state();
}

void GraphicsPanel::refresh_preview() {
    if (ibl_error_provider_ != nullptr) {
        const std::string& path = state_.graphics.ibl.custom_path;
        const QString error = ibl_error_provider_();
        if (path.empty()) {
            ibl_status_->setText(QStringLiteral("Environment: procedural sky"));
            ibl_status_->setStyleSheet(QString());
        } else if (!error.isEmpty()) {
            ibl_status_->setText(QStringLiteral("Rejected: %1").arg(error));
            ibl_status_->setStyleSheet(QStringLiteral("color: #d9534f;"));
        } else {
            ibl_status_->setText(
                QStringLiteral("Loaded: %1")
                    .arg(QFileInfo(QString::fromStdString(path)).fileName()));
            ibl_status_->setStyleSheet(QString());
        }
    }
    if (preview_provider_ == nullptr || !isVisible()) {
        return;
    }
    const QImage image = preview_provider_(QSize(280, 150));
    if (!image.isNull()) {
        preview_->setPixmap(QPixmap::fromImage(image));
    }
}

void GraphicsPanel::choose_environment() {
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Choose environment"),
        QFileInfo(QString::fromStdString(state_.graphics.ibl.custom_path)).absolutePath(),
        QStringLiteral("Environment images (*.hdr *.dds);;All files (*)"));
    if (path.isEmpty()) {
        return;
    }
    push_edit();
    state_.graphics.ibl.custom_path = path.toStdString();
    refresh_from_state();
}

void GraphicsPanel::refresh_from_state() {
    const QSignalBlocker block_renderer(renderer_);
    const QSignalBlocker block_preset(preset_);
    const QSignalBlocker block_sun_size(sun_size_);
    const QSignalBlocker block_sun_intensity(sun_intensity_);
    const QSignalBlocker block_sun_temperature(sun_temperature_);
    const QSignalBlocker block_ambient(ambient_intensity_);
    const QSignalBlocker block_exposure(exposure_);
    const QSignalBlocker block_tonemap(tonemap_);
    const QSignalBlocker block_disk(sun_disk_);
    const QSignalBlocker block_limb(limb_darkening_);
    const QSignalBlocker block_coupling(sky_coupling_);
    const QSignalBlocker block_shadow_quality(shadow_quality_);
    const QSignalBlocker block_shadow_pcss(shadow_pcss_);
    const QSignalBlocker block_shadow_light_radius(shadow_light_radius_);
    const QSignalBlocker block_shadow_blur(shadow_blur_);
    const QSignalBlocker block_contact(contact_shadow_quality_);
    const QSignalBlocker block_ssgs(ssgs_quality_);
    const QSignalBlocker block_sky_vis(sky_visibility_);
    const QSignalBlocker block_ao_type(ao_type_);
    const QSignalBlocker block_ao_quality(ao_quality_);
    const QSignalBlocker block_ao_radius(ao_radius_);
    const QSignalBlocker block_ao_intensity(ao_intensity_);
    const QSignalBlocker block_ssr_quality(ssr_quality_);
    const QSignalBlocker block_ssr_debug(ssr_debug_);
    const QSignalBlocker block_ssr_debug_mode(ssr_debug_mode_);
    const QSignalBlocker block_gi_enable(gi_enable_);
    const QSignalBlocker block_gi_quality(gi_quality_);
    const QSignalBlocker block_gi_filter(gi_filter_radius_);
    const QSignalBlocker block_gi_prefilter(gi_prefilter_radius_);
    const QSignalBlocker block_gi_sample_radius(gi_sample_radius_);
    const QSignalBlocker block_gi_thickness(gi_thickness_);
    const QSignalBlocker block_gi_bounce_weight(gi_bounce_weight_);
    const QSignalBlocker block_gi_il_amount(gi_il_amount_);
    const QSignalBlocker block_gi_ao_amount(gi_ao_amount_);
    const QSignalBlocker block_gi_sensitivity(gi_sensitivity_);
    const QSignalBlocker block_gi_bounces(gi_bounces_);
    const QSignalBlocker block_gi_specular(gi_specular_);
    const QSignalBlocker block_wireframe(wireframe_);
    const QSignalBlocker block_debug_view(debug_view_);
    const QSignalBlocker block_sky_enabled(sky_enabled_);
    const QSignalBlocker block_sky_quality(sky_quality_);
    const QSignalBlocker block_moon_size(moon_size_);
    const QSignalBlocker block_turbidity(sky_turbidity_);
    const QSignalBlocker block_ozone(sky_ozone_);
    const QSignalBlocker block_sun_radiance(sky_sun_radiance_);
    const QSignalBlocker block_mie(sky_mie_anisotropy_);
    const QSignalBlocker block_moon_intensity(moon_intensity_);
    const QSignalBlocker block_moon_phase(moon_phase_);
    const QSignalBlocker block_stars(star_brightness_);
    const QSignalBlocker block_milky(milky_way_);
    const QSignalBlocker block_cloud_coverage(cloud_coverage_);
    const QSignalBlocker block_cloud_density(cloud_density_);
    const QSignalBlocker block_cloud_speed(cloud_speed_);
    const QSignalBlocker block_cloud_turbulence(cloud_turbulence_);
    const QSignalBlocker block_ibl_quality(ibl_quality_);
    const QSignalBlocker block_ibl_intensity(ibl_intensity_);
    const QSignalBlocker block_ibl_rotation(ibl_rotation_);
    const QSignalBlocker block_ibl_hz(ibl_update_hz_);
    const QSignalBlocker block_fog_enable(fog_enable_);
    const QSignalBlocker block_fog_quality(fog_quality_);
    const QSignalBlocker block_fog_debug(fog_debug_);
    const QSignalBlocker block_fog_cloud_shadow(fog_cloud_shadow_);
    const QSignalBlocker block_fog_density(fog_density_);
    const QSignalBlocker block_fog_height(fog_height_);
    const QSignalBlocker block_fog_falloff(fog_falloff_);
    const QSignalBlocker block_fog_ground_density(fog_ground_density_);
    const QSignalBlocker block_fog_ground_height(fog_ground_height_);
    const QSignalBlocker block_fog_ground_falloff(fog_ground_falloff_);
    const QSignalBlocker block_fog_start(fog_start_);
    const QSignalBlocker block_fog_cutoff(fog_cutoff_);
    const QSignalBlocker block_fog_max_opacity(fog_max_opacity_);
    const QSignalBlocker block_fog_color_r(fog_color_r_);
    const QSignalBlocker block_fog_color_g(fog_color_g_);
    const QSignalBlocker block_fog_color_b(fog_color_b_);
    const QSignalBlocker block_fog_sun_r(fog_sun_color_r_);
    const QSignalBlocker block_fog_sun_g(fog_sun_color_g_);
    const QSignalBlocker block_fog_sun_b(fog_sun_color_b_);
    const QSignalBlocker block_fog_sun_intensity(fog_sun_intensity_);
    const QSignalBlocker block_fog_mie_phase(fog_mie_phase_);
    const QSignalBlocker block_fog_mist(fog_mist_noise_);
    const QSignalBlocker block_fog_wind(fog_wind_speed_);
    const QSignalBlocker block_fog_godray(fog_godray_intensity_);
    const QSignalBlocker block_fog_ray_density(fog_ray_density_);

    const gfx::GraphicsSettings& graphics = state_.graphics;

    renderer_->setCurrentIndex(graphics.renderer == gfx::RendererMode::Native ? 1 : 0);
    preset_->setCurrentIndex(static_cast<int>(graphics.preset));
    sun_size_->setValue(graphics.sky.sun_radius);
    sun_intensity_->setValue(graphics.sun.intensity);
    sun_temperature_->setValue(graphics.sun.color_temperature);
    shadow_quality_->setCurrentIndex(std::clamp(graphics.shadows.quality, 0, 4));
    shadow_pcss_->setChecked(graphics.shadows.pcss);
    shadow_light_radius_->setValue(graphics.shadows.light_radius);
    shadow_blur_->setChecked(graphics.shadows.blur);
    contact_shadow_quality_->setCurrentIndex(std::clamp(graphics.shadows.contact_quality, 0, 3));
    ssgs_quality_->setCurrentIndex(std::clamp(graphics.shadows.ssgs_quality, 0, 3));
    sky_visibility_->setCurrentIndex(std::clamp(graphics.ao.sky_visibility, 0, 2));
    ao_type_->setCurrentIndex(std::clamp(graphics.ao.type, 0, 2));
    ao_quality_->setCurrentIndex(std::clamp(graphics.ao.quality, 0, 6));
    ao_radius_->setValue(graphics.ao.radius);
    ao_intensity_->setValue(graphics.ao.intensity);
    ssr_quality_->setCurrentIndex(std::clamp(graphics.ssr.quality, 0, 4));
    ssr_debug_->setChecked(graphics.ssr.debug);
    ssr_debug_mode_->setCurrentIndex(std::clamp(graphics.ssr.debug_mode, 0, 4));
    gi_enable_->setCurrentIndex(std::clamp(graphics.gi.enable, 0, 5));
    gi_quality_->setCurrentIndex(std::clamp(graphics.gi.quality, 1, 4) - 1);
    gi_filter_radius_->setValue(std::clamp(graphics.gi.filter_radius, 1, 8));
    gi_prefilter_radius_->setValue(std::clamp(graphics.gi.prefilter_radius, 1, 3));
    gi_sample_radius_->setValue(graphics.gi.sample_radius);
    gi_thickness_->setValue(graphics.gi.z_thickness);
    gi_bounce_weight_->setValue(graphics.gi.bounce_weight);
    gi_il_amount_->setValue(graphics.gi.il_amount);
    gi_ao_amount_->setValue(graphics.gi.ao_amount);
    gi_sensitivity_->setValue(graphics.gi.filter_sensitivity);
    gi_bounces_->setChecked(graphics.gi.bounces);
    gi_specular_->setChecked(graphics.gi.specular_gi);

    sky_enabled_->setChecked(graphics.sky.enabled);
    sky_quality_->setCurrentIndex(std::clamp(graphics.sky.quality, 0, 4));
    moon_size_->setValue(graphics.sky.moon_radius);
    sky_turbidity_->setValue(graphics.sky.turbidity);
    sky_ozone_->setValue(graphics.sky.ozone);
    sky_sun_radiance_->setValue(graphics.sky.sun_radiance);
    sky_mie_anisotropy_->setValue(graphics.sky.mie_anisotropy);
    moon_intensity_->setValue(graphics.sky.moon_intensity);
    moon_phase_->setValue(graphics.sky.moon_phase);
    star_brightness_->setValue(graphics.sky.star_brightness);
    milky_way_->setValue(graphics.sky.milky_way_intensity);
    cloud_coverage_->setValue(graphics.sky.cloud_coverage);
    cloud_density_->setValue(graphics.sky.cloud_density);
    cloud_speed_->setValue(graphics.sky.cloud_speed);
    cloud_turbulence_->setValue(graphics.sky.cloud_turbulence);
    ibl_quality_->setCurrentIndex(std::clamp(graphics.ibl.quality, 0, 2));
    ibl_intensity_->setValue(graphics.ibl.intensity);
    ibl_rotation_->setValue(graphics.ibl.rotation_degrees);
    ibl_update_hz_->setValue(std::clamp(graphics.sky.ibl_update_hz, 1, 60));
    ibl_path_->setText(QString::fromStdString(graphics.ibl.custom_path));

    // Enable 3 is behaviourally the same as 2, so the mode list collapses them
    // and the step budget lives in the quality control.
    fog_enable_->setCurrentIndex(graphics.fog.enable >= 2 ? 2 : std::clamp(graphics.fog.enable, 0, 1));
    fog_quality_->setCurrentIndex(std::clamp(graphics.fog.quality, 0, 3));
    fog_debug_->setCurrentIndex(std::clamp(graphics.fog.debug, 0, 4));
    fog_cloud_shadow_->setChecked(graphics.fog.cloud_shadow);
    fog_density_->setValue(graphics.fog.density);
    fog_height_->setValue(graphics.fog.height);
    fog_falloff_->setValue(graphics.fog.falloff);
    fog_ground_density_->setValue(graphics.fog.ground_density);
    fog_ground_height_->setValue(graphics.fog.ground_height);
    fog_ground_falloff_->setValue(graphics.fog.ground_falloff);
    fog_start_->setValue(graphics.fog.start_distance);
    fog_cutoff_->setValue(graphics.fog.cutoff_distance);
    fog_max_opacity_->setValue(graphics.fog.max_opacity);
    fog_color_r_->setValue(graphics.fog.color.x);
    fog_color_g_->setValue(graphics.fog.color.y);
    fog_color_b_->setValue(graphics.fog.color.z);
    fog_sun_color_r_->setValue(graphics.fog.sun_color.x);
    fog_sun_color_g_->setValue(graphics.fog.sun_color.y);
    fog_sun_color_b_->setValue(graphics.fog.sun_color.z);
    fog_sun_intensity_->setValue(graphics.fog.sun_intensity);
    fog_mie_phase_->setValue(graphics.fog.mie_phase);
    fog_mist_noise_->setValue(graphics.fog.mist_noise);
    fog_wind_speed_->setValue(graphics.fog.wind_speed);
    fog_godray_intensity_->setValue(graphics.fog.godray_intensity);
    fog_ray_density_->setValue(graphics.fog.ray_density);

    const bool shadows_enabled = graphics.shadows.quality > 0;
    shadow_pcss_->setEnabled(shadows_enabled);
    shadow_light_radius_->setEnabled(shadows_enabled && graphics.shadows.pcss);
    shadow_blur_->setEnabled(shadows_enabled);
    contact_shadow_quality_->setEnabled(shadows_enabled);
    ssgs_quality_->setEnabled(shadows_enabled);
    sky_visibility_->setEnabled(shadows_enabled);

    const bool ao_on = graphics.ao.quality > 0;
    gate_control(ao_radius_, ao_on,
                 QStringLiteral("Raise the AO quality above Disabled to sample."));
    gate_control(ao_intensity_, ao_on,
                 QStringLiteral("Raise the AO quality above Disabled to sample."));

    const bool ssr_on = graphics.ssr.quality > 0;
    gate_control(ssr_debug_, ssr_on,
                 QStringLiteral("Raise the SSR quality above Disabled to trace."));
    gate_control(ssr_debug_mode_, ssr_on && graphics.ssr.debug,
                 QStringLiteral("Enable the trace debug output to choose a value."));

    const bool gi_on = graphics.gi.enable > 0;
    const QString gi_reason = QStringLiteral("Choose a GI mode to edit the trace settings.");
    for (QWidget* widget : std::array<QWidget*, 11>{
             gi_quality_, gi_filter_radius_, gi_prefilter_radius_, gi_sample_radius_,
             gi_thickness_, gi_bounce_weight_, gi_il_amount_, gi_ao_amount_, gi_sensitivity_,
             gi_bounces_, gi_specular_}) {
        gate_control(widget, gi_on, gi_reason);
    }

    const bool sky_on = graphics.sky.enabled;
    const QString sky_reason = QStringLiteral("Enable the analytic sky to edit this value.");
    for (QWidget* widget : std::array<QWidget*, 14>{
             sky_quality_, moon_size_, sky_turbidity_, sky_ozone_, sky_sun_radiance_,
             sky_mie_anisotropy_, moon_intensity_, moon_phase_, star_brightness_, milky_way_,
             cloud_coverage_, cloud_density_, cloud_speed_, cloud_turbulence_}) {
        gate_control(widget, sky_on, sky_reason);
    }

    gate_control(sky_coupling_, sky_on,
                 sky_on ? QStringLiteral("Only the direct sun can fade into the sky.")
                        : QStringLiteral("Enable the analytic sky to couple the sun."));

    const bool ibl_lit = graphics.ibl.quality > 0;
    const bool ibl_sourced = sky_on || !graphics.ibl.custom_path.empty();
    QString ibl_reason;
    if (!ibl_lit) {
        ibl_reason = QStringLiteral("Raise the IBL quality above Off to use the environment.");
    } else if (!ibl_sourced) {
        ibl_reason = QStringLiteral(
            "Enable the analytic sky or choose an environment image first.");
    }
    const bool ibl_enabled = ibl_lit && ibl_sourced;
    gate_control(ibl_intensity_, ibl_enabled, ibl_reason);
    gate_control(ibl_rotation_, ibl_enabled, ibl_reason);
    gate_control(ibl_update_hz_, sky_on && graphics.ibl.quality > 0,
                 QStringLiteral("Enable the analytic sky to capture it."));

    const bool fog_on = graphics.fog.enable > 0;
    const bool fog_volumetric = graphics.fog.enable >= 2;
    const QString fog_reason = QStringLiteral("Choose a fog mode above Off to edit the medium.");
    for (QWidget* widget : std::array<QWidget*, 15>{
             fog_density_, fog_height_, fog_falloff_, fog_ground_density_, fog_ground_height_,
             fog_ground_falloff_, fog_start_, fog_cutoff_, fog_max_opacity_, fog_color_r_,
             fog_color_g_, fog_color_b_, fog_sun_color_r_, fog_sun_color_g_, fog_sun_color_b_}) {
        gate_control(widget, fog_on, fog_reason);
    }
    const QString fog_volume_reason =
        QStringLiteral("Choose the volumetric mode to march the medium.");
    for (QWidget* widget : std::array<QWidget*, 8>{
             fog_quality_, fog_debug_, fog_sun_intensity_, fog_mie_phase_, fog_mist_noise_,
             fog_wind_speed_, fog_godray_intensity_, fog_ray_density_}) {
        gate_control(widget, fog_volumetric, fog_volume_reason);
    }
    gate_control(fog_cloud_shadow_, fog_volumetric && sky_on,
                 sky_on ? fog_volume_reason
                        : QStringLiteral("Enable the analytic sky so there are clouds to shadow."));

    refresh_preview();

    ambient_intensity_->setValue(graphics.sky.ambient_intensity);
    exposure_->setValue(graphics.post.exposure);
    tonemap_->setCurrentIndex(static_cast<int>(graphics.post.tonemap));
    sun_disk_->setChecked(graphics.sun.specular_disk);
    limb_darkening_->setChecked(graphics.sun.limb_darkening);
    sky_coupling_->setChecked(graphics.sun.sky_coupling);
    wireframe_->setChecked(graphics.wireframe);

    const int debug_idx = debug_view_->findData(static_cast<int>(graphics.debug.view));
    debug_view_->setCurrentIndex(debug_idx >= 0 ? debug_idx : 0);
}

} // namespace hikari::studio