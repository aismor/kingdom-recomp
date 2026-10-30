# Building on Linux

Native Linux builds use the same Python flow as [building.md](building.md), with **clang**, **lld**, **ninja**, and distro packages instead of MSYS2.

## Packages (Fedora example)

```sh
sudo dnf install clang lld ninja-build git python3 \
  SDL3-devel vulkan-devel vulkan-memory-allocator-devel \
  libvpx-devel opus-devel zstd-devel shaderc spirv-tools
```

Python: `.venv` (3.12) and `.venv-py314` as in the main build doc (`uv`).

## Build

```sh
cd /path/to/totk-pc-recomp
uv venv --python 3.12 .venv
uv pip install --python .venv/bin/python -r tools/requirements.txt

.venv/bin/python recomp/gen.py --exefs build/extracted/v121/exefs --out generated/v121 --patches recomp/patches-v121.txt
.venv/bin/python tools/build_game.py --version v121
```

Output: `build/v121/totk` (not `totk.exe`).

Quick check without game data (after a successful link):

```sh
build/v121/totk --selftest
```

## Linux-specific code

- `runtime/os.h` + `runtime/os_linux.c`: Win32-shaped API on POSIX (guest memory, threads, files).
- `runtime/platform/linux.c`: `platform.h`.
- `runtime/gfx_window_sdl.c`: SDL3 Vulkan window (replaces `gfx_window.c`).
- `tools/host_platform.py` + changes in `tools/build_game.py`: clang/ninja and `pkg-config` link flags.

Use **`TOTK_ASYNC_PREEMPT=0`** on Linux until signal-based preemption is implemented.

## Shader translator

```sh
.venv/bin/python tools/build_xlate.py
```

Needs .NET 10 SDK for `linux-x64` publish; binary lives under `build/xlate/`.
