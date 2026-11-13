#pragma once

#include <functional>
#include <vector>

#include <QString>
#include <QStringList>

#include "hikari/gfx/material_import.hpp"

namespace hikari::studio {

// One subset row of the Materials dock. The index is the mesh subset, which is
// also the material index for a PMX model.
struct MaterialSubsetEntry {
    QString name;
    int shading_model = 0;
    bool overridden = false;
};

// The viewport owns the loaded models, so the Graphics dock reads and writes
// their per-subset material responses through these callbacks. The main window
// installs them after both widgets exist.
struct MaterialSectionCallbacks {
    std::function<QStringList()> model_names;
    std::function<std::vector<MaterialSubsetEntry>(int model_index)> subsets;
    std::function<void(int model_index, int subset, int shading_model)> set_shading_model;
    std::function<void(int model_index, int subset, int preset_index)> apply_preset;
    std::function<void(int model_index, int subset)> clear_override;
    // Converts a Ray-MMD material file into the subset override and returns the
    // converter report, which the panel shows as applied/approximate/unsupported
    // counts. The source is read as data; its shader body never runs.
    std::function<gfx::ImportReport(int model_index, int subset, const QString& path)>
        import_ray_mmd;
};

} // namespace hikari::studio
