cmake_minimum_required(VERSION 3.12)

project(opendigitizer CXX)

option(EMCMAKE_COMMAND "path to the emcmake command" "emcmake")

include(ExternalProject)

set(PREFIX_DIR ${CMAKE_BINARY_DIR}/CMakeExternals/Prefix)
set(BUILD_DIR ${CMAKE_BINARY_DIR}/CMakeExternals/Build)
set(INSTALL_DIR ${CMAKE_BINARY_DIR}/CMakeExternals/Install)


# add_subdirectory(src/ui) # todo: make client also compile on native

ExternalProject_Add( ui-wasm
  SOURCE_DIR ${CMAKE_SOURCE_DIR}/src/ui
  PREFIX ${PREFIX_DIR}/ui-wasm
  CONFIGURE_COMMAND ${EMCMAKE_COMMAND} ${CMAKE_COMMAND} -S ${CMAKE_SOURCE_DIR}/src/ui -B ${BUILD_DIR}/ui-wasm -DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE} -DCMAKE_MAKE_PROGRAM:FILEPATH=${CMAKE_MAKE_PROGRAM}
  BINARY_DIR ${BUILD_DIR}/ui-wasm
  BUILD_ALWAYS 1
  INSTALL_COMMAND "" # skip install
  CMAKE_ARGS
    -DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE}
    -DCMAKE_MAKE_PROGRAM:FILEPATH=${CMAKE_MAKE_PROGRAM}
)
set(WASM_BUILD_DIR ${BUILD_DIR}/ui-wasm)
# touch files so cmrc does not complain that they don't exist. acutal copying is done after the compilation
file(MAKE_DIRECTORY ${BUILD_DIR}/web/)
file(TOUCH ${BUILD_DIR}/web/index.html)
file(TOUCH ${BUILD_DIR}/web/index.js)
file(TOUCH ${BUILD_DIR}/web/index.wasm)

add_subdirectory(src/service)
