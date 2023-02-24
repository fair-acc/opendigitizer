include(FetchContent)

FetchContent_Declare(
        opencmw-cpp
        GIT_REPOSITORY https://github.com/fair-acc/opencmw-cpp.git
        GIT_TAG main # todo: use proper release once available
)

FetchContent_Declare(
        mdspan
        URL https://raw.githubusercontent.com/kokkos/mdspan/d70a74c6e2015b76b30db13e58c3a46ba0fce33d/mdspan.hpp
        DOWNLOAD_NO_EXTRACT YES
)

FetchContent_Declare(
        ut
        GIT_REPOSITORY https://github.com/boost-ext/ut.git
        GIT_TAG 265199e173b16a75670fae62fc2446b9dffad39e # head as of 2022-12-19
)

FetchContent_MakeAvailable(opencmw-cpp ut mdspan)

add_library(mdspan INTERFACE)
target_include_directories(mdspan INTERFACE ${mdspan_SOURCE_DIR})
