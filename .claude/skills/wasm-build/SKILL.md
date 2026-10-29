---
name: wasm-build
description: Configure and run the local Emscripten/WebAssembly build of the opendigitizer UI as CI does, either through the native superbuild or standalone from a preset. Use when reproducing a WASM CI failure or measuring the shipped .wasm.
---

# WASM build

CI builds the WASM UI inside the native `ci-gcc-*` lanes: the superbuild's `ui-wasm` ExternalProject runs `emcmake`
on `src/ui` against an installed Emscripten GR4 (`OD_WASM_GNURADIO4_PREFIX`, default
`/opt/gnuradio4-emscripten-static`). The result lands in `<tree>/ui-wasm/web`.

```bash
source "${EMSDK:-$HOME/git/emsdk}/emsdk_env.sh"
cmake --preset ci-gcc-release                    # superbuild: native + ui-wasm
cmake --build cmake-build-ci-gcc-release --target ui-wasm --parallel 2
```

Standalone, without the native build:

```bash
cmake --preset wasm-static-blocklibs             # GR4 from /opt/gnuradio4-emscripten-static
cmake --build --preset wasm-static-blocklibs
```

`wasm-release` instead loads the blocklibs as `SIDE_MODULE` plugins at runtime and expects `/opt/gnuradio4-emscripten`.

## Traps

- **No Emscripten GR4, no WASM.** Both paths need an installed Emscripten GR4 prefix (the GR4 `latest` release debs);
  `find_package` must not fall back to a native GR4.
- **`emcmake` on `PATH`.** The superbuild stops with `emcmake not found` unless `emsdk_env.sh` was sourced or
  `-DEMCMAKE_COMMAND=` is set.
- **No WASM tests.** The WASM presets set `OPENDIGITIZER_ENABLE_TESTING=OFF`; the UI tests run natively.
