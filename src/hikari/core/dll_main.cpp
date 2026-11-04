// HikariEffects is the companion effect DLL. It shares the core effect engine and
// exists so effect-only deployments can be dropped next to the host.
#include <windows.h>

#include "hikari/core/log.hpp"

BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        hikari::core::install_av_tracer();
    }
    return TRUE;
}
