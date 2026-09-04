cmake_minimum_required(VERSION 3.27)

project(opendigitizer LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN 1)

include(ExternalProject)

# Mainly for FMT
set(CMAKE_POSITION_INDEPENDENT_CODE TRUE)

set(ROOT_BUILD_DIR ${CMAKE_BINARY_DIR})
set(PREFIX_DIR ${CMAKE_BINARY_DIR}/CMakeExternals/Prefix)
set(BUILD_DIR ${CMAKE_BINARY_DIR}/CMakeExternals/Build)
set(INSTALL_DIR ${CMAKE_BINARY_DIR}/CMakeExternals/Install)

option(USE_DWARF4 "Use DWARF-4 instead of DWARF-5 for better LLDB compatibility" OFF)
if(USE_DWARF4)
  # LLDB doesn't seem to like bleeding edge gcc debug info. Enable this if your LLDB isn't showing source. Reproduced
  # with LLDB v18, 19 and 20 against gcc 14.2.1
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -gdwarf-4")
endif()

option(OPENDIGITIZER_ENABLE_TESTING "Enable unit tests" ON)
option(OPENDIGITIZER_WARNINGS_AS_ERRORS "Treat OpenDigitizer compiler warnings as errors" ON)

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

option(ENABLE_IMGUI_TEST_ENGINE "Enable ImGui Test Engine" ${DEFAULT_ENABLE_IMGUI_TEST_ENGINE})
message("OPENDIGITIZER_ENABLE_TESTING : ${OPENDIGITIZER_ENABLE_TESTING}")
message("ENABLE_IMGUI_TEST_ENGINE     : ${ENABLE_IMGUI_TEST_ENGINE}")

option(OD_WASM_STATIC_BLOCKLIBS "WASM: statically link Gr*Blocks into the main module (no SIDE_MODULE dlopen)" ON)
if(OD_WASM_STATIC_BLOCKLIBS)
  set(DEFAULT_OD_WASM_GNURADIO4_PREFIX "/opt/gnuradio4-emscripten-static")
else()
  set(DEFAULT_OD_WASM_GNURADIO4_PREFIX "/opt/gnuradio4-emscripten")
endif()
set(OD_WASM_CMAKE_OPTIONS
    ""
    CACHE STRING "Additional cmake options to pass to the emscripten ui build")
set(OD_WASM_GNURADIO4_PREFIX
    "${DEFAULT_OD_WASM_GNURADIO4_PREFIX}"
    CACHE PATH "Installed Emscripten gnuradio4 prefix for the ui-wasm ExternalProject")

set(WITH_DOCS OFF)
set(SDL_TEST OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(cmake/Dependencies.cmake)
include(cmake/StandardProjectSettings.cmake)
include(cmake/PreventInSourceBuilds.cmake)
include(cmake/Cache.cmake) # enable cache system
include(cmake/CMakeRC.cmake)
add_library(opendigitizer-options INTERFACE)
# standard compiler warnings
include(cmake/CompilerWarnings.cmake)
set_project_warnings(opendigitizer-options)
# sanitizer options if supported by compiler
include(cmake/Sanitizers.cmake)
enable_sanitizers(opendigitizer-options)

if(NOT EMSCRIPTEN)
  add_subdirectory(src/acquisition)
  add_subdirectory(src/utils)
endif()

add_subdirectory(src/ui)

option(DISABLE_WASM_BUILD "Skip building WASM UI" OFF)

if(NOT DISABLE_WASM_BUILD)
  # FILEPATH cache would turn a bare "emcmake" into ${CMAKE_SOURCE_DIR}/emcmake.
  find_program(EMCMAKE_COMMAND emcmake)
  if(NOT EMCMAKE_COMMAND)
    message(FATAL_ERROR "emcmake not found; source emsdk_env.sh or set -DEMCMAKE_COMMAND=")
  endif()
  # BOTH: emscripten cmake otherwise ignores host CMAKE_PREFIX_PATH when finding gnuradio4. Use only the Emscripten GR4
  # prefix so find_package does not pick a native install.
  set(WASM_BUILD_DIR ${BUILD_DIR}/ui-wasm)
  string(
    REPLACE ";"
            "|"
            _wasm_cmake_prefix_path
            "${OD_WASM_GNURADIO4_PREFIX}")
  ExternalProject_Add(
    ui-wasm
    SOURCE_DIR ${CMAKE_SOURCE_DIR}/src/ui
    PREFIX ${PREFIX_DIR}/ui-wasm
    LIST_SEPARATOR |
    CONFIGURE_COMMAND
      ${EMCMAKE_COMMAND} ${CMAKE_COMMAND} -G "Unix Makefiles" -S ${CMAKE_SOURCE_DIR}/src/ui -B ${BUILD_DIR}/ui-wasm
      -DCMAKE_PREFIX_PATH:STRING=${_wasm_cmake_prefix_path} -DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=BOTH
      -DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE} -DCMAKE_MAKE_PROGRAM=${CMAKE_SOURCE_DIR}/emmake_make.sh
      -DGNURADIO_PARSE_REGISTRATIONS_TOOL_CXX_COMPLILER=${CMAKE_CXX_COMPILER}
      -DOPENDIGITIZER_WARNINGS_AS_ERRORS=${OPENDIGITIZER_WARNINGS_AS_ERRORS}
      -DOD_WASM_STATIC_BLOCKLIBS:BOOL=${OD_WASM_STATIC_BLOCKLIBS} ${OD_WASM_CMAKE_OPTIONS}
    # No --parallel here: inherits CMAKE_BUILD_PARALLEL_LEVEL from the parent build.
    BUILD_COMMAND cmake --build ${BUILD_DIR}/ui-wasm
    BINARY_DIR ${BUILD_DIR}/ui-wasm
    BUILD_ALWAYS 1
    INSTALL_COMMAND "" # skip install
    CMAKE_ARGS -DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE} -DCMAKE_MAKE_PROGRAM:FILEPATH=${CMAKE_MAKE_PROGRAM}
               -DCMAKE_MODULE_PATH="${CMAKE_SOURCE_DIR}/cmake"
    BUILD_BYPRODUCTS ${WASM_BUILD_DIR}/web/index.html ${WASM_BUILD_DIR}/web/index.js ${WASM_BUILD_DIR}/web/index.wasm)
  add_custom_command(
    TARGET ui-wasm
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${WASM_BUILD_DIR}/web ${ROOT_BUILD_DIR}/ui-wasm/web
    COMMENT "Copy WASM UI and SIDE_MODULE plugins to ui-wasm/web")
endif()

if(NOT EMSCRIPTEN)
  add_subdirectory(src/service)
endif()
