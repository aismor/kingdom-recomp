# Kingdom Recomp

**Kingdom Recomp** is based on [TotK Recomp](https://github.com/valentderah/totk-pc-recomp) by Valiantsin Dzerakh, with a native Linux host. It is a static recompilation of *The Legend of Zelda: Tears of the Kingdom* (Nintendo Switch, 2023) into a native Windows x86-64 application ([docs/architecture.md](docs/architecture.md)).

> [!IMPORTANT]
> **No game files are included.** You need your own legally obtained copy of *Tears of the Kingdom*, dumped from your own console: the base game and the 1.2.1 or 1.4.3 update. Instructions for obtaining those files will not be provided.
>
> The game is built from your files on your machine, so the result contains the game. It is for your own use. Do not share it.

## Status

Early development. The game is playable on Windows; visual bugs and crashes are still expected.

Report problems as issues, following [CONTRIBUTING.md](CONTRIBUTING.md).

## Features

- Native code, not emulation
- Game updates 1.2.1 and 1.4.3, with saves that carry over between them
- **Brazilian Portuguese (0.2.3+):** installer and in-game menu in Portuguese; official in-game text and subtitles with **update 1.4.3** and `lang = pt-BR` ([docs/linux.pt.md](docs/linux.pt.md))
- 60 fps by default; up to 144 fps or the monitor's refresh rate (experimental)
- Custom resolutions (1080p, 2k...), supersampling and AMD FSR 1 upscaling
- In-game settings menu (F1), **Mods** tab (built-in cheats and RomFS mods), and performance overlay (F10)
- Keyboard, mouse and gamepad with remapping, rumble and gyro
- Shaders prepared at install instead of during play

## Linux (native build)

Experimental native Linux support is available on forks/branches that include the Linux host port (SDL3 + Vulkan, no Wine).

| Language | Guide |
|----------|--------|
| English | [docs/linux.en.md](docs/linux.en.md) |
| Português | [docs/linux.pt.md](docs/linux.pt.md) |

Run the game from `build/<ver>/` with **`TOTK_ASYNC_PREEMPT=0 ./totk`**. Settings: `build/<ver>/totk.ini`. Saves: `build/profile/` (v121).

## Planned

- Linux and the Steam Deck (upstream merge of native host support)
- Mods made for emulators: unpack a mod into a `mods` folder
- Custom aspect ratios, including ultrawide
- VRR

## Installing

Requirements: Windows 10 or later, an x86-64 CPU with AVX2, a GPU with Vulkan 1.3 and `VK_EXT_external_memory_host`, about 36 GiB free during setup (20 GiB after).

1. Download `TotK-Setup-<version>-windows.zip` from [Releases](../../releases/latest) and unpack it.
2. Start `TotK-Setup.exe`. If SmartScreen warns about an unsigned app, choose *More info*, then *Run anyway*.
3. Pick your game files, the language and an install folder, and press *Start*. The setup builds the game once.
4. Play `totk.exe` from the install folder.

To update, run a newer `TotK-Setup.exe` on the same folder; saves and settings are kept. Settings are in the in-game menu and in `totk.ini` ([totk.ini.example](totk.ini.example) lists every key). Controls: [docs/controls.md](docs/controls.md).

## Credits

Kingdom Recomp is based on [TotK Recomp](https://github.com/valentderah/totk-pc-recomp) by Valiantsin Dzerakh, which builds on the projects below. What was taken from each, with its license, is listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

- [LAS Recomp](https://github.com/peiikai/LASRecomp)
- [MacNativeGames](https://github.com/ly05010419/MacNativeGames)
- [d2r-mobile](https://github.com/jhaizhou-ops/d2r-mobile) and [nxnative](https://github.com/seedds/nxnative)
- [SavageWindRecomp](https://github.com/Revivedx/SavageWindRecomp)
- [Ryujinx](https://github.com/Ryujinx/Ryujinx)
- [deko3d](https://github.com/devkitPro/deko3d)
- [hactool](https://github.com/SciresM/hactool)
- [SDL 3](https://libsdl.org), [Dear ImGui](https://github.com/ocornut/imgui), [AMD FSR 1](https://github.com/GPUOpen-Effects/FidelityFX-FSR), [Vulkan Memory Allocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator), [libopus](https://opus-codec.org), [libvpx](https://chromium.googlesource.com/webm/libvpx), [Zstandard](https://github.com/facebook/zstd)
- [Switchbrew](https://switchbrew.org) and the Switch homebrew community

Native Linux host port (experimental): [Bruno Morais (aismor)](https://github.com/aismor) — [kingdom-recomp](https://github.com/aismor/kingdom-recomp); guides [docs/linux.en.md](docs/linux.en.md) / [docs/linux.pt.md](docs/linux.pt.md).

## License

The project's own code is under the [MIT License](LICENSE); third-party code keeps its own license ([THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)). The license grants nothing for Nintendo's game.

Kingdom Recomp is an independent, non-commercial fan project, not affiliated with or endorsed by Nintendo. *The Legend of Zelda*, *Tears of the Kingdom* and *Nintendo Switch* are trademarks of Nintendo.

## Building from source

[docs/building.md](docs/building.md) lists the toolchain and the steps. Everything the build writes stays under `build\` and `generated\`, which git ignores; run `tools/check_content.py` before a commit. Testers: [docs/tester-guide.md](docs/tester-guide.md).

Most of the code and documentation is written by AI coding agents (Claude Code) under the maintainer's direction; the maintainer plays every build and decides what is merged. [AGENTS.md](AGENTS.md) holds the rules they work under.
