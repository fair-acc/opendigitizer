# define unit-tests

add_executable(qa_c_resource qa_c_resource.cpp)
target_include_directories(qa_c_resource INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/>
                                                   $<INSTALL_INTERFACE:include/>)
add_executable(qa_DeviceNameHelper qa_DeviceNameHelper.cpp)
target_include_directories(qa_DeviceNameHelper INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/>
                                                         $<INSTALL_INTERFACE:include/>)

target_link_libraries(qa_c_resource PRIVATE ut opendigitizer-options)
add_test(NAME qa_c_resource COMMAND qa_c_resource)

target_link_libraries(qa_DeviceNameHelper PRIVATE ut opendigitizer-options)
add_test(NAME qa_DeviceNameHelper COMMAND qa_DeviceNameHelper)

add_executable(qa_PeriodicTimer qa_PeriodicTimer.cpp)
target_include_directories(qa_PeriodicTimer INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/>
                                                      $<INSTALL_INTERFACE:include/>)
add_test(NAME qa_PeriodicTimer COMMAND qa_PeriodicTimer)
target_link_libraries(qa_PeriodicTimer PRIVATE ut gnuradio4::gnuradio-core opendigitizer-options)

add_executable(qa_Xoshiro256pp qa_Xoshiro256pp.cpp)
target_link_libraries(qa_Xoshiro256pp PRIVATE ut opendigitizer-options)
add_test(NAME qa_Xoshiro256pp COMMAND qa_Xoshiro256pp)
