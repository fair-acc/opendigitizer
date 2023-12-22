add_library(raii_wrapper INTERFACE)

target_include_directories(raii_wrapper INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
                                                  $<INSTALL_INTERFACE:include/>)
target_link_libraries(raii_wrapper INTERFACE project_options project_warnings)
set_target_properties(raii_wrapper PROPERTIES PUBLIC_HEADER /include/c_resource.hpp)

if(OPENDIGITIZER_ENABLE_TESTING)
  add_subdirectory(test)
endif()
