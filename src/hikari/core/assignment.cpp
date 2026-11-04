#include "hikari/core/effect.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "hikari/core/backend.hpp"
#include "hikari/core/log.hpp"
#include "hikari/core/mmd_host.hpp"

namespace hikari::core {

std::vector<EffectItemInfo> g_effect_registry;
std::mutex g_effect_registry_mutex;
std::mutex g_assignment_mutex;
ModelAssignmentRegistry g_assignment_registry;
std::atomic_bool g_has_assigned_effects{false};
std::atomic_bool g_effects_enabled{true};
std::atomic_bool g_auto_reload{true};
std::atomic_bool g_auto_save_mapping{true};
std::atomic_bool g_skip_device_validation{true};

namespace {

std::mutex g_deferred_mutex;
std::vector<HikariEffect*> g_deferred_effects;

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string trim(const std::string& value) {
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return {};
    }
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
}

std::vector<std::string> lines_of(const std::string& text) {
    std::vector<std::string> lines;
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        lines.push_back(trim(line));
    }
    return lines;
}

std::string read_text(const std::string& path, std::string* error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        if (error != nullptr) {
            *error = "Failed to read effect file '" + path + "'";
        }
        return {};
    }
    std::ostringstream stream;
    stream << file.rdbuf();
    return stream.str();
}

bool write_text(const std::string& path, const std::string& text, std::string* error) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        if (error != nullptr) {
            *error = "Failed to write '" + path + "'";
        }
        return false;
    }
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    return static_cast<bool>(file);
}

std::string object_key(const HostObject& object, std::size_t index) {
    return (object.is_pmd ? "Pmd" : "Acs") + std::to_string(index);
}

std::string base_name(const std::string& path) {
    return std::filesystem::path(path).filename().string();
}

