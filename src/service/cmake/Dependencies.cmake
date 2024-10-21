include(FetchContent)

FetchContent_Declare(
        opencmw-cpp
        GIT_REPOSITORY https://github.com/fair-acc/opencmw-cpp.git
        GIT_TAG bb8996babab2000a4ae3612ea146a551a96e59c4 # main as of 2024-10-18
        SYSTEM)

FetchContent_Declare(
        gnuradio4
        GIT_REPOSITORY https://github.com/fair-acc/gnuradio4.git
        GIT_TAG 5e7ecc561dfb35a6a8fd357f50d12efd09303c4f # main as of 2024-10-18
        SYSTEM)

FetchContent_Declare(
        gr-digitizers
        GIT_REPOSITORY https://github.com/fair-acc/gr-digitizers.git
        GIT_TAG b5df25c9552b473b32c47b80cca3b6379b79a6da # dev-prototype as of 2024-10-18
        SYSTEM
)

FetchContent_MakeAvailable(opencmw-cpp gr-digitizers gnuradio4)
