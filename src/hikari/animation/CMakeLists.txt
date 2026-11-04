add_library(hikari_animation STATIC
  bezier.cpp
  jiggle.cpp
  morph.cpp
  skeleton.cpp
  timeline.cpp
)

target_link_libraries(hikari_animation
  PUBLIC hikari_options hikari_formats hikari_physics
)
