#!/usr/bin/env bash
# Kingdom Recomp installer for Linux: opens the installer window (installer/gui.py) in a podman container that holds
# the whole toolchain (tools/linux/Containerfile), so the host needs only podman, a Vulkan driver and the libraries
# totk links (SDL3, libvpx, opus, zstd).
#
# The host check (installer/hostcheck.c) and the shader translation (totk --prepare-shaders) need the GPU, which the
# container does not see: this script runs both on the host, before and after the window.
#
# Usage: ./kingdom-setup.sh            (from the repository checkout)
set -euo pipefail

ROOT=$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")" && pwd -P)
SETUP="$ROOT/build/setup"
MNG_URL=https://github.com/ly05010419/MacNativeGames.git
MNG_COMMIT=b75be8cfa704df3701e86130c8ced268e79d6b11   # tools/make_release.py MNG_COMMIT
HACTOOL_COMMIT=1d64a83450e025622f3468c28fc4164dad2c5ef6   # docs/linux.en.md "hactool"

say() { printf '\033[1m[kingdom-setup]\033[0m %s\n' "$*"; }
die() { printf '\033[1;31m[kingdom-setup]\033[0m %s\n' "$*" >&2; exit 1; }

command -v podman >/dev/null || die "podman not found. Fedora: sudo dnf install podman; Ubuntu/Debian: sudo apt install podman"
mkdir -p "$SETUP"

# ---- container image: rebuilt when its recipe or the installer's Python packages change
TAG=$(cat "$ROOT/tools/linux/Containerfile" "$ROOT/tools/requirements-installer.txt" | sha256sum | cut -c1-12)
IMAGE="localhost/kingdom-setup:$TAG"
if ! podman image exists "$IMAGE"; then
    say "building the container image (first run only, a few minutes)..."
    podman build -t "$IMAGE" -f "$ROOT/tools/linux/Containerfile" "$ROOT/tools"
fi

# Paths the window may pick (game files, install folder) are mounted at the same place, so the paths totk.exe has
# built in (exefs, romfs, profile) are the same on the host.
MOUNTS=(-v "$ROOT:$ROOT")
for dir in "$HOME" /run/media /media /mnt; do
    if [[ -d "$dir" && "$dir" != "$ROOT" ]]; then MOUNTS+=(-v "$dir:$dir"); fi
done
# Fedora Atomic and others: /home links to /var/home, so the home has two names; both must name it in the container.
ALIAS="/home/$(id -un)"
if [[ "$ALIAS" != "$HOME" && "$(readlink -f "$ALIAS" 2>/dev/null)" == "$(readlink -f "$HOME")" ]]; then
    MOUNTS+=(-v "$HOME:$ALIAS")
fi

