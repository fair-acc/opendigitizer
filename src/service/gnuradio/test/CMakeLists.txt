add_executable(qa_GnuRadioWorker qa_GnuRadioWorker.cpp)
target_link_libraries(
  qa_GnuRadioWorker
  PRIVATE ut
          gr-fourier
          gr-testing
          gnuradio-blocklib-core
          od_gnuradio_worker
          digitizer_common_utils
          client
          zmq)
add_test(NAME qa_GnuRadioWorker COMMAND qa_GnuRadioWorker)
