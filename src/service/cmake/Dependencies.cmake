include(FetchContent)

# TODO this should be a graph-prototype dependency only, as gr-digitizer blocks
# (e.g. PicoScope) would be loaded via plugins, at runtime
FetchContent_Declare(
        gr-digitizers
        GIT_REPOSITORY https://github.com/fair-acc/gr-digitizers.git
        GIT_TAG 8b261c133f9560f326537fc794641aa9ee4119c5 # dev-prototype as of 2024-02-21
)

FetchContent_MakeAvailable(gr-digitizers)
