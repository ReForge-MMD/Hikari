#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "hikari/gfx/material.hpp"

namespace hikari::gfx {

// How the converter treated one parameter of a Ray-MMD material file.
enum class ImportStatus : int {
    Applied = 0,
    Approximate = 1,
    Unsupported = 2,
};

std::string_view import_status_name(ImportStatus status);

struct ImportParameter {
    std::string name;
    ImportStatus status = ImportStatus::Unsupported;
    std::string detail;
};

// The converter report: every parameter the file declares, in file order, plus
// the per-status totals a caller can show without walking the list.
struct ImportReport {
    bool ok = false;
    std::string error;
    std::vector<ImportParameter> parameters;
    int applied = 0;
    int approximate = 0;
    int unsupported = 0;
};

struct RayMmdImport {
    MaterialResponse response;
    ImportReport report;
};

// Converts the constant and define block of a Ray-MMD material `.fx` into a
// native material response. The source is read as data only: its `#include`
// chain, HLSL body, and texture slots are never compiled, resolved, or loaded.
// Parameters with no native field are listed in the report as unsupported.
RayMmdImport import_ray_mmd_material(std::string_view source);

} // namespace hikari::gfx
