add_executable(qa_GnuRadioWorker qa_GnuRadioWorker.cpp)

message("COPY ${CMAKE_CURRENT_SOURCE_DIR}/../../demo_sslcert/demo_private.key DESTINATION ${CMAKE_CURRENT_BINARY_DIR}")
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/../../demo_sslcert/demo_private.key" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/")
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/../../demo_sslcert/demo_public.crt" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/")

target_link_libraries(
  qa_GnuRadioWorker
  PRIVATE ut
          gnuradio4::GrFourierBlocksShared
          gnuradio4::GrTestingBlocksShared
          gnuradio4::GrBasicBlocksShared
          gnuradio4::gnuradio-blocklib-core
          od_gnuradio_worker
          digitizer_common_utils
          client
          od_rest
          zmq)

add_test(NAME qa_GnuRadioWorker COMMAND qa_GnuRadioWorker)
set_tests_properties(qa_GnuRadioWorker PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
