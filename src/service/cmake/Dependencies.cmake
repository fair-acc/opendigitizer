include(FetchContent)

# TODO this should be a graph-prototype dependency only, as gr-digitizer blocks
# (e.g. PicoScope) would be loaded via plugins, at runtime
FetchContent_Declare(
        gr-digitizers
        GIT_REPOSITORY https://github.com/fair-acc/gr-digitizers.git
        GIT_TAG d490abd53c59df61afaf19ebf88e73ae6b5c23d6 # dev-prototype as of 2024-02-23
)

FetchContent_MakeAvailable(gr-digitizers)
