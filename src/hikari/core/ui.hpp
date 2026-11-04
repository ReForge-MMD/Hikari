#pragma once

#include <windows.h>

namespace hikari::core {

void init_ui(HWND parent);
void ensure_ui_injected();
void notify_host_objects_changed();
void toggle_effects_manager(HWND parent);

} // namespace hikari::core
