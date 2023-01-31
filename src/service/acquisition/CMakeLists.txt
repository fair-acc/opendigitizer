add_library(od_acquisition INTERFACE acqWorker.hpp daq_api.hpp)
target_include_directories(od_acquisition INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/.)
target_link_libraries(od_acquisition INTERFACE majordomo disruptor project_options project_warnings)

add_subdirectory(test)