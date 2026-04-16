include(FetchContent)
include(DependenciesSHAs)

FetchContent_Declare(
        opencmw-cpp
        GIT_REPOSITORY https://github.com/fair-acc/opencmw-cpp.git
        GIT_TAG ${GIT_SHA_OPENCMW_CPP}
        EXCLUDE_FROM_ALL SYSTEM)

find_package(gnuradio4 4.0.0 REQUIRED)

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

FetchContent_MakeAvailable(opencmw-cpp gr-digitizers)
