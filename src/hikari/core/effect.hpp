#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "hikari/core/com.hpp"
#include "hikari/core/d3d11.hpp"
#include "hikari/core/shader.hpp"
#include "hikari/core/states.hpp"

namespace hikari::core {

constexpr std::size_t HANDLE_TECHNIQUE_BASE = 0x10000;
constexpr std::size_t HANDLE_PASS_BASE = 0x20000;
constexpr std::size_t HANDLE_PARAM_BASE = 0x30000;
constexpr std::size_t HANDLE_ANNO_BASE = 0x40000;

struct CompiledPass {
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vs;
    std::optional<std::vector<std::uint8_t>> vs_bytecode;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> ps;
    std::optional<std::vector<std::uint8_t>> ps_bytecode;
    Microsoft::WRL::ComPtr<ID3D11ComputeShader> cs;
    std::optional<std::vector<std::uint8_t>> cs_bytecode;
    Microsoft::WRL::ComPtr<ID3D11GeometryShader> gs;
    std::optional<std::vector<std::uint8_t>> gs_bytecode;
    Microsoft::WRL::ComPtr<ID3D11HullShader> hs;
    std::optional<std::vector<std::uint8_t>> hs_bytecode;
    Microsoft::WRL::ComPtr<ID3D11DomainShader> ds;
    std::optional<std::vector<std::uint8_t>> ds_bytecode;
    BlendPreset blend_preset = BlendPreset::Opaque;
    DepthPreset depth_preset = DepthPreset::ReadWriteLessEqual;
    RasterizerPreset raster_preset = RasterizerPreset::CullNoneSolid;
    std::array<std::uint32_t, 3> thread_group_size{1, 1, 1};
};

struct ActivePassInfo {
    std::vector<std::uint8_t> vs_bytecode;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vs;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> ps;
    Microsoft::WRL::ComPtr<ID3D11GeometryShader> gs;
    Microsoft::WRL::ComPtr<ID3D11Buffer> constant_buffer;
    BlendPreset blend_preset = BlendPreset::Opaque;
    DepthPreset depth_preset = DepthPreset::ReadWriteLessEqual;
    RasterizerPreset raster_preset = RasterizerPreset::CullNoneSolid;
};

struct EffectAnnotation {
    std::string name;
    std::string value;
};

enum class UIWidgetKind { None, Slider, Drag, Checkbox, Color, Combo, Input };

struct ParameterUIDesc {
    std::string label;
    UIWidgetKind widget = UIWidgetKind::None;
    float min = 0.0f;
    float max = 1.0f;
    float step = 0.01f;
    std::vector<std::string> items;
    std::string group;
    std::string help;
    std::string default_val;
};

struct EffectParameter {
    std::string name;
    std::string semantic;
    std::string hlsl_type;
    bool row_major = false;
    std::size_t offset = 0;
    std::size_t size = 0;
    std::vector<std::size_t> annotation_indices;
    std::optional<ParameterUIDesc> ui;
};

struct StandardMatrixOffsets {
    std::optional<std::size_t> world;
    std::optional<std::size_t> view;
    std::optional<std::size_t> proj;
    std::optional<std::size_t> world_view;
    std::optional<std::size_t> world_view_proj;
    std::optional<std::size_t> camera_pos;
    std::optional<std::size_t> light_dir;
    std::optional<std::size_t> mat_diffuse;
    std::optional<std::size_t> mat_ambient;
    std::optional<std::size_t> mat_specular;
};

bool parse_and_write_default_val(const std::string& value, const std::string& type,
                                 std::uint8_t* out, std::size_t size);

extern std::mutex g_active_pass_mutex;
extern std::optional<ActivePassInfo> g_active_pass;

class HikariEffect {
public:
    const ID3DXEffectVtbl* vtbl = nullptr;
    std::atomic<std::uint32_t> ref_count{1};

