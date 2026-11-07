function(od_video_test name)
  add_executable(${name} ${name}.cpp)
  target_link_libraries(
    ${name}
    PRIVATE od-video
            ut
            gnuradio4::gnuradio-core
            gnuradio4::gnuradio-blocklib-core
            gnuradio4::gr-testing
            opendigitizer-options)
  add_test(NAME ${name} COMMAND ${name})
endfunction()

od_video_test(qa_CameraSource)
target_include_directories(qa_CameraSource SYSTEM PRIVATE ${stb_SOURCE_DIR})
od_video_test(qa_CameraHardware)
set_tests_properties(
  qa_CameraHardware
  PROPERTIES RESOURCE_LOCK
             camera_hardware
             LABELS
             hardware
             SKIP_RETURN_CODE
             77
             RUN_SERIAL
             TRUE)
od_video_test(qa_ImageTransforms)
target_link_libraries(qa_ImageTransforms PRIVATE gnuradio4::gr-localisation)
