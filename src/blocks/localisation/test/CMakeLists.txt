add_executable(qa_DelayLocalisation qa_DelayLocalisation.cpp)
target_link_libraries(
  qa_DelayLocalisation
  PRIVATE od-localisation
          ut
          gnuradio4::gnuradio-core
          gnuradio4::gnuradio-blocklib-core
          gnuradio4::gr-testing
          gnuradio4::gr-basic
          gnuradio4::gr-localisation
          gnuradio4::gr-trigger
          gnuradio4::gr-filter
          gnuradio4::gr-fourier
          opendigitizer-options)
add_test(NAME qa_DelayLocalisation COMMAND qa_DelayLocalisation)
