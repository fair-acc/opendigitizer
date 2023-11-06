cmrc_add_resource_library( FLOWGRAPH_WORKER_ASSETS ALIAS assets::flowgraph NAMESPACE flowgraphFilesystem ${CMAKE_CURRENT_SOURCE_DIR}/flowgraph.grc )

add_library(od_flowgraph_worker INTERFACE flowgraphWorker.hpp)
target_include_directories(od_flowgraph_worker INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/.)
target_link_libraries(od_flowgraph_worker INTERFACE majordomo od_acquisition project_options project_warnings assets::flowgraph)
