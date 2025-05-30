include(FetchContent)
include(DependenciesSHAs)

FetchContent_Declare(
        gnuradio4
        GIT_REPOSITORY https://github.com/fair-acc/gnuradio4.git
        GIT_TAG ${GIT_SHA_GNURADIO4}
        SYSTEM
        EXCLUDE_FROM_ALL
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
function(od_get_all_targets_impl dir)
    get_property(ts DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
    if(ts)
        foreach(t IN LISTS ts)
            set_property(GLOBAL APPEND PROPERTY OD_COLLECTED_TARGETS "${t}")
        endforeach()
    endif()

    get_property(subs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
    foreach(s IN LISTS subs)
        od_get_all_targets_impl("${s}")
    endforeach()
endfunction()

function(od_get_all_targets dir out_var)
    set_property(GLOBAL PROPERTY OD_COLLECTED_TARGETS "")
    od_get_all_targets_impl("${dir}")
    get_property(_res GLOBAL PROPERTY OD_COLLECTED_TARGETS)
    list(REMOVE_DUPLICATES _res)
    set(${out_var} "${_res}" PARENT_SCOPE)
endfunction()

function(od_set_release_flags_on_gnuradio_targets dir)
    od_get_all_targets("${dir}" _targets)
    message(STATUS "WEB-UI all targets:${_targets}")

    foreach(t IN LISTS _targets)
        if(t MATCHES "^(gnuradio-|gr-|Gr)")
            get_target_property(_type ${t} TYPE)
            if(NOT _type STREQUAL "INTERFACE_LIBRARY")
                target_compile_options(
                    ${t} PRIVATE
                    -O2 -g0 -DNDEBUG -ffunction-sections -fdata-sections
                )
            endif()
        endif()
    endforeach()
endfunction()

od_set_release_flags_on_gnuradio_targets("${gnuradio4_SOURCE_DIR}")

if(TARGET exprtk_example4)
set_target_properties(exprtk_example0 PROPERTIES EXCLUDE_FROM_ALL ON)
set_target_properties(exprtk_example1 PROPERTIES EXCLUDE_FROM_ALL ON)
set_target_properties(exprtk_example2 PROPERTIES EXCLUDE_FROM_ALL ON)
set_target_properties(exprtk_example3 PROPERTIES EXCLUDE_FROM_ALL ON)
set_target_properties(exprtk_example4 PROPERTIES EXCLUDE_FROM_ALL ON)
endif()