std::vector<std::string> split_dots(const std::string& key) {
    std::vector<std::string> parts;
    std::string current;
    for (char c : key) {
        if (c == '.') {
            parts.push_back(current);
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    parts.push_back(current);
    return parts;
}

bool is_number(const std::string& value) {
    if (value.empty()) {
        return false;
    }
    return std::all_of(value.begin(), value.end(),
                       [](unsigned char c) { return std::isdigit(c) != 0; });
}

} // namespace

bool match_mme_pattern(const std::string& pattern_in, const std::string& text_in) {
    const std::string pattern = lower(trim(pattern_in));
    const std::string text = lower(trim(text_in));
    if (pattern == "*") {
        return true;
    }
    std::size_t px = 0;
    std::size_t tx = 0;
    std::size_t next_px = 0;
    std::size_t next_tx = 0;
    bool star = false;
    while (tx < text.size()) {
        if (px < pattern.size() && (pattern[px] == '?' || pattern[px] == text[tx])) {
            ++px;
            ++tx;
        } else if (px < pattern.size() && pattern[px] == '*') {
            star = true;
            ++px;
            next_px = px;
            next_tx = tx + 1;
        } else if (star) {
            px = next_px;
            tx = next_tx;
            ++next_tx;
        } else {
            return false;
        }
    }
    while (px < pattern.size() && pattern[px] == '*') {
        ++px;
    }
    return px == pattern.size();
}

HikariEffect* create_effect_instance(const std::string& path, std::string* error) {
    if (path.empty()) {
        if (error != nullptr) {
            *error = "Empty path";
        }
        return nullptr;
    }
    std::string local_error;
    const std::string source = read_text(path, &local_error);
    if (source.empty() && !local_error.empty()) {
        if (error != nullptr) {
            *error = local_error;
        }
        return nullptr;
    }
    std::lock_guard lock(g_d3d11_mutex);
    if (!g_d3d11) {
        if (error != nullptr) {
            *error = "DirectX 11 context is not initialized yet";
        }
        return nullptr;
    }
    try {
        return HikariEffect::create(source, path, *g_d3d11);
    } catch (const std::exception& exception) {
        if (error != nullptr) {
            *error = exception.what();
        }
        return nullptr;
    }
}

std::size_t load_effect_from_file(const std::string& path, std::string* error) {
    HikariEffect* effect = create_effect_instance(path, error);
    if (effect == nullptr) {
        return static_cast<std::size_t>(-1);
    }
    std::lock_guard lock(g_effect_registry_mutex);
    const std::size_t index = g_effect_registry.size();
    g_effect_registry.push_back({path, base_name(path), effect, true});
    log_msg("Successfully loaded Hikari effect #" + std::to_string(index) + ": '" + path + "'");
    return index;
}

bool reload_effect(std::size_t index, std::string* error) {
    HikariEffect* replacement = nullptr;
    std::string path;
    {
        std::lock_guard lock(g_effect_registry_mutex);
        if (index >= g_effect_registry.size()) {
            if (error != nullptr) {
                *error = "Effect index out of bounds";
            }
            return false;
        }
        auto& item = g_effect_registry[index];
        path = item.path;
        replacement = create_effect_instance(path, error);
        if (replacement == nullptr) {
            return false;
        }
        if (item.effect != nullptr) {
            for (const auto& [name, old_index] : item.effect->param_by_name) {
                const auto new_it = replacement->param_by_name.find(name);
                if (new_it == replacement->param_by_name.end()) {
                    continue;
                }
                const auto& old_parameter = item.effect->parameters[old_index];
                const auto& new_parameter = replacement->parameters[new_it->second];
                if (old_parameter.size == new_parameter.size &&
                    old_parameter.offset + old_parameter.size <=
                        item.effect->constant_data.size() &&
                    new_parameter.offset + new_parameter.size <=
                        replacement->constant_data.size()) {
                    std::copy_n(item.effect->constant_data.begin() +
                                    static_cast<std::ptrdiff_t>(old_parameter.offset),
                                static_cast<std::ptrdiff_t>(old_parameter.size),
                                replacement->constant_data.begin() +
                                    static_cast<std::ptrdiff_t>(new_parameter.offset));
                    replacement->constant_dirty = true;
                }
            }
            item.effect->release();
        }
        item.effect = replacement;
    }
    log_msg("Successfully reloaded Hikari effect #" + std::to_string(index) + ": '" + path + "'");
    return true;
}

void defer_drop_effect(HikariEffect* effect) {
    if (effect != nullptr) {
        std::lock_guard lock(g_deferred_mutex);
        g_deferred_effects.push_back(effect);
    }
}

void drain_deferred_effects() {
    std::vector<HikariEffect*> pending;
    {
        std::lock_guard lock(g_deferred_mutex);
        pending.swap(g_deferred_effects);
    }
    for (auto* effect : pending) {
        effect->release();
    }
}

std::optional<ActivePassInfo> get_active_direct_effect_pass() {
    std::lock_guard lock(g_effect_registry_mutex);
    for (auto& item : g_effect_registry) {
        if (item.enabled && item.effect != nullptr) {
            update_effect_standard_matrices(*item.effect);
            item.effect->update_gpu_constants();
            return item.effect->get_first_pass_info();
        }
    }
    return std::nullopt;
}

std::optional<ActivePassInfo> get_assigned_effect_pass(std::int32_t object_order,
                                                       std::int32_t subset_index,
                                                       std::int32_t current_tech) {
    if (!g_has_assigned_effects.load(std::memory_order_relaxed)) {
        return std::nullopt;
    }
    std::lock_guard lock(g_assignment_mutex);
    HikariEffect* effect = g_assignment_registry.resolve_effect(object_order, subset_index);
    if (effect == nullptr) {
        return std::nullopt;
    }
    update_effect_standard_matrices(*effect);
    effect->update_gpu_constants();
    return effect->get_technique_pass_info(current_tech);
}

ModelAssignmentRegistry::ModelAssignmentRegistry() {
    OffscreenTabEntry main_tab;
    main_tab.name = "Main";
    main_tab.description = "Main Render Target";
    main_tab.is_offscreen = false;
    tabs.push_back(std::move(main_tab));
}

void ModelAssignmentRegistry::update_has_assigned() {
    bool has = default_effect != nullptr;
    for (const auto& [id, model] : models) {
        if (model.effect != nullptr) {
            has = true;
            break;
        }
        for (const auto& [subset, assignment] : model.subsets) {
            if (assignment.effect != nullptr) {
                has = true;
                break;
            }
        }
        if (has) {
            break;
        }
    }
    if (!has) {
        for (const auto& tab : tabs) {
            if (tab.default_effect != nullptr) {
                has = true;
                break;
            }
            for (const auto& [id, assignment] : tab.model_overrides) {
                if (assignment.effect != nullptr) {
                    has = true;
                    break;
                }
            }
            for (const auto& [key, assignment] : tab.subset_overrides) {
                if (assignment.effect != nullptr) {
                    has = true;
                    break;
                }
            }
            if (has) {
                break;
            }
        }
    }
    g_has_assigned_effects.store(has, std::memory_order_release);
}

bool ModelAssignmentRegistry::has_assigned() const {
    return g_has_assigned_effects.load(std::memory_order_relaxed);
}

bool ModelAssignmentRegistry::assign_default_effect(const std::string& path, std::string* error) {
    if (path.empty()) {
        if (default_effect != nullptr) {
            defer_drop_effect(default_effect);
            default_effect = nullptr;
        }
        default_effect_path.clear();
        prune_unused_tabs();
        update_has_assigned();
        return true;
    }
    std::string compile_error;
    HikariEffect* effect = create_effect_instance(path, &compile_error);
    default_effect_path = path;
    if (effect == nullptr) {
        if (default_effect != nullptr) {
            defer_drop_effect(default_effect);
            default_effect = nullptr;
        }
        update_has_assigned();
        log_msg("[Assignment] Recorded default effect path '" + path +
                "' (compilation deferred/failed: " + compile_error + ")");
        if (error != nullptr) {
            *error = compile_error;
        }
        return false;
    }
    if (default_effect != nullptr) {
        defer_drop_effect(default_effect);
    }
    default_effect = effect;
    scan_and_register_offscreen_tabs(read_text(path, nullptr));
    g_has_assigned_effects.store(true, std::memory_order_release);
    log_msg("[Assignment] Set default effect: '" + path + "'");
    return true;
}

bool ModelAssignmentRegistry::assign_model_effect(std::size_t object_id,
                                                  const std::string& filename,
                                                  const std::string& path, std::string* error) {
    auto& model = models[object_id];
    model.object_id = object_id;
    if (!filename.empty()) {
        model.filename = filename;
    }
    if (path.empty()) {
        if (model.effect != nullptr) {
            defer_drop_effect(model.effect);
            model.effect = nullptr;
        }
        model.effect_path.clear();
        prune_unused_tabs();
        update_has_assigned();
        return true;
    }
    std::string compile_error;
    HikariEffect* effect = create_effect_instance(path, &compile_error);
    model.effect_path = path;
    model.enabled = true;
    if (effect == nullptr) {
        if (model.effect != nullptr) {
            defer_drop_effect(model.effect);
            model.effect = nullptr;
        }
        update_has_assigned();
        log_msg("[Assignment] Recorded model effect path '" + path +
                "' (compilation deferred/failed: " + compile_error + ")");
        if (error != nullptr) {
            *error = compile_error;
        }
        return false;
    }
    if (model.effect != nullptr) {
        defer_drop_effect(model.effect);
    }
    model.effect = effect;
    scan_and_register_offscreen_tabs(read_text(path, nullptr));
    g_has_assigned_effects.store(true, std::memory_order_release);
    log_msg("[Assignment] Set model effect: '" + path + "'");
    return true;
}

bool ModelAssignmentRegistry::assign_subset_effect(std::size_t object_id,
                                                   const std::string& filename,
                                                   std::int32_t subset, const std::string& path,
                                                   std::string* error) {
    auto& model = models[object_id];
    model.object_id = object_id;
    if (!filename.empty()) {
        model.filename = filename;
    }
    if (path.empty()) {
        const auto it = model.subsets.find(subset);
        if (it != model.subsets.end()) {
            if (it->second.effect != nullptr) {
                defer_drop_effect(it->second.effect);
                it->second.effect = nullptr;
            }
            it->second.effect_path.clear();
        }
        prune_unused_tabs();
        update_has_assigned();
        return true;
    }
    std::string compile_error;
    HikariEffect* effect = create_effect_instance(path, &compile_error);
    auto& assignment = model.subsets[subset];
    assignment.effect_path = path;
    assignment.enabled = true;
    if (effect == nullptr) {
        if (assignment.effect != nullptr) {
            defer_drop_effect(assignment.effect);
            assignment.effect = nullptr;
        }
        update_has_assigned();
        log_msg("[Assignment] Recorded subset effect path '" + path +
                "' (compilation deferred/failed: " + compile_error + ")");
        if (error != nullptr) {
            *error = compile_error;
        }
        return false;
    }
    if (assignment.effect != nullptr) {
        defer_drop_effect(assignment.effect);
    }
    assignment.effect = effect;
    scan_and_register_offscreen_tabs(read_text(path, nullptr));
    g_has_assigned_effects.store(true, std::memory_order_release);
    log_msg("[Assignment] Set model subset #" + std::to_string(subset) + " effect: '" + path + "'");
    return true;
}

void ModelAssignmentRegistry::toggle_subset_expand(std::size_t object_id) {
    if (expanded_models.count(object_id) != 0) {
        expanded_models.erase(object_id);
    } else {
        expanded_models.insert(object_id);
    }
}

bool ModelAssignmentRegistry::is_subset_expanded(std::size_t object_id) const {
    return expanded_models.count(object_id) != 0;
}

void ModelAssignmentRegistry::sync_host_objects() {
    const auto host_objects = enumerate_host_objects();
    if (is_mmd_bound()) {
        std::unordered_set<std::size_t> current_ids;
        for (const auto& object : host_objects) {
            current_ids.insert(object.id);
        }
        for (auto it = models.begin(); it != models.end();) {
            if (current_ids.count(it->first) == 0) {
                if (it->second.effect != nullptr) {
                    defer_drop_effect(it->second.effect);
                }
                for (auto& [subset, assignment] : it->second.subsets) {
                    if (assignment.effect != nullptr) {
                        defer_drop_effect(assignment.effect);
                    }
                }
                it = models.erase(it);
            } else {
                ++it;
            }
        }
        for (auto& tab : tabs) {
            for (auto it = tab.model_overrides.begin(); it != tab.model_overrides.end();) {
                it = current_ids.count(it->first) == 0 ? tab.model_overrides.erase(it) : std::next(it);
            }
            for (auto it = tab.subset_overrides.begin(); it != tab.subset_overrides.end();) {
                const std::size_t id = static_cast<std::size_t>(it->first >> 32);
                it = current_ids.count(id) == 0 ? tab.subset_overrides.erase(it) : std::next(it);
            }
        }
    }

    order_to_id.clear();
    for (const auto& object : host_objects) {
        order_to_id[object.order] = object.id;
        auto& entry = models[object.id];
        entry.object_id = object.id;
        if (entry.filename != object.filename) {
            entry.filename = object.filename;
            entry.effect_path.clear();
            if (entry.effect != nullptr) {
                defer_drop_effect(entry.effect);
                entry.effect = nullptr;
            }
            for (auto& [subset, assignment] : entry.subsets) {
                if (assignment.effect != nullptr) {
                    defer_drop_effect(assignment.effect);
                }
            }
            entry.subsets.clear();
        }
        entry.enabled = object.disp;

        if (!object.is_pmd && entry.effect_path.empty() && !object.filename.empty()) {
            const std::filesystem::path path(object.filename);
            if (lower(path.extension().string()) == ".x") {
                const auto fx = path.parent_path() / (path.stem().string() + ".fx");
                const auto fxsub = path.parent_path() / (path.stem().string() + ".fxsub");
                std::filesystem::path companion;
                if (std::filesystem::exists(fx)) {
                    companion = fx;
                } else if (std::filesystem::exists(fxsub)) {
                    companion = fxsub;
                }
                if (!companion.empty()) {
                    log_msg("[Auto-Bind] Found companion effect for accessory '" + object.filename +
                            "': '" + companion.string() + "'");
                    assign_model_effect(object.id, object.filename, companion.string());
                }
            }
        }
    }
}

HikariEffect* ModelAssignmentRegistry::resolve_effect(std::int32_t object_order,
                                                      std::int32_t subset_index) const {
    if (!g_effects_enabled.load(std::memory_order_acquire)) {
        return nullptr;
    }
    if (object_order != 0) {
        const ModelAssignment* model = nullptr;
        const auto by_order = order_to_id.find(object_order);
        if (by_order != order_to_id.end()) {
            const auto it = models.find(by_order->second);
            if (it != models.end()) {
                model = &it->second;
            }
        }
        if (model == nullptr) {
            const auto it = models.find(static_cast<std::size_t>(object_order));
            if (it != models.end()) {
                model = &it->second;
            }
        }
        if (model != nullptr && model->enabled) {
            if (subset_index >= 0) {
                const auto subset = model->subsets.find(subset_index);
                if (subset != model->subsets.end() && subset->second.enabled &&
                    subset->second.effect != nullptr) {
                    return subset->second.effect;
                }
            }
            if (model->effect != nullptr) {
                return model->effect;
            }
        }
    } else if (subset_index >= 0) {
        for (const auto& [id, model] : models) {
            if (!model.enabled) {
                continue;
            }
            const auto subset = model.subsets.find(subset_index);
            if (subset != model.subsets.end() && subset->second.enabled &&
                subset->second.effect != nullptr) {
                return subset->second.effect;
            }
            if (model.effect != nullptr) {
                return model.effect;
            }
        }
    }
    return default_effect;
}

bool ModelAssignmentRegistry::is_effect_alive(HikariEffect* effect) const {
    if (effect == nullptr) {
        return false;
    }
    if (default_effect == effect) {
        return true;
    }
    for (const auto& [id, model] : models) {
        if (model.effect == effect) {
            return true;
        }
        for (const auto& [subset, assignment] : model.subsets) {
            if (assignment.effect == effect) {
                return true;
            }
        }
    }
    return false;
}

void ModelAssignmentRegistry::add_tab(const std::string& name, const std::string& description,
                                      const std::string& default_rules) {
    for (auto& tab : tabs) {
        if (lower(tab.name) == lower(name)) {
            if (!description.empty()) {
                tab.description = description;
            }
            if (!default_rules.empty()) {
                tab.default_rules = default_rules;
            }
            return;
        }
    }
    OffscreenTabEntry tab;
    tab.name = name;
    tab.description = description.empty() ? name + " Render Target" : description;
    tab.default_rules = default_rules;
    tab.is_offscreen = true;
    tabs.push_back(std::move(tab));
    log_msg("[Tab] Added offscreen render target tab: '" + name + "'");
}

void ModelAssignmentRegistry::scan_and_register_offscreen_tabs(const std::string& source) {
    for (const auto& global : parse_global_variables(source)) {
        const bool offscreen = lower(global.semantic) == "offscreenrendertarget";
        const bool described = global.annotations.count("Description") != 0;
        const bool has_default_effect = global.annotations.count("DefaultEffect") != 0;
        if (!offscreen && !described && !has_default_effect) {
            continue;
        }
        std::string description = global.name + " Render Target";
        const auto desc = global.annotations.find("Description");
        if (desc != global.annotations.end()) {
            description = desc->second;
        }
        std::string rules = "*=none;";
        const auto rule = global.annotations.find("DefaultEffect");
        if (rule != global.annotations.end()) {
            rules = rule->second;
        }
        add_tab(global.name, description, rules);
    }
}

bool ModelAssignmentRegistry::assign_tab_effect(std::size_t tab_index,
                                                std::optional<std::size_t> object_id,
                                                const std::string& filename, std::int32_t subset,
                                                const std::string& path, std::string* error) {
    if (tab_index == 0) {
        if (!object_id) {
            return assign_default_effect(path, error);
        }
        return subset >= 0 ? assign_subset_effect(*object_id, filename, subset, path, error)
                           : assign_model_effect(*object_id, filename, path, error);
    }
    if (tab_index >= tabs.size()) {
        if (error != nullptr) {
            *error = "Invalid tab index";
        }
        return false;
    }
    auto& tab = tabs[tab_index];
    if (path.empty()) {
        if (!object_id) {
            if (tab.default_effect != nullptr) {
                defer_drop_effect(tab.default_effect);
                tab.default_effect = nullptr;
            }
            tab.default_effect_path.clear();
        } else if (subset >= 0) {
            const auto it = tab.subset_overrides.find(OffscreenTabEntry::subset_key(*object_id, subset));
            if (it != tab.subset_overrides.end()) {
                if (it->second.effect != nullptr) {
                    defer_drop_effect(it->second.effect);
                    it->second.effect = nullptr;
                }
                it->second.effect_path.clear();
            }
        } else {
            const auto it = tab.model_overrides.find(*object_id);
            if (it != tab.model_overrides.end()) {
                if (it->second.effect != nullptr) {
                    defer_drop_effect(it->second.effect);
                    it->second.effect = nullptr;
                }
                it->second.effect_path.clear();
            }
        }
        prune_unused_tabs();
        return true;
    }

    std::string compile_error;
    HikariEffect* new_effect = create_effect_instance(path, &compile_error);
    if (!object_id) {
        if (tab.default_effect != nullptr) {
            defer_drop_effect(tab.default_effect);
        }
        tab.default_effect = new_effect;
        tab.default_effect_path = path;
    } else if (subset >= 0) {
        auto& assignment = tab.subset_overrides[OffscreenTabEntry::subset_key(*object_id, subset)];
        if (assignment.effect != nullptr) {
            defer_drop_effect(assignment.effect);
        }
        assignment.effect = new_effect;
        assignment.effect_path = path;
        assignment.enabled = true;
    } else {
        auto& assignment = tab.model_overrides[*object_id];
        if (assignment.effect != nullptr) {
            defer_drop_effect(assignment.effect);
        }
        assignment.effect = new_effect;
        assignment.effect_path = path;
        assignment.enabled = true;
    }
    if (error != nullptr) {
        *error = compile_error;
    }
    return new_effect != nullptr;
}

void ModelAssignmentRegistry::set_tab_item_shown(std::size_t tab_index,
                                                 std::optional<std::size_t> object_id,
                                                 std::int32_t subset, bool shown) {
    if (!object_id) {
        return;
    }
    if (tab_index == 0) {
        const auto it = models.find(*object_id);
        if (it == models.end()) {
            return;
        }
        if (subset >= 0) {
            it->second.subsets[subset].enabled = shown;
        } else {
            it->second.enabled = shown;
        }
    } else if (tab_index < tabs.size()) {
        auto& tab = tabs[tab_index];
        if (subset >= 0) {
            tab.subset_overrides[OffscreenTabEntry::subset_key(*object_id, subset)].enabled = shown;
        } else {
            tab.model_overrides[*object_id].enabled = shown;
        }
    }
}

std::tuple<HikariEffect*, std::string, bool> ModelAssignmentRegistry::resolve_tab_effect(
    std::size_t tab_index, std::size_t object_id, std::int32_t subset_index,
    const std::string& filename) const {
    if (tab_index == 0) {
        const auto it = models.find(object_id);
        if (it != models.end()) {
            const auto& model = it->second;
            if (subset_index >= 0) {
                const auto subset = model.subsets.find(subset_index);
                if (subset != model.subsets.end()) {
                    HikariEffect* effect = subset->second.effect != nullptr
                                               ? subset->second.effect
                                               : (model.effect != nullptr ? model.effect : default_effect);
                    std::string path = !subset->second.effect_path.empty()
                                           ? subset->second.effect_path
                                           : (!model.effect_path.empty() ? model.effect_path
                                                                         : default_effect_path);
                    return {effect, path, subset->second.enabled && model.enabled};
                }
            }
            HikariEffect* effect = model.effect != nullptr ? model.effect : default_effect;
            const std::string path =
                !model.effect_path.empty() ? model.effect_path : default_effect_path;
            return {effect, path, model.enabled};
        }
        return {default_effect, default_effect_path, true};
    }
    if (tab_index >= tabs.size()) {
        return {nullptr, "", true};
    }
    const auto& tab = tabs[tab_index];
    if (subset_index >= 0) {
        const auto it = tab.subset_overrides.find(OffscreenTabEntry::subset_key(object_id, subset_index));
        if (it != tab.subset_overrides.end()) {
            return {it->second.effect != nullptr ? it->second.effect : tab.default_effect,
                    it->second.effect_path, it->second.enabled};
        }
    }
    const auto model = tab.model_overrides.find(object_id);
    if (model != tab.model_overrides.end()) {
        return {model->second.effect != nullptr ? model->second.effect : tab.default_effect,
                model->second.effect_path, model->second.enabled};
    }

    const std::string name = base_name(filename);
    std::stringstream rules(tab.default_rules);
    std::string rule;
    while (std::getline(rules, rule, ';')) {
        rule = trim(rule);
        const auto equals = rule.find('=');
        if (equals == std::string::npos) {
            continue;
        }
        const std::string pattern = trim(rule.substr(0, equals));
        const std::string action = trim(rule.substr(equals + 1));
        if (!match_mme_pattern(pattern, name)) {
            continue;
        }
        if (lower(action) == "hide") {
            return {nullptr, "(hide)", false};
        }
        if (lower(action) == "none") {
            return {nullptr, "(none)", true};
        }
        return {tab.default_effect, action, true};
    }
    return {tab.default_effect, tab.default_effect_path, true};
}

bool ModelAssignmentRegistry::reload_all_effects(std::string* error) {
    std::size_t count = 0;
    std::unordered_map<std::string, HikariEffect*> cache;

    const auto reload_path = [&](const std::string& path) -> HikariEffect* {
        if (path.empty()) {
            return nullptr;
        }
        const auto it = cache.find(path);
        if (it != cache.end()) {
            return it->second;
        }
        HikariEffect* effect = create_effect_instance(path, error);
        if (effect != nullptr) {
            cache[path] = effect;
            ++count;
        }
        return effect;
    };

    if (!default_effect_path.empty()) {
        if (HikariEffect* replacement = reload_path(default_effect_path)) {
            if (default_effect != nullptr) {
                defer_drop_effect(default_effect);
            }
            default_effect = replacement;
        }
    }
    for (auto& [id, model] : models) {
        if (HikariEffect* replacement = reload_path(model.effect_path)) {
            if (model.effect != nullptr) {
                defer_drop_effect(model.effect);
            }
            model.effect = replacement;
        }
        for (auto& [subset, assignment] : model.subsets) {
            if (HikariEffect* replacement = reload_path(assignment.effect_path)) {
                if (assignment.effect != nullptr) {
                    defer_drop_effect(assignment.effect);
                }
                assignment.effect = replacement;
            }
        }
    }
    for (auto& tab : tabs) {
        if (HikariEffect* replacement = reload_path(tab.default_effect_path)) {
            if (tab.default_effect != nullptr) {
                defer_drop_effect(tab.default_effect);
            }
            tab.default_effect = replacement;
        }
        for (auto& [id, assignment] : tab.model_overrides) {
            if (HikariEffect* replacement = reload_path(assignment.effect_path)) {
                if (assignment.effect != nullptr) {
                    defer_drop_effect(assignment.effect);
                }
                assignment.effect = replacement;
            }
        }
        for (auto& [key, assignment] : tab.subset_overrides) {
            if (HikariEffect* replacement = reload_path(assignment.effect_path)) {
                if (assignment.effect != nullptr) {
                    defer_drop_effect(assignment.effect);
                }
                assignment.effect = replacement;
            }
        }
    }
    update_has_assigned();
    (void)count;
    return true;
}

void ModelAssignmentRegistry::prune_unused_tabs() {
    std::unordered_set<std::string> referenced;
    const auto scan = [&](const std::string& path) {
        if (path.empty()) {
            return;
        }
        for (const auto& variable : parse_global_variables(read_text(path, nullptr))) {
            if (lower(variable.semantic) == "offscreenrendertarget") {
                referenced.insert(lower(variable.name));
            }
        }
    };
    scan(default_effect_path);
    for (const auto& [id, model] : models) {
        scan(model.effect_path);
        for (const auto& [subset, assignment] : model.subsets) {
            scan(assignment.effect_path);
        }
    }
    tabs.erase(std::remove_if(tabs.begin(), tabs.end(), [&](const OffscreenTabEntry& tab) {
                   if (!tab.is_offscreen) {
                       return false;
                   }
                   if (referenced.count(lower(tab.name)) != 0) {
                       return false;
                   }
                   return tab.default_effect_path.empty() && tab.model_overrides.empty() &&
                          tab.subset_overrides.empty();
               }),
               tabs.end());
    if (current_tab_idx >= tabs.size()) {
        current_tab_idx = 0;
    }
}

// ---------------------------------------------------------------------------
// Mapping files (.hmm/.hmd and legacy .emm/.emd)
// ---------------------------------------------------------------------------

namespace {

std::string convert_legacy_to_hmm(const std::string& content) {
    std::ostringstream out;
    out << "[HikariMapping]\nVersion = 1.0\n\n";
    std::string section;
    for (const auto& line : lines_of(content)) {
        if (line.empty() || line[0] == ';' || line[0] == '#') {
            continue;
        }
        if (line.front() == '[' && line.back() == ']') {
            section = line.substr(1, line.size() - 2);
            if (section == "Effect") {
                out << "[Main]\n";
            } else if (section.rfind("Effect@", 0) == 0) {
                out << "[" << section.substr(7) << "]\n";
            } else {
                out << line << "\n";
            }
            continue;
        }
        const auto equals = line.find('=');
        if (equals == std::string::npos) {
            continue;
        }
        std::string key = trim(line.substr(0, equals));
        const std::string value = trim(line.substr(equals + 1));
        if (section == "Effect" || section.rfind("Effect@", 0) == 0) {
            const auto parts = split_dots(key);
            if (parts.size() >= 2 && is_number(parts[1])) {
                key = parts[0] + ".Subset." + parts[1];
                for (std::size_t i = 2; i < parts.size(); ++i) {
                    key += "." + parts[i];
                }
            }
        }
        out << key << " = " << value << "\n";
    }
    return out.str();
}

std::string convert_legacy_to_hmd(const std::string& content) {
    std::ostringstream out;
    out << "[HikariModelData]\nVersion = 1.0\n\n";
    std::string section;
    for (const auto& line : lines_of(content)) {
        if (line.empty() || line[0] == ';' || line[0] == '#') {
            continue;
        }
        if (line.front() == '[' && line.back() == ']') {
            section = line.substr(1, line.size() - 2);
            if (section == "Effect") {
                out << "[Main]\n";
            } else if (section.rfind("Effect@", 0) == 0) {
                out << "[" << section.substr(7) << "]\n";
            }
            continue;
        }
        const auto equals = line.find('=');
        if (equals == std::string::npos) {
            continue;
        }
        std::string key = trim(line.substr(0, equals));
        const std::string value = trim(line.substr(equals + 1));
        const auto parts = split_dots(key);
        if (parts.size() == 1 && is_number(parts[0])) {
            key = "Subset." + parts[0];
        } else if (parts.size() == 2 && is_number(parts[0])) {
            key = "Subset." + parts[0] + "." + parts[1];
        }
        out << key << " = " << value << "\n";
    }
    return out.str();
}

} // namespace

bool ModelAssignmentRegistry::save_hmm(const std::string& path, std::string* error) const {
    auto host_objects = enumerate_host_objects();
    if (host_objects.empty() && !models.empty()) {
        std::vector<const ModelAssignment*> sorted;
        for (const auto& [id, model] : models) {
            sorted.push_back(&model);
        }
        std::sort(sorted.begin(), sorted.end(),
                  [](const ModelAssignment* a, const ModelAssignment* b) {
                      return a->object_id < b->object_id;
                  });
        for (std::size_t i = 0; i < sorted.size(); ++i) {
            HostObject object;
            object.id = sorted[i]->object_id;
            object.is_pmd = lower(sorted[i]->filename).find(".x") == std::string::npos;
            object.index = static_cast<std::int32_t>(i);
            object.order = static_cast<std::int32_t>(i);
            object.filename = sorted[i]->filename;
            object.disp = sorted[i]->enabled;
            host_objects.push_back(std::move(object));
        }
    }

    std::ostringstream out;
    out << "[HikariMapping]\nVersion = 1.0\n\n[Object]\n";
    for (std::size_t i = 0; i < host_objects.size(); ++i) {
        out << object_key(host_objects[i], i) << " = " << host_objects[i].filename << "\n";
    }
    out << "\n[Main]\n";
    if (!default_effect_path.empty()) {
        out << "Default = " << default_effect_path << "\n";
    }
    for (std::size_t i = 0; i < host_objects.size(); ++i) {
        const std::string key = object_key(host_objects[i], i);
        const auto model = models.find(host_objects[i].id);
        if (model == models.end()) {
            continue;
        }
        if (!model->second.effect_path.empty()) {
            out << key << " = " << model->second.effect_path << "\n";
        }
        out << key << ".show = " << (model->second.enabled ? "true" : "false") << "\n";
        std::vector<std::pair<std::int32_t, const ModelSubsetAssignment*>> subsets;
        for (const auto& [index, assignment] : model->second.subsets) {
            subsets.push_back({index, &assignment});
        }
        std::sort(subsets.begin(), subsets.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });
        for (const auto& [index, assignment] : subsets) {
            if (!assignment->effect_path.empty()) {
                out << key << ".Subset." << index << " = " << assignment->effect_path << "\n";
            }
            out << key << ".Subset." << index << ".show = "
                << (assignment->enabled ? "true" : "false") << "\n";
        }
    }

    for (std::size_t tab_index = 1; tab_index < tabs.size(); ++tab_index) {
        const auto& tab = tabs[tab_index];
        out << "\n[" << tab.name << "]\n";
        if (!tab.description.empty()) {
            out << "Description = " << tab.description << "\n";
        }
        if (!tab.default_rules.empty()) {
            out << "DefaultRules = " << tab.default_rules << "\n";
        }
        if (!tab.default_effect_path.empty()) {
            out << "Default = " << tab.default_effect_path << "\n";
        }
        for (std::size_t i = 0; i < host_objects.size(); ++i) {
            const std::string key = object_key(host_objects[i], i);
            const auto model = tab.model_overrides.find(host_objects[i].id);
            if (model != tab.model_overrides.end()) {
                if (!model->second.effect_path.empty()) {
                    out << key << " = " << model->second.effect_path << "\n";
                }
                out << key << ".show = " << (model->second.enabled ? "true" : "false") << "\n";
            }
            std::vector<std::pair<std::int32_t, const ModelSubsetAssignment*>> subsets;
            for (const auto& [packed, assignment] : tab.subset_overrides) {
                if (static_cast<std::size_t>(packed >> 32) == host_objects[i].id) {
                    subsets.push_back({static_cast<std::int32_t>(packed & 0xFFFFFFFF), &assignment});
                }
            }
            std::sort(subsets.begin(), subsets.end(),
                      [](const auto& a, const auto& b) { return a.first < b.first; });
            for (const auto& [index, assignment] : subsets) {
                if (!assignment->effect_path.empty()) {
                    out << key << ".Subset." << index << " = " << assignment->effect_path << "\n";
                }
                out << key << ".Subset." << index << ".show = "
                    << (assignment->enabled ? "true" : "false") << "\n";
            }
        }
    }
    return write_text(path, out.str(), error);
}

