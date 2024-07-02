include(FetchContent)

# TODO this should be a gnuradio4 dependency only, as gr-digitizer blocks
# (e.g. PicoScope) would be loaded via plugins, at runtime
FetchContent_Declare(
        gr-digitizers
        GIT_REPOSITORY https://github.com/fair-acc/gr-digitizers.git
        GIT_TAG 0ad25d0d2947aac288cf76b1626816a5044b753e # dev-prototype as of 2024-04-25
        SYSTEM
)

FetchContent_MakeAvailable(gr-digitizers)
