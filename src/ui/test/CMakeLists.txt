add_subdirectory(testdata)

# define unit-tests

add_executable(qa_FlowgraphLayout qa_FlowgraphLayout.cpp ../FlowgraphLayout.cpp)
target_link_libraries(qa_FlowgraphLayout PRIVATE ut opendigitizer-options)
target_include_directories(qa_FlowgraphLayout PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
add_test(NAME qa_FlowgraphLayout COMMAND qa_FlowgraphLayout)

# Headless chart abstraction tests (links opendigitizer-uilib for shared TestSinks.hpp)
add_executable(qa_SignalSink qa_SignalSink.cpp)
target_link_libraries(qa_SignalSink PRIVATE ut opendigitizer-uilib opendigitizer-options)
target_include_directories(qa_SignalSink PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
add_test(NAME qa_SignalSink COMMAND qa_SignalSink)

# Chart abstraction tests for SignalSink/SinkRegistry/Chart mixin APIs Note: links opendigitizer-uilib because Chart.hpp
# menu helpers reference LookAndFeel
add_executable(qa_ChartAbstraction qa_ChartAbstraction.cpp)
target_link_libraries(qa_ChartAbstraction PRIVATE ut opendigitizer-uilib opendigitizer-options)
target_include_directories(qa_ChartAbstraction PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
add_test(NAME qa_ChartAbstraction COMMAND qa_ChartAbstraction)

add_executable(qa_SortFilterModels qa_SortFilterModels.cpp)
target_link_libraries(qa_SortFilterModels PRIVATE ut opendigitizer-uilib opendigitizer-options)
target_include_directories(qa_SortFilterModels PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
add_test(NAME qa_SortFilterModels COMMAND qa_SortFilterModels)

add_executable(qa_KeypadParser qa_KeypadParser.cpp)
target_link_libraries(qa_KeypadParser PRIVATE ut opendigitizer::ui-components opendigitizer-options)
target_include_directories(qa_KeypadParser PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
add_test(NAME qa_KeypadParser COMMAND qa_KeypadParser)

add_executable(qa_FuzzySearch qa_FuzzySearch.cpp)
target_link_libraries(qa_FuzzySearch PRIVATE ut opendigitizer-uilib opendigitizer-options)
target_include_directories(qa_FuzzySearch PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
add_test(NAME qa_FuzzySearch COMMAND qa_FuzzySearch)

add_executable(qa_TouchHandler qa_TouchHandler.cpp)
target_link_libraries(qa_TouchHandler PRIVATE ut opendigitizer-uilib opendigitizer-options)
target_include_directories(qa_TouchHandler PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
add_test(NAME qa_TouchHandler COMMAND qa_TouchHandler)

add_executable(qa_ImguiXKCD qa_ImguiXKCD.cpp)
target_link_libraries(qa_ImguiXKCD PRIVATE ut opendigitizer-uilib opendigitizer-options)
target_include_directories(qa_ImguiXKCD PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
add_test(NAME qa_ImguiXKCD COMMAND qa_ImguiXKCD)

add_executable(qa_DataTypeStyle qa_DataTypeStyle.cpp)
target_link_libraries(qa_DataTypeStyle PRIVATE ut opendigitizer::dashboard opendigitizer-options)
add_test(NAME qa_DataTypeStyle COMMAND qa_DataTypeStyle)

add_executable(qa_TargetMap qa_TargetMap.cpp)
target_link_libraries(qa_TargetMap PRIVATE ut opendigitizer::dashboard opendigitizer-options)
add_test(NAME qa_TargetMap COMMAND qa_TargetMap)

add_executable(qa_GraphSession qa_GraphSession.cpp)
target_link_libraries(qa_GraphSession PRIVATE ut opendigitizer::dashboard opendigitizer-options)
add_test(NAME qa_GraphSession COMMAND qa_GraphSession)

add_executable(qa_LogHistory qa_LogHistory.cpp)
target_link_libraries(qa_LogHistory PRIVATE ut opendigitizer::dashboard opendigitizer-options)
add_test(NAME qa_LogHistory COMMAND qa_LogHistory)

add_executable(qa_DashboardPreview qa_DashboardPreview.cpp)
target_link_libraries(qa_DashboardPreview PRIVATE ut opendigitizer-uilib opendigitizer-options)
target_include_directories(qa_DashboardPreview PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
add_test(NAME qa_DashboardPreview COMMAND qa_DashboardPreview)

add_executable(qa_TestSpectrumGenerator qa_TestSpectrumGenerator.cpp)
target_link_libraries(
  qa_TestSpectrumGenerator
  PRIVATE ut
          gnuradio4::gnuradio-core
          gnuradio4::gnuradio-blocklib-core
          opendigitizer-options)
target_include_directories(qa_TestSpectrumGenerator PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..
                                                            ${CMAKE_CURRENT_SOURCE_DIR}/../../utils/include)
add_test(NAME qa_TestSpectrumGenerator COMMAND qa_TestSpectrumGenerator)

add_executable(qa_AudioSynthesis qa_AudioSynthesis.cpp)
target_link_libraries(
  qa_AudioSynthesis
  PRIVATE ut
          gnuradio4::gnuradio-core
          gnuradio4::gnuradio-blocklib-core
          opendigitizer-options)
add_test(NAME qa_AudioSynthesis COMMAND qa_AudioSynthesis)

add_executable(qa_PulsedPowerGrc qa_PulsedPowerGrc.cpp)
target_link_libraries(
  qa_PulsedPowerGrc
  PRIVATE ut
          gnuradio4::gnuradio-core
          gnuradio4::gr-basic
          gnuradio4::gr-electrical
          gnuradio4::gr-filter
          opendigitizer-options)
target_compile_definitions(qa_PulsedPowerGrc PRIVATE OD_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../..")
add_test(NAME qa_PulsedPowerGrc COMMAND qa_PulsedPowerGrc)

if(ENABLE_IMGUI_TEST_ENGINE)

  cmrc_add_resource_library(
    ui_test_assets
    NAMESPACE
    ui_test_assets
    examples/fg_dipole_intensity_ramp.grc
    examples/qa_chart.grc
    examples/qa_image_chart.grc
    examples/qa_grouping.grc
    examples/qa_layout.grc
    examples/qa_localisation.grc
    examples/qa_subgraph.grc
    examples/qa_yy_scatter.grc
    examples/qa_statusbar.grc
    examples/qa_controls.grc
    examples/qa_imcontrol.grc
    examples/qa_imcontrol_conflicting_init_values.grc
    examples/qa_imcontrol_initial_value.grc
    examples/qa_imcontrol_color.grc
    examples/qa_uicontrol_dragdrop.grc
    examples/qa_uicontrol_connections.grc
    examples/qa_uicontrol_readout.grc
    examples/qa_uicontrol_compatibility.grc
    examples/qa_property_limits.grc
    examples/qa_toolbar.grc
    examples/qa_host_composition.grc
    baselines/dashboard_layout.txt)

  # Code shared between all tests
  add_library(imgui_tests_common STATIC ImGuiTestApp.cpp)
  target_link_libraries(
    imgui_tests_common
    PRIVATE opendigitizer-options
    PUBLIC opendigitizer-uilib
           raii_wrapper
           ui_assets
           ui_test_assets)

  target_compile_definitions(imgui_tests_common PUBLIC OPENDIGITIZER_TEST
                                                       OPENDIGITIZER_BUILD_DIRECTORY="${CMAKE_BINARY_DIR}")

  target_include_directories(imgui_tests_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

  function(add_imgui_test NAME)
    add_executable(${NAME} ${NAME}.cpp)
    target_link_libraries(
      ${NAME}
      PRIVATE opendigitizer-uilib
              opendigitizer-options
              ut
              imgui_tests_common
              gnuradio4::gnuradio-blocklib-core
              gnuradio4::GrBasicBlocksShared
              gnuradio4::GrTestingBlocksShared
              gnuradio4::GrFourierBlocksShared)
    add_test(NAME ${NAME} COMMAND ${NAME})
  endfunction()

  add_imgui_test(qa_PopupMenu)
  add_imgui_test(qa_Dialog)
  add_imgui_test(qa_FilterComboBoxes)
  add_imgui_test(qa_Keypad)
  add_imgui_test(qa_chart)
  set_tests_properties(qa_chart PROPERTIES TIMEOUT 300) # a hang reports its pending wait within the CI step
  add_imgui_test(qa_ImageChart)
  add_imgui_test(qa_SampleDashboards)
  if(OD_ACOUSTIC_CAMERA)
    cmrc_add_resources(
      ui_test_assets
      WHENCE
      ../assets/sampleDashboards
      PREFIX
      dashboards
      ../assets/sampleDashboards/browser/XtraAcousticCamera.grc
      ../assets/sampleDashboards/browser/XtraLocalisation.grc
      ../assets/sampleDashboards/browser/XtraLocalisationSquare.grc
      ../assets/sampleDashboards/browser/XtraLocalisationTetrahedron.grc
      ../assets/sampleDashboards/browser/XtraLocalisationLine.grc
      ../assets/sampleDashboards/browser/XtraLocalisationUma16.grc
      ../assets/sampleDashboards/browser/XtraLocalisationStereo.grc)
    target_link_libraries(qa_ImageChart PRIVATE gnuradio4::GrAudioBlocksShared gnuradio4::GrLocalisationBlocksShared
                                                gnuradio4::GrFilterBlocksShared)
    target_compile_definitions(qa_ImageChart PRIVATE OD_ACOUSTIC_CAMERA)
    target_link_libraries(qa_chart PRIVATE gnuradio4::GrAudioBlocksShared gnuradio4::GrLocalisationBlocksShared
                                           gnuradio4::GrFilterBlocksShared)
    target_compile_definitions(qa_chart PRIVATE OD_ACOUSTIC_CAMERA)
  endif()
  add_imgui_test(qa_ChartFontSize)
  add_imgui_test(qa_ChartCompatibility)
  add_imgui_test(qa_BlockNeighboursPreview)
  add_imgui_test(qa_Docking)
  add_imgui_test(qa_FramePacer)
  add_imgui_test(qa_layout)
  add_imgui_test(qa_DashboardBaseline)
  add_imgui_test(qa_DashboardView)
  add_imgui_test(qa_CompactLayout)
  add_test(NAME qa_CompactLayout_portrait COMMAND qa_CompactLayout --window 412x915)
  add_test(NAME qa_CompactLayout_landscape COMMAND qa_CompactLayout --window 915x412)
  add_imgui_test(qa_ToolbarView)
  add_imgui_test(qa_ImControl)
  add_imgui_test(qa_ImControlNumber)
  add_imgui_test(qa_StatusBarView)
  add_imgui_test(qa_HostComposition)
  add_imgui_test(qa_ChartStyle)
  add_imgui_test(qa_GpuRenderer)
  add_imgui_test(qa_ZoomablePlot)
  add_imgui_test(qa_SignalColours)
  add_imgui_test(qa_flowgraph)
  add_imgui_test(qa_GraphModel)
  add_imgui_test(qa_StyledWidgets)
endif()

# the WASM UI in headless Chrome, from the superbuild's ui-wasm output (or OD_WASM_WEB_DIR); skipped without it
find_program(OD_NODE node)
find_program(
  OD_BROWSER
  NAMES google-chrome
        chromium
        chromium-browser
        google-chrome-stable)
# GR4's headless-browser harness (devtools/browser of the pinned GR4): the fetched GR4's, or the checkout CI makes in
# .gr4-devtools; the tests are skipped without it
if(gnuradio4_SOURCE_DIR AND EXISTS "${gnuradio4_SOURCE_DIR}/devtools/browser/browser_harness.mjs")
  set(_odGr4BrowserHarness "${gnuradio4_SOURCE_DIR}/devtools/browser")
else()
  set(_odGr4BrowserHarness "${CMAKE_SOURCE_DIR}/.gr4-devtools/devtools/browser")
endif()
set(OD_GR4_BROWSER_HARNESS
    "${_odGr4BrowserHarness}"
    CACHE PATH "GR4's devtools/browser, the harness of the browser tests")
# the harness uses node's global WebSocket: node 22 or later (emsdk's default node is 20)
set(_odBrowserTests OFF)
if(OD_NODE AND OD_BROWSER)
  execute_process(
    COMMAND ${OD_NODE} --version
    OUTPUT_VARIABLE _odNodeVersion
    OUTPUT_STRIP_TRAILING_WHITESPACE)
  string(
    REGEX
    REPLACE "^v([0-9]+).*"
            "\\1"
            _odNodeMajor
            "${_odNodeVersion}")
  if(_odNodeMajor GREATER_EQUAL 22)
    set(_odBrowserTests ON)
  else()
    message(
      WARNING "browser tests need node 22 or later; ${OD_NODE} is ${_odNodeVersion}: set OD_NODE to register them")
  endif()
endif()
set(OD_WASM_WEB_DIR
    "${CMAKE_BINARY_DIR}/CMakeExternals/Build/ui-wasm/web"
    CACHE PATH "WASM UI build output for the browser tests")
# an unoptimised (Debug) WASM UI is too slow in headless, software-rendered Chrome to load the localisation dashboards
if(_odBrowserTests
   AND NOT
       CMAKE_BUILD_TYPE
       STREQUAL
       "Debug")
  add_test(NAME qa_BrowserMicrophones COMMAND ${OD_NODE} ${CMAKE_CURRENT_SOURCE_DIR}/browser/qa_BrowserMicrophones.mjs
                                              ${OD_WASM_WEB_DIR})
  set_tests_properties(
    qa_BrowserMicrophones
    PROPERTIES ENVIRONMENT
               "OD_GR4_BROWSER_HARNESS=${OD_GR4_BROWSER_HARNESS}"
               LABELS
               browser
               TIMEOUT
               300
               RUN_SERIAL
               TRUE
               SKIP_RETURN_CODE
               77)
  add_test(NAME qa_BrowserSampleDashboards
           COMMAND ${OD_NODE} ${CMAKE_CURRENT_SOURCE_DIR}/browser/qa_BrowserSampleDashboards.mjs ${OD_WASM_WEB_DIR})
  set_tests_properties(
    qa_BrowserSampleDashboards
    PROPERTIES ENVIRONMENT
               "OD_GR4_BROWSER_HARNESS=${OD_GR4_BROWSER_HARNESS}"
               LABELS
               browser
               TIMEOUT
               900
               RUN_SERIAL
               TRUE
               SKIP_RETURN_CODE
               77)
endif()
# the WebGPU flavour of the WASM UI (preset wasm-webgpu), a Release build of its own; skipped without it
set(OD_WASM_WEBGPU_WEB_DIR
    "${CMAKE_SOURCE_DIR}/cmake-build-wasm-webgpu/web"
    CACHE PATH "WebGPU WASM UI build output for the browser tests")
if(_odBrowserTests)
  add_test(NAME qa_BrowserWebGpu COMMAND ${OD_NODE} ${CMAKE_CURRENT_SOURCE_DIR}/browser/qa_BrowserWebGpu.mjs
                                         ${OD_WASM_WEBGPU_WEB_DIR})
  set_tests_properties(
    qa_BrowserWebGpu
    PROPERTIES ENVIRONMENT
               "OD_GR4_BROWSER_HARNESS=${OD_GR4_BROWSER_HARNESS}"
               LABELS
               browser
               TIMEOUT
               300
               RUN_SERIAL
               TRUE
               SKIP_RETURN_CODE
               77)
endif()
if(_odBrowserTests
   AND NOT
       CMAKE_BUILD_TYPE
       STREQUAL
       "Debug"
   AND TARGET ex_test_card_y4m)
  add_test(NAME qa_BrowserCamera COMMAND ${OD_NODE} ${CMAKE_CURRENT_SOURCE_DIR}/browser/qa_BrowserCamera.mjs
                                         ${OD_WASM_WEB_DIR} $<TARGET_FILE:ex_test_card_y4m>)
  set_tests_properties(
    qa_BrowserCamera
    PROPERTIES ENVIRONMENT
               "OD_GR4_BROWSER_HARNESS=${OD_GR4_BROWSER_HARNESS}"
               LABELS
               browser
               TIMEOUT
               600
               RUN_SERIAL
               TRUE
               SKIP_RETURN_CODE
               77)
endif()
