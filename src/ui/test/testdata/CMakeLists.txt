add_executable(generate_dashboard_testdata EXCLUDE_FROM_ALL generate_dashboard_testdata.cpp)
target_link_libraries(generate_dashboard_testdata PRIVATE gnuradio4::gnuradio-core)

add_custom_target(
  run_generate_dashboard_testdata
  COMMAND generate_dashboard_testdata "${CMAKE_CURRENT_SOURCE_DIR}/dashboards"
  DEPENDS generate_dashboard_testdata
  COMMENT "Generating dashboard test data in ${CMAKE_CURRENT_SOURCE_DIR}"
  VERBATIM)
