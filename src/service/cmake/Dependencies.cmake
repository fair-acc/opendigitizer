include(FetchContent)

# TODO this should be a graph-prototype dependency only, as gr-digitizer blocks
# (e.g. PicoScope) would be loaded via plugins, at runtime
FetchContent_Declare(
        gr-digitizers
        GIT_REPOSITORY https://github.com/fair-acc/gr-digitizers.git
        GIT_TAG e8c3a49fd23a13d026c0177a1faee7427797f54b # dev-prototype as of 2024-03-07
)

FetchContent_MakeAvailable(gr-digitizers)
