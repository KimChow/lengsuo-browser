cmake_minimum_required(VERSION 3.20)
project(lengsuo_browser_native LANGUAGES CXX)
add_library(profile_wire native/profile_wire.cc)
target_include_directories(profile_wire PUBLIC native)
target_compile_features(profile_wire PUBLIC cxx_std_20)
set_target_properties(profile_wire PROPERTIES CXX_EXTENSIONS OFF)
add_executable(profile_wire_test native/profile_wire_test.cc)
target_link_libraries(profile_wire_test PRIVATE profile_wire)
add_executable(golden_v2_decoder_test native/golden_v2_decoder_test.cc)
target_link_libraries(golden_v2_decoder_test PRIVATE profile_wire)
# The fonts dimension's {switcher, value, mask} shape (docs/cc-task-rulings.md
# §C). Header-only decoder with a stub TU so -Werror covers it.
add_library(font_family_traits native/font_family_traits.cc)
target_compile_features(font_family_traits PUBLIC cxx_std_20)
add_executable(font_family_traits_test native/font_family_traits_test.cc)
target_link_libraries(font_family_traits_test PRIVATE font_family_traits)
# The metric perturbation itself: the accepted mechanism for the dimension.
add_library(font_metrics native/font_metrics.cc)
target_compile_features(font_metrics PUBLIC cxx_std_20)
add_executable(font_metrics_test native/font_metrics_test.cc)
target_link_libraries(font_metrics_test PRIVATE font_metrics)
# The renderer-side switch path. fp_overrides.cc was in the GN source set but
# never in CMake, so a broken accessor compiled only in the browser build; this
# target keeps it -Werror-checked and driven through its real entry point.
add_executable(font_switch_test native/font_switch_test.cc
               native/fp_overrides.cc)
target_link_libraries(font_switch_test PRIVATE font_family_traits font_metrics)
add_library(profile_catalog native/profile_catalog.cc)
target_link_libraries(profile_catalog PUBLIC profile_wire)
add_library(canvas_noise native/canvas_noise.cc)
target_compile_features(canvas_noise PUBLIC cxx_std_20)
add_executable(canvas_noise_test native/canvas_noise_test.cc)
# The frozen reference fixture lives next to its generator; canvas_noise_test.cc
# includes it by bare name.
target_include_directories(canvas_noise_test PRIVATE tests/canvas_ref)
target_link_libraries(canvas_noise_test PRIVATE canvas_noise)
add_executable(profile_catalog_test native/profile_catalog_test.cc)
target_link_libraries(profile_catalog_test PRIVATE profile_catalog)
foreach(native_target profile_wire profile_wire_test golden_v2_decoder_test
                      profile_catalog profile_catalog_test canvas_noise
                      canvas_noise_test font_family_traits
                      font_family_traits_test font_metrics font_metrics_test
                      font_switch_test)
  if(MSVC)
    target_compile_options(${native_target} PRIVATE /W4 /WX)
  else()
    target_compile_options(${native_target} PRIVATE -Wall -Wextra -Werror -pedantic)
  endif()
endforeach()
enable_testing()
add_test(NAME profile_wire COMMAND profile_wire_test)
add_test(NAME golden_v2_decoder COMMAND golden_v2_decoder_test)
add_test(NAME profile_catalog COMMAND profile_catalog_test)
add_test(NAME canvas_noise COMMAND canvas_noise_test)
add_test(NAME font_family_traits COMMAND font_family_traits_test)
add_test(NAME font_metrics COMMAND font_metrics_test)
add_test(NAME font_switch COMMAND font_switch_test)