    std::vector<TechniqueDesc> techniques;
    std::vector<std::vector<CompiledPass>> compiled_techniques;
    std::size_t active_tech_idx = 0;
    std::size_t active_pass_idx = 0;

    std::vector<EffectAnnotation> annotations;
    std::vector<std::vector<std::size_t>> technique_annotations;
    std::vector<std::vector<std::vector<std::size_t>>> pass_annotations;
    std::vector<EffectParameter> parameters;
    std::unordered_map<std::string, std::size_t> param_by_name;
    std::unordered_map<std::string, std::size_t> param_by_semantic;
    std::unordered_map<std::string, std::uint32_t> name_to_texture_slot;
    std::array<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>, 16> bound_texture_srvs;
    std::unordered_map<std::size_t, std::string> string_values;

    StandardMatrixOffsets std_offsets;
    Microsoft::WRL::ComPtr<ID3D11Buffer> constant_buffer;
    std::vector<std::uint8_t> constant_data;
    std::size_t constant_used = 0;
    bool constant_dirty = false;

    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;

    static HikariEffect* create(const std::string& source, const std::string& source_name,
                                D3D11Context& d3d11);
    static HikariEffect* from_handle(void* self);
    static bool is_hikari_effect(void* self);

    std::uint32_t add_ref();
    std::uint32_t release();

    void update_gpu_constants();
    std::optional<float> get_param_value_f32(std::size_t index) const;
    void set_param_value_f32(std::size_t index, float value);
    std::optional<std::array<float, 3>> get_param_value_float3(std::size_t index) const;
    void set_param_value_float3(std::size_t index, const std::array<float, 3>& value);
    std::optional<std::int32_t> get_param_value_i32(std::size_t index) const;
    void set_param_value_i32(std::size_t index, std::int32_t value);
    std::optional<bool> get_param_value_bool(std::size_t index) const;
    void set_param_value_bool(std::size_t index, bool value);

    std::optional<ActivePassInfo> get_first_pass_info();
    std::optional<std::size_t> find_technique_for_pass(std::int32_t current_tech) const;
    std::optional<ActivePassInfo> get_technique_pass_info(std::int32_t current_tech);

    std::optional<std::size_t> resolve_parameter_index(D3DXHANDLE handle) const;
    HRESULT set_param_block(D3DXHANDLE handle, const void* source, std::size_t bytes);
    HRESULT get_param_block(D3DXHANDLE handle, void* destination, std::size_t bytes) const;
    std::pair<std::uint32_t, std::array<std::uint32_t, 4>> parameter_type_shape(
        const EffectParameter& parameter) const;
};

bool is_hikari_effect(void* self);
std::string disassemble_hikari_effect(void* self);

// ---------------------------------------------------------------------------
// Effect registry and per-object assignment (.hmm/.hmd/.emm/.emd)
// ---------------------------------------------------------------------------

struct EffectItemInfo {
    std::string path;
    std::string name;
    HikariEffect* effect = nullptr;
    bool enabled = true;
};

struct ModelSubsetAssignment {
    std::string effect_path;
    HikariEffect* effect = nullptr;
    bool enabled = true;
};

struct ModelAssignment {
    std::size_t object_id = 0;
    std::string filename;
    std::string effect_path;
    HikariEffect* effect = nullptr;
    std::unordered_map<std::int32_t, ModelSubsetAssignment> subsets;
    bool enabled = true;
};

struct OffscreenTabEntry {
    std::string name;
    std::string description;
    std::string default_effect_path;
    HikariEffect* default_effect = nullptr;
    std::string default_rules;
    bool is_offscreen = false;
    std::unordered_map<std::size_t, ModelSubsetAssignment> model_overrides;
    std::unordered_map<std::uint64_t, ModelSubsetAssignment> subset_overrides;

