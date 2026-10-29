---
name: restyle
description: Check or apply the formatter set CI's formatting bot runs (clang-format, cmake-format, prettier for Markdown and YAML, shellcheck, whitespace, black) at the versions formatFiles.sh pins. Use before committing, or when the Formatting status is red.
---

# Restyle

`./formatFiles.sh` installs and runs the pinned formatters, the same ones CI's formatting bot runs:

| file                       | tool         |
| -------------------------- | ------------ |
| `*.cpp` `*.hpp` `*.cc`     | clang-format |
| `CMakeLists.txt` `*.cmake` | cmake-format |
| `*.md` `*.yml`             | prettier     |
| `*.sh`                     | shellcheck   |
| `*.py`                     | black        |

```bash
./formatFiles.sh --install        # once: the pinned versions into ~/.cache/gr4-formatters
./formatFiles.sh                  # files changed since HEAD, plus untracked ones
./formatFiles.sh --since origin/main
./formatFiles.sh --all            # every tracked file, as CI
./formatFiles.sh --versions
```

The bot reports the files this change touches. Its patch is the `formatting-patch` artefact of the run:
`gh run download <run-id> -n formatting-patch`, then `git apply formatting.patch`.

## Two traps

**Config resolves from the file's path.** Checking a copy under `/tmp` reports every file as mis-formatted, because the
tool never finds `.clang-format` or `.cmake-format.yaml`. Always check in place.

**`cmake-format` picks its dialect from the filename.** A copy named `CMakeLists.txt.bak` parses as something else.
Keep the basename.

Never adjust whitespace by hand.
