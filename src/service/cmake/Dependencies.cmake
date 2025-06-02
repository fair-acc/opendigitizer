include(FetchContent)
include(DependenciesSHAs)

FetchContent_Declare(
        opencmw-cpp
        GIT_REPOSITORY https://github.com/fair-acc/opencmw-cpp.git
        GIT_TAG ${GIT_SHA_OPENCMW_CPP}
        EXCLUDE_FROM_ALL SYSTEM)

FetchContent_Declare(
        gnuradio4
        GIT_REPOSITORY https://github.com/fair-acc/gnuradio4.git
        GIT_TAG ${GIT_SHA_GNURADIO4}
        EXCLUDE_FROM_ALL SYSTEM)

FetchContent_Declare(
        gr-digitizers
        GIT_REPOSITORY https://github.com/fair-acc/gr-digitizers.git
        GIT_TAG ${GIT_SHA_GR_DIGITIZERS}
        EXCLUDE_FROM_ALL SYSTEM
)

FetchContent_MakeAvailable(opencmw-cpp gr-digitizers gnuradio4)
