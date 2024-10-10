include(FetchContent)

# TODO this should be a gnuradio4 dependency only, as gr-digitizer blocks
# (e.g. PicoScope) would be loaded via plugins, at runtime
FetchContent_Declare(
        gr-digitizers
        GIT_REPOSITORY https://github.com/fair-acc/gr-digitizers.git
        GIT_TAG 582a759020b310ba2e4b47efd5c0f2984d886489 # main as of 2024-10-11
        SYSTEM
)

FetchContent_MakeAvailable(gr-digitizers)
