add_executable(hikari_tests
  test_animation.cpp
  test_d3d11_effect.cpp
  test_effect.cpp
  test_formats.cpp
  test_gfx_frame_graph.cpp
  test_gfx_ao_ssr_gi.cpp
  test_gfx_fog.cpp
  test_gfx_ibl.cpp
  test_gfx_material.cpp
  test_gfx_settings.cpp
  test_gfx_shadow.cpp
  test_gfx_sky.cpp
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

if(TARGET hikari_gfx_lab)
  add_test(NAME gfx_phase3_shadow
    COMMAND hikari_gfx_lab
      --phase 3 --scene synthetic
      --capture ${CMAKE_SOURCE_DIR}/tests/gfx/golden/phase3_shadow.bmp
      --probe ${CMAKE_SOURCE_DIR}/tests/gfx/golden/phase3_shadow.json
      --compare ${CMAKE_SOURCE_DIR}/tests/gfx/golden/phase3_shadow.bmp
    WORKING_DIRECTORY $<TARGET_FILE_DIR:hikari_gfx_lab>
  )
  set_tests_properties(gfx_phase3_shadow PROPERTIES TIMEOUT 60)

  # Phase 4 captures go to the build tree so the committed goldens stay inputs.
  add_test(NAME gfx_phase4_sky
    COMMAND hikari_gfx_lab
      --phase 4 --scene synthetic --time-of-day 12 --ibl 1
      --capture ${CMAKE_CURRENT_BINARY_DIR}/phase4_sky.bmp
      --probe ${CMAKE_CURRENT_BINARY_DIR}/phase4_sky.json
      --compare ${CMAKE_SOURCE_DIR}/tests/gfx/golden/phase4_sky.bmp
    WORKING_DIRECTORY $<TARGET_FILE_DIR:hikari_gfx_lab>
  )
  set_tests_properties(gfx_phase4_sky PROPERTIES TIMEOUT 120)

  add_test(NAME gfx_phase4_night
    COMMAND hikari_gfx_lab
      --phase 4 --scene synthetic --time-of-day 0 --ibl 1
      --capture ${CMAKE_CURRENT_BINARY_DIR}/phase4_night.bmp
      --probe ${CMAKE_CURRENT_BINARY_DIR}/phase4_night.json
      --compare ${CMAKE_SOURCE_DIR}/tests/gfx/golden/phase4_night.bmp
    WORKING_DIRECTORY $<TARGET_FILE_DIR:hikari_gfx_lab>
  )
  set_tests_properties(gfx_phase4_night PROPERTIES TIMEOUT 120)

  add_test(NAME gfx_phase4_custom_ibl
    COMMAND hikari_gfx_lab
      --phase 4 --scene synthetic --time-of-day 12 --ibl 1
      --ibl-path ${CMAKE_SOURCE_DIR}/tests/gfx/assets/phase4_custom.hdr
      --capture ${CMAKE_CURRENT_BINARY_DIR}/phase4_custom_ibl.bmp
      --probe ${CMAKE_CURRENT_BINARY_DIR}/phase4_custom_ibl.json
      --compare ${CMAKE_SOURCE_DIR}/tests/gfx/golden/phase4_custom_ibl.bmp
    WORKING_DIRECTORY $<TARGET_FILE_DIR:hikari_gfx_lab>
  )
  set_tests_properties(gfx_phase4_custom_ibl PROPERTIES TIMEOUT 120)

  # Phase 5 goldens. Three frames let the GI feedback equilibrium settle, and
  # captures go to the build tree so the committed goldens stay inputs.
  add_test(NAME gfx_phase5_ao
    COMMAND hikari_gfx_lab
      --phase 5 --scene phase5 --frames 3 --debug 18
      --capture ${CMAKE_CURRENT_BINARY_DIR}/phase5_ao.bmp
      --probe ${CMAKE_CURRENT_BINARY_DIR}/phase5_ao.json
      --compare ${CMAKE_SOURCE_DIR}/tests/gfx/golden/phase5_ao.bmp
    WORKING_DIRECTORY $<TARGET_FILE_DIR:hikari_gfx_lab>
  )
  set_tests_properties(gfx_phase5_ao PROPERTIES TIMEOUT 120)

  add_test(NAME gfx_phase5_reflection
    COMMAND hikari_gfx_lab
      --phase 5 --scene phase5 --frames 3
      --capture ${CMAKE_CURRENT_BINARY_DIR}/phase5_reflection.bmp
      --probe ${CMAKE_CURRENT_BINARY_DIR}/phase5_reflection.json
      --compare ${CMAKE_SOURCE_DIR}/tests/gfx/golden/phase5_reflection.bmp
      --expect-mean-min 0.24 --expect-mean-max 0.34
    WORKING_DIRECTORY $<TARGET_FILE_DIR:hikari_gfx_lab>
  )
  set_tests_properties(gfx_phase5_reflection PROPERTIES TIMEOUT 120)

  add_test(NAME gfx_phase5_reflection_parallax
    COMMAND hikari_gfx_lab
      --phase 5 --scene phase5 --frames 3 --camera-azimuth 20
      --capture ${CMAKE_CURRENT_BINARY_DIR}/phase5_reflection_parallax.bmp
      --probe ${CMAKE_CURRENT_BINARY_DIR}/phase5_reflection_parallax.json
      --compare ${CMAKE_SOURCE_DIR}/tests/gfx/golden/phase5_reflection_parallax.bmp
    WORKING_DIRECTORY $<TARGET_FILE_DIR:hikari_gfx_lab>
  )
  set_tests_properties(gfx_phase5_reflection_parallax PROPERTIES TIMEOUT 120)

  add_test(NAME gfx_phase5_gi_bleed
    COMMAND hikari_gfx_lab
      --phase 5 --scene phase5 --frames 3 --debug 22
      --capture ${CMAKE_CURRENT_BINARY_DIR}/phase5_gi_bleed.bmp
      --probe ${CMAKE_CURRENT_BINARY_DIR}/phase5_gi_bleed.json
      --compare ${CMAKE_SOURCE_DIR}/tests/gfx/golden/phase5_gi_bleed.bmp
    WORKING_DIRECTORY $<TARGET_FILE_DIR:hikari_gfx_lab>
  )
  set_tests_properties(gfx_phase5_gi_bleed PROPERTIES TIMEOUT 120)

  # GI off must remove the indirect energy instead of merely dimming it.
  add_test(NAME gfx_phase5_gi_off
    COMMAND hikari_gfx_lab
      --phase 5 --scene phase5 --frames 3 --gi 0
      --capture ${CMAKE_CURRENT_BINARY_DIR}/phase5_gi_off.bmp
      --probe ${CMAKE_CURRENT_BINARY_DIR}/phase5_gi_off.json
      --compare ${CMAKE_SOURCE_DIR}/tests/gfx/golden/phase5_gi_off.bmp
      --expect-mean-max 0.21
    WORKING_DIRECTORY $<TARGET_FILE_DIR:hikari_gfx_lab>
  )
  set_tests_properties(gfx_phase5_gi_off PROPERTIES TIMEOUT 120)

  # The confidence view exposes the SSR depth-discontinuity rejection.
  add_test(NAME gfx_phase5_ssr_edges
    COMMAND hikari_gfx_lab
      --phase 5 --scene phase5 --frames 3 --debug 21
      --capture ${CMAKE_CURRENT_BINARY_DIR}/phase5_ssr_edges.bmp
      --probe ${CMAKE_CURRENT_BINARY_DIR}/phase5_ssr_edges.json
      --compare ${CMAKE_SOURCE_DIR}/tests/gfx/golden/phase5_ssr_edges.bmp
    WORKING_DIRECTORY $<TARGET_FILE_DIR:hikari_gfx_lab>
  )
  set_tests_properties(gfx_phase5_ssr_edges PROPERTIES TIMEOUT 120)

  # One-tap single-bounce SSPT is the noisiest configuration; the prefilter and
  # denoiser must keep fireflies from dominating the image.
  add_test(NAME gfx_phase5_firefly
    COMMAND hikari_gfx_lab
      --phase 5 --scene phase5 --frames 3 --gi-quality 1
      --capture ${CMAKE_CURRENT_BINARY_DIR}/phase5_firefly.bmp
      --probe ${CMAKE_CURRENT_BINARY_DIR}/phase5_firefly.json
      --compare ${CMAKE_SOURCE_DIR}/tests/gfx/golden/phase5_firefly.bmp
      --expect-mean-min 0.22 --expect-mean-max 0.31
    WORKING_DIRECTORY $<TARGET_FILE_DIR:hikari_gfx_lab>
  )
  set_tests_properties(gfx_phase5_firefly PROPERTIES TIMEOUT 120)

  # Phase 6 goldens. The analytical path is a closed form, so it must differ from
  # the marched one while both stay inside a narrow energy band; fog off has to
  # give back the unfogged room instead of a dimmed copy.
  add_test(NAME gfx_phase6_analytical
    COMMAND hikari_gfx_lab
      --phase 6 --scene phase6 --frames 3 --fog 1
      --capture ${CMAKE_CURRENT_BINARY_DIR}/phase6_analytical.bmp
      --probe ${CMAKE_CURRENT_BINARY_DIR}/phase6_analytical.json
      --compare ${CMAKE_SOURCE_DIR}/tests/gfx/golden/phase6_analytical.bmp
      --expect-mean-min 0.19 --expect-mean-max 0.23
    WORKING_DIRECTORY $<TARGET_FILE_DIR:hikari_gfx_lab>
  )
  set_tests_properties(gfx_phase6_analytical PROPERTIES TIMEOUT 120)

  add_test(NAME gfx_phase6_volumetric
    COMMAND hikari_gfx_lab
      --phase 6 --scene phase6 --frames 3
      --capture ${CMAKE_CURRENT_BINARY_DIR}/phase6_volumetric.bmp
      --probe ${CMAKE_CURRENT_BINARY_DIR}/phase6_volumetric.json
      --compare ${CMAKE_SOURCE_DIR}/tests/gfx/golden/phase6_volumetric.bmp
      --expect-mean-min 0.18 --expect-mean-max 0.22
    WORKING_DIRECTORY $<TARGET_FILE_DIR:hikari_gfx_lab>
  )
  set_tests_properties(gfx_phase6_volumetric PROPERTIES TIMEOUT 120)

  # The shaft channel must stay sparse: only the volume the sun reaches through
  # the three openings carries in-scattered light.
  add_test(NAME gfx_phase6_godrays
    COMMAND hikari_gfx_lab
      --phase 6 --scene phase6 --frames 3 --fog-debug 4 --target FogDebug
      --capture ${CMAKE_CURRENT_BINARY_DIR}/phase6_godrays.bmp
      --probe ${CMAKE_CURRENT_BINARY_DIR}/phase6_godrays.json
      --compare ${CMAKE_SOURCE_DIR}/tests/gfx/golden/phase6_godrays.bmp
    WORKING_DIRECTORY $<TARGET_FILE_DIR:hikari_gfx_lab>
  )
  set_tests_properties(gfx_phase6_godrays PROPERTIES TIMEOUT 120)

  # Dense overcast plus the cloud shadow switched off brightens the shafts,
  # which is the contribution the flag removes.
  add_test(NAME gfx_phase6_cloud_shadow
    COMMAND hikari_gfx_lab
      --phase 6 --scene phase6 --frames 3 --cloud-coverage 0.7 --fog-cloud-shadow 0
      --capture ${CMAKE_CURRENT_BINARY_DIR}/phase6_cloud_shadow.bmp
      --probe ${CMAKE_CURRENT_BINARY_DIR}/phase6_cloud_shadow.json
      --compare ${CMAKE_SOURCE_DIR}/tests/gfx/golden/phase6_cloud_shadow.bmp
      --expect-mean-min 0.19 --expect-mean-max 0.23
    WORKING_DIRECTORY $<TARGET_FILE_DIR:hikari_gfx_lab>
  )
  set_tests_properties(gfx_phase6_cloud_shadow PROPERTIES TIMEOUT 120)

  # Transmittance view: dense ground mist, clear air above it, and the pillar.
  add_test(NAME gfx_phase6_debug
    COMMAND hikari_gfx_lab
      --phase 6 --scene phase6 --frames 3 --fog-debug 1 --target FogDebug
      --capture ${CMAKE_CURRENT_BINARY_DIR}/phase6_debug.bmp
      --probe ${CMAKE_CURRENT_BINARY_DIR}/phase6_debug.json
      --compare ${CMAKE_SOURCE_DIR}/tests/gfx/golden/phase6_debug.bmp
    WORKING_DIRECTORY $<TARGET_FILE_DIR:hikari_gfx_lab>
  )
  set_tests_properties(gfx_phase6_debug PROPERTIES TIMEOUT 120)

  add_test(NAME gfx_phase6_off
    COMMAND hikari_gfx_lab
      --phase 6 --scene phase6 --frames 3 --fog 0
      --capture ${CMAKE_CURRENT_BINARY_DIR}/phase6_off.bmp
      --probe ${CMAKE_CURRENT_BINARY_DIR}/phase6_off.json
      --compare ${CMAKE_SOURCE_DIR}/tests/gfx/golden/phase6_off.bmp
      --expect-mean-max 0.09
    WORKING_DIRECTORY $<TARGET_FILE_DIR:hikari_gfx_lab>
  )
  set_tests_properties(gfx_phase6_off PROPERTIES TIMEOUT 120)

  # Twelve steps is the low quality budget; the blurred volume still has to keep
  # the shafts free of banding, so the golden is compared at the same tolerance.
  add_test(NAME gfx_phase6_quality_low
    COMMAND hikari_gfx_lab
      --phase 6 --scene phase6 --frames 3 --fog-quality 0
      --capture ${CMAKE_CURRENT_BINARY_DIR}/phase6_quality_low.bmp
      --probe ${CMAKE_CURRENT_BINARY_DIR}/phase6_quality_low.json
      --compare ${CMAKE_SOURCE_DIR}/tests/gfx/golden/phase6_quality_low.bmp
      --expect-mean-min 0.18 --expect-mean-max 0.22
    WORKING_DIRECTORY $<TARGET_FILE_DIR:hikari_gfx_lab>
  )
  set_tests_properties(gfx_phase6_quality_low PROPERTIES TIMEOUT 120)
endif()
