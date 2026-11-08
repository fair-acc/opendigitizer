add_library(od-localisation INTERFACE)
target_include_directories(od-localisation INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
if(TARGET gnuradio4::gr-localisation)
  target_link_libraries(od-localisation INTERFACE gnuradio4::gr-localisation)
endif()

if(OPENDIGITIZER_ENABLE_TESTING AND NOT EMSCRIPTEN)
  add_subdirectory(test)
endif()
