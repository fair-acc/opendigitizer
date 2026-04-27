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

# gr-digitizers still links against legacy non-namespaced targets.
# Provide compatibility aliases to the installed gnuradio4 imported targets.
if(TARGET gnuradio4::gnuradio-core)
        if(NOT TARGET gnuradio-core)
                add_library(gnuradio-core INTERFACE IMPORTED GLOBAL)
                target_link_libraries(gnuradio-core INTERFACE gnuradio4::gnuradio-core)
        endif()
        if(NOT TARGET gnuradio-algorithm)
                add_library(gnuradio-algorithm INTERFACE IMPORTED GLOBAL)
                target_link_libraries(gnuradio-algorithm INTERFACE gnuradio4::gnuradio-core)
        endif()
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
