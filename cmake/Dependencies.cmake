include(FetchContent)
include(DependenciesSHAs)

FetchContent_Declare(
        gnuradio4
        GIT_REPOSITORY https://github.com/fair-acc/gnuradio4.git
        GIT_TAG ${GIT_SHA_GNURADIO4}
        SYSTEM
)

FetchContent_Declare(
        opencmw-cpp
        GIT_REPOSITORY https://github.com/fair-acc/opencmw-cpp.git
        GIT_TAG ${GIT_SHA_OPENCMW_CPP}
        SYSTEM
)

FetchContent_Declare(
        ut
        GIT_REPOSITORY https://github.com/boost-ext/ut.git
        GIT_TAG ${GIT_SHA_UT}
        SYSTEM
)

FetchContent_MakeAvailable(opencmw-cpp gnuradio4 ut)

set_target_properties(exprtk_example0 PROPERTIES EXCLUDE_FROM_ALL ON)
set_target_properties(exprtk_example1 PROPERTIES EXCLUDE_FROM_ALL ON)
set_target_properties(exprtk_example2 PROPERTIES EXCLUDE_FROM_ALL ON)
set_target_properties(exprtk_example3 PROPERTIES EXCLUDE_FROM_ALL ON)
set_target_properties(exprtk_example4 PROPERTIES EXCLUDE_FROM_ALL ON)
