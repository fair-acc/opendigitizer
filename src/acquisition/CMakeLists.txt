add_library(od_acquisition INTERFACE)

target_link_libraries(od_acquisition INTERFACE gnuradio-core)
target_include_directories(od_acquisition INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
                                                    $<INSTALL_INTERFACE:include/>)

set_target_properties(od_acquisition PROPERTIES PUBLIC_HEADER daq_api.hpp)
