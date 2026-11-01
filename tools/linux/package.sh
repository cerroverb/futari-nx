#!/usr/bin/env bash
# Puts a Linux build and your game files together in one folder that runs on its own:
#
#   out/futari-nx-<preset>/
#     futari                 the executable (RPATH $ORIGIN: its libraries are found next to it)
#     futari.sh              starts it from its own folder (for a desktop entry or a non-Steam game)
#     *.so                   the SDK runtime, the GPU plugin and the eight game cores
#     futari.toml            your settings (app/futari_linux.toml; an existing one is kept)
#     game_root/             the files of your disc (assets/game_root)
#     dlc/                   optional: Black Label and Ver 1.01, installed by the game at its first start
#
#   tools/linux/package.sh [--preset linux-arm64|linux-amd64] [--out DIR]
#
# --complete packages a build made with tools/linux/build.sh (translated shaders built in) for distribution: the
# program, extract-xiso, its settings template, the launcher and the licenses, without any game file, as
# out/futari-linux-<aarch64|x86_64>/ and out/futari-linux-<aarch64|x86_64>.tar.gz (the release asset). Players add
# their ISO or game files; the first start sets them up (app/src/futari_game_setup.cpp).
#
# --release packages a build made with tools/linux/build.sh --release instead: the program, its
# settings template, the launcher and the licenses, without the game files or any shader, as
# out/futari-linux-installer-<aarch64|x86_64>/ and its .tar.gz (what the web installer's deployment takes). Players get
# the game files and vulkan_spirv.bin from the installer page.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
preset=""
out=""
release=0
complete=0
while [ $# -gt 0 ]; do
    case "$1" in
        --preset) preset="$2"; shift ;;
        --out) out="$2"; shift ;;
        --release) release=1 ;;
        --complete) complete=1 ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
    shift
done
if [ -z "$preset" ]; then
    case "$(uname -m)" in
        aarch64|arm64) preset=linux-arm64 ;;
        *) preset=linux-amd64 ;;
    esac
fi
case "$preset" in
    linux-arm64) arch=aarch64 ;;
    linux-amd64) arch=x86_64 ;;
    *) echo "unknown preset $preset" >&2; exit 2 ;;
esac
build="$root/out/$preset"
if [ ! -x "$build/futari" ]; then
    echo "$build/futari not found: run tools/linux/build.sh first" >&2
    exit 1
fi

write_launcher() {
    printf '%s\n' '#!/usr/bin/env bash' 'cd "$(dirname "$(readlink -f "$0")")"' \
        'exec ./futari --log_level=info --log_file="$PWD/last-run.log" "$@"' > "$1/futari.sh"
    chmod +x "$1/futari.sh"
}

copy_program() {
    mkdir -p "$1"
    cp -f "$build/futari" "$1/"
    find "$build" -maxdepth 1 -name '*.so' -exec cp -f {} "$1/" \;
    write_launcher "$1"
}

if [ "$complete" -eq 1 ]; then
    if grep -q '^FUTARI_LINUX_RELEASE_SHADERS:BOOL=ON$' "$build/CMakeCache.txt"; then
        echo "$build is a --release build (no shaders): run tools/linux/build.sh without --release" >&2
        exit 1
    fi
    name="futari-linux-$arch"
    out="${out:-$root/out/$name}"
    rm -rf "$out"
    copy_program "$out"
    mkdir -p "$out/tools/extract-xiso" "$out/game_root" "$out/dlc"
    cp "$build/tools/extract-xiso/extract-xiso" "$build/tools/extract-xiso/LICENSE.TXT" "$out/tools/extract-xiso/"
    cp "$root/app/futari_linux.toml" "$out/futari.toml"
    cp "$root/LICENSE" "$root/THIRD_PARTY_NOTICES.md" "$out/"
    # No game data: every file of the folder is the program's (game_root/ and dlc/ are empty).
    if [ -n "$(find "$out/game_root" "$out/dlc" -type f)" ]; then echo "game files in the release" >&2; exit 1; fi
    tar -C "$(dirname "$out")" -czf "$out.tar.gz" "$name"
    echo "done: $out.tar.gz"
    exit 0
fi

if [ "$release" -eq 1 ]; then
    if ! grep -q '^FUTARI_LINUX_RELEASE_SHADERS:BOOL=ON$' "$build/CMakeCache.txt"; then
        echo "$build was not configured with FUTARI_LINUX_RELEASE_SHADERS: run tools/linux/build.sh --release" >&2
        exit 1
    fi
    name="futari-linux-installer-$arch"
    out="${out:-$root/out/$name}"
    rm -rf "$out"
    copy_program "$out"
    cp "$root/app/futari_linux.toml" "$out/futari.toml"
    cp "$root/LICENSE" "$root/THIRD_PARTY_NOTICES.md" "$out/"
    # No game data in a release: every file of the folder is the program's.
    for f in "$out"/*; do
        case "$(basename "$f")" in
            futari|futari.sh|futari.toml|LICENSE|THIRD_PARTY_NOTICES.md|*.so) ;;
            *) echo "unexpected file in the release: $f" >&2; exit 1 ;;
        esac
    done
    tar -C "$(dirname "$out")" -czf "$out.tar.gz" "$name"
    (cd "$out" && sha256sum futari futari.sh futari.toml LICENSE THIRD_PARTY_NOTICES.md *.so) > "$out.sha256"
    echo "done: $out.tar.gz"
    exit 0
fi

out="${out:-$root/out/futari-nx-$preset}"
if [ ! -f "$root/assets/game_root/default.xex" ]; then
    echo "assets/game_root/default.xex not found" >&2
    exit 1
fi
copy_program "$out"
if [ ! -f "$out/futari.toml" ]; then
    cp "$root/app/futari_linux.toml" "$out/futari.toml"
fi
mkdir -p "$out/game_root"
cp -a "$root/assets/game_root/." "$out/game_root/"
# DLC packages (tools/linux/build.sh --game-dir moves a game folder's dlc/ there): installed by the game at its next
# start.
if [ -d "$root/out/dlc" ] && [ ! -e "$out/dlc" ]; then
    cp -a "$root/out/dlc" "$out/dlc"
fi
echo "done: $out"
