cmake_minimum_required(VERSION 3.17)

# set a variable in CMake
set(projectName "opendigitizer-ui")

project(${projectName} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
#set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_VERBOSE_MAKEFILE OFF)

set(sources
        main.cpp
        app.cpp
        flowgraph.cpp
        flowgraphitem.cpp
        dashboard.cpp
        flowgraph/datasource.cpp
        flowgraph/datasink.cpp
        flowgraph/fftblock.cpp
        flowgraph/remotedatasource.cpp
        flowgraph/arithmetic_block.cpp
        dashboardpage.cpp
        opendashboardpage.cpp
        imguiutils.cpp
 )

set(GNURADIO_PREFIX "/usr/" CACHE FILEPATH "Prefix of the GNURadio installation")

add_subdirectory(app_header)

if (EMSCRIPTEN)
    message(STATUS "Detected emscripten webassembly build")

    # CAUTION: The "SHELL:" before some compile/link options has to be used to explicity tell CMake to not sum up options
    # (e.g. -option A -option B => -option A B)(CMake default behaviour), but instead keep them separated (needed for
    # proper operation of emscripten)
    add_compile_options(
            -DIMGUI_DISABLE_FILE_FUNCTIONS
            -g
            -Wall -Wformat -Os
            "SHELL:-s USE_SDL=2"
    )

    add_link_options(
            "SHELL:-s WASM=1"
            "SHELL:-s ALLOW_MEMORY_GROWTH=1"
            "SHELL:-s NO_EXIT_RUNTIME=0"
            "SHELL:-s ASSERTIONS=1"
            "SHELL:-s NO_FILESYSTEM=1"
            --shell-file ${CMAKE_CURRENT_SOURCE_DIR}/shell_minimal.html
            "SHELL:-s USE_SDL=2"
            -sFETCH
    )
endif()

# dependencies
include(cmake/Dependencies.cmake)
cmrc_add_resource_library(ui_assets NAMESPACE ui_assets assets/fair-logo/FAIR_Logo_rgb_72dpi.png
                                                        assets/fair-logo/FAIR_Logo_rgb_72dpi_dark.png
                                                        assets/fontawesome/fa-regular-400.ttf
                                                        assets/fontawesome/fa-solid-900.ttf)
cmrc_add_resource_library(fonts NAMESPACE fonts WHENCE ${imgui_SOURCE_DIR}/misc/fonts
                        ${imgui_SOURCE_DIR}/misc/fonts/Roboto-Medium.ttf)
cmrc_add_resource_library(sample_dashboards NAMESPACE sample_dashboards assets/sampleDashboards/ExtendedDemoDashboard.grc
        assets/sampleDashboards/ExtendedDemoDashboard.yml
        assets/sampleDashboards/DemoDashboard.grc
        assets/sampleDashboards/DemoDashboard.yml)

if (EMSCRIPTEN)
    # locate the "executable" output in the build/web directory
    set(EXECUTABLE_OUTPUT_PATH ${CMAKE_BINARY_DIR}/web)

    # necessary to tell CMake (with emscripten) to extend the standard .js and .wasm output with .html output (so that we can execute it with the webserver)
    set(CMAKE_EXECUTABLE_SUFFIX ".html")

    set(outputFileName "index")
    add_executable(${outputFileName})

    target_sources(index PRIVATE ${sources})
    set(target_name "index")

    # link the executable
    target_link_libraries(
            ${outputFileName}
            PRIVATE
            implot
            plf_colony
            app_header
            ui_assets
            sample_dashboards
            fonts
            core
            client
            imgui-node-editor
            yaml-cpp::yaml-cpp
    )

    add_custom_target(serve
            python3 -m http.server -d ${EXECUTABLE_OUTPUT_PATH} 8000
            COMMENT start a python server serving the webassembly app
            DEPENDS ${outputFileName}
    )
else () # native build

    set(target_name "opendigitizer-ui")
    add_executable(${target_name})

    target_sources(${target_name} PRIVATE ${sources})

    # link the executable
    target_link_libraries(
            ${target_name}
            PRIVATE
            SDL2
            implot
            imgui-node-editor
            plf_colony
            yaml-cpp::yaml-cpp
            core
            client
            app_header
            ui_assets
            sample_dashboards
            fonts
    )
    target_compile_definitions(${target_name} PRIVATE BLOCKS_DIR="${GNURADIO_PREFIX}/share/gnuradio/grc/blocks/")
endif()

target_link_libraries(${target_name} PRIVATE function2::function2)
target_include_directories(${target_name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../service/acquisition) #for daq_api.hpp
