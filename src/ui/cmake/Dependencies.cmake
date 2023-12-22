include(FetchContent)

FetchContent_Declare(
    imgui
    GIT_REPOSITORY  https://github.com/ocornut/imgui.git
    GIT_TAG         v1.90 # latest as of 2023-12-19
)

# Enables 32 bit vertex indices for ImGui
add_compile_definitions("ImDrawIdx=unsigned int")

FetchContent_Declare(
    implot
    GIT_REPOSITORY  https://github.com/epezent/implot.git
    GIT_TAG         v0.16 # latest as of 2023-12-19
)

FetchContent_Declare(
    imgui-node-editor
    GIT_REPOSITORY  https://github.com/thedmd/imgui-node-editor.git
    GIT_TAG         v0.9.3 # latest as of 2023-12-19
)

FetchContent_Declare(
    yaml-cpp
    GIT_REPOSITORY  https://github.com/jbeder/yaml-cpp.git
    GIT_TAG         yaml-cpp-0.7.0
)

FetchContent_Declare(
    plf_colony
    GIT_REPOSITORY  https://github.com/mattreecebentley/plf_colony.git
    GIT_TAG         41e387e281b8323ca5584e79f67d632964b24bbf #v7.11
)

FetchContent_Declare( # needed to load images in ImGui
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG 8b5f1f37b5b75829fc72d38e7b5d4bcbf8a26d55 # master from Sep 2022
)

# TODO use proper release once available
FetchContent_Declare(
        opencmw-cpp
        GIT_REPOSITORY https://github.com/fair-acc/opencmw-cpp.git
        GIT_TAG 57f31a19d8998da944ec73223d7f3fba4feeb324
)

FetchContent_Declare(
        graph-prototype
        GIT_REPOSITORY https://github.com/fair-acc/graph-prototype.git
        GIT_TAG 52f48e9c6449a88ff99807479481a8e80d7ecae4
)

FetchContent_MakeAvailable(imgui implot imgui-node-editor yaml-cpp stb opencmw-cpp plf_colony graph-prototype)

if (NOT EMSCRIPTEN)
    find_package(SDL2 REQUIRED)
    find_package(OpenGL REQUIRED COMPONENTS OpenGL)
    FetchContent_Declare(
        sdl2
        OVERRIDE_FIND_PACKAGE
        GIT_REPOSITORY "https://github.com/libsdl-org/SDL"
        GIT_TAG        release-2.28.2
    )
    FetchContent_MakeAvailable(sdl2)
endif()

# imgui and implot are not CMake Projects, so we have to define their targets manually here
add_library(
    imgui
    OBJECT
        ${imgui_SOURCE_DIR}/imgui_demo.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
)
if(NOT EMSCRIPTEN) # emscripten comes with its own sdl, for native we have to specify the dependency
    target_link_libraries(imgui PUBLIC SDL2::SDL2  OpenGL::GL)
endif()

target_include_directories(
    imgui BEFORE
    PUBLIC
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
)

add_library(
    implot
    OBJECT ${implot_SOURCE_DIR}/implot_demo.cpp ${implot_SOURCE_DIR}/implot_items.cpp ${implot_SOURCE_DIR}/implot.cpp
)
target_include_directories(
    implot BEFORE
    PUBLIC
        ${implot_SOURCE_DIR}
)
target_link_libraries(implot PUBLIC imgui $<TARGET_OBJECTS:imgui>)

add_library(
    imgui-node-editor
    OBJECT
        ${imgui-node-editor_SOURCE_DIR}/imgui_node_editor.cpp
        ${imgui-node-editor_SOURCE_DIR}/imgui_canvas.cpp
        ${imgui-node-editor_SOURCE_DIR}/imgui_node_editor_api.cpp
        ${imgui-node-editor_SOURCE_DIR}/crude_json.cpp
)
target_include_directories(
    imgui-node-editor BEFORE
    PUBLIC
        ${imgui-node-editor_SOURCE_DIR}
)
target_link_libraries(imgui-node-editor PUBLIC imgui $<TARGET_OBJECTS:imgui>)

add_library(stb INTERFACE)
target_include_directories(stb INTERFACE ${stb_SOURCE_DIR})

add_library(plf_colony INTERFACE)
target_include_directories(plf_colony INTERFACE ${plf_colony_SOURCE_DIR})
