find_package(FFMPEG REQUIRED)

add_library(hikari_audio STATIC
  audio_player.cpp
  audio_player.hpp
  audio_track.cpp
  audio_track.hpp
)

target_link_libraries(hikari_audio
  PUBLIC
    hikari_options
)

target_include_directories(hikari_audio SYSTEM PRIVATE ${FFMPEG_INCLUDE_DIRS})
target_link_directories(hikari_audio PUBLIC ${FFMPEG_LIBRARY_DIRS})
target_link_libraries(hikari_audio PUBLIC ${FFMPEG_LIBRARIES})
target_link_libraries(hikari_audio PRIVATE xaudio2 ole32)
