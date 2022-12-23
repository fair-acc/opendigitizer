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
    GIT_TAG         master #v0.13
)

FetchContent_Declare(
    imgui-node-editor
    GIT_REPOSITORY  https://github.com/thedmd/imgui-node-editor.git
    GIT_TAG         master
)

FetchContent_Declare(
    yaml-cpp
    GIT_REPOSITORY  https://github.com/jbeder/yaml-cpp.git
    GIT_TAG         yaml-cpp-0.7.0
)

if (EMSCRIPTEN)
    FetchContent_MakeAvailable(imgui implot imgui-node-editor yaml-cpp)
else () # native build
    FetchContent_Declare(
            sdl2
            GIT_REPOSITORY "https://github.com/libsdl-org/SDL"
            GIT_TAG        release-2.24.2
    )
    FetchContent_MakeAvailable(sdl2 imgui implot imgui-node-editor yaml-cpp)
    find_package(OpenGL REQUIRED COMPONENTS OpenGL)
    target_link_libraries(SDL2 PUBLIC OpenGL::GL )
    target_include_directories(SDL2 PUBLIC ${sdl2_SOURCE_DIR}/include)
endif()

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
)
if(NOT EMSCRIPTEN) # emscripten comes with its own sdl, for native we have to specify the dependency
    target_link_libraries(imgui PUBLIC SDL2main SDL2)
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
