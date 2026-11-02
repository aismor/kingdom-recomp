# Upstream settings migration scope

This document defines the boundary for migrating settings from the upstream
checkout at `/home/game/git/totk-pc-recomp` into this repository.

## Goal

Bring the upstream project's graphics and performance improvements into this
project, starting with the in-game Settings menu and preserving this project's
existing behavior.

## In scope

- Graphics options and their Settings menu presentation.
- Display, resolution, scaling, image quality, color and HDR options.
- Frame pacing, synchronization, latency and performance options.
- Performance diagnostics that support graphics work, such as the performance
  overlay.
- Runtime, Vulkan and platform changes required by an adopted graphics or
  performance setting.
- Configuration schema, translated UI text, tests and documentation required
  by those changes.

## Explicitly out of scope

- Mods UI or mod loading behavior.
- Cheats UI, cheat definitions or cheat application behavior.
- Upstream changes that replace, restructure or remove this project's existing
  Mods/Cheats implementation.
- Incidental Mods/Cheats edits bundled into an otherwise relevant upstream
  commit.

This project already has its own Mods/Cheats implementation. It must remain the
source of truth. Relevant upstream work must be ported selectively rather than
copied as whole files when those files also contain Mods/Cheats changes.

## Migration rules

1. Review each upstream change by feature and dependency, not by copying an
   entire commit or directory.
2. Separate shared Settings infrastructure from Mods/Cheats code before
   applying a change.
3. Preserve the current Mods and Cheats tabs, configuration keys, translations
   and runtime integration.
4. Do not resolve merge conflicts by accepting the complete upstream version
   of a file that contains local Mods/Cheats work.
5. Add graphics and performance options incrementally, with focused tests after
   each migration.
6. Keep generated game-derived content under `build/` or `generated/` and never
   expose game data or keys.

## Initial work order

1. Inventory the Settings differences between both projects.
2. Classify each difference as graphics, performance, shared infrastructure,
   Mods/Cheats or unrelated.
3. Port shared Settings infrastructure without changing the existing
   Mods/Cheats behavior.
4. Port graphics options and their runtime dependencies.
5. Port performance options and validate their behavior.
6. Run the relevant menu, configuration, graphics and content-boundary checks.

## Initial inventory

The first comparison of the two Settings schemas identified these candidate
migration groups. This is an inventory, not approval to copy the corresponding
upstream files wholesale.

| Group | Candidate settings | Main dependency areas |
| --- | --- | --- |
| Image scaling and anti-aliasing | DLAA, DLSS modes and FXAA | Menu, configuration, Vulkan presentation and DLSS integration |
| View and texture quality | Aspect mode, field of view and anisotropic filtering | Resolution patches, camera handling and Vulkan samplers |
| Color output | Presets, brightness, contrast, saturation and gamma | Menu, configuration and the Vulkan color pass |
| HDR | HDR output, peak brightness, game brightness and UI brightness | Swapchain/presentation, shaders and platform display support |
| Device profiles | Automatic, desktop and Steam Deck profiles | Configuration precedence, platform detection and defaults |
| Shadows | Resolution, scale, range, contact shadows and PCSS | Game patches, shaders and Vulkan rendering |
| Frame-rate choices | Additional 40, 45 and 90 FPS limits | Menu values, frame limiter and timing behavior |
| Texture performance | VRAM pressure policy and ASTC-to-BC path | Texture cache, memory management, shaders and capability checks |

The first migration should use a small, self-contained group. Larger features
such as DLSS, HDR and shadows must be split into their complete runtime
dependencies and validated independently.

## Progress

- Colour output: implementation started from upstream commit `c056719`. The
  Settings menu, live configuration, Vulkan colour pass, Win32/SDL presentation
  integration, English/Russian/Portuguese text and example configuration have
  been migrated without changing Mods/Cheats code.

## Review checklist

Before accepting a migrated change, verify that:

- it has a direct graphics or performance purpose;
- its required dependencies were identified;
- no Mods/Cheats source, UI text or behavior was unintentionally replaced;
- existing configuration files remain compatible where practical;
- menu text and supported translations are complete;
- the appropriate tests pass; and
- `python tools/check_content.py --staged` passes before a commit is proposed.
