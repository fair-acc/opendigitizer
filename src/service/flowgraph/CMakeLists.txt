cmrc_add_resource_library( FLOWGRAPH_WORKER_ASSETS ALIAS assets::flowgraph NAMESPACE flowgraphFilesystem ${CMAKE_CURRENT_SOURCE_DIR}/flowgraph.grc )

add_library(od_flowgraph INTERFACE flowgraphWorker.hpp)
target_include_directories(od_flowgraph INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/.)
target_link_libraries(od_flowgraph INTERFACE majordomo project_options project_warnings assets::flowgraph)
