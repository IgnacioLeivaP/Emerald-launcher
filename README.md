# Emerald Launcher

A small, controller-driven game launcher for a curated *Legend of Zelda* collection,
running libretro cores **in-process**. Built in C/C++ with SDL2 + OpenGL, it runs on
**Windows/Linux desktop** and as **Nintendo Switch homebrew** (`.nro`) from the same
source tree.

- Browse games grouped by title, with per-version logos, screenshots and descriptions.
- Light systems (NES, SNES, Game Boy/Color, GBA) emulate **inside the launcher** via
  statically-linked libretro cores.
- Heavy systems (N64) are delegated to **RetroArch** via chainload.
- Custom CRT/bloom/ScaleFX shaders, save-RAM handling, and an in-game overlay.

> **No ROMs, BIOS files, or emulator cores are included in this repository.** You must
> supply your own legally-obtained game files, and build or download the libretro cores
> yourself (see below). The bundled UI icons/boot graphics are Zelda-themed placeholders —
> replace them with your own art if you redistribute a build.

---

## Repository layout

```
src/            C/C++ source (shared across PC and Switch; platform code behind #ifdef)
include/        Vendored header-only deps (nlohmann/json, MIT)
imgs/           UI chrome (background, tablet, icons)
sounds/         UI sound effects (.wav)
switch/         Switch icon + (gitignored) static core .a libraries
maker/          Small HTML helper to author db.json
db.json         The catalog: groups, entries, per-system core mapping, image paths
CMakeLists.txt  Cross-platform build (desktop OpenGL 3.3 / Switch GLES 3.0)
package.sh         Assemble a shippable Windows dist/ folder
package-switch.sh  Assemble the Switch NRO + sdmc:/emerald/ content folder
SWITCH_DEV_NOTES.md  Accumulated notes on the Switch port
```

`roms/`, `saves/`, `system/` (BIOS), `cores/`, `*.dll`, `switch/cores/*.a`, `cores-src/`
and the build/dist folders are **gitignored** — they are user content or generated/binary
artifacts.

---

## Content layout (your files)

The launcher reads `db.json` and scans a ROM directory. Image/screenshot paths in
`db.json` are relative to the content root.

```
roms/            your ROMs + the per-game logo/screenshot PNGs referenced by db.json
saves/           .srm save files (created automatically)
system/          BIOS files some cores need (e.g. Satellaview BIOS for BS-Zelda)
```

Supported extensions and their cores (from `db.json`):

| Ext            | System            | Core (PC dll / Switch)                 | Where it runs        |
|----------------|-------------------|----------------------------------------|----------------------|
| `.nes`         | NES               | `fceumm`                               | in-launcher          |
| `.smc` `.sfc`  | SNES              | `snes9x`                               | in-launcher          |
| `.gb` `.gbc`   | Game Boy / Color  | `gambatte`                             | in-launcher          |
| `.gba`         | Game Boy Advance  | `mgba`                                 | in-launcher          |
| `.n64` `.z64` `.v64` | Nintendo 64 | `mupen64plus_next`                     | RetroArch chainload  |
| `.cue`         | CD-i              | `same_cdi`                             | PC only (no Switch core) |

---

## Building — Desktop (Windows / Linux)

### Dependencies

- A C++17 compiler and CMake ≥ 3.20
- SDL2, SDL2_image, SDL2_ttf, OpenGL

On Windows the project is built with the **MSYS2 mingw64** toolchain:

```bash
pacman -S mingw-w64-x86_64-toolchain mingw-w64-x86_64-cmake \
          mingw-w64-x86_64-SDL2 mingw-w64-x86_64-SDL2_image mingw-w64-x86_64-SDL2_ttf
```

### Build

```bash
cmake -B build -S .
cmake --build build
```

### Cores (desktop)

Download the libretro core DLLs (e.g. from the RetroArch buildbot) and place them in a
`cores/` folder next to the executable, named as in `db.json`
(`fceumm_libretro.dll`, `snes9x_libretro.dll`, `gambatte_libretro.dll`, `mgba_libretro.dll`,
`mupen64plus_next_libretro.dll`, `same_cdi_libretro.dll`).

`./package.sh` assembles a standalone `dist/` folder (binary + assets + cores).

---

## Building — Nintendo Switch

### Dependencies (devkitPro)

Install [devkitPro](https://devkitpro.org/) with the **switch-dev** group, then add the
SDL2 portlibs (from the devkitPro MSYS2 / pacman):

```bash
pacman -S switch-dev switch-sdl2 switch-sdl2_image switch-sdl2_ttf
```

### 1. Build the static libretro cores

The in-launcher cores are linked statically, so each must be built for libnx as a
`.a`. For each core, clone its libretro source and build with `make platform=libnx`, then
copy the resulting `<core>_libretro_libnx.a` into `switch/cores/`:

```bash
# example: snes9x (repeat for gambatte, fceumm, mgba)
git clone --depth 1 https://github.com/libretro/snes9x
cd snes9x/libretro
DEVKITPRO=/opt/devkitpro make platform=libnx -j4
cp snes9x_libretro_libnx.a  <project>/switch/cores/
```

Cores currently wired into the build (`switch/cores/`):
`gambatte_libretro_libnx.a`, `snes9x_libretro_libnx.a`,
`fceumm_libretro_libnx.a`, `mgba_libretro_libnx.a`.

> **Multiple static cores** all export the same libretro symbols (`retro_init`, …) and a
> few internal globals (`PPU`, core-options tables), which would collide at link time. The
> build resolves this automatically with `objcopy` — each core's API symbols are renamed
> to a per-core prefix and the duplicated option tables are localized (see the
> `el_add_builtin_core` function in `CMakeLists.txt`). To add a new core: build its `.a`,
> copy it into `switch/cores/`, add one `el_add_builtin_core(...)` line in `CMakeLists.txt`
> and one `CORE_ENTRY(...)` in `src/core.c`.

### 2. Build the NRO

```bash
cmake -B build-switch -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/Switch.cmake
cmake --build build-switch
```

This produces `build-switch/emerald_launcher.nro`.

### 3. Package and install

```bash
./package-switch.sh
```

assembles `dist-switch/`. Copy onto the SD card:

```
dist-switch/emerald_launcher.nro  ->  sdmc:/switch/emerald_launcher.nro
dist-switch/emerald/              ->  sdmc:/emerald/        (db.json + roms + saves)
```

N64 titles are launched through RetroArch — install RetroArch with its cores in
`sdmc:/retroarch/cores/`.

See [`SWITCH_DEV_NOTES.md`](SWITCH_DEV_NOTES.md) for deeper notes on the port (GLES
shaders, paths, audio, aspect ratio, the multi-core setup, etc.).

---

## Legal

This project is a frontend only. It does not contain or distribute game ROMs, BIOS images,
or emulator cores. Game titles and trademarks belong to their respective owners. Provide
your own legally-obtained content, and obtain the libretro cores from their official
sources under their respective licenses.
