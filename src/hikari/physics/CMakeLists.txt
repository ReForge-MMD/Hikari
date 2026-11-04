add_library(hikari_physics STATIC
  mmd_physics.cpp
  rig.cpp
  world.cpp
)

target_link_libraries(hikari_physics
  PUBLIC hikari_options hikari_formats
  PRIVATE Jolt::Jolt
)

# Jolt exports its headers with warnings that are not actionable here.
target_compile_options(hikari_physics PRIVATE /wd4244 /wd4267)
