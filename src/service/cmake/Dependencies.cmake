include(FetchContent)
include(DependenciesSHAs)

FetchContent_Declare(
        opencmw-cpp
        GIT_REPOSITORY https://github.com/fair-acc/opencmw-cpp.git
        GIT_TAG ${GIT_SHA_OPENCMW_CPP}
        EXCLUDE_FROM_ALL SYSTEM)

find_package(gnuradio4 4.0.0 QUIET)
if (NOT gnuradio4_FOUND)
  message(STATUS "Pre-built gnuradio4 not found, fetching and building from source...")
  FetchContent_Declare(
    gnuradio4
    GIT_REPOSITORY https://github.com/fair-acc/gnuradio4.git
    GIT_TAG ${GIT_SHA_GNURADIO4}
                OVERRIDE_FIND_PACKAGE
    SYSTEM EXCLUDE_FROM_ALL)
  list(APPEND FETCH_CONTENT_SERVICE_TARGETS gnuradio4)
endif()

FetchContent_Declare(
        gr-digitizers
        GIT_REPOSITORY https://github.com/fair-acc/gr-digitizers.git
        GIT_TAG ${GIT_SHA_GR_DIGITIZERS}
        EXCLUDE_FROM_ALL SYSTEM
)

list(APPEND FETCH_CONTENT_SERVICE_TARGETS
  opencmw-cpp
  gr-digitizers
)

FetchContent_MakeAvailable(${FETCH_CONTENT_SERVICE_TARGETS})
