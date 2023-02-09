include(FetchContent)

FetchContent_Declare(
        opencmw-cpp
        GIT_REPOSITORY https://github.com/fair-acc/opencmw-cpp.git
        GIT_TAG main # todo: use proper release once available
)

FetchContent_MakeAvailable(opencmw-cpp)

## cmake support in gr4 is not finished yet, use pkg-config directly
##find_package(gnuradio-runtime REQUIRED 4.0.0)
#find_package(PkgConfig REQUIRED)
## these calls create special `PkgConfig::<MODULE>` variables
#pkg_check_modules(gnuradio-runtime REQUIRED IMPORTED_TARGET GLOBAL gnuradio-runtime)
##pkg_check_modules(YOUR_PKG REQUIRED IMPORTED_TARGET ya-package)
