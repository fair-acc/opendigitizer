add_library(od-video INTERFACE)
target_include_directories(od-video INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

if(NOT EMSCRIPTEN)
  include(FetchContent)
  include(${CMAKE_CURRENT_LIST_DIR}/../../../cmake/DependenciesSHAs.cmake)
  FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG ${GIT_SHA_STB}
    EXCLUDE_FROM_ALL SYSTEM)
  FetchContent_MakeAvailable(stb)
  # stb's decoder is compiled static (file-local) here, so it cannot clash with other stb_image implementations
  add_library(od-video-mjpeg STATIC src/Mjpeg.cpp)
  target_include_directories(od-video-mjpeg SYSTEM PRIVATE ${stb_SOURCE_DIR})
  target_include_directories(od-video-mjpeg PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_link_libraries(od-video-mjpeg PRIVATE opendigitizer-options)
  target_compile_options(od-video-mjpeg PRIVATE -O3) # decoding paces capture, also in Debug builds
  target_link_libraries(od-video INTERFACE od-video-mjpeg)
  add_executable(ex_test_card_y4m src/ex_test_card_y4m.cpp)
  target_link_libraries(ex_test_card_y4m PRIVATE od-video opendigitizer-options)
  add_executable(ex_camera_terminal src/ex_camera_terminal.cpp)
  target_link_libraries(
    ex_camera_terminal
    PRIVATE od-video
            gnuradio4::gnuradio-core
            gnuradio4::gnuradio-blocklib-core
            opendigitizer-options)
  if(OPENDIGITIZER_ENABLE_TESTING)
    add_subdirectory(test)
  endif()
endif()
