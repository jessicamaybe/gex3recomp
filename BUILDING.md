# Building Guide

This guide builds the Gex 3 PC port from source. You must supply your own
NTSC-U *Gex 3: Deep Cover Gecko* ROM — no game assets are distributed here.

## 1. Clone with submodules

The vendored dependencies (N64Recomp, N64ModernRuntime, RT64) live under `lib/`.

```bash
git clone --recurse-submodules <repo-url>
# if you forgot --recurse-submodules:
# git submodule update --init --recursive
```

The submodules in `lib/` are kept pristine; local fixes to them live as patches.
Apply them after init (idempotent; see `patches/README.md`):

```bash
./patches/apply.sh    # N64ModernRuntime runtime fixes (RT64/N64Recomp unmodified)
```

## 2. Install dependencies

### Linux (Ubuntu; use your distro's equivalents otherwise)

```bash
sudo apt-get install cmake ninja-build libsdl2-dev libgtk-3-dev lld llvm clang
```

## 3. Provide the ROM

Place an NTSC-U Gex 3 ROM at the repo root as `gex3.z64`
(sha1 `467bc88942e02d542e1a4705dcab98ab7281819f`). Use a normal big-endian
`.z64` ROM, not a decompressed one.

## 4. Generate the recompiled C

Build `N64Recomp` / `RSPRecomp` (see
[N64Recomp](https://github.com/N64Recomp/N64Recomp)), then from the repo root:

```bash
lib/N64Recomp/build/N64Recomp gex3.toml              # -> RecompiledFuncs/
```

`gex3.toml` and `Gex3Syms/gex3.syms.toml` carry everything the recompiler needs,
including the corrected fall-through function boundaries and the `[patches] hook`
entries that inject the boot-race / module-load / audio-wavetable native calls.
There is **no post-processing step** — the old `scripts/` pipeline is gone.

## 5. Build

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build build -j"$(nproc)" --target gex3
```

> [!IMPORTANT]
> Configure `build/` against **this** repo. If the directory was copied from
> another checkout, its `CMakeCache.txt` may point `CMAKE_HOME_DIRECTORY` at a
> different source tree and your edits silently won't take effect — delete
> `build/` and reconfigure if so.

## 6. Run

```bash
cd build && ./gex3            # loads ../gex3.z64 (or pass a ROM path as argv[1])
```

## Game patches

Whole-function C patches (Zelda/Banjo `RECOMP_PATCH` style) go in `patches/` and
are recompiled via `patches.toml` at build time. For hooks *inside* an existing
function — where there is no decompiled source to replace the whole function —
add a `[patches] hook` entry to `gex3.toml` instead.
