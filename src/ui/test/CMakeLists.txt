# define unit-tests

add_executable(qa_play_stop_bar qa_play_stop_bar.cpp)
target_include_directories(qa_play_stop_bar INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/>
                                                      $<INSTALL_INTERFACE:include/>)
target_link_libraries(
  qa_play_stop_bar
  PRIVATE fmt
          ut
          core
          gnuradio-core
          gnuradio-meta
          gnuradio-algorithm
          gr-basic
          gr-fourier
          implot
          imgui-node-editor
          services
          stb
          ui_assets
          fonts
          sample_dashboards
          client
          plf_colony)
# workaround for getting out of source tree dependencies, should be modelled as proper targets
target_include_directories(qa_play_stop_bar PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../acquisition) # for daq_api.hpp
target_include_directories(qa_play_stop_bar PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../utils/include)

add_test(NAME qa_play_stop_bar COMMAND qa_play_stop_bar)

if(ENABLE_IMGUI_TEST_ENGINE)

  cmrc_add_resource_library(
    ui_test_assets
    NAMESPACE
    ui_test_assets
    examples/fg_dipole_intensity_ramp.grc
    examples/fg_dipole_intensity_ramp.yml)

  # Code shared between all tests
  add_library(imgui_tests_common STATIC ImGuiTestApp.cpp)
  target_link_libraries(imgui_tests_common PUBLIC
    opendigitizer-uilib
    raii_wrapper
    ui_assets
    ui_test_assets
  )

  target_compile_definitions(imgui_tests_common PUBLIC IMGUI_DEFINE_MATH_OPERATORS
                                                       OPENDIGITIZER_BUILD_DIRECTORY="${CMAKE_BINARY_DIR}")

  target_include_directories(imgui_tests_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

  function(add_imgui_test NAME)
    add_executable(${NAME} ${NAME}.cpp)
    target_link_libraries(${NAME} PRIVATE opendigitizer-uilib ut imgui_tests_common)
    add_test(NAME ${NAME} COMMAND ${NAME})
  endfunction()

  add_imgui_test(qa_popup_menu)
  add_imgui_test(qa_dialog)
  add_imgui_test(qa_filtercomboboxes)
  add_imgui_test(qa_keypad)
  add_imgui_test(qa_chart)

endif()
