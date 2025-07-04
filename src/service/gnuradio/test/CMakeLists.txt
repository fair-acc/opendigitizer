add_executable(qa_GnuRadioWorker qa_GnuRadioWorker.cpp)

message("COPY ${CMAKE_SOURCE_DIR}/src/service/demo_sslcert/demo_private.key DESTINATION ${CMAKE_CURRENT_BINARY_DIR}")
file(COPY "${CMAKE_SOURCE_DIR}/src/service/demo_sslcert/demo_private.key" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/")
file(COPY "${CMAKE_SOURCE_DIR}/src/service/demo_sslcert/demo_public.crt" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/")

target_link_directories(qa_GnuRadioWorker PRIVATE ${CMAKE_BINARY_DIR}/ngtcp2-install/lib)
target_link_libraries(qa_GnuRadioWorker PRIVATE ngtcp2)

find_package(OpenSSL REQUIRED)

target_link_libraries(
  qa_GnuRadioWorker
  PRIVATE ngtcp2
          ngtcp2_crypto_ossl # <<— add this (or gnutls/wolfssl)
          OpenSSL::SSL
          OpenSSL::Crypto # already in your link line, keep them here
          ut
          gr-fourier
          gr-testing
          gnuradio-blocklib-core
          od_gnuradio_worker
          digitizer_common_utils
          client
          services
          od_rest
          zmq)

add_test(NAME qa_GnuRadioWorker COMMAND qa_GnuRadioWorker)
