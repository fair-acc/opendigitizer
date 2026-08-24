add_library(od_acquisition INTERFACE)

if(EMSCRIPTEN AND NOT OD_WASM_STATIC_BLOCKLIBS)
  # Headers/compile defs only. The UI links gnuradio-core-dynload (WHOLE_ARCHIVE); a DEFAULT/LINK_ONLY edge of
  # gnuradio-core here is rejected by CMake 3.31+.
  target_link_libraries(od_acquisition INTERFACE "$<COMPILE_ONLY:gnuradio4::gnuradio-core>")
else()
  target_link_libraries(od_acquisition INTERFACE gnuradio4::gnuradio-core)
endif()
target_include_directories(od_acquisition INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
                                                    $<INSTALL_INTERFACE:include/>)

set_target_properties(od_acquisition PROPERTIES PUBLIC_HEADER daq_api.hpp)
