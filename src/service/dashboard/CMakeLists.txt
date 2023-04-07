cmrc_add_resource_library(FLOWGRAPH_DASHBOARD_ASSETS ALIAS assets::dashboard NAMESPACE dashboardFilesystem
                            ${CMAKE_CURRENT_SOURCE_DIR}/header.yaml
                            ${CMAKE_CURRENT_SOURCE_DIR}/dashboard.yaml
                            ${CMAKE_CURRENT_SOURCE_DIR}/dashboard.grc)

add_library(od_dashboard INTERFACE dashboardWorker.hpp)
target_include_directories(od_dashboard INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/.)
target_link_libraries(od_dashboard INTERFACE majordomo core project_options project_warnings assets::dashboard)
