include(FetchContent)

FetchContent_Declare(
    imgui
    GIT_REPOSITORY  https://github.com/ocornut/imgui.git
    GIT_TAG         v1.88
)

# Enables 32 bit vertex indices for ImGui
add_compile_definitions("ImDrawIdx=unsigned int")

FetchContent_Declare(
    implot
    GIT_REPOSITORY  https://github.com/epezent/implot.git
    GIT_TAG         18758e237e8906a97ddf42de1e75793526f30ce9 #latest 2023-04-19
)

FetchContent_Declare(
    imgui-node-editor
    GIT_REPOSITORY  https://github.com/thedmd/imgui-node-editor.git
    GIT_TAG         2f99b2d613a400f6579762bd7e7c343a0d844158
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
        GIT_TAG a245ee17adaa61f530d95883da38b68fb53f4de8
)

FetchContent_Declare(
    function2
    GIT_REPOSITORY https://github.com/Naios/function2.git
    GIT_TAG 4.2.2
)

FetchContent_MakeAvailable(imgui implot imgui-node-editor yaml-cpp stb opencmw-cpp plf_colony function2)

FetchContent_Declare(
        sdl2
        OVERRIDE_FIND_PACKAGE
        GIT_REPOSITORY "https://github.com/libsdl-org/SDL"
        GIT_TAG        release-2.28.2
)

if (EMSCRIPTEN)
    set(sdl2_SDL_ATOMIC ON CACHE INTERNAL "ON")
endif()

FetchContent_MakeAvailable(sdl2)

find_package(SDL2 REQUIRED)
find_package(OpenGL REQUIRED COMPONENTS OpenGL)
# imgui and implot are not CMake Projects, so we have to define their targets manually here
add_library(
    imgui
    OBJECT
        ${imgui_SOURCE_DIR}/imgui_demo.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
)
if(NOT EMSCRIPTEN) # emscripten comes with its own sdl, for native we have to specify the dependency
    target_link_libraries(imgui PUBLIC SDL2::SDL2  OpenGL::GL)
else()
    target_link_libraries(imgui PUBLIC  SDL2::SDL2 )
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
