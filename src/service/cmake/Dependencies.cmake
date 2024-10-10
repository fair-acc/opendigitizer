include(FetchContent)

# TODO this should be a gnuradio4 dependency only, as gr-digitizer blocks
# (e.g. PicoScope) would be loaded via plugins, at runtime
FetchContent_Declare(
        gr-digitizers
        GIT_REPOSITORY https://github.com/fair-acc/gr-digitizers.git
        GIT_TAG b5df25c9552b473b32c47b80cca3b6379b79a6da # dev-prototype as of 2024-10-18
        SYSTEM
)

FetchContent_MakeAvailable(gr-digitizers)
