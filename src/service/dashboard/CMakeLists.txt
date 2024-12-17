cmrc_add_resource_library(
  FLOWGRAPH_DASHBOARD_ASSETS
  ALIAS
  assets::dashboard
  NAMESPACE
  dashboardFilesystem
  ${CMAKE_CURRENT_SOURCE_DIR}/defaultDashboards/RemoteStream/header
  ${CMAKE_CURRENT_SOURCE_DIR}/defaultDashboards/RemoteStream/dashboard
  ${CMAKE_CURRENT_SOURCE_DIR}/defaultDashboards/RemoteStream/flowgraph
  ${CMAKE_CURRENT_SOURCE_DIR}/defaultDashboards/RemoteDataSet/header
  ${CMAKE_CURRENT_SOURCE_DIR}/defaultDashboards/RemoteDataSet/dashboard
  ${CMAKE_CURRENT_SOURCE_DIR}/defaultDashboards/RemoteDataSet/flowgraph
  ${CMAKE_CURRENT_SOURCE_DIR}/defaultDashboards/RemoteTags/header
  ${CMAKE_CURRENT_SOURCE_DIR}/defaultDashboards/RemoteTags/dashboard
  ${CMAKE_CURRENT_SOURCE_DIR}/defaultDashboards/RemoteTags/flowgraph)

add_library(od_dashboard_worker INTERFACE dashboardWorker.hpp)
target_include_directories(od_dashboard_worker INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/.)
target_link_libraries(
  od_dashboard_worker
  INTERFACE majordomo
            core
            project_options
            assets::dashboard)
