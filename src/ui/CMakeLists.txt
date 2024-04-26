cmake_minimum_required(VERSION 3.17)

# set a variable in CMake
set(projectName "opendigitizer-ui")

project(${projectName} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
# set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_VERBOSE_MAKEFILE OFF)

# Mainly for FMT
set(CMAKE_POSITION_INDEPENDENT_CODE TRUE)

set(sources
    main.cpp
    flowgraph.cpp
    flowgraphitem.cpp
    dashboard.cpp
    dashboardpage.cpp
    imguiutils.cpp
    opendashboardpage.cpp
    remotesignalsources.cpp)

set(GNURADIO_PREFIX
    "/usr/"
    CACHE FILEPATH "Prefix of the GNURadio installation")

add_subdirectory(app_header)
add_subdirectory(utils)

if(EMSCRIPTEN)
  message(STATUS "Detected emscripten webassembly build")

  # CAUTION: The "SHELL:" before some compile/link options has to be used to explicitly tell CMake to not sum up options
  # (e.g. -option A -option B => -option A B)(CMake default behaviour), but instead keep them separated (needed for
  # proper operation of emscripten)
  add_compile_options(
    -DIMGUI_DISABLE_FILE_FUNCTIONS
    -g
    -Wall
    -Wformat
    -Os
    -fwasm-exceptions
    "SHELL:-s USE_SDL=2"
    -pthread)

  add_link_options(
    "SHELL:-s WASM=1"
    "SHELL:-s ALLOW_MEMORY_GROWTH=1"
    "SHELL:-s NO_EXIT_RUNTIME=0"
    "SHELL:-s ASSERTIONS=1"
    "SHELL:-s NO_FILESYSTEM=1"
    --shell-file
    ${CMAKE_CURRENT_SOURCE_DIR}/shell_minimal.html
    "SHELL:-s PTHREAD_POOL_SIZE=30"
    "SHELL:-s USE_SDL=2"
    -fwasm-exceptions
    -pthread
    -sFETCH)
endif()

# dependencies
include(../../cmake/CMakeRC.cmake)
include(cmake/Dependencies.cmake)
cmrc_add_resource_library(
  ui_assets
  NAMESPACE
  ui_assets
  assets/fair-logo/FAIR_Logo_rgb_72dpi.png
  assets/fair-logo/FAIR_Logo_rgb_72dpi_dark.png
  assets/fontawesome/fa-regular-400.otf
  assets/fontawesome/fa-solid-900.otf
  assets/xkcd/xkcd.otf
  assets/xkcd/xkcd-script.ttf)
cmrc_add_resource_library(
  fonts
  NAMESPACE
  fonts
  WHENCE
  ${imgui_SOURCE_DIR}/misc/fonts
  ${imgui_SOURCE_DIR}/misc/fonts/Roboto-Medium.ttf)
cmrc_add_resource_library(
  sample_dashboards
  NAMESPACE
  sample_dashboards
  assets/sampleDashboards/ExtendedDemoDashboard.grc
  assets/sampleDashboards/ExtendedDemoDashboard.yml
  assets/sampleDashboards/DemoDashboard.grc
  assets/sampleDashboards/DemoDashboard.yml)

if(EMSCRIPTEN)
  # locate the "executable" output in the build/web directory
  set(EXECUTABLE_OUTPUT_PATH ${CMAKE_BINARY_DIR}/web)

  # necessary to tell CMake (with emscripten) to extend the standard .js and .wasm output with .html output (so that we
  # can execute it with the webserver)
  set(CMAKE_EXECUTABLE_SUFFIX ".html")

  set(outputFileName "index")
  add_executable(${outputFileName})

  target_sources(index PRIVATE ${sources})
  set(target_name "index")

  # link the executable
  target_link_libraries(
    ${outputFileName}
    PRIVATE implot
            plf_colony
            app_header
            utils
            ui_assets
            sample_dashboards
            fonts
            core
            client
            services
            imgui-node-editor
            yaml-cpp::yaml-cpp)

  add_custom_target(
    serve
    python3
    -m
    http.server
    -d
    ${EXECUTABLE_OUTPUT_PATH}
    8000
    COMMENT start
    a
    python
    server
    serving
    the
    webassembly
    app
    DEPENDS ${outputFileName})

  # TODO should be pulled via linking the targets, but they're outside the source folder (src/ui) for the ui-wasm build
  target_include_directories(${target_name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../acquisition) # for daq_api.hpp
  target_include_directories(${target_name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../utils/include)

else() # native build

  set(target_name "opendigitizer-ui")
  add_executable(${target_name} utils/TouchHandler.hpp)

  target_sources(${target_name} PRIVATE ${sources})

  # link the executable
  target_link_libraries(
    ${target_name}
    PRIVATE od_acquisition
            SDL2
            implot
            imgui-node-editor
            plf_colony
            yaml-cpp::yaml-cpp
            core
            client
            services
            app_header
            utils
            ui_assets
            sample_dashboards
            digitizer_settings
            fonts)
  target_compile_definitions(${target_name} PRIVATE BLOCKS_DIR="${GNURADIO_PREFIX}/share/gnuradio/grc/blocks/")

  if(OPENDIGITIZER_ENABLE_ASAN)
    target_compile_options(${target_name} PRIVATE -fsanitize=address)
    target_link_options(${target_name} PRIVATE -fsanitize=address)
  endif()
endif()

target_link_libraries(
  ${target_name}
  PRIVATE gnuradio-core
          gnuradio-meta
          gnuradio-algorithm
          gr-basic
          gr-fourier
          gr-testing
          fftw
          vir
          pmtv)
target_include_directories(${target_name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../service/acquisition) # for daq_api.hpp
target_compile_options(${target_name} PRIVATE "-Wfatal-errors")

if(OPENDIGITIZER_ENABLE_TESTING)
  add_subdirectory(test)
endif()
