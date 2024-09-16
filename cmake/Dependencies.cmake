include(FetchContent)

FetchContent_Declare(
        gnuradio4
        GIT_REPOSITORY https://github.com/fair-acc/gnuradio4.git
        GIT_TAG d2584d0afe2b9f4f952513a28a60576ed5bd6416 # main as of 2024-10-04
        SYSTEM
)

FetchContent_Declare(
        opencmw-cpp
        GIT_REPOSITORY https://github.com/fair-acc/opencmw-cpp.git
        GIT_TAG b10bf280023775a6a65cd9f6138ed004e8140dc6 # main as of 2024-10-02
        SYSTEM
)

FetchContent_Declare(
        ut
        GIT_REPOSITORY https://github.com/boost-ext/ut.git
        GIT_TAG v2.0.1 # latest version as of 2023-12-19
        SYSTEM
)

FetchContent_MakeAvailable(opencmw-cpp gnuradio4 ut)
