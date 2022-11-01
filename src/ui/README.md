# opendigitizer UI

This is the UI part of the opendigitizer project.

## Building

### Building (wasm)

Install the emscripten compiler according to the documentation and build this subproject part of the repository like so

```shell
emcmake cmake -S . -B build-wasm -DCMAKE_BUILD_TYPE=Debug && (cd build-wasm && EM_CACHE="$TMP" emmake make -j)
```
Note: `cmake --build` is broken with em(c)make, hence the cd + make call.

To test, launch a python http server and navigate your browser to http://localhost:8000;
```shell
(cd build-wasm/ && make serve)
```

## Building for native

This is not yet implemented, but in general native compilation should be supported. This is mainly adding some macros
which switch between wasm and native implementations of the dependencies.

```shell
cmake -S . -B build-native -DCMAKE_BUILD_TYPE=Debug && cmake --build build-native -j)
```

The application can then be launched via
```shell
./build-native/opendigitizer-ui
```
