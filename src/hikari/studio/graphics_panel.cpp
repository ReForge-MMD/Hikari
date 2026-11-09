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

    ambient_intensity_ = make_spin(0.0, 10.0, 0.05, 2);
    exposure_ = make_spin(0.0, 100.0, 0.05, 2);
    tonemap_ = new QComboBox();
    for (int i = 0; i <= static_cast<int>(gfx::TonemapOperator::AgXGolden); ++i) {
        tonemap_->addItem(
            gfx::tonemap_operator_name(static_cast<gfx::TonemapOperator>(i)));
    }
    wireframe_ = new QCheckBox(QStringLiteral("Wireframe"));

    auto* image = new QWidget();
    auto* image_form = new QFormLayout(image);
    image_form->setContentsMargins(0, 0, 0, 0);
    image_form->addRow(QStringLiteral("Ambient"), ambient_intensity_);
    image_form->addRow(QStringLiteral("Exposure"), exposure_);
    image_form->addRow(QStringLiteral("Tonemap"), tonemap_);
    image_form->addRow(wireframe_);
    layout->addWidget(make_group(QStringLiteral("Image"), image));

    auto* reset = new QPushButton(QStringLiteral("Reset to Medium"));
    layout->addWidget(reset);
    status_ = new QLabel(QStringLiteral(
        "Phase 2: G-buffer, sun lighting, PBR and AgX. Shadows, IBL, AO, SSR, GI, "
        "fog, SSS and the full post chain are added in the next phases."));
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
    const QSignalBlocker block_wireframe(wireframe_);

    renderer_->setCurrentIndex(state_.graphics.renderer == gfx::RendererMode::Native ? 1 : 0);
    preset_->setCurrentIndex(static_cast<int>(state_.graphics.preset));
    sun_mode_->setCurrentIndex(state_.graphics.sun.light_mode);
    sun_intensity_->setValue(state_.graphics.sun.intensity);
    sun_temperature_->setValue(state_.graphics.sun.color_temperature);
    ambient_intensity_->setValue(state_.graphics.sky.ambient_intensity);
    exposure_->setValue(state_.graphics.post.exposure);
    tonemap_->setCurrentIndex(static_cast<int>(state_.graphics.post.tonemap));
    sun_disk_->setChecked(state_.graphics.sun.specular_disk);
    limb_darkening_->setChecked(state_.graphics.sun.limb_darkening);
    wireframe_->setChecked(state_.graphics.wireframe);
}

} // namespace hikari::studio
