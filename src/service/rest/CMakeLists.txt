cmrc_add_resource_library(
  OPENDIGITIZER_ASSETS
  ALIAS
  assets::rest
  NAMESPACE
  assets
  ${CMAKE_CURRENT_SOURCE_DIR}/assets/error.mustache
  ${CMAKE_CURRENT_SOURCE_DIR}/assets/main.css
  ${CMAKE_CURRENT_SOURCE_DIR}/assets/img/english.png
  ${CMAKE_CURRENT_SOURCE_DIR}/assets/img/german.png
  ${CMAKE_CURRENT_SOURCE_DIR}/assets/img/logo_b.png
  ${CMAKE_CURRENT_SOURCE_DIR}/assets/img/logo_b.svg
  ${CMAKE_CURRENT_SOURCE_DIR}/assets/img/logo_w.png
  ${CMAKE_CURRENT_SOURCE_DIR}/assets/img/logo_w.svg
  ${CMAKE_CURRENT_SOURCE_DIR}/assets/mustache/default.mustache
  ${CMAKE_CURRENT_SOURCE_DIR}/assets/mustache/ServicesList.mustache)

add_library(od_rest INTERFACE fileserverRestBackend.hpp)
target_include_directories(od_rest INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/.)
target_link_libraries(od_rest INTERFACE majordomo project_options assets::rest)
