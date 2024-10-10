include(FetchContent)

FetchContent_Declare(
        gnuradio4
        GIT_REPOSITORY https://github.com/fair-acc/gnuradio4.git
        GIT_TAG 582a759020b310ba2e4b47efd5c0f2984d886489 # main as of 2024-10-11
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
