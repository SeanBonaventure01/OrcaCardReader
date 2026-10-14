# Host build: portable (★) firmware components + fakes + tests + simulator, built natively.
# No ESP-IDF include paths are visible here, so a ★ component that includes an ESP-IDF
# header fails to compile (tools/check_portable.sh gives a clearer error first).
cmake_minimum_required(VERSION 3.20)
project(orca_host CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ORCA_COVERAGE "Build with coverage instrumentation" OFF)

if(ORCA_COVERAGE)
  add_compile_options(--coverage -O0 -g)
  add_link_options(--coverage)
endif()

include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Our code builds with warnings as errors (third-party code above does not).
add_compile_options(-Wall -Wextra -Wpedantic -Werror)

set(ORCA_COMPONENTS ${CMAKE_CURRENT_LIST_DIR}/../firmware/components)

# ★ portable components
foreach(comp nlohmann_json orca_hal game_core games config api)
  add_subdirectory(${ORCA_COMPONENTS}/${comp} ${CMAKE_BINARY_DIR}/components/${comp})
endforeach()

# Fakes for tests and the simulator
add_library(orca_fakes INTERFACE)
target_include_directories(orca_fakes INTERFACE fakes/include)
target_link_libraries(orca_fakes INTERFACE orca_hal nlohmann_json)

# Tests
enable_testing()
file(GLOB ORCA_TEST_SOURCES CONFIGURE_DEPENDS tests/*_test.cpp)
add_executable(orca_tests ${ORCA_TEST_SOURCES})
target_include_directories(orca_tests PRIVATE sim)
target_link_libraries(orca_tests PRIVATE orca_fakes orca_games orca_api GTest::gtest_main GTest::gmock)
target_compile_definitions(orca_tests PRIVATE ORCA_API_FIXTURES_DIR="${CMAKE_CURRENT_LIST_DIR}/tests/api_fixtures")
# One ctest entry; googletest itself reports each test. (gtest_discover_tests mis-parses
# parameterized test listings and registers duplicates.)
add_test(NAME orca_tests COMMAND orca_tests --gtest_brief=1)
