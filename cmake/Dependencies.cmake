include(FetchContent)
include(DependenciesSHAs)
include(CompileGr4Release)

find_package(gnuradio4 4.0.0 QUIET)
if (NOT gnuradio4_FOUND)
  message(STATUS "Pre-built gnuradio4 not found, fetching and building from source...")
  FetchContent_Declare(
    gnuradio4
    GIT_REPOSITORY https://github.com/fair-acc/gnuradio4.git
    GIT_TAG ${GIT_SHA_GNURADIO4}
    OVERRIDE_FIND_PACKAGE
    SYSTEM EXCLUDE_FROM_ALL)
  list(APPEND FETCH_CONTENT_MAIN_TARGETS gnuradio4)
endif()

FetchContent_Declare(
  opencmw-cpp
  GIT_REPOSITORY https://github.com/fair-acc/opencmw-cpp.git
  GIT_TAG ${GIT_SHA_OPENCMW_CPP}
  SYSTEM EXCLUDE_FROM_ALL)

FetchContent_Declare(
  ut
  GIT_REPOSITORY https://github.com/boost-ext/ut.git
  GIT_TAG ${GIT_SHA_UT}
  SYSTEM EXCLUDE_FROM_ALL)

list(APPEND FETCH_CONTENT_MAIN_TARGETS
  opencmw-cpp
  ut
)

FetchContent_MakeAvailable(${FETCH_CONTENT_MAIN_TARGETS})

od_set_release_flags_on_gnuradio_targets("${gnuradio4_SOURCE_DIR}")

if(TARGET exprtk_example4)
  set_target_properties(exprtk_example0 PROPERTIES EXCLUDE_FROM_ALL ON)
  set_target_properties(exprtk_example1 PROPERTIES EXCLUDE_FROM_ALL ON)
  set_target_properties(exprtk_example2 PROPERTIES EXCLUDE_FROM_ALL ON)
  set_target_properties(exprtk_example3 PROPERTIES EXCLUDE_FROM_ALL ON)
  set_target_properties(exprtk_example4 PROPERTIES EXCLUDE_FROM_ALL ON)
endif()
