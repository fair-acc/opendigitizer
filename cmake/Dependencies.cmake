include(FetchContent)
include(DependenciesSHAs)
include(CompileGr4Release)

FetchContent_Declare(
  gnuradio4
  GIT_REPOSITORY https://github.com/fair-acc/gnuradio4.git
  GIT_TAG ${GIT_SHA_GNURADIO4}
  SYSTEM EXCLUDE_FROM_ALL)

# Pre-declare zeromq before opencmw-cpp so our PATCH_COMMAND wins (first FetchContent_Declare takes precedence).
# zeromq's polling_util.hpp uses std::nothrow without #include <new>; Clang 22+ no longer provides it transitively.
FetchContent_Declare(
  zeromq
  GIT_REPOSITORY https://github.com/zeromq/libzmq.git
  GIT_TAG 7a7bfa10e6b0e99210ed9397369b59f9e69cef8e
  PATCH_COMMAND git apply --reverse --check ${CMAKE_CURRENT_LIST_DIR}/patches/zeromq-include-new.patch
                || git apply ${CMAKE_CURRENT_LIST_DIR}/patches/zeromq-include-new.patch
  SYSTEM EXCLUDE_FROM_ALL)

FetchContent_Declare(
  opencmw-cpp
  GIT_REPOSITORY https://github.com/fair-acc/opencmw-cpp.git
  GIT_TAG ${GIT_SHA_OPENCMW_CPP}
  PATCH_COMMAND git apply -p0 --reverse --check ${CMAKE_CURRENT_LIST_DIR}/patches/opencmw-cpp-apple-arm64.patch
                || git apply -p0 ${CMAKE_CURRENT_LIST_DIR}/patches/opencmw-cpp-apple-arm64.patch
  SYSTEM EXCLUDE_FROM_ALL)

FetchContent_Declare(
  ut
  GIT_REPOSITORY https://github.com/boost-ext/ut.git
  GIT_TAG ${GIT_SHA_UT}
  SYSTEM EXCLUDE_FROM_ALL)

FetchContent_MakeAvailable(opencmw-cpp gnuradio4 ut)

if(APPLE AND TARGET gr-http)
  target_link_libraries(gr-http INTERFACE "-framework CFNetwork")
endif()

od_set_release_flags_on_gnuradio_targets("${gnuradio4_SOURCE_DIR}")

if(TARGET exprtk_example4)
  set_target_properties(exprtk_example0 PROPERTIES EXCLUDE_FROM_ALL ON)
  set_target_properties(exprtk_example1 PROPERTIES EXCLUDE_FROM_ALL ON)
  set_target_properties(exprtk_example2 PROPERTIES EXCLUDE_FROM_ALL ON)
  set_target_properties(exprtk_example3 PROPERTIES EXCLUDE_FROM_ALL ON)
  set_target_properties(exprtk_example4 PROPERTIES EXCLUDE_FROM_ALL ON)
endif()
