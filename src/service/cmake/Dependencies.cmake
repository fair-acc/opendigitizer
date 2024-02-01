include(FetchContent)

# TODO this should be a graph-prototype dependency only, as gr-digitizer blocks
# (e.g. PicoScope) would be loaded via plugins, at runtime
FetchContent_Declare(
        gr-digitizers
        GIT_REPOSITORY https://github.com/fair-acc/gr-digitizers.git
        GIT_TAG aff5eae3f55eb9a9fcfbf841b08921d5530078b2 # dev-prototype as of 2024-02-01
)

FetchContent_MakeAvailable(gr-digitizers)