    static std::uint64_t subset_key(std::size_t object_id, std::int32_t subset) {
        return (static_cast<std::uint64_t>(object_id) << 32) |
               static_cast<std::uint32_t>(subset);
    }
};

bool match_mme_pattern(const std::string& pattern, const std::string& text);

class ModelAssignmentRegistry {
public:
    ModelAssignmentRegistry();

    std::string default_effect_path;
    HikariEffect* default_effect = nullptr;
    std::unordered_map<std::size_t, ModelAssignment> models;
    std::unordered_map<std::int32_t, std::size_t> order_to_id;
    std::unordered_set<std::size_t> expanded_models;
    std::vector<OffscreenTabEntry> tabs;
    std::size_t current_tab_idx = 0;

    void update_has_assigned();
    bool has_assigned() const;

    bool assign_default_effect(const std::string& path, std::string* error = nullptr);
    bool assign_model_effect(std::size_t object_id, const std::string& filename,
                             const std::string& path, std::string* error = nullptr);
    bool assign_subset_effect(std::size_t object_id, const std::string& filename,
                              std::int32_t subset, const std::string& path,
                              std::string* error = nullptr);

    void toggle_subset_expand(std::size_t object_id);
    bool is_subset_expanded(std::size_t object_id) const;
    void sync_host_objects();
    HikariEffect* resolve_effect(std::int32_t object_order, std::int32_t subset_index) const;
    bool is_effect_alive(HikariEffect* effect) const;

    void add_tab(const std::string& name, const std::string& description,
                 const std::string& default_rules);
    void scan_and_register_offscreen_tabs(const std::string& source);
    bool assign_tab_effect(std::size_t tab_index, std::optional<std::size_t> object_id,
                           const std::string& filename, std::int32_t subset,
                           const std::string& path, std::string* error = nullptr);
    void set_tab_item_shown(std::size_t tab_index, std::optional<std::size_t> object_id,
                            std::int32_t subset, bool shown);
    std::tuple<HikariEffect*, std::string, bool> resolve_tab_effect(
        std::size_t tab_index, std::size_t object_id, std::int32_t subset_index,
        const std::string& filename) const;

    bool reload_all_effects(std::string* error = nullptr);
    bool save_emm(const std::string& path, std::string* error = nullptr) const;
    bool load_emm(const std::string& path, std::string* error = nullptr);
    bool save_emd(std::size_t object_id, const std::string& path, std::string* error = nullptr) const;
    bool load_emd(std::size_t object_id, const std::string& path, std::string* error = nullptr);
    void prune_unused_tabs();
    bool save_hmm(const std::string& path, std::string* error = nullptr) const;
    bool load_hmm(const std::string& path, std::string* error = nullptr);
    bool save_hmd(std::size_t object_id, const std::string& path, std::string* error = nullptr) const;
    bool load_hmd(std::size_t object_id, const std::string& path, std::string* error = nullptr);

private:
    HikariEffect* create_instance(const std::string& path, std::string* error);
};

extern std::vector<EffectItemInfo> g_effect_registry;
extern std::mutex g_effect_registry_mutex;
extern std::mutex g_assignment_mutex;
extern ModelAssignmentRegistry g_assignment_registry;
extern std::atomic_bool g_has_assigned_effects;
extern std::atomic_bool g_effects_enabled;
extern std::atomic_bool g_auto_reload;
extern std::atomic_bool g_auto_save_mapping;
extern std::atomic_bool g_skip_device_validation;

std::size_t load_effect_from_file(const std::string& path, std::string* error = nullptr);
bool reload_effect(std::size_t index, std::string* error = nullptr);
std::optional<ActivePassInfo> get_active_direct_effect_pass();
std::optional<ActivePassInfo> get_assigned_effect_pass(std::int32_t object_order,
                                                       std::int32_t subset_index,
                                                       std::int32_t current_tech);
HikariEffect* create_effect_instance(const std::string& path, std::string* error = nullptr);
void defer_drop_effect(HikariEffect* effect);
void drain_deferred_effects();

} // namespace hikari::core
