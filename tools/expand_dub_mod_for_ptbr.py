#!/usr/bin/env python3
"""Mirror USen dub RomFS paths to USes so TotK loads them with lang pt-BR / es-419.

Community dub packs usually ship Voice/Resource/USen and Sound/.../NV_USen_* only.
On Switch, Brazilian Portuguese voice assets live under the USes tree (same region pack).
This script hardlinks duplicates where possible to avoid doubling disk use.

Does not add subtitle text: in-game subtitles come from Nintendo's pt-BR localization in
your extracted RomFS (update 1.4.3), not from dub mods.

    python tools/expand_dub_mod_for_ptbr.py --romfs /path/to/mods/my-dub/romfs
"""
from __future__ import annotations

import argparse
import os
from pathlib import Path


def link_or_copy(src: Path, dst: Path) -> str:
    dst.parent.mkdir(parents=True, exist_ok=True)
    if dst.exists() or dst.is_symlink():
        if dst.samefile(src):
            return "skip"
        dst.unlink()
    try:
        os.link(src, dst)
        return "link"
    except OSError:
        import shutil

        shutil.copy2(src, dst)
        return "copy"


def mirror_usen_tree(romfs: Path) -> tuple[int, int, int]:
    linked = copied = skipped = 0
    voice_usen = romfs / "Voice" / "Resource" / "USen"
    voice_uses = romfs / "Voice" / "Resource" / "USes"
    if voice_usen.is_dir():
        for src in voice_usen.rglob("*"):
            if not src.is_file():
                continue
            rel = src.relative_to(voice_usen)
            dst = voice_uses / rel
            kind = link_or_copy(src, dst)
            if kind == "link":
                linked += 1
            elif kind == "copy":
                copied += 1
            else:
                skipped += 1

    for src in romfs.rglob("*"):
        if not src.is_file():
            continue
        parts = src.relative_to(romfs).parts
        name = parts[-1]
        if "USen" not in name:
            continue
        new_name = name.replace("USen", "USes", 1)
        if new_name == name:
            continue
        dst = src.with_name(new_name)
        kind = link_or_copy(src, dst)
        if kind == "link":
            linked += 1
        elif kind == "copy":
            copied += 1
        else:
            skipped += 1
    return linked, copied, skipped


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--romfs", type=Path, required=True, help="mods/<name>/romfs directory")
    args = ap.parse_args()
    romfs = args.romfs.resolve()
    if not romfs.is_dir():
        raise SystemExit(f"not a directory: {romfs}")
    linked, copied, skipped = mirror_usen_tree(romfs)
    print(f"USes mirror: {linked} hardlinks, {copied} copies, {skipped} already present under {romfs}")


if __name__ == "__main__":
    main()
