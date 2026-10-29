# CLAUDE.md (AGENTS.md) — opendigitizer

> For LLM coding agents. opendigitizer is a GR4 application, and GR4's rules apply unchanged.
> This file adds only what differs here.

## 1 · Rules

GR4's [`CLAUDE.md`](https://github.com/fair-acc/gnuradio4/blob/main/CLAUDE.md) is authoritative: the working agreement,
design, naming, layout, documentation, C++23, GR4 conventions, testing, commits and the pre-submit checklist. Read it
first.

The local copies below are taken verbatim from GR4 `bd3d68bb` (2026-10-29) and are re-synced with each GR4 bump:

| File                            | Purpose                                                 |
| ------------------------------- | ------------------------------------------------------- |
| `CORE_DEVELOPMENT_GUIDELINE.md` | rationale                                               |
| `CORE_NAMING_GUIDELINE.md`      | naming                                                  |
| `CORE_AGENT_WORKFLOW.md`        | orchestration                                           |
| `CORE_REVIEW_GUIDELINE.md`      | review                                                  |
| `.claude/commands/`             | review commands and the refactor and fix-style commands |

Where these files name GR4 modules (`core`, `meta`, `algorithm`, `blocks/*`), the `src/*` modules here count as
leaves. This file and the copies MUST NOT be changed without the author's express approval.

## 2 · UI conventions

All color, layout, timing, and size constants related to UI/UX should go into LookAndFeel. Always consider whether a UI
component will be legible in both light and dark mode, and under different themes.

## 3 · UI tests

No `sleep` or timing-dependent tests — use deterministic scheduling or event signalling. This includes
"belt-and-suspenders" waits and sleeps. If you think the test should pass without the sleep, then the sleep should not
be there at all. An example would be `ctx->Yield(10);` in a UI test. This is unacceptable. In this case, one of these is
applicable, in order of likelihood:

a) the test should pass without the sleep/wait, remove it, and investigate any breakage

b) the condition of completion is verifiable but the time it takes to complete is nondeterministic or not part of any
API contract, in which case you should use a function which waits for a condition to become true or an event to arrive.
ctest handles failing a timed-out test.

c) (rarest) This is correct, iff the task cannot easily be verified as completed and the amount of computation is
deterministic. the only known example of this is the node editor which may take 2 frames to resolve the positions of
nodes. This is a code smell for the implementation anyways, which is why the only time this makes sense to do is when
dealing with third-party code. Our code should provide (at least for testing purposes) a way of indicating that a UI
animation or calculation is completed.

When writing UI tests, try to use `TestDashboardRunner.hpp` when implementing the global test state. Factor out common
test utilities into this header to reduce duplication between tests. For short (<=15 line) dashboards, you may define
them inline with strings, but generally prefer creating `qa_foo.grc` files in `src/ui/test/examples/` and using the
`TestDashboardRunner::reload()` function.

## 4 · Repository

- **Layout.** `src/ui` (ImGui application, native and WASM), `src/service` (OpenCMW service, GCC only), `src/acquisition`
  (DAQ API), `src/utils`; tests are `test/qa_*.cpp` beside each module, UI dashboards for tests in
  `src/ui/test/examples/`.
- **Options.** `OPENDIGITIZER_ENABLE_TESTING`, `ENABLE_IMGUI_TEST_ENGINE`, `OPENDIGITIZER_BUILD_SERVICE`,
  `DISABLE_WASM_BUILD`, `OD_WASM_STATIC_BLOCKLIBS`.
- **Where GR4 comes from.** `find_package(gnuradio4)` prefers an installed GR4. In CI that is the GR4 `latest` release
  debs in `/opt/gnuradio4-gcc` and `/opt/gnuradio4-emscripten-static`, rebuilt from GR4 `main`.
  - Without an installed GR4, FetchContent builds GR4 at `GIT_SHA_GNURADIO4` in `cmake/DependenciesSHAs.cmake`.
  - A GR4 API change therefore reaches CI with GR4 `main`, not with the pin. Bump the pin together with the source
    change.
  - `.grc` settings keys are wire keys: a renamed GR4 setting is silently ignored at load, not a compile error.
- **Toolchains:** GCC 15 (main, `ci-gcc-*` lanes, with the WASM UI as superbuild), Clang, then Emscripten (the
  `wasm-build` skill).

## 5 · Building

```bash
cmake --preset gcc-debug            # or: cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=g++-15
cmake --build --preset gcc-debug --parallel <N>
ctest --test-dir <tree> --output-on-failure
```

A FetchContent GR4 build compiles GR4's own translation units, which can exceed 7 GB each. Size `<N>` from measured
peak RSS (the `measure-build-cost` skill), not from the core count.

## 6 · Quality checks

- `./formatFiles.sh` runs the pinned formatters over the changed files, and `--all` over every file. It MUST run before
  each commit (the `restyle` skill).
- In CI, `build_cmake.yml` runs GR4's quality bots from a checkout of GR4's `devtools/` at the pinned revision:
  formatting, naming, code-style, coverage, hot-paths and wasm-size.
  - Their scope is set in `.github/qualitybot.conf`.
  - The bots post sticky PR comments and statuses but never gate a merge.
  - Their history is kept on the `ci-metrics` branch.
- When a CI job fails, use the `ci-triage` skill.
