#!/usr/bin/env bash
# Builds the static libretro cores the Switch NRO links (switch/cores/*.a):
# gambatte (GB / GBC), snes9x (SNES), fceumm (NES) and mGBA (GBA).
#
#   tools/build-switch-cores.sh [core ...]      (default: all four)
#
# Sources are cloned into cores-src/ (gitignored; the build also takes
# libretro-common from gambatte's tree). Set a variable to pin a core to a
# commit, e.g. SNES9X_REF=<sha>. Needs devkitPro (devkitA64 + libnx), git
# and make; DEVKITPRO defaults to /opt/devkitpro.
set -euo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$REPO/cores-src"
OUT="$REPO/switch/cores"
export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"
mkdir -p "$SRC" "$OUT"

# name | git url | folder with the libretro makefile | makefile | pin variable
CORES="
gambatte|https://github.com/libretro/gambatte-libretro|.|Makefile|GAMBATTE_REF
snes9x|https://github.com/libretro/snes9x|libretro|Makefile|SNES9X_REF
fceumm|https://github.com/libretro/libretro-fceumm|.|Makefile|FCEUMM_REF
mgba|https://github.com/libretro/mgba|.|Makefile.libretro|MGBA_REF
"

build() {
  local name="$1" url="$2" dir="$3" mk="$4" pinvar="$5"
  local tree="$SRC/$(basename "$url")"
  local ref="${!pinvar:-}"
  if [ ! -d "$tree/.git" ]; then
    echo ">> $name: cloning $url"
    git clone --depth 1 "$url" "$tree"
  fi
  if [ -n "$ref" ]; then
    echo ">> $name: checking out $ref"
    git -C "$tree" fetch --depth 1 origin "$ref"
    git -C "$tree" checkout --quiet FETCH_HEAD
  fi
  echo ">> $name: building ($(git -C "$tree" rev-parse --short HEAD))"
  make -C "$tree/$dir" -f "$mk" platform=libnx -j"$JOBS"
  cp "$tree/$dir/${name}_libretro_libnx.a" "$OUT/"
  echo ">> $name: $OUT/${name}_libretro_libnx.a"
}

wanted="${*:-gambatte snes9x fceumm mgba}"
while IFS='|' read -r name url dir mk pinvar; do
  [ -n "$name" ] || continue
  case " $wanted " in *" $name "*) build "$name" "$url" "$dir" "$mk" "$pinvar" ;; esac
done <<< "$CORES"
ls -la "$OUT"
