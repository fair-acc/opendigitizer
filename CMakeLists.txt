cmake_minimum_required(VERSION 3.12)

project(opendigitizer CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN 1)

set(EMCMAKE_COMMAND
    "emcmake"
    CACHE FILEPATH "Path to the emcmake command")

include(ExternalProject)

set(ROOT_BUILD_DIR ${CMAKE_BINARY_DIR})
set(PREFIX_DIR ${CMAKE_BINARY_DIR}/CMakeExternals/Prefix)
set(BUILD_DIR ${CMAKE_BINARY_DIR}/CMakeExternals/Build)
set(INSTALL_DIR ${CMAKE_BINARY_DIR}/CMakeExternals/Install)
set(WASM_BUILD_DIR ${BUILD_DIR}/ui-wasm)
set(SERVING_DIR ${ROOT_BUILD_DIR}/ui-wasm)

configure_file(build_configuration.hpp.in ${CMAKE_BINARY_DIR}/build_configuration.hpp @ONLY)
include_directories(${CMAKE_BINARY_DIR})

option(USE_DWARF4 "Use DWARF-4 instead of DWARF-5 for better LLDB compatibility" OFF)
if(USE_DWARF4)
  # LLDB doesn't seem to like bleeding edge gcc debug info. Enable this if your LLDB isn't showing source. Reproduced
  # with LLDB v18, 19 and 20 against gcc 14.2.1
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -gdwarf-4")
endif()

option(OPENDIGITIZER_ENABLE_TESTING "Enable unit tests" ON)

# Set proper default for ENABLE_IMGUI_TEST_ENGINE, the idea is:

# * Release people shouldn't enable it by mistake, as it instrumentalizes ImGui
# * Developers shouldn't skip it by mistake
# * All other people should explicitly pass -DOPENDIGITIZER_ENABLE_TESTING= with their intention
if(OPENDIGITIZER_ENABLE_TESTING)
  enable_testing()
  if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(DEFAULT_ENABLE_IMGUI_TEST_ENGINE ON)
  endif()
else()
  set(DEFAULT_ENABLE_IMGUI_TEST_ENGINE OFF)
endif()

option(ENABLE_IMGUI_TEST_ENGINE, "Enable ImGui Test Engine" ${DEFAULT_ENABLE_IMGUI_TEST_ENGINE})

include(cmake/Dependencies.cmake)
include(cmake/StandardProjectSettings.cmake)
include(cmake/PreventInSourceBuilds.cmake)
include(cmake/Cache.cmake) # enable cache system
include(cmake/CMakeRC.cmake)
add_library(project_warnings INTERFACE) # Link this 'library' to use the warnings specified in CompilerWarnings.cmake
# standard compiler warnings
include(cmake/CompilerWarnings.cmake)
set_project_warnings(project_warnings)

add_subdirectory(src/acquisition)
add_subdirectory(src/utils)
add_subdirectory(src/ui)

option(DISABLE_WASM_BUILD "Skip building WASM UI" OFF)

if(NOT DISABLE_WASM_BUILD)
  ExternalProject_Add(
    ui-wasm
    SOURCE_DIR ${CMAKE_SOURCE_DIR}/src/ui
    PREFIX ${PREFIX_DIR}/ui-wasm
    CONFIGURE_COMMAND
      ${EMCMAKE_COMMAND} ${CMAKE_COMMAND} -G "Unix Makefiles" -S ${CMAKE_SOURCE_DIR}/src/ui -B ${BUILD_DIR}/ui-wasm
      -DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE} -DCMAKE_MAKE_PROGRAM=${CMAKE_SOURCE_DIR}/emmake_make.sh
    BUILD_COMMAND cmake --build ${BUILD_DIR}/ui-wasm
    BINARY_DIR ${BUILD_DIR}/ui-wasm
    BUILD_ALWAYS 1
    INSTALL_COMMAND "" # skip install
    CMAKE_ARGS -DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE} -DCMAKE_MAKE_PROGRAM:FILEPATH=${CMAKE_MAKE_PROGRAM}
    BUILD_BYPRODUCTS
      ${WASM_BUILD_DIR}/web/index.html
      ${WASM_BUILD_DIR}/web/index.js
      ${WASM_BUILD_DIR}/web/index.worker.js
      ${WASM_BUILD_DIR}/web/index.wasm)
  set(WASM_BUILD_DIR ${BUILD_DIR}/ui-wasm)
  add_custom_command(
    TARGET ui-wasm
    POST_BUILD
    COMMAND
      ${CMAKE_COMMAND} -E make_directory ${ROOT_BUILD_DIR}/ui-wasm/web && ${CMAKE_COMMAND} -E copy
      ${WASM_BUILD_DIR}/web/index.html ${WASM_BUILD_DIR}/web/index.js ${WASM_BUILD_DIR}/web/index.worker.js
      ${WASM_BUILD_DIR}/web/index.wasm ${ROOT_BUILD_DIR}/ui-wasm/web)
endif()

add_subdirectory(src/service)
