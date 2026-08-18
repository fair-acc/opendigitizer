add_library(od_acquisition INTERFACE)

if(EMSCRIPTEN AND NOT OD_WASM_STATIC_BLOCKLIBS)
  # dynload already WHOLE_ARCHIVEs gnuradio-core; a second DEFAULT edge is rejected by CMake.
  target_link_libraries(od_acquisition INTERFACE gnuradio4::gnuradio-core-dynload)
else()
  target_link_libraries(od_acquisition INTERFACE gnuradio4::gnuradio-core)
endif()
target_include_directories(od_acquisition INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
                                                    $<INSTALL_INTERFACE:include/>)

set_target_properties(od_acquisition PROPERTIES PUBLIC_HEADER daq_api.hpp)
