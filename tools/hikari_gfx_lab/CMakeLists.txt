add_executable(hikari_gfx_lab
  capture.cpp
  main.cpp
  scene.cpp
)

target_link_libraries(hikari_gfx_lab
  PRIVATE
    hikari_options
    hikari_gfx
    hikari_render
    hikari_core
    hikari_formats
    hikari_animation
)

target_include_directories(hikari_gfx_lab PRIVATE ${Stb_INCLUDE_DIR})

if(WIN32)
  target_link_libraries(hikari_gfx_lab PRIVATE d3d11 dxgi dxguid d3dcompiler user32 gdi32)
endif()

if(HIKARI_BUILD_TESTS)
  add_test(
    NAME gfx_phase2_synthetic
    COMMAND hikari_gfx_lab --phase 2 --scene synthetic
            --capture "${CMAKE_CURRENT_BINARY_DIR}/phase2_synthetic.bmp"
            --probe "${CMAKE_CURRENT_BINARY_DIR}/phase2_synthetic.json"
            --compare "${CMAKE_SOURCE_DIR}/tests/gfx/golden/phase2_synthetic.bmp"
  )
  set_tests_properties(gfx_phase2_synthetic PROPERTIES
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
endif()
