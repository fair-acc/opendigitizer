add_executable(qa_GnuRadioWorker qa_GnuRadioWorker.cpp)
target_link_libraries(
  qa_GnuRadioWorker
  PRIVATE fmt
          ut
          gr-testing
          od_gnuradio_worker
          client
          zmq)
add_test(NAME qa_GnuRadioWorker COMMAND qa_GnuRadioWorker)
