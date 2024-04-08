include(FetchContent)

# TODO this should be a graph-prototype dependency only, as gr-digitizer blocks
# (e.g. PicoScope) would be loaded via plugins, at runtime
FetchContent_Declare(
        gr-digitizers
        GIT_REPOSITORY https://github.com/fair-acc/gr-digitizers.git
        GIT_TAG 417f997f696f2bdaadfaf41ac7d8a339a95481be # dev-prototype as of 2024-04-05
)

FetchContent_MakeAvailable(gr-digitizers)
