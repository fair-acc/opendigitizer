cmake_minimum_required(VERSION 3.19)
set(CMAKE_CXX_STANDARD 23)

project(opendigitizer-service LANGUAGES CXX)
include(GNUInstallDirs)

include(cmake/Dependencies.cmake)

if(NOT TARGET od_acquisition)
  add_subdirectory(../acquisition acquisition)
endif()
if(NOT TARGET digitizer_settings)
  add_subdirectory(../utils utils)
endif()
if(NOT TARGET digitizer_common_utils)
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

target_compile_features(project_options INTERFACE cxx_std_23)

if(CMAKE_CXX_COMPILER_ID MATCHES ".*Clang")
  option(ENABLE_BUILD_WITH_TIME_TRACE "Enable -ftime-trace to generate time tracing .json files on clang" OFF)
  if(ENABLE_BUILD_WITH_TIME_TRACE)
    target_compile_options(project_options INTERFACE -ftime-trace)
  endif()
endif()

add_subdirectory(gnuradio)
add_subdirectory(rest) # worker providing access to static assets
add_subdirectory(dashboard)

file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/demo_sslcert/demo_private.key" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/")
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/demo_sslcert/demo_public.crt" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/")

add_executable(opendigitizer main.cpp)

# TODO fair-picoscope is a run-time dependencies only, should be loaded as plugin
target_link_libraries(
  opendigitizer
  PRIVATE od_dashboard_worker
          od_gnuradio_worker
          od_rest
          opendigitizer_version
          majordomo
          services
          client
          opendigitizer-options
          project_options
          fair-picoscope
          assets::rest
          digitizer_common_utils
          digitizer_settings
          gnuradio4::gnuradio-core
          gnuradio4::gnuradio-blocklib-core
          gnuradio4::GrBasicBlocksShared
          gnuradio4::GrElectricalBlocksShared
          gnuradio4::GrFileIoBlocksShared
          gnuradio4::GrFilterBlocksShared
          gnuradio4::GrFourierBlocksShared
          gnuradio4::GrHttpBlocksShared
          gnuradio4::GrMathBlocksShared
          gnuradio4::GrTestingBlocksShared)

# saftlib, beneath gr-digitizers' timing library, exposes a libstdc++ ABI and cannot link against libc++
if(NOT
   CMAKE_CXX_COMPILER_ID
   MATCHES
   "Clang")
  target_link_libraries(opendigitizer PRIVATE timing)
  target_compile_definitions(opendigitizer PRIVATE OPENDIGITIZER_TIMING)
endif()

add_custom_target(
  run-opendigitizer
  USES_TERMINAL
  COMMAND
    ${CMAKE_COMMAND} -E env
    OPENCMW_REST_CERT_FILE="${CMAKE_BINARY_DIR}/_deps/opencmw-cpp-src/src/client/test/assets/server-cert.pem"
    OPENCMW_REST_PRIVATE_KEY_FILE="${CMAKE_BINARY_DIR}/_deps/opencmw-cpp-src/src/client/test/assets/server-key.pem"
    DIGITIZER_CHECK_CERTIFICATES=0 DIGITIZER_HOSTNAME=localhost DIGITIZER_WASM_SERVE_DIR="${CMAKE_BINARY_DIR}/ui-wasm"
    DIGITIZER_REMOTE_DASHBOARDS="${PROJECT_SOURCE_DIR}/dashboard/defaultDashboards" "\"$<TARGET_FILE:opendigitizer>\"")
