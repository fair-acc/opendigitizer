include(FetchContent)

FetchContent_Declare(
        gnuradio4
        GIT_REPOSITORY https://github.com/fair-acc/gnuradio4.git
        GIT_TAG f00cba2c81422f143c9d48552921ec63bbaef652 # main as of 2024-05-29
)

FetchContent_Declare(
        opencmw-cpp
        GIT_REPOSITORY https://github.com/fair-acc/opencmw-cpp.git
        GIT_TAG 0fb3758c3ffe7707aa5e0bd2ad25f9e8fb19f79d # main as of 2024-04-26
)

FetchContent_Declare(
        ut
        GIT_REPOSITORY https://github.com/boost-ext/ut.git
        GIT_TAG v2.0.1 # latest version as of 2023-12-19
)

FetchContent_MakeAvailable(opencmw-cpp gnuradio4 ut)
