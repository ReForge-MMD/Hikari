include_guard(GLOBAL)

function(hikari_apply_warnings target)
  target_compile_definitions(${target} INTERFACE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    _CRT_SECURE_NO_WARNINGS
  )
  if(MSVC)
    # Embedded debug information (/Z7) so ccache can cache objects with symbols.
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "Embedded" CACHE STRING "MSVC debug information format" FORCE)
    target_compile_options(${target} INTERFACE
      /W4
      /permissive-
      /Zc:__cplusplus
      /Zc:preprocessor
      /utf-8
      /EHsc
      /MP
    )
    # Full Program Database on the linker side, /Z7 on the compiler side so ccache can
    # cache object files that carry debug information.
    target_compile_options(${target} INTERFACE
      $<$<CONFIG:Debug>:/Od>
      $<$<CONFIG:RelWithDebInfo>:/O2>
      $<$<CONFIG:Release>:/O2>
    )
    target_link_options(${target} INTERFACE
      $<$<CONFIG:Debug>:/DEBUG:FULL>
      $<$<CONFIG:RelWithDebInfo>:/DEBUG:FULL>
    )
  endif()
endfunction()

find_program(HIKARI_CCACHE_EXECUTABLE NAMES ccache)
if(HIKARI_CCACHE_EXECUTABLE)
  set(CMAKE_CXX_COMPILER_LAUNCHER "${HIKARI_CCACHE_EXECUTABLE}" CACHE STRING "C++ compiler launcher")
  message(STATUS "Hikari: using ccache at ${HIKARI_CCACHE_EXECUTABLE}")
else()
  message(WARNING "Hikari: ccache was not found; builds will not use the compiler cache")
endif()
