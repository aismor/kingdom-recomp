"""Host OS detection and default toolchain paths for tools/build_game.py."""
import os
import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
IS_WINDOWS = os.name == 'nt'
EXE_NAME = 'totk.exe' if IS_WINDOWS else 'totk'


def find_tool(name):
    p = shutil.which(name)
    return Path(p) if p else None


def default_clang():
    if IS_WINDOWS:
        p = Path(r'C:\msys64\ucrt64\bin\clang.exe')
        return p if p.is_file() else None
    return find_tool('clang')


def default_ninja():
    if IS_WINDOWS:
        p = Path(r'C:\msys64\ucrt64\bin\ninja.exe')
        if p.is_file():
            return p
        p = ROOT / 'bin' / 'ninja.exe'
        return p if p.is_file() else None
    return find_tool('ninja')


def default_ar(clang):
    if IS_WINDOWS:
        return Path(r'C:\msys64\ucrt64\bin\llvm-ar.exe')
    if clang:
        sib = clang.parent / ('llvm-ar' + ('.exe' if IS_WINDOWS else ''))
        if sib.is_file():
            return sib
    return find_tool('llvm-ar')


def pkg_config_flags(*names):
    if IS_WINDOWS:
        return [], []
    inc, lib = [], []
    for name in names:
        r = __import__('subprocess').run(['pkg-config', '--cflags-only-I', name], capture_output=True, text=True)
        if r.returncode == 0:
            inc.extend(r.stdout.split())
        r = __import__('subprocess').run(['pkg-config', '--libs', name], capture_output=True, text=True)
        if r.returncode == 0:
            lib.extend(r.stdout.split())
    return inc, lib


def linux_link_libs():
    _, libs = pkg_config_flags('sdl3', 'vulkan', 'vpx', 'opus', 'libzstd')
    if not libs:
        libs = ['-lSDL3', '-lvulkan', '-lvpx', '-lopus', '-lzstd']
    libs += ['-lstdc++', '-lpthread', '-ldl', '-lm']   # libstdc++: Dear ImGui (C++)
    return libs


def linux_rt_skip():
    """Runtime sources replaced or omitted on Linux."""
    return {'prof_win.c', 'rdoc_win.c', 'swkbd_win.c', 'gfx_window.c'}


def linux_rt_extra():
    return ['prof_stub.c', 'rdoc_stub.c', 'gfx_window_sdl.c', 'swkbd_stub.c']
