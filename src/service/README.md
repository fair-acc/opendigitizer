# opendigitizer service

This repository contains the opendigitizer service component.
It features a majordomo broker which allows access to the flowgraph and acquisition properties of the digitizer as well
as a HTTP interface to these properties and a WebAssembly based visualisation client.

## Building

```shell
cmake -S . -B build -DWASM_BUILD_DIR=`pwd`/../ui/build-wasm && cmake --build build
```

See the top-level README on how to use the resulting binary.
