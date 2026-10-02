cmake_minimum_required(VERSION 3.17)

# set a variable in CMake
set(projectName "opendigitizer-ui")

project(${projectName} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_VERBOSE_MAKEFILE OFF)

option(OD_DISABLE_DEMO_FLOWGRAPHS "Disable adding the demo flowgraphs to UI" OFF)
option(OPENDIGITIZER_BUILD_UI_APP "Build the OpenDigitizer UI application by default" ON)
option(OD_WASM_STATIC_BLOCKLIBS "WASM: statically link Gr*Blocks into the main module (no SIDE_MODULE dlopen)" ON)
set(GR_MAX_WASM_THREAD_COUNT
    16
    CACHE STRING "Max number of threads for WASM pthread pool")

if(EMSCRIPTEN)
  message(STATUS "Emscripten version: ${EMSCRIPTEN_VERSION}")
  # MAIN_MODULE=1 (gnuradio-core-dynload) requires PIC; the standalone src/ui ExternalProject does not inherit the root
  # CMAKE_POSITION_INDEPENDENT_CODE.
  set(CMAKE_POSITION_INDEPENDENT_CODE TRUE)
  # locate the "executable" output in the build/web directory
  set(EXECUTABLE_OUTPUT_PATH ${CMAKE_BINARY_DIR}/web)
  # necessary to tell CMake (with emscripten) to extend the standard .js and .wasm output with .html output (so that we
  # can execute it with the webserver)
  set(CMAKE_EXECUTABLE_SUFFIX ".html")

  if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(_od_emscripten_optimisation_flags -g -Os)
    set(_od_emscripten_assertions_flag "SHELL:-s ASSERTIONS=1")
    set(_od_emscripten_debug_info_flags -g)
  else()
    set(_od_emscripten_optimisation_flags -O3)
    set(_od_emscripten_assertions_flag "SHELL:-s ASSERTIONS=0")
    set(_od_emscripten_debug_info_flags -g0)
  endif()

  # CAUTION: The "SHELL:" before some compile/link options has to be used to explicitly tell CMake to not sum up options
  # (e.g. -option A -option B => -option A B)(CMake default behaviour), but instead keep them separated (needed for
  # proper operation of emscripten)
  add_compile_options(
    -Wall
    -Wformat
    ${_od_emscripten_optimisation_flags}
    ${_od_emscripten_debug_info_flags}
    -fexceptions
    "SHELL:-s USE_SDL=3"
    -pthread)

  # Main-module link flags. Dynload host contract (MAIN_MODULE, --export-all, WHOLE_ARCHIVE core,
  # gnuradio_dynlink_post.js) comes from gnuradio4::gnuradio-core-dynload.
  set(_od_emscripten_main_link_options
      "SHELL:-s WASM=1"
      "SHELL:-sFULL_ES3=1"
      "SHELL:-sMAX_WEBGL_VERSION=2"
      "SHELL:-sMIN_WEBGL_VERSION=2"
      "SHELL:-s ALLOW_MEMORY_GROWTH=1"
      "SHELL:-s NO_EXIT_RUNTIME=0"
      ${_od_emscripten_assertions_flag}
      "SHELL:-s INITIAL_MEMORY=300MB"
      "SHELL:-s STACK_SIZE=5MB"
      "SHELL:-s MINIFY_HTML=0"
      --shell-file
      ${CMAKE_CURRENT_SOURCE_DIR}/shell_minimal.html
      "SHELL:-s PTHREAD_POOL_SIZE=${GR_MAX_WASM_THREAD_COUNT}"
      "SHELL:-s USE_SDL=3"
      ${_od_emscripten_debug_info_flags}
      -fexceptions
      -pthread
      "-sFETCH=1" # needed for file_io
      "--bind" # needed for Clipboard
  )
endif()

# dependencies: needs to be after adding the emscripten option so they are applied to dependency targets as well
include(../../cmake/CMakeRC.cmake)
include(cmake/Dependencies.cmake)

if(NOT TARGET opendigitizer-options)
  option(OPENDIGITIZER_WARNINGS_AS_ERRORS "Treat OpenDigitizer compiler warnings as errors" ON)
  add_library(opendigitizer-options INTERFACE)
  include(../../cmake/CompilerWarnings.cmake)
  set_project_warnings(opendigitizer-options)
