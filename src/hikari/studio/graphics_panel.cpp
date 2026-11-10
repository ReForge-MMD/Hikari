#include "hikari/studio/graphics_panel.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <array>

#include "hikari/gfx/settings.hpp"

namespace hikari::studio {
namespace {

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
    refresh_from_state();
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

    sun_mode_ = new QComboBox();
    sun_mode_->addItems({QStringLiteral("Disabled"), QStringLiteral("Simple directional"),
                         QStringLiteral("Sky coupled")});
    sun_intensity_ = make_spin(0.0, 100.0, 0.1, 2);
    sun_temperature_ = make_spin(1000.0, 20000.0, 100.0, 0);
    sun_temperature_->setSuffix(QStringLiteral(" K"));
    sun_disk_ = new QCheckBox(QStringLiteral("Physical sun disk specular"));
    limb_darkening_ = new QCheckBox(QStringLiteral("Solar limb darkening"));

    auto* sun = new QWidget();
    auto* sun_form = new QFormLayout(sun);
    sun_form->setContentsMargins(0, 0, 0, 0);
    sun_form->addRow(QStringLiteral("Mode"), sun_mode_);
    sun_form->addRow(QStringLiteral("Intensity"), sun_intensity_);
    sun_form->addRow(QStringLiteral("Temperature"), sun_temperature_);
    sun_form->addRow(sun_disk_);
    sun_form->addRow(limb_darkening_);
    layout->addWidget(make_group(QStringLiteral("Sun"), sun));

    shadow_quality_ = new QComboBox();
    shadow_quality_->addItems({QStringLiteral("Disabled"), QStringLiteral("Low (1024)"),
                               QStringLiteral("Medium (2048)"), QStringLiteral("High (4096)"),
                               QStringLiteral("Cinematic (4096)")});
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
        "Phase 3: G-buffer, sun lighting, CSM, PCSS, contact shadows, SSGS, shadow blur and sky visibility."));
    status_->setWordWrap(true);

    connect(renderer_, &QComboBox::currentIndexChanged, this, [this](int index) {
        push_edit();
        state_.graphics.renderer = index == 1 ? gfx::RendererMode::Native
                                              : gfx::RendererMode::Legacy;
    });
    connect(preset_, &QComboBox::currentIndexChanged, this,
            [this](int index) { apply_preset(index); });
    connect(sun_mode_, &QComboBox::currentIndexChanged, this, [this](int index) {
        push_edit();
        state_.graphics.sun.light_mode = index;
    });
    connect(sun_intensity_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        push_edit();
        state_.graphics.sun.intensity = static_cast<float>(value);
    });
    connect(sun_temperature_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        push_edit();
        state_.graphics.sun.color_temperature = static_cast<float>(value);
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
    connect(sun_disk_, &QCheckBox::toggled, this, [this](bool checked) {
        push_edit();
        state_.graphics.sun.specular_disk = checked;
    });
    connect(limb_darkening_, &QCheckBox::toggled, this, [this](bool checked) {
        push_edit();
        state_.graphics.sun.limb_darkening = checked;
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

void GraphicsPanel::refresh_from_state() {
    const QSignalBlocker block_renderer(renderer_);
    const QSignalBlocker block_preset(preset_);
    const QSignalBlocker block_sun_mode(sun_mode_);
    const QSignalBlocker block_sun_intensity(sun_intensity_);
    const QSignalBlocker block_sun_temperature(sun_temperature_);
    const QSignalBlocker block_ambient(ambient_intensity_);
    const QSignalBlocker block_exposure(exposure_);
    const QSignalBlocker block_tonemap(tonemap_);
    const QSignalBlocker block_disk(sun_disk_);
    const QSignalBlocker block_limb(limb_darkening_);
    const QSignalBlocker block_shadow_quality(shadow_quality_);
    const QSignalBlocker block_shadow_pcss(shadow_pcss_);
    const QSignalBlocker block_shadow_light_radius(shadow_light_radius_);
    const QSignalBlocker block_shadow_blur(shadow_blur_);
    const QSignalBlocker block_contact(contact_shadow_quality_);
    const QSignalBlocker block_ssgs(ssgs_quality_);
    const QSignalBlocker block_sky_vis(sky_visibility_);
    const QSignalBlocker block_wireframe(wireframe_);
    const QSignalBlocker block_debug_view(debug_view_);

    renderer_->setCurrentIndex(state_.graphics.renderer == gfx::RendererMode::Native ? 1 : 0);
    preset_->setCurrentIndex(static_cast<int>(state_.graphics.preset));
    sun_mode_->setCurrentIndex(state_.graphics.sun.light_mode);
    sun_intensity_->setValue(state_.graphics.sun.intensity);
    sun_temperature_->setValue(state_.graphics.sun.color_temperature);
    shadow_quality_->setCurrentIndex(std::clamp(state_.graphics.shadows.quality, 0, 4));
    shadow_pcss_->setChecked(state_.graphics.shadows.pcss);
    shadow_light_radius_->setValue(state_.graphics.shadows.light_radius);
    shadow_blur_->setChecked(state_.graphics.shadows.blur);
    contact_shadow_quality_->setCurrentIndex(std::clamp(state_.graphics.shadows.contact_quality, 0, 3));
    ssgs_quality_->setCurrentIndex(std::clamp(state_.graphics.shadows.ssgs_quality, 0, 3));
    sky_visibility_->setCurrentIndex(std::clamp(state_.graphics.ao.sky_visibility, 0, 2));

    const bool shadows_enabled = state_.graphics.shadows.quality > 0;
    shadow_pcss_->setEnabled(shadows_enabled);
    shadow_light_radius_->setEnabled(shadows_enabled && state_.graphics.shadows.pcss);
    shadow_blur_->setEnabled(shadows_enabled);
    contact_shadow_quality_->setEnabled(shadows_enabled);
    ssgs_quality_->setEnabled(shadows_enabled);
    sky_visibility_->setEnabled(shadows_enabled);

    ambient_intensity_->setValue(state_.graphics.sky.ambient_intensity);
    exposure_->setValue(state_.graphics.post.exposure);
    tonemap_->setCurrentIndex(static_cast<int>(state_.graphics.post.tonemap));
    sun_disk_->setChecked(state_.graphics.sun.specular_disk);
    limb_darkening_->setChecked(state_.graphics.sun.limb_darkening);
    wireframe_->setChecked(state_.graphics.wireframe);

    const int debug_idx = debug_view_->findData(static_cast<int>(state_.graphics.debug.view));
    debug_view_->setCurrentIndex(debug_idx >= 0 ? debug_idx : 0);
}

} // namespace hikari::studio
