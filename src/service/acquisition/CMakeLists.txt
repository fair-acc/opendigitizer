add_library(od_acquisition INTERFACE acqWorker.hpp daq_api.hpp)
target_include_directories(od_acquisition INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/.)
target_link_libraries(od_acquisition INTERFACE majordomo mdspan project_options project_warnings)

if (BUILD_TESTING)
    add_subdirectory(test)
endif()