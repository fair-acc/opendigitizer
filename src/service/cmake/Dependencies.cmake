include(FetchContent)

# TODO this should be a gnuradio4 dependency only, as gr-digitizer blocks
# (e.g. PicoScope) would be loaded via plugins, at runtime
FetchContent_Declare(
        gr-digitizers
        GIT_REPOSITORY https://github.com/fair-acc/gr-digitizers.git
        GIT_TAG ba28ef2be685b84d917843b8c790e9156ca5e5e6 # dev-prototype as of 2024-10-09
        SYSTEM
)

FetchContent_MakeAvailable(gr-digitizers)
