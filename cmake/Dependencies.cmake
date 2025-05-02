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

# Compile gnuradio4 targets with Release flags to keep the resulting binaries small.
# 1. find all targets in all subdirectories
# 2. select only gnuradio non-interface targets
# 3. set compiler options using target_compile_options
function(_get_all_targets_impl dir)
    get_property(ts DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
    if(ts)
        foreach(t IN LISTS ts)
            set_property(GLOBAL APPEND PROPERTY _ALL_COLLECTED_TARGETS "${t}")
        endforeach()
    endif()

    get_property(subs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
    foreach(s IN LISTS subs)
        _get_all_targets_impl("${s}")
    endforeach()
endfunction()

function(get_all_targets dir out_var)
    set_property(GLOBAL PROPERTY _ALL_COLLECTED_TARGETS "")
    _get_all_targets_impl("${dir}")
    get_property(_res GLOBAL PROPERTY _ALL_COLLECTED_TARGETS)
    set(${out_var} "${_res}" PARENT_SCOPE)
endfunction()


get_all_targets("${gnuradio4_SOURCE_DIR}" GNURADIO4_ALL_TARGETS)
foreach(t IN LISTS GNURADIO4_ALL_TARGETS)
    if(t MATCHES "^gnuradio-" OR t MATCHES "^gr-" OR t MATCHES "^Gr")
        get_target_property(_type ${t} TYPE)

        if(NOT _type STREQUAL "INTERFACE_LIBRARY")
            target_compile_options(${t} PRIVATE -O2 -g0 -DNDEBUG -ffunction-sections -fdata-sections)
        endif()
    endif()
endforeach()

set_target_properties(exprtk_example0 PROPERTIES EXCLUDE_FROM_ALL ON)
set_target_properties(exprtk_example1 PROPERTIES EXCLUDE_FROM_ALL ON)
set_target_properties(exprtk_example2 PROPERTIES EXCLUDE_FROM_ALL ON)
set_target_properties(exprtk_example3 PROPERTIES EXCLUDE_FROM_ALL ON)
set_target_properties(exprtk_example4 PROPERTIES EXCLUDE_FROM_ALL ON)
