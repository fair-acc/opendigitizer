add_library(od_gnuradio_worker INTERFACE GnuRadioWorker.hpp)
target_include_directories(od_gnuradio_worker INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/.)
target_link_libraries(
  od_gnuradio_worker
  INTERFACE od_acquisition
            majordomo
            disruptor
            gr-basic
            gr-testing
            yaml-cpp::yaml-cpp
            project_options)

add_subdirectory(test)

add_executable(cli-signal-subscribe cli-signal-subscribe.cpp)
target_link_libraries(cli-signal-subscribe PRIVATE core client od_acquisition fmt::fmt project_options)