bool ModelAssignmentRegistry::load_hmm(const std::string& path, std::string* error) {
    std::string content = read_text(path, error);
    if (content.empty()) {
        return false;
    }
    if (content.find("[HikariMapping]") == std::string::npos) {
        log_msg("[HMM] Legacy .emm format detected in '" + path + "', falling back");
        content = convert_legacy_to_hmm(content);
    }

    const auto host_objects = enumerate_host_objects();
    std::unordered_map<std::string, std::size_t> object_map;
    for (std::size_t i = 0; i < host_objects.size(); ++i) {
        object_map[object_key(host_objects[i], i)] = host_objects[i].id;
    }

    std::string section;
    for (const auto& line : lines_of(content)) {
        if (line.empty() || line[0] == ';' || line[0] == '#') {
            continue;
        }
        if (line.front() == '[' && line.back() == ']') {
            section = line.substr(1, line.size() - 2);
            if (section != "HikariMapping" && section != "Object" && section != "Main") {
                add_tab(section, "", "");
            }
            continue;
        }
        const auto equals = line.find('=');
        if (equals == std::string::npos) {
            continue;
        }
        const std::string key = trim(line.substr(0, equals));
        const std::string value = trim(line.substr(equals + 1));

        if (section == "HikariMapping") {
            continue;
        }
        if (section == "Object") {
            if (object_map.count(key) == 0) {
                const bool is_pmd = key.rfind("Pmd", 0) == 0;
                std::string digits;
                for (char c : key) {
                    if (std::isdigit(static_cast<unsigned char>(c))) {
                        digits.push_back(c);
                    }
                }
                const std::size_t index = digits.empty() ? 0 : std::stoul(digits);
                const std::size_t id = is_pmd ? index + 1 : index + 1000;
                object_map[key] = id;
                auto& model = models[id];
                model.object_id = id;
                model.filename = value;
            }
            continue;
        }
        if (section == "Main") {
            if (lower(key) == "default") {
                assign_default_effect(value);
                continue;
            }
            const auto parts = split_dots(key);
            const auto object = object_map.find(parts[0]);
            if (object == object_map.end()) {
                continue;
            }
            if (parts.size() == 1) {
                assign_model_effect(object->second, "", value);
            } else if (parts.size() == 2) {
                if (lower(parts[1]) == "show") {
                    models[object->second].enabled = lower(value) == "true";
                } else if (is_number(parts[1])) {
                    assign_subset_effect(object->second, "", std::stoi(parts[1]), value);
                }
            } else if (parts.size() == 3) {
                if (lower(parts[1]) == "subset" && is_number(parts[2])) {
                    assign_subset_effect(object->second, "", std::stoi(parts[2]), value);
                } else if (is_number(parts[1]) && lower(parts[2]) == "show") {
                    models[object->second].subsets[std::stoi(parts[1])].enabled =
                        lower(value) == "true";
                }
            } else if (parts.size() == 4 && lower(parts[1]) == "subset" &&
                       lower(parts[3]) == "show") {
                models[object->second].subsets[std::stoi(parts[2])].enabled =
                    lower(value) == "true";
            }
            continue;
        }

        std::size_t tab_index = 0;
        for (std::size_t i = 0; i < tabs.size(); ++i) {
            if (lower(tabs[i].name) == lower(section)) {
                tab_index = i;
                break;
            }
        }
        if (tab_index == 0) {
            continue;
        }
        if (lower(key) == "default") {
            assign_tab_effect(tab_index, std::nullopt, "", -1, value);
        } else if (lower(key) == "description") {
            tabs[tab_index].description = value;
        } else if (lower(key) == "defaultrules") {
            tabs[tab_index].default_rules = value;
        } else {
            const auto parts = split_dots(key);
            const auto object = object_map.find(parts[0]);
            if (object == object_map.end()) {
                continue;
            }
            if (parts.size() == 1) {
                assign_tab_effect(tab_index, object->second, "", -1, value);
            } else if (parts.size() == 2) {
                if (lower(parts[1]) == "show") {
                    set_tab_item_shown(tab_index, object->second, -1, lower(value) == "true");
                } else if (is_number(parts[1])) {
                    assign_tab_effect(tab_index, object->second, "", std::stoi(parts[1]), value);
                }
            } else if (parts.size() == 3) {
                if (lower(parts[1]) == "subset" && is_number(parts[2])) {
                    assign_tab_effect(tab_index, object->second, "", std::stoi(parts[2]), value);
                } else if (is_number(parts[1]) && lower(parts[2]) == "show") {
                    set_tab_item_shown(tab_index, object->second, std::stoi(parts[1]),
                                       lower(value) == "true");
                }
            } else if (parts.size() == 4 && lower(parts[1]) == "subset" &&
                       lower(parts[3]) == "show") {
                set_tab_item_shown(tab_index, object->second, std::stoi(parts[2]),
                                   lower(value) == "true");
            }
        }
    }
    update_has_assigned();
    return true;
}

