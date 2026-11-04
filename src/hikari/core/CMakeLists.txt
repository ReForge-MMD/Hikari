add_library(hikari_core STATIC
  assignment.cpp
  backend.cpp
  com.cpp
  d3d11.cpp
  effect.cpp
  effect_com.cpp
  log.cpp
  mmd_host.cpp
  reflection.cpp
  shader.cpp
  states.cpp
  targets.cpp
  ui.cpp
)

# The plug-in DLL also produces an import library named hikari_core.lib; keep the
# static archive distinct on disk.
set_target_properties(hikari_core PROPERTIES OUTPUT_NAME "hikari_core_static")

target_link_libraries(hikari_core
  PUBLIC hikari_options hikari_formats
  PRIVATE d3d11 dxgi dxguid d3dcompiler comctl32 comdlg32 gdi32 user32 ole32 dbghelp
)

target_include_directories(hikari_core PRIVATE ${Stb_INCLUDE_DIR})

add_library(HikariEffects SHARED
  dll_main.cpp
)
target_link_libraries(HikariEffects PRIVATE hikari_core)
set_target_properties(HikariEffects PROPERTIES OUTPUT_NAME "HikariEffects")

add_library(hikari_core_plugin SHARED
  plugin_dll.cpp
)
target_link_libraries(hikari_core_plugin PRIVATE hikari_core)
set_target_properties(hikari_core_plugin PROPERTIES OUTPUT_NAME "hikari_core")
