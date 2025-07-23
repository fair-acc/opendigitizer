add_executable(qa_GnuRadioWorker qa_GnuRadioWorker.cpp)

message("COPY ${CMAKE_CURRENT_SOURCE_DIR}/../../demo_sslcert/demo_private.key DESTINATION ${CMAKE_CURRENT_BINARY_DIR}")
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/../../demo_sslcert/demo_private.key" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/")
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/../../demo_sslcert/demo_public.crt" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/")

target_link_directories(qa_GnuRadioWorker PRIVATE ${CMAKE_BINARY_DIR}/_deps/ngtcp2-install/lib)

target_link_libraries(
  qa_GnuRadioWorker
  PRIVATE ut
          gr-fourier
          gr-testing
          gnuradio-blocklib-core
          od_gnuradio_worker
          digitizer_common_utils
          services
          client
          od_rest
          zmq)

add_test(NAME qa_GnuRadioWorker COMMAND qa_GnuRadioWorker)
