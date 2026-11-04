#include "hikari/core/mmd_host.hpp"

#include <windows.h>

#include <cstdint>
#include <functional>
#include <string>

#include "hikari/core/log.hpp"

namespace hikari::core {
namespace {

template <typename T>
T resolve(const char* name) {
    static HMODULE module = ::GetModuleHandleW(nullptr);
    if (module == nullptr) {
        return nullptr;
    }
    return reinterpret_cast<T>(::GetProcAddress(module, name));
}

struct HostExports {
    using FrameTime = float (*)();
    using Count = std::int32_t (*)();
    using Filename = const char* (*)(std::int32_t);
    using Int = std::int32_t (*)(std::int32_t);
    using Disp = bool (*)(std::int32_t);
    using Flag = bool (*)();
    using Id = void* (*)(std::int32_t);

    FrameTime frame_time = nullptr;
    Count pmd_num = nullptr;
    Filename pmd_filename = nullptr;
    Int pmd_order = nullptr;
    Int pmd_mat_num = nullptr;
    Disp pmd_disp = nullptr;
    Id pmd_id = nullptr;
    Count acs_num = nullptr;
    Filename acs_filename = nullptr;
    Int acs_order = nullptr;
    Int acs_mat_num = nullptr;
    Disp acs_disp = nullptr;
    Id acs_id = nullptr;
    Count current_object = nullptr;
    Count current_material = nullptr;
    Count current_technic = nullptr;
    Flag english_mode = nullptr;
};

HostExports& exports() {
    static HostExports value = [] {
        HostExports out;
        out.frame_time = resolve<HostExports::FrameTime>("ExpGetFrameTime");
        out.pmd_num = resolve<HostExports::Count>("ExpGetPmdNum");
        out.pmd_filename = resolve<HostExports::Filename>("ExpGetPmdFilename");
        out.pmd_order = resolve<HostExports::Int>("ExpGetPmdOrder");
        out.pmd_mat_num = resolve<HostExports::Int>("ExpGetPmdMatNum");
        out.pmd_disp = resolve<HostExports::Disp>("ExpGetPmdDisp");
        out.pmd_id = resolve<HostExports::Id>("ExpGetPmdID");
        out.acs_num = resolve<HostExports::Count>("ExpGetAcsNum");
        out.acs_filename = resolve<HostExports::Filename>("ExpGetAcsFilename");
        out.acs_order = resolve<HostExports::Int>("ExpGetAcsOrder");
        out.acs_mat_num = resolve<HostExports::Int>("ExpGetAcsMatNum");
        out.acs_disp = resolve<HostExports::Disp>("ExpGetAcsDisp");
        out.acs_id = resolve<HostExports::Id>("ExpGetAcsID");
        out.current_object = resolve<HostExports::Count>("ExpGetCurrentObject");
        out.current_material = resolve<HostExports::Count>("ExpGetCurrentMaterial");
        out.current_technic = resolve<HostExports::Count>("ExpGetCurrentTechnic");
        out.english_mode = resolve<HostExports::Flag>("ExpGetEnglishMode");
        log_msg(std::string("[mmd_host] Initialized host exports from main module. MMD active: ") +
                (out.pmd_num != nullptr && out.current_object != nullptr ? "true" : "false"));
        return out;
    }();
    return value;
}

std::string safe_filename(const char* pointer) {
    return pointer != nullptr ? std::string(pointer) : std::string();
}

} // namespace

float get_frame_time() {
    return exports().frame_time ? exports().frame_time() : 0.0f;
}
std::int32_t get_pmd_num() {
    return exports().pmd_num ? exports().pmd_num() : 0;
}
std::string get_pmd_filename(std::int32_t index) {
    return exports().pmd_filename ? safe_filename(exports().pmd_filename(index)) : std::string();
}
std::int32_t get_pmd_order(std::int32_t index) {
    return exports().pmd_order ? exports().pmd_order(index) : 0;
}
std::int32_t get_pmd_mat_num(std::int32_t index) {
    return exports().pmd_mat_num ? exports().pmd_mat_num(index) : 0;
}
bool get_pmd_disp(std::int32_t index) {
    return exports().pmd_disp ? exports().pmd_disp(index) : true;
}
std::size_t get_pmd_id(std::int32_t index) {
    return exports().pmd_id ? reinterpret_cast<std::size_t>(exports().pmd_id(index)) : 0;
}
std::int32_t get_acs_num() {
    return exports().acs_num ? exports().acs_num() : 0;
}
std::string get_acs_filename(std::int32_t index) {
    return exports().acs_filename ? safe_filename(exports().acs_filename(index)) : std::string();
}
std::int32_t get_acs_order(std::int32_t index) {
    return exports().acs_order ? exports().acs_order(index) : 0;
}
std::int32_t get_acs_mat_num(std::int32_t index) {
    return exports().acs_mat_num ? exports().acs_mat_num(index) : 0;
}
bool get_acs_disp(std::int32_t index) {
    return exports().acs_disp ? exports().acs_disp(index) : true;
}
std::size_t get_acs_id(std::int32_t index) {
    return exports().acs_id ? reinterpret_cast<std::size_t>(exports().acs_id(index)) : 0;
}
std::int32_t get_current_object() {
    return exports().current_object ? exports().current_object() : 0;
}
std::int32_t get_current_material() {
    return exports().current_material ? exports().current_material() : -1;
}
std::int32_t get_current_technic() {
    return exports().current_technic ? exports().current_technic() : 0;
}
bool is_english_mode() {
    return exports().english_mode ? exports().english_mode() : false;
}

std::vector<HostObject> enumerate_host_objects() {
    std::vector<HostObject> objects;
    const std::int32_t pmd_count = get_pmd_num();
    for (std::int32_t i = 0; i < pmd_count; ++i) {
        const std::size_t id = get_pmd_id(i);
        if (id != 0) {
            objects.push_back({id, true, i, get_pmd_order(i), get_pmd_filename(i),
                               get_pmd_mat_num(i), get_pmd_disp(i)});
        }
    }
    const std::int32_t acs_count = get_acs_num();
    for (std::int32_t i = 0; i < acs_count; ++i) {
        const std::size_t id = get_acs_id(i);
        if (id != 0) {
            objects.push_back({id, false, i, get_acs_order(i), get_acs_filename(i),
                               get_acs_mat_num(i), get_acs_disp(i)});
        }
    }
    return objects;
}

bool is_mmd_bound() {
    return exports().pmd_num != nullptr && exports().current_object != nullptr;
}

std::uint64_t get_host_objects_fingerprint() {
    std::uint64_t hash = 1469598103934665603ull;
    const auto mix = [&](std::uint64_t value) {
        hash ^= value;
        hash *= 1099511628211ull;
    };
    const auto mix_string = [&](const std::string& value) {
        for (unsigned char c : value) {
            mix(c);
        }
    };

    const std::int32_t pmd_count = get_pmd_num();
    const std::int32_t acs_count = get_acs_num();
    mix(static_cast<std::uint64_t>(pmd_count));
    mix(static_cast<std::uint64_t>(acs_count));
    for (std::int32_t i = 0; i < pmd_count; ++i) {
        mix(get_pmd_id(i));
        mix(static_cast<std::uint64_t>(get_pmd_order(i)));
        mix(get_pmd_disp(i) ? 1 : 0);
        mix_string(get_pmd_filename(i));
    }
    for (std::int32_t i = 0; i < acs_count; ++i) {
        mix(get_acs_id(i));
        mix(static_cast<std::uint64_t>(get_acs_order(i)));
        mix(get_acs_disp(i) ? 1 : 0);
        mix_string(get_acs_filename(i));
    }
    return hash;
}

} // namespace hikari::core
