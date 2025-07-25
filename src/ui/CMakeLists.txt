cmake_minimum_required(VERSION 3.17)

# set a variable in CMake
set(projectName "opendigitizer-ui")

project(${projectName} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_VERBOSE_MAKEFILE OFF)

option(OD_DISABLE_DEMO_FLOWGRAPHS "Disable adding the demo flowgraphs to UI" OFF)
set(GR_MAX_WASM_THREAD_COUNT
    60
    CACHE STRING "Max number of threads for WASM pthread pool")

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
    "SHELL:-s USE_SDL=3"
    -pthread)
  add_link_options(
    "SHELL:-s WASM=1"
    "SHELL:-sFULL_ES2=1"
    "SHELL:-sMAX_WEBGL_VERSION=2"
    "SHELL:-sMIN_WEBGL_VERSION=2"
    "SHELL:-s ALLOW_MEMORY_GROWTH=1"
    "SHELL:-s NO_EXIT_RUNTIME=0"
    "SHELL:-s ASSERTIONS=1"
    "SHELL:-s INITIAL_MEMORY=300MB"
    "SHELL:-s STACK_SIZE=5MB"
    --shell-file
    ${CMAKE_CURRENT_SOURCE_DIR}/shell_minimal.html
    "SHELL:-s PTHREAD_POOL_SIZE=${GR_MAX_WASM_THREAD_COUNT}"
    "SHELL:-s USE_SDL=3"
    -fwasm-exceptions
    -pthread
    "-sFETCH=1" # needed for file_io
    "--bind" # needed for Clipboard
  )
endif()

# dependencies: needs to be after adding the emscripten option so they are applied to dependency targets as well
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/../../cmake")
include(../../cmake/CMakeRC.cmake)
include(cmake/Dependencies.cmake)

add_compile_definitions(GR_MAX_WASM_THREAD_COUNT=${GR_MAX_WASM_THREAD_COUNT}) # propagate to C++

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
  assets/sampleDashboards/dashboard.grc
  assets/sampleDashboards/ExtendedDemoDashboard.grc
  assets/sampleDashboards/DemoDashboard.grc)

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
  target_link_libraries(${target_name} PRIVATE SDL3::SDL3) # for emscripten SDL is already included in the build

  if(OPENDIGITIZER_ENABLE_ASAN)
    target_compile_options(${target_name} PRIVATE -fsanitize=address)
    target_link_options(${target_name} PRIVATE -fsanitize=address)
  endif()
endif()

# All ui/ translation-units (except main.cpp) are built by ${target_name}lib static lib just so they can be shared by
# the tests and not build N times
add_library(
  ${target_name}lib STATIC
  App.hpp
  FlowgraphPage.cpp
  FlowgraphPage.hpp
  GraphModel.cpp
  GraphModel.hpp
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
  components/BlockNeighboursPreview.cpp
  components/BlockNeighboursPreview.hpp
  components/Dialog.hpp
  components/Docking.cpp
  components/Docking.hpp
  components/FilterComboBoxes.hpp
  components/Keypad.hpp
  components/ListBox.hpp
  components/ImGuiNotify.hpp
  components/PopupMenu.hpp
  components/SelectedLabelsView.hpp
  components/SignalSelector.hpp
  components/SignalSelector.cpp
  components/NewBlockSelector.hpp
  components/NewBlockSelector.cpp
  components/Splitter.cpp
  components/Splitter.hpp
  components/Toolbar.hpp
  utils/stb_impl.cpp # STB is a library with linking issues -- impl instantiated only here
  common/AppDefinitions.hpp
  common/Events.hpp
  common/ImguiWrap.hpp
  common/LookAndFeel.hpp
  common/LookAndFeel.cpp
  common/TouchHandler.hpp)

target_sources(${target_name} PRIVATE main.cpp)

target_link_libraries(
  ${target_name}lib
  PUBLIC implot
         imgui-node-editor
         od_acquisition
         services
         opendigitizer_version
         plf_colony
         ui_assets
         core
         client
         stb
         fonts
         digitizer_settings
         digitizer_common_utils
         raii_wrapper
         fftw
         vir
         pmtv
         gnuradio-core
         gnuradio-blocklib-core
         GrBasicBlocksShared
         GrElectricalBlocksShared
         GrFileIoBlocksShared
         GrFilterBlocksShared
         GrFourierBlocksShared
         GrHttpBlocksShared
         GrMathBlocksShared
         GrTestingBlocksShared)
if(OD_DISABLE_DEMO_FLOWGRAPHS)
  target_compile_definitions(${target_name}lib PUBLIC OD_DISABLE_DEMO_FLOWGRAPHS)
else()
  target_link_libraries(${target_name}lib PUBLIC sample_dashboards)
endif()
target_compile_options(${target_name}lib PUBLIC "-Wfatal-errors")
target_compile_definitions(${target_name}lib PUBLIC IMGUI_DEFINE_MATH_OPERATORS)
target_link_libraries(${target_name} PRIVATE ${target_name}lib)

if(OPENDIGITIZER_ENABLE_TESTING)
  add_subdirectory(test)
endif()
