add_library(raii_wrapper INTERFACE)

target_include_directories(raii_wrapper INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
                                                  $<INSTALL_INTERFACE:include/>)
set_target_properties(raii_wrapper PROPERTIES PUBLIC_HEADER /include/c_resource.hpp)

add_library(digitizer_settings INTERFACE)

target_include_directories(digitizer_settings INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
                                                        $<INSTALL_INTERFACE:include>)
set_target_properties(digitizer_settings PROPERTIES PUBLIC_HEADER include/settings.h)

if(OPENDIGITIZER_ENABLE_TESTING)
  add_subdirectory(test)
endif()
