include(FetchContent)

# TODO this should be a graph-prototype dependency only, as gr-digitizer blocks
# (e.g. PicoScope) would be loaded via plugins, at runtime
FetchContent_Declare(
        gr-digitizers
        GIT_REPOSITORY https://github.com/fair-acc/gr-digitizers.git
        GIT_TAG 787c96365ecce93ca08bbc688f3c2f0e79d7f53b # dev-prototype as of 2024-03-27
)

FetchContent_MakeAvailable(gr-digitizers)
