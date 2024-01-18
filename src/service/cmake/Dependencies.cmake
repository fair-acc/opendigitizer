include(FetchContent)

# TODO this should be a graph-prototype dependency only, as gr-digitizer blocks
# (e.g. PicoScope) would be loaded via plugins, at runtime
FetchContent_Declare(
        gr-digitizers
        GIT_REPOSITORY https://github.com/fair-acc/gr-digitizers.git
        GIT_TAG 68b8bfc2880a4eac9ce978458be63729d9d88c01 # latest dev-prototype as of 2014-01-18
)

FetchContent_MakeAvailable(gr-digitizers)
