# Kingdom Recomp on Linux

Guide to **build and run** Kingdom Recomp (based on [TotK Recomp](https://github.com/valentderah/totk-pc-recomp)) natively on Linux (x86-64, Vulkan, SDL3), without Wine or other Windows compatibility layers.

For project architecture, see [architecture.md](architecture.md). Extraction flow and game versions match [building.md](building.md); this document covers Linux-specific steps only.

> [!IMPORTANT]
> **No game files ship with the repository.** You need a legally obtained copy of *Tears of the Kingdom* (base + update 1.2.1 or 1.4.3) dumped from **your** console, plus **your** Switch `prod.keys`. We do not provide instructions for obtaining ROMs or keys from third parties.

## Requirements

| Item | Detail |
|------|--------|
| OS | Recent Linux (tested on Fedora; other distros with equivalent packages) |
| CPU | x86-64 with **AVX2** |
| GPU | Vulkan **1.3** and **`VK_EXT_external_memory_host`** |
| Disk | About **60 GB** under the work folder (`build/`) during extract and compile |
| Tools | **clang**, **lld**, **ninja**, **git**, **pkg-config** |
| Dev libraries | SDL3, Vulkan, libvpx, opus, libzstd (`-devel` / `-dev` packages) |
| Python | **3.12** (`.venv`) for lifter and build; **3.14** (`.venv-py314`) for extraction (`compression.zstd`) |
| .NET | **SDK 10** to publish the shader translator (`totk_xlate`) for **linux-x64** |

Optional: [uv](https://docs.astral.sh/uv/) to create Python environments (same as [building.md](building.md)).

## 1. Clone the repository

```bash
git clone https://github.com/aismor/kingdom-recomp.git
cd kingdom-recomp
```

Use the fork or branch that contains Linux host support.

## 2. Packages (Fedora example)

```bash
sudo dnf install clang lld ninja-build git python3 python3.14 \
  SDL3-devel vulkan-devel libicu-devel zlib-devel \
  libvpx-devel opus-devel libzstd-devel \
  dotnet-sdk-10.0
```

Vulkan Memory Allocator: the build needs `vk_mem_alloc.h` on the include path (e.g. `/tmp/vma` or a `vulkan-memory-allocator-devel` package if your distro provides it).

On **Debian/Ubuntu**, install the equivalent packages (`libsdl3-dev`, `libvulkan-dev`, `libvpx-dev`, `libopus-dev`, `libzstd-dev`, etc.).

## 3. Python environments

From the repository root:

```bash
uv venv --python 3.12 .venv
uv pip install --python .venv/bin/python -r tools/requirements.txt

uv venv --python 3.14 .venv-py314
uv pip install --python .venv-py314/bin/python -r tools/requirements-py314.txt
```

Without `uv`:

```bash
python3.12 -m venv .venv && .venv/bin/pip install -r tools/requirements.txt
python3.14 -m venv .venv-py314 && .venv-py314/bin/pip install -r tools/requirements-py314.txt
```

## 4. Configure game paths

Copy the example and edit with **absolute paths** on your machine:

```bash
cp config.example.toml config.local.toml
```

Example (adjust to your layout):

```toml
[input]
base = '/path/to/base.nsp'
updates = ['/path/to/update-1.2.1.nsp']
keys = '/path/to/prod.keys'

[work]
dir = 'build'
```

- `config.local.toml` is **gitignored**; do not share keys or ROMs.
- Supported versions: **v121** (1.2.1) and **v143** (1.4.3). Scripts default to `v121` when `--version` is omitted.
- A single **XCI** may need to be split into separate base and update NSP files before extraction, depending on your dump layout.

## 5. hactool (extraction)

`tools/extract.py` invokes `toolchain/hactool/hactool` (no `.exe` suffix on Linux).

`tools/build_hactool.py` is **Windows-only** (MSYS2). On Linux, build manually:

```bash
COMMIT=1d64a83450e025622f3468c28fc4164dad2c5ef6
SRC=toolchain/src/hactool
OUT=toolchain/hactool
mkdir -p "$SRC" "$OUT"
git -C "$SRC" init -q 2>/dev/null || true
git -C "$SRC" remote add origin https://github.com/SciresM/hactool.git 2>/dev/null || true
git -C "$SRC" fetch -q --depth 1 origin "$COMMIT"
git -C "$SRC" checkout -q FETCH_HEAD
git -C "$SRC" apply --whitespace=nowarn tools/hactool/portable.patch
make -C "$SRC" CC=gcc EXEEXT= LDFLAGS='-lmbedtls -lmbedx509 -lmbedcrypto'
install -m755 "$SRC/hactool" "$OUT/hactool"
cp "$SRC/LICENSE" "$OUT/"
```

Point `-k` at your `prod.keys` path through `config.local.toml`; never commit or log key values.

## 6. Extract and verify game data

```bash
.venv-py314/bin/python tools/extract.py --hactool toolchain/hactool/hactool
```

This writes `build/extracted/v0` and `build/extracted/<version>` according to your configured updates.

## 7. Shader translator (once)

```bash
.venv/bin/python tools/build_xlate.py
```

Output: `build/xlate/totk_xlate` (the runtime still looks for the name `totk_xlate.exe` beside the build).

Requires .NET SDK 10 publishing for **linux-x64**.

## 8. Generate C, RomFS patches, and link

Replace `<ver>` with `v121` or `v143`:

```bash
.venv/bin/python recomp/gen.py \
  --exefs build/extracted/<ver>/exefs \
  --out generated/<ver> \
  --patches recomp/patches-<ver>.txt

.venv-py314/bin/python tools/make_romfs_patches.py --version <ver>

.venv/bin/python tools/build_game.py --version <ver>
```

Executable: **`build/<ver>/totk`** (not `totk.exe`).

Quick check without game data (runtime/layout only):

```bash
build/<ver>/totk --selftest
```

## 9. Prepare shaders (recommended, once per build folder)

Reduces first-session stutter. Needs `refs/MacNativeGames` (`sh tools/fetch_refs.sh`) or `tools/data/shader_list.tsv`:

```bash
.venv-py314/bin/python tools/prepare_shaders.py build/<ver> --version <ver>
```

On Linux the last step runs `build/<ver>/totk --prepare-shaders`. Some NVIDIA drivers may crash near the end of batch prebuild; translated shader cache is still useful.

## 10. Run the game

Always run from the executable directory (RomFS index, patches, and translator paths are relative to it):

```bash
cd build/<ver>
TOTK_ASYNC_PREEMPT=0 ./totk
```

| Variable / file | Purpose |
|-----------------|--------|
| `TOTK_ASYNC_PREEMPT=0` | **Required on Linux** for now (async guest preemption not implemented yet). |
| `build/<ver>/totk.ini` | Settings from the in-game menu (F1) and options; see [totk.ini.example](../totk.ini.example). |
| `build/profile/` | Saves for v121; other versions use `build/profile-<ver>/`. |
| `build/<ver>/logs/totk.log` | Host log file. |
| Controls | [controls.md](controls.md); keyboard, mouse, and first SDL3 gamepad. |

Built-in FPS overlay: `TOTK_PERF_OVERLAY=fps` or `full`, or **F10** in-game.

With [MangoHud](https://github.com/flightlessmango/MangoHud) installed:

```bash
cd build/<ver>
TOTK_ASYNC_PREEMPT=0 mangohud ./totk
```

Keep the window focused when possible; unfocused windows affect mouse capture behavior.

## Known Linux limitations

- Switch software keyboard (`nn::swkbd`): **stub** — in-game text entry may not work.
- Guest **write-watch** is implemented in software (correct but may affect performance).
- Some test probes (`--probe-services` for swkbd) may fail; gameplay can still work.
- NVIDIA: occasional `--prepare-shaders` crash on one vertex shader; report with logs if it happens in-game.

## Layout

```
kingdom-recomp/
  config.local.toml          # your paths (gitignored)
  build/
    extracted/<ver>/         # extracted exefs, romfs
    generated/<ver>/         # generated C (gitignored)
    v121/totk                # game binary
    v121/totk.ini            # settings
    profile/save/...         # committed save
    xlate/                   # .NET translator
  generated/<ver>/           # gen.py output
```

## Troubleshooting

| Symptom | What to check |
|---------|----------------|
| `host memory import failed` | Old Vulkan driver or missing `VK_EXT_external_memory_host`. |
| Window / Vulkan surface failure | Wayland surface extensions (recent Linux builds); updated GPU drivers. |
| Gamepad not responding | Reconnect or restart; test `/dev/input/event*`; `pad_layout` in `totk.ini`. |
| Bad settings after experiments | Delete or restore `totk.ini`; remove `replay_async` / `free_cores` if set. |
| “Lost” last session changes | `host save recovery: deleted the last run's working copy` means crash before commit; last committed save remains under `build/profile/save/`. |

## Credits (Linux port)

Linux host implementation and guides: [Bruno Morais (aismor)](https://github.com/aismor) — [github.com/aismor/kingdom-recomp](https://github.com/aismor/kingdom-recomp).

## Contributing

Linux changes should not break the **Windows** build. Before commit: `python tools/check_content.py --staged`.
