include(FetchContent)

FetchContent_Declare(
        opencmw-cpp
        GIT_REPOSITORY https://github.com/fair-acc/opencmw-cpp.git
        GIT_TAG c7236c423f4f1f4de8a762c1e946f6ad76cd0a8d # main as of 2024-03-13
)

FetchContent_Declare(
        graph-prototype
        GIT_REPOSITORY https://github.com/fair-acc/graph-prototype.git
        GIT_TAG 3cb5c2aff7046bc3760ccd8f8b701cbde361ecfd # main as of 2024-04-05
)

FetchContent_Declare(
        ut
        GIT_REPOSITORY https://github.com/boost-ext/ut.git
        GIT_TAG v2.0.1 # latest version as of 2023-12-19
)

FetchContent_MakeAvailable(opencmw-cpp graph-prototype ut)
