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
add_library(profile_catalog native/profile_catalog.cc)
target_link_libraries(profile_catalog PUBLIC profile_wire)
add_library(canvas_noise native/canvas_noise.cc)
target_compile_features(canvas_noise PUBLIC cxx_std_20)
add_executable(canvas_noise_test native/canvas_noise_test.cc)
target_link_libraries(canvas_noise_test PRIVATE canvas_noise)
add_executable(profile_catalog_test native/profile_catalog_test.cc)
target_link_libraries(profile_catalog_test PRIVATE profile_catalog)

# L3 storage layer: storage pinning, cross-store consistency, cache record
# format, extension pinning. Sourced from the Reference Chromium-leg findings;
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

foreach(native_target profile_wire profile_wire_test golden_v2_decoder_test
                      profile_catalog profile_catalog_test canvas_noise
                      canvas_noise_test storage_pin storage_pin_test
                      storage_consistency storage_consistency_test
                      cache_record cache_record_test extension_pin
                      extension_pin_test)
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
               extension_pin_test)
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
