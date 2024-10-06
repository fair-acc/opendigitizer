include(FetchContent)

# Enable Unicode planes for icons used in notifications
add_compile_definitions(IMGUI_USE_WCHAR32)

FetchContent_Declare(
  imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG v1.91.0 # latest as of 2024-08-15
  SYSTEM)

# Enables 32 bit vertex indices for ImGui
add_compile_definitions("ImDrawIdx=unsigned int")

FetchContent_Declare(
  implot
  GIT_REPOSITORY https://github.com/epezent/implot.git
  GIT_TAG v0.16 # latest as of 2023-12-19
  SYSTEM)

FetchContent_Declare(
  imgui-node-editor
  # GIT_REPOSITORY  https://github.com/thedmd/imgui-node-editor.git GIT_TAG         v0.9.3 # latest as of 2023-12-19
  # Temporary until https://github.com/thedmd/imgui-node-editor/pull/291 is merged
  GIT_REPOSITORY https://github.com/ivan-cukic/wip-fork-imgui-node-editor.git
  GIT_TAG 2e4740361b7bddb924807f6d5be64818b72bf15e
  SYSTEM)

FetchContent_Declare(
  plf_colony
  GIT_REPOSITORY https://github.com/mattreecebentley/plf_colony.git
  GIT_TAG 41e387e281b8323ca5584e79f67d632964b24bbf # v7.11
  SYSTEM)

FetchContent_Declare(
  # needed to load images in ImGui
  stb
  GIT_REPOSITORY https://github.com/nothings/stb.git
  GIT_TAG 8b5f1f37b5b75829fc72d38e7b5d4bcbf8a26d55 # master from Sep 2022
  SYSTEM)

# TODO use proper release once available
FetchContent_Declare(
  opencmw-cpp
  GIT_REPOSITORY https://github.com/fair-acc/opencmw-cpp.git
  GIT_TAG b10bf280023775a6a65cd9f6138ed004e8140dc6 # main as of 2024-10-02
  SYSTEM)

FetchContent_Declare(
  gnuradio4
  GIT_REPOSITORY https://github.com/fair-acc/gnuradio4.git
  GIT_TAG d2584d0afe2b9f4f952513a28a60576ed5bd6416 # main as of 2024-10-04
  SYSTEM)

FetchContent_MakeAvailable(
  imgui
  implot
  imgui-node-editor
  stb
  opencmw-cpp
  plf_colony
  gnuradio4)

if(NOT EMSCRIPTEN)
  find_package(SDL2 REQUIRED)
  find_package(OpenGL REQUIRED COMPONENTS OpenGL)
  FetchContent_Declare(
    sdl2
    OVERRIDE_FIND_PACKAGE
    GIT_REPOSITORY "https://github.com/libsdl-org/SDL"
    GIT_TAG release-2.28.2
    SYSTEM)
  FetchContent_MakeAvailable(sdl2)
endif()

set(IMGUI_SRCS
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp)

if(ENABLE_IMGUI_TEST_ENGINE)
  # Test engine requires a special build of ImGui with special code for testing, should not be used in production
  if(NOT OPENDIGITIZER_ENABLE_TESTING)
    message(FATAL_ERROR "ENABLE_IMGUI_TEST_ENGINE does not make sense without OPENDIGITIZER_ENABLE_TESTING")
  endif()

  FetchContent_Declare(
    imgui_test_engine
    GIT_REPOSITORY https://github.com/ocornut/imgui_test_engine.git
    GIT_TAG v1.91.0 # Can be bumped independently from imgui version. Docs recommend they are not too far apart
    SYSTEM)

  FetchContent_MakeAvailable(imgui_test_engine)

  # The recommended way to build the test engine is to bake it inside imgui, instead of a separate target as there's a
  # bidirectional dependency between imgui and the test engine Hence this is guarded by ENABLE_IMGUI_TEST_ENGINE, not to
  # be enabled in production.
  set(IMGUI_SRCS
      ${IMGUI_SRCS}
      ${imgui_test_engine_SOURCE_DIR}/imgui_test_engine/imgui_capture_tool.cpp
      ${imgui_test_engine_SOURCE_DIR}/imgui_test_engine/imgui_te_context.cpp
      ${imgui_test_engine_SOURCE_DIR}/imgui_test_engine/imgui_te_coroutine.cpp
      ${imgui_test_engine_SOURCE_DIR}/imgui_test_engine/imgui_te_engine.cpp
      ${imgui_test_engine_SOURCE_DIR}/imgui_test_engine/imgui_te_exporters.cpp
      ${imgui_test_engine_SOURCE_DIR}/imgui_test_engine/imgui_te_perftool.cpp
      ${imgui_test_engine_SOURCE_DIR}/imgui_test_engine/imgui_te_ui.cpp
      ${imgui_test_engine_SOURCE_DIR}/imgui_test_engine/imgui_te_utils.cpp
      ${imgui_test_engine_SOURCE_DIR}/shared/imgui_app.cpp)
else()
  # Only link the backends if we're not building imgui_test_engine, as it already builds them
  set(IMGUI_SRCS ${IMGUI_SRCS} ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
                 ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp)
endif()

# imgui and implot are not CMake Projects, so we have to define their targets manually here
add_library(imgui OBJECT ${IMGUI_SRCS})

if(NOT EMSCRIPTEN) # emscripten comes with its own sdl, for native we have to specify the dependency
  target_link_libraries(imgui PUBLIC SDL2::SDL2 OpenGL::GL)
endif()

target_include_directories(imgui SYSTEM BEFORE PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)

if(ENABLE_IMGUI_TEST_ENGINE)
  target_compile_definitions(imgui PUBLIC IMGUI_ENABLE_TEST_ENGINE IMGUI_TEST_ENGINE_ENABLE_COROUTINE_STDTHREAD_IMPL=1
                                          IMGUI_APP_SDL2_GL3)

  target_include_directories(imgui PUBLIC ${imgui_test_engine_SOURCE_DIR})
endif()

add_library(implot OBJECT ${implot_SOURCE_DIR}/implot_demo.cpp ${implot_SOURCE_DIR}/implot_items.cpp
                          ${implot_SOURCE_DIR}/implot.cpp)
target_include_directories(implot SYSTEM BEFORE PUBLIC ${implot_SOURCE_DIR})
target_link_libraries(implot PUBLIC imgui $<TARGET_OBJECTS:imgui>)

add_library(
  imgui-node-editor OBJECT
  ${imgui-node-editor_SOURCE_DIR}/imgui_node_editor.cpp
  ${imgui-node-editor_SOURCE_DIR}/imgui_canvas.cpp
  ${imgui-node-editor_SOURCE_DIR}/imgui_node_editor_api.cpp
  ${imgui-node-editor_SOURCE_DIR}/crude_json.cpp)
target_include_directories(imgui-node-editor SYSTEM BEFORE PUBLIC ${imgui-node-editor_SOURCE_DIR})
target_link_libraries(imgui-node-editor PUBLIC imgui $<TARGET_OBJECTS:imgui>)

add_library(stb INTERFACE)
target_include_directories(stb SYSTEM INTERFACE ${stb_SOURCE_DIR})

add_library(plf_colony INTERFACE)
target_include_directories(plf_colony SYSTEM INTERFACE ${plf_colony_SOURCE_DIR})