endif()

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
  assets/sampleDashboards/PropertyControlDashboard.grc
  assets/sampleDashboards/DemoDashboard.grc
  assets/sampleDashboards/PulsedPowerDemo.grc)

if(EMSCRIPTEN)
  message(STATUS "Detected emscripten webassembly build")
  set(target_name "index")
  add_executable(${target_name})

  file(
    WRITE ${CMAKE_CURRENT_BINARY_DIR}/serve_wasm.py
    "#!/usr/bin/env python3\n"
    "import http.server, os, ssl, subprocess, threading, time, webbrowser\n"
    "\n"
    "def log(message):\n"
    "    print(message, flush=True)\n"
    "\n"
    "WEB_DIR = '${EXECUTABLE_OUTPUT_PATH}'\n"
    "CERT_FILE = os.path.join(WEB_DIR, 'localhost.pem')\n"
    "HTTP_PORT = 8000\n"
    "HTTPS_PORT = 8443\n"
    "\n"
    "os.chdir(WEB_DIR)\n"
    "\n"
    "# stop any previous servers on our ports\n"
    "for port in (HTTP_PORT, HTTPS_PORT):\n"
    "    try:\n"
    "        subprocess.run(['fuser', '-k', f'{port}/tcp'], capture_output=True, timeout=5)\n"
    "    except Exception:\n"
    "        pass\n"
    "time.sleep(0.5)\n"
    "\n"
    "# generate a self-signed certificate if missing\n"
    "if not os.path.exists(CERT_FILE):\n"
    "    log('Generating self-signed certificate...')\n"
    "    subprocess.run([\n"
    "        'openssl', 'req', '-x509', '-newkey', 'rsa:2048',\n"
    "        '-keyout', CERT_FILE, '-out', CERT_FILE,\n"
    "        '-days', '365', '-nodes',\n"
    "        '-subj', '/CN=localhost',\n"
    "    ], check=True)\n"
    "\n"
    "class Handler(http.server.SimpleHTTPRequestHandler):\n"
    "    def end_headers(self):\n"
    "        # COOP/COEP for SharedArrayBuffer (pthreads); CORP so COEP allow same-origin wasm/js.\n"
    "        self.send_header('Cross-Origin-Opener-Policy', 'same-origin')\n"
    "        self.send_header('Cross-Origin-Embedder-Policy', 'require-corp')\n"
    "        self.send_header('Cross-Origin-Resource-Policy', 'same-origin')\n"
    "        super().end_headers()\n"
    "    def log_message(self, fmt, *args):\n"
    "        pass  # silence per-request logs\n"
    "\n"
    "# ThreadingHTTPServer: large plugin .wasm transfers (e.g. 24MB BasicBlocks) must not\n"
    "# block serving index.js/wasm on a single-threaded server or the page hangs on spinner.\n"
    "class ReuseServer(http.server.ThreadingHTTPServer):\n"
    "    allow_reuse_address = True\n"
    "    daemon_threads = True\n"
    "\n"
    "# HTTP server (fallback)\n"
    "http_server = ReuseServer(('', HTTP_PORT), Handler)\n"
    "threading.Thread(target=http_server.serve_forever, daemon=True).start()\n"
    "log(f'  HTTP  -> http://localhost:{HTTP_PORT}')\n"
    "\n"
    "# HTTPS server (preferred — browsers need one-time certificate override)\n"
    "https_server = ReuseServer(('', HTTPS_PORT), Handler)\n"
    "ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)\n"
    "ctx.load_cert_chain(CERT_FILE)\n"
    "https_server.socket = ctx.wrap_socket(https_server.socket, server_side=True)\n"
    "log(f'  HTTPS -> https://localhost:{HTTPS_PORT}')\n"
    "\n"
    "def open_browser():\n"
    "    try:\n"
    "        if not webbrowser.open(f'https://localhost:{HTTPS_PORT}'): \n"
    "            log('Browser auto-open was not handled; open the HTTPS URL manually.')\n"
    "    except Exception as exc:\n"
    "        log(f'Browser auto-open failed: {exc}')\n"
    "\n"
    "threading.Timer(0.5, open_browser).start()\n"
    "https_server.serve_forever()\n")

  file(
    CHMOD
    ${CMAKE_CURRENT_BINARY_DIR}/serve_wasm.py
    PERMISSIONS
    OWNER_READ
    OWNER_WRITE
    OWNER_EXECUTE
    GROUP_READ
    GROUP_EXECUTE
    WORLD_READ
    WORLD_EXECUTE)

  # CLI usage: cmake --build --preset emscripten-release --target serve IDE usage: use the "opendigitizer-ui (WASM)" run
  # configuration (auto-loaded from .run/)
  add_custom_target(
    serve
    COMMAND python3 -u ${CMAKE_CURRENT_BINARY_DIR}/serve_wasm.py
    COMMENT "serve WASM app: https://localhost:8443 (preferred) + http://localhost:8000 (fallback)"
    DEPENDS ${target_name}
    USES_TERMINAL)
