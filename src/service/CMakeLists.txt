cmake_minimum_required(VERSION 3.19)
set(CMAKE_CXX_STANDARD 23)

include(cmake/Dependencies.cmake)

project(opendigitizer-service CXX)
include(cmake/StandardProjectSettings.cmake)
include(cmake/PreventInSourceBuilds.cmake)
include(GNUInstallDirs)

option(WASM_BUILD_DIR "The directory containing the build output of the emscripten ui build to be bundled into the assets" ${CMAKE_CURRENT_SOURCE_DIR}/../ui/build-wasm)

# Check for supported compiler versions
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    if (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 11.1.0)
        message(FATAL_ERROR "GCC>=11.1.0 required, but gcc ${CMAKE_CXX_COMPILER_VERSION} detected.")
    endif ()
elseif (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    if (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 13.0.0)
        message(FATAL_ERROR "Clang>=13.0.0 required, but clang ${CMAKE_CXX_COMPILER_VERSION} detected.")
    endif ()
else ()
    message(WARN "No version check for your compiler (${CMAKE_CXX_COMPILER_ID}) implemented, "
            "in case of build problems consider updating your compiler or check if you can switch to gcc or clang")
endif ()

# Link this 'library' to set the c++ standard / compile-time options requested
add_library(project_options INTERFACE)
target_include_directories(project_options INTERFACE ${CMAKE_SOURCE_DIR}/3rd_party)

target_compile_features(project_options INTERFACE cxx_std_23)

if (CMAKE_CXX_COMPILER_ID MATCHES ".*Clang")
    option(ENABLE_BUILD_WITH_TIME_TRACE "Enable -ftime-trace to generate time tracing .json files on clang" OFF)
    if (ENABLE_BUILD_WITH_TIME_TRACE)
        target_compile_options(project_options INTERFACE -ftime-trace)
    endif ()
endif ()

if (CMAKE_BUILD_TYPE STREQUAL "Debug" OR GENERATOR_IS_MULTI_CONFIG)
    option(ENABLE_COVERAGE "Enable Coverage" ON)
else ()
    option(ENABLE_COVERAGE "Enable Coverage" OFF)
endif ()

# enable cache system
include(cmake/Cache.cmake)

# Link this 'library' to use the warnings specified in CompilerWarnings.cmake
add_library(project_warnings INTERFACE)
# standard compiler warnings
include(cmake/CompilerWarnings.cmake)
set_project_warnings(project_warnings)

if (ENABLE_COVERAGE)
    if (UNIX AND NOT APPLE) # Linux
        message("Coverage reporting enabled")
        include(cmake/CodeCoverage.cmake) # https://github.com/bilke/cmake-modules/blob/master/CodeCoverage.cmake
        # (License: BSL-1.0)
        target_compile_options(project_options INTERFACE --coverage -O0 -g -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0) # fortify_source is not possible without optimization
        target_link_libraries(project_options INTERFACE --coverage)
        append_coverage_compiler_flags()
    else ()
        message(WARNING "Coverage is only supported on linux")
    endif ()
endif()

add_subdirectory(acquisition)
add_subdirectory(flowgraph) # worker providing access to the mock flowgraph and allows editing it
add_subdirectory(rest) # worker providing access to static assets
add_subdirectory(dashboard)

message("COPY ${CMAKE_SOURCE_DIR}/demo_sslcert/demo_private.key DESTINATION ${CMAKE_CURRENT_BINARY_DIR}" )
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/demo_sslcert/demo_private.key" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/")
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/demo_sslcert/demo_public.crt" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/")

add_executable(opendigitizer main.cpp)
target_link_libraries(opendigitizer PRIVATE od_flowgraph_worker od_dashboard_worker od_acquisition_worker od_rest majordomo client project_options project_warnings assets::rest)
