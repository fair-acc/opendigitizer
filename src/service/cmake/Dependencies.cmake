include(FetchContent)

FetchContent_Declare(
        opencmw-cpp
        GIT_REPOSITORY https://github.com/fair-acc/opencmw-cpp.git
        GIT_TAG main # todo: use proper release once available
)

FetchContent_MakeAvailable(opencmw-cpp)
