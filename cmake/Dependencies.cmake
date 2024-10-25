include(FetchContent)

FetchContent_Declare(
        gnuradio4
        GIT_REPOSITORY https://github.com/fair-acc/gnuradio4.git
        GIT_TAG 2465afbdafc1bd98bb8a0aa232134893bb19991a # main as of 2024-10-28
        SYSTEM
)

FetchContent_Declare(
        opencmw-cpp
        GIT_REPOSITORY https://github.com/fair-acc/opencmw-cpp.git
        GIT_TAG bb8996babab2000a4ae3612ea146a551a96e59c4 # main as of 2024-10-18
        SYSTEM
)

FetchContent_Declare(
        ut
        GIT_REPOSITORY https://github.com/boost-ext/ut.git
        GIT_TAG v2.0.1 # latest version as of 2023-12-19
        SYSTEM
)

FetchContent_MakeAvailable(opencmw-cpp gnuradio4 ut)