box() {   # box [podman run options] -- command...
    local opts=()
    while [[ $# -gt 0 && "$1" != "--" ]]; do opts+=("$1"); shift; done
    shift
    podman run --rm --userns=keep-id --security-opt label=disable "${MOUNTS[@]}" -e HOME="$HOME" -w "$ROOT" \
        "${opts[@]}" "$IMAGE" "$@"
}

# ---- host check: CPU (x86-64-v3) and a Vulkan 1.3 device, probed on the host
say "checking CPU and Vulkan..."
box -- clang -O1 installer/hostcheck.c -ldl -o "$SETUP/hostcheck"
HOST_JSON=$("$SETUP/hostcheck") || die "the host check failed to run"
python3 - "$HOST_JSON" <<'EOF' || exit 1
import json, sys
h = json.loads(sys.argv[1])
fail = None
if not h['x86_64_v3']:
    fail = 'the CPU lacks AVX2 (x86-64-v3)'
elif not h['loader']:
    fail = 'no Vulkan loader (libvulkan.so.1): install your GPU driver and vulkan-loader'
elif not h['devices']:
    fail = 'no Vulkan device: check the GPU driver'
elif not any(tuple(d['api'][:2]) >= (1, 3) for d in h['devices']):
    fail = 'no Vulkan 1.3 device: ' + ', '.join(f"{d['name']} ({d['api'][0]}.{d['api'][1]})" for d in h['devices'])
if fail:
    sys.exit(f'[kingdom-setup] {fail}')
print('[kingdom-setup] ' + ', '.join(f"{d['name']} (Vulkan {d['api'][0]}.{d['api'][1]})" for d in h['devices']))
EOF

# ---- what the installer steps run, built once in the container (installer/layout.py, Linux dev layout)
if [[ ! -x "$ROOT/toolchain/hactool/hactool" ]]; then
    say "building hactool..."
    box -- bash -euc '
        SRC=toolchain/src/hactool OUT=toolchain/hactool
        mkdir -p "$SRC" "$OUT"
        git -C "$SRC" init -q
        git -C "$SRC" remote add origin https://github.com/SciresM/hactool.git 2>/dev/null || true
        git -C "$SRC" fetch -q --depth 1 origin "$1"
        git -C "$SRC" checkout -q -f FETCH_HEAD
        git -C "$SRC" apply --whitespace=nowarn "$PWD/tools/hactool/portable.patch"
        make -C "$SRC" CC=gcc EXEEXT= LDFLAGS="-lmbedtls -lmbedx509 -lmbedcrypto"
        install -m755 "$SRC/hactool" "$OUT/hactool"
        cp "$SRC/LICENSE" "$OUT/"' _ "$HACTOOL_COMMIT"
fi
if [[ ! -f "$ROOT/build/xlate/totk_xlate.exe" ]]; then
    say "building the shader translator (first run only, several minutes)..."
    box -- python tools/build_xlate.py
fi
if [[ ! -f "$ROOT/build/shader_list.tsv" ]]; then
    say "fetching the shader list (MacNativeGames)..."
    box -- bash -euc '
        REPO=build/MacNativeGames-history
        [[ -d "$REPO/.git" ]] || git clone -q --filter=blob:none --no-checkout "$1" "$REPO"
        git -C "$REPO" fetch -q origin
        python -c "import sys; sys.path.insert(0, \"tools\"); import shader_list; \
sys.stdout.write(shader_list.history(sys.argv[1], sys.argv[2]))" "$REPO" "$2" > build/shader_list.tsv.tmp
        mv build/shader_list.tsv.tmp build/shader_list.tsv' _ "$MNG_URL" "$MNG_COMMIT"
fi

# ---- the window
DISPLAY_OPTS=()
[[ -n "${DISPLAY:-}" ]] || die "no DISPLAY: run this from a desktop session (X11 or Wayland with XWayland)"
DISPLAY_OPTS+=(-e DISPLAY="$DISPLAY")
if [[ -d /tmp/.X11-unix ]]; then DISPLAY_OPTS+=(-v /tmp/.X11-unix:/tmp/.X11-unix); fi
if [[ -n "${XAUTHORITY:-}" && -f "$XAUTHORITY" ]]; then
    DISPLAY_OPTS+=(-v "$XAUTHORITY:$XAUTHORITY:ro" -e XAUTHORITY="$XAUTHORITY")
fi
for var in LANG LC_ALL LC_MESSAGES; do
    if [[ -n "${!var:-}" ]]; then DISPLAY_OPTS+=(-e "$var=${!var}"); fi
done

PENDING="$SETUP/pending-shaders"
: > "$PENDING"
# "Open folder" in the window: the container has no file manager, so the host opens what the window writes here.
OPEN_FIFO="$SETUP/open-folder"
rm -f "$OPEN_FIFO" && mkfifo "$OPEN_FIFO"
( while read -r dir < "$OPEN_FIFO"; do xdg-open "$dir" >/dev/null 2>&1 & done ) &
OPENER=$!
say "opening the installer window..."
box "${DISPLAY_OPTS[@]}" -e KINGDOM_HOST_CHECKED=1 -e KINGDOM_PENDING_SHADERS="$PENDING" \
    -e KINGDOM_OPEN_FIFO="$OPEN_FIFO" -- python -m installer.gui || true
kill "$OPENER" 2>/dev/null || true
rm -f "$OPEN_FIFO"

# ---- after the window: the shader translation of every install the Prepare step seeded, on the host's GPU
sort -u "$PENDING" | while IFS= read -r dest; do
    [[ -n "$dest" && -x "$dest/totk" && -f "$dest/seed-answers.bin" ]] || continue
    missing=$(ldd "$dest/totk" | awk '/not found/ {print $1}')
    if [[ -n "$missing" ]]; then
        say "totk needs libraries the system lacks: $(echo $missing)"
        say "Fedora: sudo dnf install SDL3 libvpx opus libzstd   Ubuntu/Debian: sudo apt install libsdl3-0 libvpx9 libopus0 libzstd1"
        continue
    fi
    if [[ ! -f "$dest/shader-prepared.bin" ]]; then
        say "preparing shaders in $dest (a few minutes, uses the GPU)..."
        (cd "$dest" && ./totk --prepare-shaders) || say "shader preparation failed; the game still runs, with stutter at first"
    fi
    say "ready: cd \"$dest\" && ./totk"
done
rm -f "$PENDING"
