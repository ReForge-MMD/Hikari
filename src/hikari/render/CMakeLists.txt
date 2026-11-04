add_library(hikari_render STATIC
  material.cpp
  mesh.cpp
  parallel.cpp
  scene_renderer.cpp
  skinning.cpp
  stb_impl.cpp
  texture.cpp
)

target_link_libraries(hikari_render
  PUBLIC hikari_options hikari_core hikari_formats hikari_animation
)

target_include_directories(hikari_render PRIVATE ${Stb_INCLUDE_DIR})
