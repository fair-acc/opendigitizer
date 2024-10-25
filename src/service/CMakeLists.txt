cmake_minimum_required(VERSION 3.19)
set(CMAKE_CXX_STANDARD 23)

# Mainly for FMT
set(CMAKE_POSITION_INDEPENDENT_CODE TRUE)

project(opendigitizer-service CXX)
include(GNUInstallDirs)

include(cmake/Dependencies.cmake)

if(NOT TARGET od_acquisition)
  add_subdirectory(../acquisition acquisition)
endif()
if(NOT TARGET digitizer_settings)
  add_subdirectory(../utils utils)
endif()

set(WASM_BUILD_DIR
    "${CMAKE_BINARY_DIR}/build-wasm"
    CACHE
      FILEPATH
      "The absolute path to a directory containing the build output of the emscripten ui build to be bundled into the assets"
)

set(SERVING_DIR ${WASM_BUILD_DIR})
configure_file(build_configuration.hpp.in ${CMAKE_BINARY_DIR}/build_configuration.hpp @ONLY)
include_directories(${CMAKE_BINARY_DIR})

# Check for supported compiler versions
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
  if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 11.1.0)
    message(FATAL_ERROR "GCC>=11.1.0 required, but gcc ${CMAKE_CXX_COMPILER_VERSION} detected.")
  endif()
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 13.0.0)
    message(FATAL_ERROR "Clang>=13.0.0 required, but clang ${CMAKE_CXX_COMPILER_VERSION} detected.")
  endif()
else()
  message(WARN "No version check for your compiler (${CMAKE_CXX_COMPILER_ID}) implemented, "
          "in case of build problems consider updating your compiler or check if you can switch to gcc or clang")
endif()

# Link this 'library' to set the c++ standard / compile-time options requested
add_library(project_options INTERFACE)
target_include_directories(project_options INTERFACE ${CMAKE_SOURCE_DIR}/3rd_party)

target_compile_features(project_options INTERFACE cxx_std_23)

if(CMAKE_CXX_COMPILER_ID MATCHES ".*Clang")
  option(ENABLE_BUILD_WITH_TIME_TRACE "Enable -ftime-trace to generate time tracing .json files on clang" OFF)
  if(ENABLE_BUILD_WITH_TIME_TRACE)
    target_compile_options(project_options INTERFACE -ftime-trace)
  endif()
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR GENERATOR_IS_MULTI_CONFIG)
  option(ENABLE_COVERAGE "Enable Coverage" ON)
else()
  option(ENABLE_COVERAGE "Enable Coverage" OFF)
endif()

if(OPENDIGITIZER_ENABLE_TESTING)
  if(ENABLE_COVERAGE)
    if(UNIX AND NOT APPLE) # Linux
      message("Coverage reporting enabled")
      include(cmake/CodeCoverage.cmake) # https://github.com/bilke/cmake-modules/blob/master/CodeCoverage.cmake
      # (License: BSL-1.0)
      target_compile_options(
        project_options
        INTERFACE --coverage
                  -O0
                  -g
                  -U_FORTIFY_SOURCE
                  -D_FORTIFY_SOURCE=0) # fortify_source is not possible without optimization
      target_link_libraries(project_options INTERFACE --coverage)
      set(GCOVR_ADDITIONAL_ARGS --print-summary --gcov-ignore-errors=all --verbose)
      append_coverage_compiler_flags()
      setup_target_for_coverage_gcovr_xml(
        NAME
        opendigitizer_coverage
        EXECUTABLE
        ctest
        EXECUTABLE_ARGS
        "--output-on-failure"
        DEPENDENCIES
        qa_GnuRadioWorker # TODO prevent qa_BlockScalingOffset (gr-digitizers) from being run and remove it here
        EXCLUDE
        "$CMAKE_BUILD_DIR/*"
        ".*/test/.*")
      setup_target_for_coverage_gcovr_html(
        NAME
        opendigitizer_coverage_html
        EXECUTABLE
        ctest
        EXECUTABLE_ARGS
        "--output-on-failure"
        DEPENDENCIES
        qa_GnuRadioWorker
        EXCLUDE
        "$CMAKE_BUILD_DIR/*"
        ".*/test/.*")
    else()
      message(WARNING "Coverage is only supported on linux")
    endif()
  endif()
endif()

add_subdirectory(gnuradio)
add_subdirectory(rest) # worker providing access to static assets
add_subdirectory(dashboard)

message("COPY ${CMAKE_SOURCE_DIR}/demo_sslcert/demo_private.key DESTINATION ${CMAKE_CURRENT_BINARY_DIR}")
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/demo_sslcert/demo_private.key" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/")
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/demo_sslcert/demo_public.crt" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/")

add_executable(opendigitizer main.cpp)
# TODO fair-picoscope is a run-time dependencies only, should be loaded as plugin
target_link_libraries(
  opendigitizer
  PRIVATE od_dashboard_worker
          od_gnuradio_worker
          od_rest
          majordomo
          services
          client
          project_options
          assets::rest
          gr-testing
          gr-basic
          digitizer_settings)
if(NOT EMSCRIPTEN)
  target_link_libraries(opendigitizer PRIVATE fair-picoscope)
endif()
