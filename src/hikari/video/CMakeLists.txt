find_package(FFMPEG REQUIRED)

add_library(hikari_video STATIC
  video_encoder.cpp
  video_encoder.hpp
)

target_link_libraries(hikari_video
  PUBLIC
    hikari_options
)

target_include_directories(hikari_video SYSTEM PRIVATE ${FFMPEG_INCLUDE_DIRS})
target_link_directories(hikari_video PUBLIC ${FFMPEG_LIBRARY_DIRS})
target_link_libraries(hikari_video PUBLIC ${FFMPEG_LIBRARIES})
