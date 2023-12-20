include(FetchContent)

# TODO this should be a graph-prototype dependency only, as gr-digitizer blocks
# (e.g. PicoScope) would be loaded via plugins, at runtime
FetchContent_Declare(
        gr-digitizers
        GIT_REPOSITORY https://github.com/fair-acc/gr-digitizers.git
        GIT_TAG fe402e92b7e4cfc8ffc745f2349ab26b941fc8b9 # latest dev-prototype as of 2023-11-09
)

FetchContent_MakeAvailable(gr-digitizers)
