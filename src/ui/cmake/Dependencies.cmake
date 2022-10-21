include(FetchContent)

FetchContent_Declare(
    imgui
    GIT_REPOSITORY  https://github.com/ocornut/imgui.git
    GIT_TAG         v1.88
)

FetchContent_Declare(
    implot
    GIT_REPOSITORY  https://github.com/epezent/implot.git
    GIT_TAG         master #v0.13
)

FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY  https://github.com/nlohmann/json.git
    GIT_TAG         69d744867f8847c91a126fa25e9a6a3d67b3be41 #v3.11.1
)

FetchContent_MakeAvailable(nlohmann_json imgui implot)

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
target_include_directories(
    imgui BEFORE
    PUBLIC
        ${CURRENT_SOURCE_DIR}/imgui # include the modified imconfig header
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

