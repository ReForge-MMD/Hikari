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

if(HIKARI_BUILD_STUDIO)
  target_sources(hikari_tests PRIVATE
    ../src/hikari/studio/project_io.cpp
    test_project.cpp
  )
  target_link_libraries(hikari_tests PRIVATE Qt6::Core)
  add_custom_command(TARGET hikari_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "$<TARGET_FILE:Qt6::Core>" "$<TARGET_FILE_DIR:hikari_tests>"
    COMMENT "Copying the Qt Core runtime next to hikari_tests.exe"
  )
endif()

include(Catch)
catch_discover_tests(hikari_tests)
