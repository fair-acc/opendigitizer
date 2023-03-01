add_executable(acqWorkerTest acqWorker_test.cpp)
target_include_directories(acqWorkerTest INTERFACE ../include)
target_link_libraries(acqWorkerTest PRIVATE fmt ut od_acquisition client)
add_test(NAME acqWorker_test COMMAND acqWorkerTest)