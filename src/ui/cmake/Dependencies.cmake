include(FetchContent)
include(DependenciesSHAs)
include(CompileGr4Release)

# Enable Unicode planes for icons used in notifications
add_compile_definitions(IMGUI_USE_WCHAR32)

FetchContent_Declare(
  imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG v1.91.4-docking # latest version still compatible with imgui-node-editor # v1.91.9b-docking # latest as of
                          # 2025-06-02
  EXCLUDE_FROM_ALL SYSTEM)

# Enables 32 bit vertex indices for ImGui
add_compile_definitions("ImDrawIdx=unsigned int")

FetchContent_Declare(
  implot
  GIT_REPOSITORY https://github.com/epezent/implot.git
  GIT_TAG v0.16 # latest as of 2024-10-23
  EXCLUDE_FROM_ALL SYSTEM)

FetchContent_Declare(
  imgui-node-editor
  # Upstream https://github.com/thedmd/imgui-node-editor.git
  GIT_REPOSITORY https://github.com/fair-acc/imgui-node-editor.git
  GIT_TAG e0788005f3280af4a93cdd1a7d55c16bb4ff3088
  EXCLUDE_FROM_ALL SYSTEM)

FetchContent_Declare(
  plf_colony
  GIT_REPOSITORY https://github.com/mattreecebentley/plf_colony.git
  GIT_TAG 41e387e281b8323ca5584e79f67d632964b24bbf # v7.11
  EXCLUDE_FROM_ALL SYSTEM)

FetchContent_Declare(
  # needed to load images in ImGui
  stb
  GIT_REPOSITORY https://github.com/nothings/stb.git
  GIT_TAG 8b5f1f37b5b75829fc72d38e7b5d4bcbf8a26d55 # master from Sep 2022
  EXCLUDE_FROM_ALL SYSTEM)

# TODO use proper release once available
FetchContent_Declare(
  opencmw-cpp
  GIT_REPOSITORY https://github.com/fair-acc/opencmw-cpp.git
  GIT_TAG ${GIT_SHA_OPENCMW_CPP}
  EXCLUDE_FROM_ALL SYSTEM)

FetchContent_Declare(
  gnuradio4
  GIT_REPOSITORY https://github.com/fair-acc/gnuradio4.git
  GIT_TAG ${GIT_SHA_GNURADIO4}
  EXCLUDE_FROM_ALL SYSTEM)

FetchContent_MakeAvailable(
  imgui
  implot
  imgui-node-editor
  stb
  opencmw-cpp
  plf_colony
  gnuradio4)

od_set_release_flags_on_gnuradio_targets("${gnuradio4_SOURCE_DIR}")

if(NOT EMSCRIPTEN)
  find_package(SDL3 QUIET)

  # cmake-format: off
  if(SDL3_FOUND)
    message(STATUS "SDL3 found system-wide.")
  else()
    message(STATUS "SDL3 not found system-wide; falling back to FetchContent.")
    # set(SDL3_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
    set(SDL3_DISABLE_SDL3MAIN ON CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)

    FetchContent_Declare(
      sdl3
      OVERRIDE_FIND_PACKAGE
      GIT_REPOSITORY "https://github.com/libsdl-org/SDL"
      GIT_TAG release-3.2.16
      SYSTEM)

    FetchContent_MakeAvailable(sdl3)
  endif()
  # cmake-format: on

  find_package(SDL3 REQUIRED)
  find_package(OpenGL REQUIRED COMPONENTS OpenGL)
else() # Emscripten build
  find_package(SDL3 REQUIRED)
  find_package(OpenGL REQUIRED COMPONENTS OpenGL)
endif()
message(STATUS "SDL3_FOUND: ${SDL3_FOUND}")
message(STATUS "SDL3_INCLUDE_DIRS: ${SDL3_INCLUDE_DIRS}")
message(STATUS "SDL3_LIBRARIES: ${SDL3_LIBRARIES}")

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
    GIT_TAG v1.91.9 # Can be bumped independently from imgui version. Docs recommend they are not too far apart
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
endif()

list(
  APPEND
  IMGUI_SRCS
  ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp)

# imgui and implot are not CMake Projects, so we have to define their targets manually here
add_library(imgui OBJECT ${IMGUI_SRCS})

if(NOT EMSCRIPTEN) # emscripten comes with its own sdl, for native we have to specify the dependency
  target_link_libraries(imgui PUBLIC SDL3::SDL3 OpenGL::GL)
endif()

target_include_directories(imgui SYSTEM BEFORE PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)

if(ENABLE_IMGUI_TEST_ENGINE)
  target_compile_definitions(
    imgui
    PUBLIC IMGUI_ENABLE_TEST_ENGINE
           IMGUI_TEST_ENGINE_ENABLE_COROUTINE_STDTHREAD_IMPL=1
           IMGUI_APP_SDL3_GL3
           IMGUI_TEST_ENGINE_ENABLE_CAPTURE)

  target_compile_options(imgui PRIVATE -Wno-old-style-cast -Wno-deprecated-enum-enum-conversion -Wno-double-promotion)

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
target_compile_options(imgui-node-editor PRIVATE -Wno-deprecated-declarations)
target_include_directories(imgui-node-editor SYSTEM BEFORE PUBLIC ${imgui-node-editor_SOURCE_DIR})
target_link_libraries(imgui-node-editor PUBLIC imgui $<TARGET_OBJECTS:imgui>)

add_library(stb INTERFACE)
target_include_directories(stb SYSTEM INTERFACE ${stb_SOURCE_DIR})

add_library(plf_colony INTERFACE)
target_include_directories(plf_colony SYSTEM INTERFACE ${plf_colony_SOURCE_DIR})
