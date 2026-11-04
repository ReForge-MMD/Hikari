add_library(hikari_formats STATIC
  pmx.cpp
  vmd.cpp
)

target_link_libraries(hikari_formats
  PUBLIC hikari_options
)

target_compile_definitions(hikari_formats PRIVATE _CRT_SECURE_NO_WARNINGS)
