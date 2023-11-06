add_executable(qa_GnuRadioWorker qa_GnuRadioWorker.cpp)
target_link_libraries(qa_GnuRadioWorker PRIVATE fmt ut od_gnuradio_worker client zmq)
add_test(NAME qa_GnuRadioWorker COMMAND qa_GnuRadioWorker)

add_executable(GnuRadioWorkerDemo GnuRadioWorker_demo.cpp)
target_link_libraries(GnuRadioWorkerDemo PRIVATE fmt od_gnuradio_worker client zmq)

if(NOT EMSCRIPTEN)
    target_link_libraries(GnuRadioWorkerDemo PRIVATE fair-picoscope)
endif()
