include(FetchContent)

FetchContent_Declare(
        opencmw-cpp
        GIT_REPOSITORY https://github.com/fair-acc/opencmw-cpp.git
        GIT_TAG ac69eaf402259a6cf74865c583a632703f7cfcff # main as of 2024-02-27
)

FetchContent_Declare(
        graph-prototype
        GIT_REPOSITORY https://github.com/fair-acc/graph-prototype.git
        GIT_TAG fc33c1793eec5567a8291c885b610a6178a9c84d # main as of 2024-03-05
)

FetchContent_Declare(
        ut
        GIT_REPOSITORY https://github.com/boost-ext/ut.git
        GIT_TAG v2.0.1 # latest version as of 2023-12-19
)

FetchContent_MakeAvailable(opencmw-cpp graph-prototype ut)
