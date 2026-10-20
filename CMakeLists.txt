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

# L3 storage layer: storage pinning, cross-store consistency, cache record
# format, extension pinning. Sourced from the the reference Chromium-leg findings;
# see docs/cc-task-brief-l3.md and docs/l3-reports/.
add_library(storage_pin native/storage_pin.cc)
target_compile_features(storage_pin PUBLIC cxx_std_20)
add_executable(storage_pin_test native/storage_pin_test.cc)
target_link_libraries(storage_pin_test PRIVATE storage_pin)

add_library(storage_consistency native/storage_consistency.cc)
target_compile_features(storage_consistency PUBLIC cxx_std_20)
add_executable(storage_consistency_test native/storage_consistency_test.cc)
target_link_libraries(storage_consistency_test PRIVATE storage_consistency)

add_library(cache_record native/cache_record.cc)
target_compile_features(cache_record PUBLIC cxx_std_20)
add_executable(cache_record_test native/cache_record_test.cc)
target_link_libraries(cache_record_test PRIVATE cache_record)

add_library(extension_pin native/extension_pin.cc)
target_compile_features(extension_pin PUBLIC cxx_std_20)
add_executable(extension_pin_test native/extension_pin_test.cc)
target_link_libraries(extension_pin_test PRIVATE extension_pin)

# L3W1 document decoder plus the renderer-side runtime. The test executables
# link the whole primitive set because l3_wire's header exposes it all.
add_library(l3_wire native/l3_wire.cc native/storage_pin.cc
            native/storage_consistency.cc native/cache_record.cc
            native/extension_pin.cc)
target_compile_features(l3_wire PUBLIC cxx_std_20)
add_executable(l3_wire_test native/l3_wire_test.cc)
target_link_libraries(l3_wire_test PRIVATE l3_wire)

add_library(l3_runtime native/l3_runtime.cc)
target_compile_features(l3_runtime PUBLIC cxx_std_20)
target_link_libraries(l3_runtime PUBLIC l3_wire)
add_executable(l3_runtime_test native/l3_runtime_test.cc)
target_link_libraries(l3_runtime_test PRIVATE l3_runtime)

add_executable(l3_bootstrap_test integration/l3_bootstrap_test.cc)
target_compile_features(l3_bootstrap_test PUBLIC cxx_std_20)
target_link_libraries(l3_bootstrap_test PRIVATE l3_wire)
target_include_directories(l3_bootstrap_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

foreach(native_target profile_wire profile_wire_test golden_v2_decoder_test
                      profile_catalog profile_catalog_test canvas_noise
                      canvas_noise_test storage_pin storage_pin_test
                      storage_consistency storage_consistency_test
                      cache_record cache_record_test extension_pin
                      extension_pin_test font_family_traits
                      font_family_traits_test font_metrics font_metrics_test
                      font_switch_test l3_wire l3_wire_test l3_runtime
                      l3_runtime_test l3_bootstrap_test)
  if(MSVC)
    target_compile_options(${native_target} PRIVATE /W4 /WX)
  else()
    target_compile_options(${native_target} PRIVATE -Wall -Wextra -Werror -pedantic)
  endif()
endforeach()

# googletest: use a system package when available, otherwise build it from the
# Chromium source tree (the canonical copy this project is validated against).
# Set -DGTEST_ROOT=<path to googletest/src/googletest> to point at it.
find_package(GTest QUIET)
if(NOT GTest_FOUND)
  if(NOT GTEST_ROOT)
    set(GTEST_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/googletest/src/googletest")
  endif()
  if(NOT EXISTS "${GTEST_ROOT}/src/gtest-all.cc")
    message(FATAL_ERROR
      "googletest not found. Install libgtest-dev or pass "
      "-DGTEST_ROOT=<chromium>/third_party/googletest/src/googletest")
  endif()
  add_library(gtest_l3 STATIC
    "${GTEST_ROOT}/src/gtest-all.cc"
    "${GTEST_ROOT}/src/gtest_main.cc")
  target_include_directories(gtest_l3 PUBLIC
    "${GTEST_ROOT}/include" "${GTEST_ROOT}")
  set(L3_GTEST_TARGET gtest_l3)
else()
  set(L3_GTEST_TARGET GTest::gtest GTest::gtest_main)
endif()

foreach(l3_test storage_pin_test storage_consistency_test cache_record_test
               extension_pin_test l3_wire_test l3_runtime_test
               l3_bootstrap_test)
  target_link_libraries(${l3_test} PRIVATE ${L3_GTEST_TARGET})
endforeach()

enable_testing()
add_test(NAME profile_wire COMMAND profile_wire_test)
add_test(NAME golden_v2_decoder COMMAND golden_v2_decoder_test)
add_test(NAME profile_catalog COMMAND profile_catalog_test)
add_test(NAME canvas_noise COMMAND canvas_noise_test)
add_test(NAME storage_pin COMMAND storage_pin_test)
add_test(NAME storage_consistency COMMAND storage_consistency_test)
add_test(NAME cache_record COMMAND cache_record_test)
add_test(NAME extension_pin COMMAND extension_pin_test)
add_test(NAME l3_wire COMMAND l3_wire_test)
add_test(NAME l3_runtime COMMAND l3_runtime_test)
add_test(NAME l3_bootstrap COMMAND l3_bootstrap_test)
add_test(NAME font_family_traits COMMAND font_family_traits_test)
add_test(NAME font_metrics COMMAND font_metrics_test)
add_test(NAME font_switch COMMAND font_switch_test)
