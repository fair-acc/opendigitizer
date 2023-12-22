# define unit-tests

add_executable(qa_c_wrapper qa_c_wrapper.cpp)
target_include_directories(qa_c_wrapper INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/>
                                                  $<INSTALL_INTERFACE:include/>)
target_link_libraries(qa_c_wrapper PRIVATE fmt ut)
add_test(NAME qa_c_wrapper COMMAND qa_c_wrapper)
