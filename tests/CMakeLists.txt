add_executable(hikari_tests
  test_animation.cpp
  test_d3d11_effect.cpp
  test_effect.cpp
  test_formats.cpp
  test_math.cpp
  test_physics.cpp
  test_skinning.cpp
)

target_link_libraries(hikari_tests
  PRIVATE
    hikari_options
    hikari_formats
    hikari_animation
    hikari_physics
    hikari_render
    hikari_core
    Catch2::Catch2WithMain
)

include(Catch)
catch_discover_tests(hikari_tests)
