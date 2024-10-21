cmake_minimum_required(VERSION 3.17)

# set a variable in CMake
set(projectName "opendigitizer-ui")

project(${projectName} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_VERBOSE_MAKEFILE OFF)

# Mainly for FMT
set(CMAKE_POSITION_INDEPENDENT_CODE TRUE)

if(EMSCRIPTEN)
  # locate the "executable" output in the build/web directory
  set(EXECUTABLE_OUTPUT_PATH ${CMAKE_BINARY_DIR}/web)
  # necessary to tell CMake (with emscripten) to extend the standard .js and .wasm output with .html output (so that we
  # can execute it with the webserver)
  set(CMAKE_EXECUTABLE_SUFFIX ".html")
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

if(NOT TARGET od_acquisition)
  add_subdirectory(../acquisition acquisition)
endif()
if(NOT TARGET raii_wrapper)
  add_subdirectory(../utils utils)
endif()

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
  message(STATUS "Detected emscripten webassembly build")
  set(target_name "index")
  add_executable(${target_name})

  add_custom_target(
    serve
    "python3 -m http.server -d ${EXECUTABLE_OUTPUT_PATH} 8000"
    COMMENT "start a python server serving the webassembly app"
    DEPENDS ${target_name})
else() # native build
  set(target_name "opendigitizer-ui")
  add_executable(${target_name})
  target_link_libraries(${target_name} PRIVATE SDL2) # for emscripten SDL is already included in the build

  if(OPENDIGITIZER_ENABLE_ASAN)
    target_compile_options(${target_name} PRIVATE -fsanitize=address)
    target_link_options(${target_name} PRIVATE -fsanitize=address)
  endif()
endif()

target_sources(
  ${target_name}
  PRIVATE main.cpp
          Flowgraph.cpp
          Flowgraph.hpp
          FlowgraphItem.cpp
          FlowgraphItem.hpp
          Dashboard.cpp
          Dashboard.hpp
          DashboardPage.cpp
          DashboardPage.hpp
          OpenDashboardPage.cpp
          OpenDashboardPage.hpp
          RemoteSignalSources.cpp
          RemoteSignalSources.hpp
          components/AppHeader.hpp
          components/Block.cpp
          components/Block.hpp
          components/Dialog.hpp
          components/FilterComboBoxes.hpp
          components/Keypad.hpp
          components/ListBox.hpp
          components/ImGuiNotify.hpp
          components/PopupMenu.hpp
          components/SelectedLabelsView.hpp
          components/SignalSelector.hpp
          components/Splitter.cpp
          components/Splitter.hpp
          components/Toolbar.hpp
          utils/stb_impl.cpp # STB is a library with linking issues -- impl instantiated only here
          common/AppDefinitions.hpp
          common/Events.hpp
          common/ImguiWrap.hpp
          common/LookAndFeel.hpp
          common/TouchHandler.hpp)

target_link_libraries(
  ${target_name}
  PRIVATE implot
          imgui-node-editor
          od_acquisition
          services
          plf_colony
          ui_assets
          yaml-cpp::yaml-cpp
          core
          client
          stb
          sample_dashboards
          fonts
          digitizer_settings
          gnuradio-core
          gnuradio-meta
          gnuradio-algorithm
          gr-basic
          gr-fourier
          raii_wrapper
          gr-testing
          fftw
          vir
          pmtv)
target_compile_options(${target_name} PRIVATE "-Wfatal-errors")

if(OPENDIGITIZER_ENABLE_TESTING)
  add_subdirectory(test)
endif()
