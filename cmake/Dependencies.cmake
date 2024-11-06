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
        GIT_TAG e3cb6f9d275836a1bd5d644f6573bbd6f871078c # main as of 2024-11-13
        SYSTEM
)

FetchContent_Declare(
        ut
        GIT_REPOSITORY https://github.com/boost-ext/ut.git
        GIT_TAG v2.0.1 # latest version as of 2023-12-19
        SYSTEM
)

FetchContent_MakeAvailable(opencmw-cpp gnuradio4 ut)
