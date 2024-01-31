include(FetchContent)

# TODO this should be a graph-prototype dependency only, as gr-digitizer blocks
# (e.g. PicoScope) would be loaded via plugins, at runtime
FetchContent_Declare(
        gr-digitizers
        GIT_REPOSITORY https://github.com/fair-acc/gr-digitizers.git
        GIT_TAG 1a09de8345d40720911049c8c2998470276a5cc9 # dev-prototype as of 2024-02-01
)

FetchContent_MakeAvailable(gr-digitizers)
