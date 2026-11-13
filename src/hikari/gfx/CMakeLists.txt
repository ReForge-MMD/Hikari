add_library(hikari_gfx STATIC
  ao_ssr_gi.cpp
  fog.cpp
  frame_graph.cpp
  gbuffer.cpp
  ibl.cpp
  material.cpp
  material_import.cpp
  material_presets.cpp
  matgen/material_compiler.cpp
  matgen/material_graph.cpp
  matgen/material_io.cpp
  renderer.cpp
  settings.cpp
  shader_library.cpp
  sky.cpp
  sky_assets.cpp
  target_pool.cpp
  toon_ramp.cpp
)

target_link_libraries(hikari_gfx
  PUBLIC hikari_options hikari_core hikari_render hikari_formats hikari_animation
)

target_include_directories(hikari_gfx PRIVATE ${Stb_INCLUDE_DIR})

set(HIKARI_GFX_SHADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
set(HIKARI_GFX_GENERATED "${CMAKE_CURRENT_BINARY_DIR}/gfx_shader_sources.generated.cpp")
file(GLOB HIKARI_GFX_SHADER_FILES CONFIGURE_DEPENDS "${HIKARI_GFX_SHADER_DIR}/*.hlsl")

add_custom_command(
  OUTPUT "${HIKARI_GFX_GENERATED}"
  COMMAND "${CMAKE_COMMAND}"
          -DGFX_SHADER_DIR=${HIKARI_GFX_SHADER_DIR}
          -DOUTPUT=${HIKARI_GFX_GENERATED}
          -P "${CMAKE_SOURCE_DIR}/cmake/embed_gfx_shaders.cmake"
  DEPENDS ${HIKARI_GFX_SHADER_FILES}
          "${CMAKE_SOURCE_DIR}/cmake/embed_gfx_shaders.cmake"
  COMMENT "Embedding Hikari gfx shaders"
  VERBATIM
)

target_sources(hikari_gfx PRIVATE "${HIKARI_GFX_GENERATED}")
set_source_files_properties("${HIKARI_GFX_GENERATED}" PROPERTIES GENERATED TRUE)
