// hikari_core.dll is the MMD host plug-in. It installs the crash tracer and lets the
// host call the exported MME-compatible effect helpers.
#include <windows.h>

#include "hikari/core/backend.hpp"
#include "hikari/core/effect.hpp"
#include "hikari/core/log.hpp"
#include "hikari/core/ui.hpp"

BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        hikari::core::install_av_tracer();
    } else if (reason == DLL_PROCESS_DETACH) {
        // Under the loader lock: never block on a mutex here.
        {
            std::lock_guard lock(hikari::core::g_d3d11_mutex);
            hikari::core::g_d3d11.reset();
        }
    }
    return TRUE;
}

extern "C" __declspec(dllexport) const char* HikariGetVersion() {
    return "Hikari 1.0.0 (Direct3D 11, C++20)";
}

extern "C" __declspec(dllexport) void HikariInstallUi(void* hwnd) {
    hikari::core::init_ui(static_cast<HWND>(hwnd));
}

extern "C" __declspec(dllexport) void HikariOnDeviceCreated(void* hwnd) {
    hikari::core::on_device_created(static_cast<HWND>(hwnd));
}

extern "C" __declspec(dllexport) void HikariOnEndScene() {
    hikari::core::on_end_scene();
}

extern "C" __declspec(dllexport) const char* HikariGetRecentLogs() {
    static thread_local std::string logs;
    logs = hikari::core::get_recent_logs();
    return logs.c_str();
}
