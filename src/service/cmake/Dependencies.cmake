include(FetchContent)

# TODO use proper releases once available

FetchContent_Declare(
        opencmw-cpp
        GIT_REPOSITORY https://github.com/fair-acc/opencmw-cpp.git
        GIT_TAG a245ee17adaa61f530d95883da38b68fb53f4de8
)

# TODO this should be a graph-prototype dependency only, as gr-digitizer blocks
# (e.g. PicoScope) would be loaded via plugins, at runtime
FetchContent_Declare(
        gr-digitizers
        GIT_REPOSITORY https://github.com/fair-acc/gr-digitizers.git
        GIT_TAG fe402e92b7e4cfc8ffc745f2349ab26b941fc8b9 # latest dev-prototype as of 2023-11-09
)

FetchContent_Declare(
        ut
        GIT_REPOSITORY https://github.com/boost-ext/ut.git
        GIT_TAG 265199e173b16a75670fae62fc2446b9dffad39e # head as of 2022-12-19
)

FetchContent_MakeAvailable(opencmw-cpp gr-digitizers ut)