bool ModelAssignmentRegistry::save_hmd(std::size_t object_id, const std::string& path,
                                       std::string* error) const {
    std::ostringstream out;
    out << "[HikariModelData]\nVersion = 1.0\n\n";
    const auto model = models.find(object_id);
    if (model != models.end()) {
        out << "[Main]\n";
        if (!model->second.effect_path.empty()) {
            out << "Default = " << model->second.effect_path << "\n";
        }
        out << "show = " << (model->second.enabled ? "true" : "false") << "\n";
        std::vector<std::pair<std::int32_t, const ModelSubsetAssignment*>> subsets;
        for (const auto& [index, assignment] : model->second.subsets) {
            subsets.push_back({index, &assignment});
        }
        std::sort(subsets.begin(), subsets.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });
        for (const auto& [index, assignment] : subsets) {
            if (!assignment->effect_path.empty()) {
                out << "Subset." << index << " = " << assignment->effect_path << "\n";
            }
            out << "Subset." << index << ".show = " << (assignment->enabled ? "true" : "false")
                << "\n";
        }
    }
    for (std::size_t index = 1; index < tabs.size(); ++index) {
        const auto& tab = tabs[index];
        const auto tab_model = tab.model_overrides.find(object_id);
        bool has_subset = false;
        for (const auto& [packed, assignment] : tab.subset_overrides) {
            if (static_cast<std::size_t>(packed >> 32) == object_id) {
                has_subset = true;
                break;
            }
        }
        if (tab_model == tab.model_overrides.end() && !has_subset) {
            continue;
        }
        out << "\n[" << tab.name << "]\n";
        if (tab_model != tab.model_overrides.end()) {
            if (!tab_model->second.effect_path.empty()) {
                out << "Default = " << tab_model->second.effect_path << "\n";
            }
            out << "show = " << (tab_model->second.enabled ? "true" : "false") << "\n";
        }
        for (const auto& [packed, assignment] : tab.subset_overrides) {
            if (static_cast<std::size_t>(packed >> 32) != object_id) {
                continue;
            }
            const auto subset = static_cast<std::int32_t>(packed & 0xFFFFFFFF);
            if (!assignment.effect_path.empty()) {
                out << "Subset." << subset << " = " << assignment.effect_path << "\n";
            }
            out << "Subset." << subset << ".show = " << (assignment.enabled ? "true" : "false")
                << "\n";
        }
    }
    return write_text(path, out.str(), error);
}

