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
        "Phase 4: analytic sky, day and night, clouds, image based lighting, and a "
        "custom environment, on top of the phase 3 shadows."));
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

    const bool shadows_enabled = graphics.shadows.quality > 0;
    shadow_pcss_->setEnabled(shadows_enabled);
    shadow_light_radius_->setEnabled(shadows_enabled && graphics.shadows.pcss);
    shadow_blur_->setEnabled(shadows_enabled);
    contact_shadow_quality_->setEnabled(shadows_enabled);
    ssgs_quality_->setEnabled(shadows_enabled);
    sky_visibility_->setEnabled(shadows_enabled);

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