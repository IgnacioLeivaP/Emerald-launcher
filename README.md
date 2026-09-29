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
- **Ancient Stone Tablets mode** — recreates the BS Zelda 4-week Satellaview broadcast:
  the launcher auto-detects when you've finished a week and carries your save into the
  next one ([details below](#ancient-stone-tablets--automatic-weekly-progression)).

> **No ROMs, BIOS files, or emulator cores are included in this repository.** You must
> supply your own legally-obtained game files, and build or download the libretro cores
> yourself (see below). The bundled UI icons/boot graphics are Zelda-themed placeholders —
> replace them with your own art if you redistribute a build.

---

## Download — prebuilt Switch build

The [**Releases**](https://github.com/IgnacioLeivaP/Emerald-launcher/releases) page has a
prebuilt **`.nro`** — the ready-to-run **Nintendo Switch homebrew executable** (the format
the Homebrew Menu loads). You don't need to compile anything to try it on a Switch:

1. Download
   [`emerald_launcher.nro`](https://github.com/IgnacioLeivaP/Emerald-launcher/releases/latest)
   and copy it to `sdmc:/switch/emerald_launcher.nro`.
2. Put your own content in `sdmc:/emerald/` — `db.json`, your `roms/` and `saves/`
   (see [Content layout](#content-layout-your-files)).
3. For N64 titles, install RetroArch with its cores in `sdmc:/retroarch/cores/`.

The NRO bundles only the launcher, its UI assets, and the statically-linked open-source
cores (gambatte / snes9x / fceumm / mGBA) — **no ROMs, BIOS, or game artwork are included**.

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
branding.json   Optional: app name, window icon, splash/background/font overrides
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

An optional `branding.json` next to `db.json` lets you customize the app without
recompiling — window title, window icon, splash screen, background image and font.
Any field left empty (or the whole file, if absent) falls back to the built-in
default:

```json
{
  "app_name": "My Launcher",
  "assets": {
    "icon":       "branding/icon.png",
    "splash":     "branding/splash.png",
    "background": "branding/background.jpg",
    "font":       "branding/font.ttf"
  }
}
```

Asset paths are relative to the content root, same as `db.json`'s image paths. Put
your custom files under a `branding/` folder (as in the example above) — `package.sh`
and `package-switch.sh` copy that folder into the distribution automatically, the
same way they already copy `imgs/`; assets referenced from elsewhere are your own
responsibility to bundle. Note this only covers what can change at runtime: the icon
**embedded in the .exe** file itself (Windows Explorer/shortcut) and the name/icon
shown on the **Switch Home menu** are baked in at build time and still require
recompiling/repackaging.

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

## Ancient Stone Tablets — automatic weekly progression

*BS The Legend of Zelda: Ancient Stone Tablets* was a Satellaview game broadcast in Japan
over **four consecutive weeks** in 1997, each week adding new dungeons and story. The
launcher recreates that event automatically so you can play it end-to-end today:

- The four weeks are one **sequential** group — each week unlocks once you've cleared the
  previous one, so you play them in order.
- While in-game, the launcher watches the cartridge RAM and **detects when you've collected
  that week's stone tablets** (via each entry's `week_complete_mask`). A `W1▸W2` badge then
  appears in the corner.
- Press **R3** (or `Tab`) to advance to the next week — your progress is **carried forward**
  automatically (`carry_save_from` copies the previous week's save into the new one), just
  like the original broadcast continued your file.
- Press **L3** (or `Esc`) any time to return to the launcher.

This is driven entirely by `db.json`: set `"sequential": true` on the group and give each
entry a `week_complete_mask` and `carry_save_from`. No code changes needed to author a
similar multi-part event for other games.

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
