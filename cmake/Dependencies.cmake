include(FetchContent)

FetchContent_Declare(
        opencmw-cpp
        GIT_REPOSITORY https://github.com/fair-acc/opencmw-cpp.git
        GIT_TAG a7a7c5c319b93ddfaf160893665a011d2d88bff8 # main as of 2024-02-06
)

FetchContent_Declare(
        graph-prototype
        GIT_REPOSITORY https://github.com/fair-acc/graph-prototype.git
        GIT_TAG c9b2dd33dcfaf7ab56dc6b1c42b73f46e7dfc001 # main as of 2024-02-01
)

FetchContent_Declare(
        ut
        GIT_REPOSITORY https://github.com/boost-ext/ut.git
        GIT_TAG v2.0.1 # latest version as of 2023-12-19
)

FetchContent_MakeAvailable(opencmw-cpp graph-prototype ut)