bool ModelAssignmentRegistry::load_hmd(std::size_t object_id, const std::string& path,
                                       std::string* error) {
    std::string content = read_text(path, error);
    if (content.empty()) {
        return false;
    }
    if (content.find("[HikariModelData]") == std::string::npos) {
        log_msg("[HMD] Legacy .emd format detected in '" + path + "', falling back");
        content = convert_legacy_to_hmd(content);
    }
    std::string section;
    for (const auto& line : lines_of(content)) {
        if (line.empty() || line[0] == ';' || line[0] == '#') {
            continue;
        }
        if (line.front() == '[' && line.back() == ']') {
            section = line.substr(1, line.size() - 2);
            if (section != "HikariModelData" && section != "Main") {
                add_tab(section, "", "");
            }
            continue;
        }
        const auto equals = line.find('=');
        if (equals == std::string::npos) {
            continue;
        }
        const std::string key = trim(line.substr(0, equals));
        const std::string value = trim(line.substr(equals + 1));

        if (section == "HikariModelData") {
            continue;
        }
        if (section == "Main") {
            if (lower(key) == "default") {
                assign_model_effect(object_id, "", value);
            } else if (lower(key) == "show") {
                models[object_id].enabled = lower(value) == "true";
            } else {
                const auto parts = split_dots(key);
                if (parts.size() == 1 && is_number(parts[0])) {
                    assign_subset_effect(object_id, "", std::stoi(parts[0]), value);
                } else if (parts.size() == 2 && lower(parts[0]) == "subset" &&
                           is_number(parts[1])) {
                    assign_subset_effect(object_id, "", std::stoi(parts[1]), value);
                } else if (parts.size() == 2 && is_number(parts[0]) &&
                           lower(parts[1]) == "show") {
                    models[object_id].subsets[std::stoi(parts[0])].enabled =
                        lower(value) == "true";
                } else if (parts.size() == 3 && lower(parts[0]) == "subset" &&
                           lower(parts[2]) == "show") {
                    models[object_id].subsets[std::stoi(parts[1])].enabled =
                        lower(value) == "true";
                }
            }
            continue;
        }

        std::size_t tab_index = 0;
        for (std::size_t i = 0; i < tabs.size(); ++i) {
            if (lower(tabs[i].name) == lower(section)) {
                tab_index = i;
                break;
            }
        }
        if (tab_index == 0) {
            continue;
        }
        if (lower(key) == "default") {
            assign_tab_effect(tab_index, object_id, "", -1, value);
        } else if (lower(key) == "show") {
            set_tab_item_shown(tab_index, object_id, -1, lower(value) == "true");
        } else {
            const auto parts = split_dots(key);
            if (parts.size() == 1 && is_number(parts[0])) {
                assign_tab_effect(tab_index, object_id, "", std::stoi(parts[0]), value);
            } else if (parts.size() == 2 && lower(parts[0]) == "subset" &&
                       is_number(parts[1])) {
                assign_tab_effect(tab_index, object_id, "", std::stoi(parts[1]), value);
            } else if (parts.size() == 2 && is_number(parts[0]) &&
                       lower(parts[1]) == "show") {
                set_tab_item_shown(tab_index, object_id, std::stoi(parts[0]),
                                   lower(value) == "true");
            } else if (parts.size() == 3 && lower(parts[0]) == "subset" &&
                       lower(parts[2]) == "show") {
                set_tab_item_shown(tab_index, object_id, std::stoi(parts[1]),
                                   lower(value) == "true");
            }
        }
    }
    update_has_assigned();
    return true;
}

bool ModelAssignmentRegistry::save_emm(const std::string& path, std::string* error) const {
    return save_hmm(path, error);
}
bool ModelAssignmentRegistry::load_emm(const std::string& path, std::string* error) {
    return load_hmm(path, error);
}
bool ModelAssignmentRegistry::save_emd(std::size_t object_id, const std::string& path,
                                       std::string* error) const {
    return save_hmd(object_id, path, error);
}
bool ModelAssignmentRegistry::load_emd(std::size_t object_id, const std::string& path,
                                       std::string* error) {
    return load_hmd(object_id, path, error);
}

} // namespace hikari::core
