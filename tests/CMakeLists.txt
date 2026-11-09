add_executable(hikari_tests
  test_animation.cpp
  test_d3d11_effect.cpp
  test_effect.cpp
  test_formats.cpp
  test_gfx_frame_graph.cpp
  test_gfx_material.cpp
  test_gfx_settings.cpp
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
    hikari_gfx
    hikari_core
    Catch2::Catch2WithMain
)

if(HIKARI_BUILD_STUDIO)
  target_sources(hikari_tests PRIVATE
    ../src/hikari/studio/app_settings.cpp
    ../src/hikari/studio/app_state.cpp
    ../src/hikari/studio/localization.cpp
    ../src/hikari/studio/project_io.cpp
    test_frustum.cpp
    test_project.cpp
    test_settings.cpp
    test_state.cpp
    test_video.cpp
  )
  target_link_libraries(hikari_tests PRIVATE hikari_video Qt6::Widgets Qt6::Gui Qt6::Core)
  add_custom_command(TARGET hikari_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "$<TARGET_FILE:Qt6::Core>" "$<TARGET_FILE:Qt6::Gui>"
      "$<TARGET_FILE:Qt6::Widgets>" "$<TARGET_FILE_DIR:hikari_tests>"
    COMMENT "Copying the Qt runtime next to hikari_tests.exe"
  )
endif()

include(Catch)
catch_discover_tests(hikari_tests)