else() # native build
  set(target_name "opendigitizer-ui")
  add_executable(${target_name})
  target_link_libraries(${target_name} PRIVATE SDL3::SDL3) # for emscripten SDL is already included in the build

  if(OPENDIGITIZER_ENABLE_ASAN)
    target_compile_options(${target_name} PRIVATE -fsanitize=address)
    target_link_options(${target_name} PRIVATE -fsanitize=address)
  endif()
endif()

# each layer links only the one below; consumers pick the lowest layer they need via the opendigitizer:: aliases
add_library(
  opendigitizer-ui-common STATIC
  common/AppDefinitions.hpp
  common/Events.hpp
  common/FramePacer.hpp
  common/ImguiWrap.cpp
  common/ImguiWrap.hpp
  common/LookAndFeel.hpp
  common/TouchHandler.hpp
  utils/EmscriptenHelper.hpp
  utils/TransparentStringHash.hpp)
target_include_directories(opendigitizer-ui-common PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
target_link_libraries(
  opendigitizer-ui-common
  PRIVATE opendigitizer-options
  PUBLIC implot raii_wrapper digitizer_common_utils)

add_library(opendigitizer-look-and-feel STATIC common/LookAndFeel.cpp)
target_link_libraries(
  opendigitizer-look-and-feel
  PRIVATE opendigitizer-options
  PUBLIC opendigitizer-ui-common fonts ui_assets)

add_library(
  opendigitizer-ui-components STATIC
  components/DateInput.hpp
  components/Dialog.hpp
  components/EditableStringList.hpp
  components/FilterComboBoxes.hpp
  components/FilterTagBar.hpp
  components/FilterTagBar.cpp
  components/ImGuiNotify.hpp
  components/InputTextCompletion.hpp
  components/Keypad.hpp
  components/ListBox.hpp
  components/PopupMenu.hpp
  components/SearchAndFilterComponents.cpp
  components/SearchAndFilterComponents.hpp
  components/SelectedLabelsView.hpp
  components/SortFilterModel.hpp
  components/SortFilterTreeModel.hpp
  components/Splitter.cpp
  components/Splitter.hpp
  components/VirtualScrollTable.hpp
  components/YesNoPopup.hpp)
target_link_libraries(
  opendigitizer-ui-components
  PRIVATE opendigitizer-options
  PUBLIC opendigitizer-look-and-feel imguidatepicker)

add_library(opendigitizer-charts INTERFACE)
target_sources(
  opendigitizer-charts
  INTERFACE FILE_SET
            HEADERS
            FILES
            blocks/ImPlotSink.hpp
            charts/Chart.hpp
            charts/Charts.hpp
            charts/SignalSink.hpp
            charts/SinkRegistry.hpp
            charts/SpectrumDensity.hpp
            charts/SpectrumHelper.hpp
            charts/SpectrumPlot.hpp
            charts/SpectrumView.hpp
            charts/SurfacePlot.hpp
            charts/WaterfallPlot.hpp
            charts/XYChart.hpp
            charts/YYChart.hpp
            components/ColourManager.hpp
            utils/ShaderHelper.hpp)
target_link_libraries(opendigitizer-charts INTERFACE opendigitizer-ui-components implot3d gnuradio4::vir)
target_compile_definitions(opendigitizer-charts INTERFACE GR_MAX_WASM_THREAD_COUNT=${GR_MAX_WASM_THREAD_COUNT})
if(NOT EMSCRIPTEN)
  target_compile_definitions(opendigitizer-charts INTERFACE GL_GLEXT_PROTOTYPES)
endif()

if(EMSCRIPTEN AND NOT OD_WASM_STATIC_BLOCKLIBS)
  # Imported targets re-add gnuradio-core as DEFAULT (blocklib-core) or LINK_ONLY (dynload-impl). CMake 3.31 rejects
  # that next to dynload's WHOLE_ARCHIVE. FetchContent exposes gnuradio4::* as ALIAS — mutate the real target.
  foreach(_od_gr4_tgt IN ITEMS gnuradio4::gnuradio-blocklib-core gnuradio4::gnuradio-core-dynload-impl)
    if(TARGET ${_od_gr4_tgt})
      get_target_property(_od_gr4_real ${_od_gr4_tgt} ALIASED_TARGET)
      if(NOT _od_gr4_real)
        set(_od_gr4_real ${_od_gr4_tgt})
      endif()
      set_property(TARGET ${_od_gr4_real} PROPERTY INTERFACE_LINK_LIBRARIES "")
    endif()
  endforeach()
  target_link_libraries(opendigitizer-charts INTERFACE gnuradio4::gnuradio-blocklib-core
                                                       gnuradio4::gnuradio-core-dynload)
  # Headers only (FetchContent). Must be COMPILE_ONLY — gr-basic/gr-filter INTERFACE-link gnuradio-core as DEFAULT,
  # which CMake 3.31 rejects next to dynload's WHOLE_ARCHIVE.
  target_link_libraries(opendigitizer-charts INTERFACE "$<COMPILE_ONLY:gnuradio4::gr-basic>"
                                                       "$<COMPILE_ONLY:gnuradio4::gr-filter>")
elseif(EMSCRIPTEN AND OD_WASM_STATIC_BLOCKLIBS)
  # Old behaviour: bake blocklibs into the main module (no MAIN_MODULE / dlopen).
  target_link_libraries(opendigitizer-charts INTERFACE gnuradio4::gnuradio-blocklib-core gnuradio4::gnuradio-core)
  target_compile_definitions(opendigitizer-charts INTERFACE OD_WASM_STATIC_BLOCKLIBS=1)
  target_compile_definitions(${target_name} PUBLIC OD_WASM_STATIC_BLOCKLIBS=1)
else()
  target_link_libraries(opendigitizer-charts INTERFACE gnuradio4::gnuradio-blocklib-core
                                                       gnuradio4::gnuradio-core-dynload)
endif()

# Native always links blocklibs. WASM dynload loads SIDE_MODULE plugins at runtime; WASM static links them into the main
# module like native.
if(NOT EMSCRIPTEN OR OD_WASM_STATIC_BLOCKLIBS)
  target_link_libraries(
    opendigitizer-charts
    INTERFACE gnuradio4::GrBasicBlocksShared
              gnuradio4::GrElectricalBlocksShared
              gnuradio4::GrFileIoBlocksShared
              gnuradio4::GrFilterBlocksShared
              gnuradio4::GrFourierBlocksShared
              gnuradio4::GrHttpBlocksShared
              gnuradio4::GrMathBlocksShared
              gnuradio4::GrTestingBlocksShared)
endif()

add_library(
  opendigitizer-dashboard STATIC
  Dashboard.cpp
  Dashboard.hpp
  DashboardPage.cpp
  DashboardPage.hpp
  DashboardsCollection.hpp
  GraphModel.cpp
  GraphModel.hpp
  MapUtils.hpp
  RemoteSignalSources.cpp
  RemoteSignalSources.hpp
  Scheduler.hpp
  Setup.cpp
  Setup.hpp
  blocks/Arithmetic.hpp
  blocks/RemoteSource.hpp
  blocks/SineSource.hpp
  blocks/StridedWindow.hpp
  blocks/TagToSample.hpp
  blocks/TestSpectrumGenerator.hpp
  blocks/meta.hpp
  components/Block.cpp
  components/Block.hpp
  components/BlockNeighboursPreview.cpp
  components/BlockNeighboursPreview.hpp
  components/DashboardPreview.cpp
  components/DashboardPreview.hpp
  components/DataTypeStyle.cpp
  components/DataTypeStyle.hpp
  components/Docking.cpp
  components/Docking.hpp
  components/ExportedPropertiesList.cpp
  components/ExportedPropertiesList.hpp
  components/GlobalSignalLegend.hpp
  components/SignalSelector.cpp
  components/SignalSelector.hpp
  utils/ImGuiDockSpaceState.cpp
  utils/ImGuiDockSpaceState.hpp
  utils/emscripten_compat.hpp)
target_link_libraries(
  opendigitizer-dashboard
  PRIVATE opendigitizer-options
  PUBLIC opendigitizer-charts
         od_acquisition
         services
         core
         client
         plf_colony
         digitizer_settings)
if(OD_DISABLE_DEMO_FLOWGRAPHS)
  target_compile_definitions(opendigitizer-dashboard PUBLIC OD_DISABLE_DEMO_FLOWGRAPHS)
else()
  target_link_libraries(opendigitizer-dashboard PUBLIC sample_dashboards)
endif()

add_library(
  opendigitizer-flowgraph-editor STATIC
  FlowgraphLayout.cpp
  FlowgraphLayout.hpp
  FlowgraphPage.cpp
  FlowgraphPage.hpp
  common/ImguiNodeEditorWrap.hpp
  components/NewBlockSelector.cpp
  components/NewBlockSelector.hpp
  components/NewBlockSelectorFuzzySearch.cpp
  components/NewBlockSelectorFuzzySearch.hpp)
target_link_libraries(
  opendigitizer-flowgraph-editor
  PRIVATE opendigitizer-options
  PUBLIC opendigitizer-dashboard imgui-node-editor)

add_library(
  ${target_name}lib STATIC
  App.hpp
  OAuthSession.cpp
  OAuthSession.hpp
  OpenDashboardPage.cpp
  OpenDashboardPage.hpp
  blocks/ToolbarBlock.hpp
  common/ImGuiHelperSDL.hpp
  common/ImguiXKCD.hpp
  components/AppHeader.hpp
  components/Toolbar.hpp
  utils/stb_impl.cpp) # STB is a library with linking issues -- impl instantiated only here
target_link_libraries(
  ${target_name}lib
  PRIVATE opendigitizer-options
  PUBLIC opendigitizer-flowgraph-editor opendigitizer_version stb)
if(NOT EMSCRIPTEN)
  target_link_libraries(${target_name}lib PUBLIC fair-picoscope) # PicoScope blocks (gr-digitizers), native-only
endif()

foreach(
  _odLayer IN
  ITEMS ui-common
        look-and-feel
        ui-components
        charts
        dashboard
        flowgraph-editor)
  add_library(opendigitizer::${_odLayer} ALIAS opendigitizer-${_odLayer})
endforeach()
add_library(opendigitizer::ui-app ALIAS ${target_name}lib)

target_sources(${target_name} PRIVATE main.cpp)
if(NOT OPENDIGITIZER_BUILD_UI_APP)
  set_target_properties(${target_name} PROPERTIES EXCLUDE_FROM_ALL ON)
endif()
target_link_libraries(${target_name} PRIVATE ${target_name}lib opendigitizer-options)

if(EMSCRIPTEN)
  target_link_options(${target_name} PRIVATE ${_od_emscripten_main_link_options})

  if(OD_WASM_STATIC_BLOCKLIBS)
    message(STATUS "OD_WASM_STATIC_BLOCKLIBS: linking Gr*Blocks into the main module (no SIDE_MODULE plugins)")
  else()
    # Host contract (MAIN_MODULE / --export-all / WHOLE_ARCHIVE core / dynlink post-js) is on
    # gnuradio4::gnuradio-core-dynload. Copy SIDE_MODULE artefacts next to index.wasm.
    if(NOT GNURADIO4_WASM_PLUGIN_FILES)
      message(
        FATAL_ERROR
          "GNURADIO4_WASM_PLUGIN_FILES is empty. Install GR4 with ci-latest-wasm "
          "(/opt/gnuradio4-emscripten), build GR4 from source with INTERNAL_ENABLE_BLOCK_PLUGINS=ON, "
          "or use the wasm-static-blocklibs preset with ci-latest-wasm-static "
          "(/opt/gnuradio4-emscripten-static).")
    endif()

    set(_od_plugin_wasm_files ${GNURADIO4_WASM_PLUGIN_FILES})
    # Audio needs Web Audio / device APIs the UI does not use yet.
    list(
      FILTER
      _od_plugin_wasm_files
      EXCLUDE
      REGEX
      "Audio")

    # Prefer smallest first so early SIDE_MODULE instantiate returns to the event loop sooner. Installed packages expose
    # IMPORTED_LOCATION; FetchContent builds use $<TARGET_FILE> at build time (configure-time size may be 0 until the
    # plugin is linked).
    set(_od_plugin_sized "")
    foreach(_wasm IN LISTS _od_plugin_wasm_files)
      string(
        REGEX
        REPLACE "^lib(.+)\\.wasm$"
                "\\1"
                _plugin_target_leaf
                "${_wasm}")
      if(NOT TARGET gnuradio4::${_plugin_target_leaf})
        message(FATAL_ERROR "Missing gnuradio4::${_plugin_target_leaf} for ${_wasm}")
      endif()
      set(_plugin_bytes 0)
      get_target_property(_plugin_loc gnuradio4::${_plugin_target_leaf} IMPORTED_LOCATION_RELEASE)
      if(NOT _plugin_loc)
        get_target_property(_plugin_loc gnuradio4::${_plugin_target_leaf} IMPORTED_LOCATION)
      endif()
      if(_plugin_loc AND EXISTS "${_plugin_loc}")
        file(SIZE "${_plugin_loc}" _plugin_bytes)
      endif()
      list(APPEND _od_plugin_sized "${_plugin_bytes}|${_wasm}|${_plugin_target_leaf}")
    endforeach()
    list(SORT _od_plugin_sized COMPARE NATURAL)

    set(_od_plugin_names_code "")
    set(_od_plugin_targets "")
    set(_od_plugin_name_list "")
    foreach(_entry IN LISTS _od_plugin_sized)
      string(
        REPLACE "|"
                ";"
                _parts
                "${_entry}")
      list(
        GET
        _parts
        1
        _wasm)
      list(
        GET
        _parts
        2
        _plugin_target_leaf)
      string(APPEND _od_plugin_names_code "    \"${_wasm}\",\n")
      list(APPEND _od_plugin_name_list "${_wasm}")

      set(_plugin_target "od_plugin_${_plugin_target_leaf}")
      add_custom_target(
        ${_plugin_target}
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:gnuradio4::${_plugin_target_leaf}>"
                "${EXECUTABLE_OUTPUT_PATH}/$<TARGET_FILE_NAME:gnuradio4::${_plugin_target_leaf}>"
        DEPENDS gnuradio4::${_plugin_target_leaf}
        COMMENT "Copy ${_wasm} next to index.wasm")
      list(APPEND _od_plugin_targets ${_plugin_target})
    endforeach()

    list(LENGTH _od_plugin_name_list _od_plugin_count)
    set(_od_plugin_wasm_header "${CMAKE_CURRENT_BINARY_DIR}/include/PluginWasmNames.hpp")
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/include")
    file(
      WRITE "${_od_plugin_wasm_header}"
      "#pragma once
#include <array>
#include <string_view>

namespace Digitizer {
// Generated from GNURADIO4_WASM_PLUGIN_FILES (Audio excluded), sorted smallest-first.
inline constexpr std::array<std::string_view, ${_od_plugin_count}> kPluginWasmNames{
${_od_plugin_names_code}};
} // namespace Digitizer
")
    target_include_directories(opendigitizer-dashboard PUBLIC "${CMAKE_CURRENT_BINARY_DIR}/include")
    message(STATUS "Using gnuradio4 SIDE_MODULE plugins: ${_od_plugin_name_list}")
    add_dependencies(${target_name} ${_od_plugin_targets})
  endif()
endif()

if(OPENDIGITIZER_ENABLE_TESTING)
  add_subdirectory(test)
endif()
