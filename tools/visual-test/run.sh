#!/usr/bin/env bash
# Visual test: builds a sandbox with placeholder content and captures the
# main screens of the launcher, so a change can be looked at (or compared
# with an earlier run) without real games.
#
#   tools/visual-test/run.sh <launcher binary> [output dir]
#
# Output: <output dir>/*.png (default: visual-test-out/). Linux only; needs
# a C compiler, Python 3 with Pillow, Xvfb, xdotool and ImageMagick.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
BIN="$(realpath "$1")"
OUT="$(realpath -m "${2:-visual-test-out}")"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$OUT"

cc -shared -fPIC -O2 -o "$WORK/testcore.so" "$HERE/testcore.c"
python3 "$HERE/make_sandbox.py" "$REPO" "$WORK/sandbox" "$WORK/testcore.so"
SB="$WORK/sandbox"

# Start every scenario from the same place: a given game, language, view.
prefs() {   # prefs <game key> <language> <view>
  printf '{ "last_group": "%s", "language": "%s", "view": "%s", "attract": false }\n' \
    "$1" "$2" "$3" > "$SB/prefs.json"
  rm -rf "$SB/saves/states" "$SB/stats.json" "$SB/input.json"
}
shoot() { "$HERE/shoot.sh" "$BIN" "$SB" "$OUT/$1" "${@:2}"; }

prefs zelda1 en 3d
shoot shelf     wait:5 snap:1-stack key:Right wait:1.2 snap:2-neighbor \
                key:Left wait:1 key:Return wait:1.5 snap:3-versions key:space wait:2 snap:4-box-back

prefs zelda_oot en 3d
shoot big       wait:5 snap:1-n64-stack key:Tab wait:0.6 snap:2-settings

prefs zelda6 en 3d
shoot game      wait:5 key:Return wait:2.5 snap:1-playing key:Escape wait:0.6 snap:2-pause \
                key:Down wait:0.3 key:Return wait:0.8 snap:3-state-saved

prefs zelda6 en 3d
shoot controls  wait:5 key:Tab wait:0.5 key:Up wait:0.3 key:Return wait:0.6 snap:1-controls

prefs bszelda_ast es 3d
shoot spanish   wait:5 snap:1-estante key:Return wait:1.5 snap:2-semanas

prefs zelda3 en classic
shoot classic   wait:5 snap:1-list key:Return wait:1 snap:2-versions

echo "screenshots in $OUT:"
ls "$OUT"/*.png
