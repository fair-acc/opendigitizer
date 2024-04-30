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
