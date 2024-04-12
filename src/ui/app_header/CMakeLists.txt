add_library(
    app_header INTERFACE
    "fair_header.hpp"
    PopupMenu.hpp
)
target_include_directories(app_header INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(app_header INTERFACE stb fmt)
