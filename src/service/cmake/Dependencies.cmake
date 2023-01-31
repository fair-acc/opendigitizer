include(FetchContent)

FetchContent_Declare(
        opencmw-cpp
        GIT_REPOSITORY https://github.com/fair-acc/opencmw-cpp.git
        GIT_TAG main # todo: use proper release once available
)

FetchContent_Declare(
        ut
        GIT_REPOSITORY https://github.com/boost-ext/ut.git
        GIT_TAG 265199e173b16a75670fae62fc2446b9dffad39e # head as of 2022-12-19
)

FetchContent_MakeAvailable(opencmw-cpp ut)

## cmake support in gr4 is not finished yet, use pkg-config directly
##find_package(gnuradio-runtime REQUIRED 4.0.0)
#find_package(PkgConfig REQUIRED)
## these calls create special `PkgConfig::<MODULE>` variables
#pkg_check_modules(gnuradio-runtime REQUIRED IMPORTED_TARGET GLOBAL gnuradio-runtime)
##pkg_check_modules(YOUR_PKG REQUIRED IMPORTED_TARGET ya-package)
