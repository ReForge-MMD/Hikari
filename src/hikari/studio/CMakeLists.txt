set(CMAKE_AUTOMOC ON)

add_executable(hikari_studio WIN32
  app_state.cpp
  app_state.hpp
  d3d_viewport.cpp
  d3d_viewport.hpp
  main.cpp
  main_window.cpp
  main_window.hpp
  model_instance.hpp
  project_io.cpp
  project_io.hpp
  theme.hpp
  timeline_widget.cpp
  timeline_widget.hpp
  viewport_chrome.cpp
  viewport_chrome.hpp
)

target_link_libraries(hikari_studio
  PRIVATE
    hikari_options
    hikari_core
    hikari_formats
    hikari_animation
    hikari_physics
    hikari_render
    Qt6::Widgets
    Qt6::Gui
    Qt6::Core
)

target_include_directories(hikari_studio PRIVATE ${Stb_INCLUDE_DIR})

if(WIN32)
  target_link_libraries(hikari_studio PRIVATE d3d11 dxgi dxguid d3dcompiler)

  get_target_property(_hikari_qmake Qt6::qmake IMPORTED_LOCATION)
  if(_hikari_qmake)
    get_filename_component(_hikari_qt_bin "${_hikari_qmake}" DIRECTORY)
    find_program(HIKARI_WINDEPLOYQT windeployqt HINTS "${_hikari_qt_bin}")
    if(HIKARI_WINDEPLOYQT)
      add_custom_command(TARGET hikari_studio POST_BUILD
        COMMAND "${HIKARI_WINDEPLOYQT}"
                --no-compiler-runtime
                --no-translations
                --no-system-d3d-compiler
                --no-opengl-sw
                "$<TARGET_FILE:hikari_studio>"
        COMMENT "Deploying the Qt 6 runtime next to hikari_studio.exe"
      )
    endif()
  endif()
endif()
