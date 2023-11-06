add_library(od_acquisition INTERFACE)

target_include_directories(od_acquisition INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}> $<INSTALL_INTERFACE:include/>)

target_link_libraries(od_acquisition INTERFACE project_options project_warnings)
set_target_properties(od_acquisition PROPERTIES PUBLIC_HEADER daq_api.hpp)
