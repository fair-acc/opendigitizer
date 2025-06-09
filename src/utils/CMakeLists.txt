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

if(Git_FOUND)
  # cmake-format: off
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
          "${CMAKE_CURRENT_SOURCE_DIR}/../../.git/HEAD"
          "${CMAKE_CURRENT_SOURCE_DIR}/../../.git/index"
          "${CMAKE_CURRENT_SOURCE_DIR}/../../.git/refs/heads/main"
          "${CMAKE_CURRENT_SOURCE_DIR}/../../.git/packed-refs"
  )
  # cmake-format: on
endif()

# Build timestamp
string(TIMESTAMP OPENDIGITIZER_BUILD_TIME "%Y-%m-%d %H:%M:%S")

if(EMSCRIPTEN)
  set(OPENDIGITIZER_IS_EMSCRIPTEN "true")
else()
  set(OPENDIGITIZER_IS_EMSCRIPTEN "false")
endif()

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "unknown")
endif()

string(
  CONCAT OPENDIGITIZER_VERSION_INFO
         "${OPENDIGITIZER_GIT_DESCRIBE} | "
         "built: ${OPENDIGITIZER_BUILD_TIME} | "
         "emscripten: ${OPENDIGITIZER_IS_EMSCRIPTEN} | "
         "build-type: ${CMAKE_BUILD_TYPE}")

# Configure version file
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/version/version.cpp.in ${CMAKE_BINARY_DIR}/generated_version/version.cpp
               @ONLY)

add_library(opendigitizer_version STATIC ${CMAKE_BINARY_DIR}/generated_version/version.cpp)
target_include_directories(opendigitizer_version PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/version/include)
target_include_directories(opendigitizer_version PUBLIC ${CMAKE_BINARY_DIR}/generated_version)
