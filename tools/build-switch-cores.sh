#!/usr/bin/env bash
# Builds the static libretro cores the Switch NRO links (switch/cores/*.a):
# gambatte (GB / GBC), snes9x (SNES), fceumm (NES) and mGBA (GBA).
#
#   tools/build-switch-cores.sh [core ...]      (default: all four)
#
# Sources are fetched into cores-src/ (gitignored; the build also takes
# libretro-common from gambatte's tree). Each core is pinned to the commit the
# launcher was last built and checked with; set a variable to build another
# one, e.g. SNES9X_REF=<sha> or MGBA_REF=master. Needs devkitPro (devkitA64 +
# libnx), git, make and CMake (mGBA); DEVKITPRO defaults to /opt/devkitpro.
set -euo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$REPO/cores-src"
OUT="$REPO/switch/cores"
export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"
mkdir -p "$SRC" "$OUT"

: "${GAMBATTE_REF:=d9d6cd06382d1ced30de34d56d3609452323dab1}"   # 2026-08-21
: "${SNES9X_REF:=fae2fea08f74180759ef540ee94259213f503480}"     # 2026-09-19
: "${FCEUMM_REF:=7a542dab1e87679921962a9f056186eca425c0c2}"     # 2026-09-26
: "${MGBA_REF:=7a12d6d4b9acb14c0ae62c9166b6a2f3d08007f6}"       # 2026-09-16

# name | git url | folder with the libretro makefile | makefile | CMake options
# (for a checkout without that makefile) | pin variable
CORES="
gambatte|https://github.com/libretro/gambatte-libretro|.|Makefile||GAMBATTE_REF
snes9x|https://github.com/libretro/snes9x|libretro|Makefile||SNES9X_REF
fceumm|https://github.com/libretro/libretro-fceumm|.|Makefile||FCEUMM_REF
mgba|https://github.com/libretro/mgba|.|Makefile.libretro|-DLIBMGBA_ONLY=ON -DBUILD_LIBRETRO=ON|MGBA_REF
"

build() {
  local name="$1" url="$2" dir="$3" mk="$4" cmake_opts="$5" pinvar="$6"
  local tree="$SRC/$(basename "$url")"
  local ref="${!pinvar:-HEAD}" lib
  if [ ! -d "$tree/.git" ]; then
    git init -q "$tree"
    git -C "$tree" remote add origin "$url"
  fi
  echo ">> $name: fetching $url ($ref)"
  git -C "$tree" fetch --quiet --depth 1 origin "$ref"
  git -C "$tree" checkout --quiet FETCH_HEAD
  echo ">> $name: building ($(git -C "$tree" rev-parse --short HEAD))"
  if [ -f "$tree/$dir/$mk" ]; then
    make -C "$tree/$dir" -f "$mk" platform=libnx -j"$JOBS"
    lib="$tree/$dir/${name}_libretro_libnx.a"
  elif [ -n "$cmake_opts" ]; then
    # Newer mGBA builds its libretro core with CMake only (the same options
    # libretro's own libnx build uses).
    cmake -S "$tree" -B "$tree/build-libnx" -DCMAKE_TOOLCHAIN_FILE="$DEVKITPRO/cmake/Switch.cmake" \
          -DCMAKE_BUILD_TYPE=Release $cmake_opts -DLIBRETRO_STATIC=ON -DLIBRETRO_SUFFIX=_libnx
    cmake --build "$tree/build-libnx" --target "${name}_libretro" -j"$JOBS"
    lib="$tree/build-libnx/${name}_libretro_libnx.a"
  else
    echo ">> $name: no $dir/$mk in this checkout" >&2
    return 1
  fi
  cp "$lib" "$OUT/"
  echo ">> $name: $OUT/${name}_libretro_libnx.a"
}

wanted="${*:-gambatte snes9x fceumm mgba}"
while IFS='|' read -r name url dir mk cmake_opts pinvar; do
  [ -n "$name" ] || continue
  case " $wanted " in *" $name "*) build "$name" "$url" "$dir" "$mk" "$cmake_opts" "$pinvar" ;; esac
done <<< "$CORES"
ls -la "$OUT"
