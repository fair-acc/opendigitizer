add_library(raii_wrapper INTERFACE)

target_include_directories(raii_wrapper INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
                                                  $<INSTALL_INTERFACE:include/>)
set_target_properties(raii_wrapper PROPERTIES PUBLIC_HEADER /include/c_resource.hpp)

add_library(digitizer_settings INTERFACE)

target_include_directories(digitizer_settings INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
                                                        $<INSTALL_INTERFACE:include>)
set_target_properties(digitizer_settings PROPERTIES PUBLIC_HEADER include/settings.h)

add_library(digitizer_common_utils INTERFACE)

target_include_directories(digitizer_common_utils INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
                                                            $<INSTALL_INTERFACE:include>)
set_target_properties(digitizer_common_utils PROPERTIES PUBLIC_HEADER include/conversion.hpp)

if(OPENDIGITIZER_ENABLE_TESTING)
  add_subdirectory(test)
endif()

# Automatically generate current version of opendigitizer
set(OPENDIGITIZER_GIT_DESCRIBE "unknown")
find_package(Git)
if(Git_FOUND)
  execute_process(
    COMMAND ${GIT_EXECUTABLE} describe --tags --long --always --dirty #
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    OUTPUT_VARIABLE OPENDIGITIZER_GIT_DESCRIBE
    OUTPUT_STRIP_TRAILING_WHITESPACE)
endif()
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/version/version.cpp.in ${CMAKE_BINARY_DIR}/generated_version/version.cpp
               @ONLY)
if(Git_FOUND)
  set_property(
    DIRECTORY
    APPEND
    PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/../../.git/HEAD")
endif()
add_library(opendigitizer_version STATIC ${CMAKE_BINARY_DIR}/generated_version/version.cpp)
target_include_directories(opendigitizer_version PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/version/include)
