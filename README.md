# Emerald Launcher

A small, controller-driven game launcher for a curated *Legend of Zelda* collection,
running libretro cores **in-process**. Built in C/C++ with SDL2 + OpenGL, it runs on
**Windows/Linux desktop** and as **Nintendo Switch homebrew** (`.nro`) from the same
source tree.

- **Emerald Launcher 2.0 — the 3D shelf**: every game is a game box you can browse, pick
  up and turn around. Games with several versions show up as a *stack* of boxes, so you
  can see what's inside before pressing anything ([details below](#emerald-launcher-20--the-3d-shelf)).
- Browse games grouped by title, with per-version logos, screenshots and descriptions
  (the original list view is still available as **Classic list**).
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

## Emerald Launcher 2.0 — the 3D shelf

The launcher opens on a shelf of 3D game boxes standing on a glossy stage. Each box has
the shape and look of its platform's real retail box:

| Platform | Box |
|----------|-----|
| NES | tall cardboard box, black grid front with the red platform tab |
| SNES / Satellaview | wide cardboard box with the grey side band and purple stripes |
| N64 | wide cardboard box, black top band with the colored platform letters |
| Game Boy / Color / Advance | small cardboard boxes (white GB / GBC fronts, indigo GBA band) |
| CD-i | black plastic clamshell with a printed insert, like a Mega Drive / VHS case |
| PC ports | big cardboard box |

Cardboard boxes are matte with rounded folds, a printed spine and top flap, a little
grain and scuffed edges; the CD-i case is glossy plastic. The art is generated from what
`db.json` already has — the version's logo, its first screenshot as cover art, the
platform and a ribbon with the version name — and each box also gets a back cover.

**You always know whether a game has other versions.** A game with more than one
version is a fanned **stack** of boxes (one per version, each with its own cover), the
focused stack carries a `3 VERSIONS` sticker, the neighbors show one ◆ per version, and
the info panel lists every version by name. The button hint says what will happen:
`Play` for a single-version game (it launches right away) or `Versions (3)`.

| View | What it shows |
|------|---------------|
| **Shelf** | Cover-flow of games. **A** plays a single-version game, or opens a stack. |
| **Versions** | The stack fans out; pick a version (description + screenshots below) and play it. Sequential games (Ancient Stone Tablets) show their weeks here: locked weeks are greyed out with a padlock, completed ones can be replayed. |
| **Look at box** | Picks the focused box up and turns it over: the back cover has the description, screenshots and version notes. Turn it around with ◀ ▶, the right stick or the mouse. |

The version you play last becomes the front of its stack, and the launcher reopens on
the game you were on.

### Controls

| Action | Switch | Xbox pad (PC) | Keyboard | Mouse |
|--------|--------|---------------|----------|-------|
| Browse games / versions | D-pad or left stick | D-pad or left stick | ← → (or A / D) | wheel, click a box |
| Open versions / play | A | A | Enter | click the focused box |
| Back | B | B | Esc / Backspace | right click |
| Look at the box | X | Y | Space (or I) | — |
| Turn the box (while looking) | ◀ ▶ / right stick | ◀ ▶ / right stick | ← → | drag |
| Turn the focused box a bit | right stick | right stick | — | drag |
| Jump 5 games | L / R | LB / RB | PgUp / PgDn (Q / E) | — |
| Settings | Y or + | X or Start | Tab | — |

On the Switch the touch screen works too (tap a box). In-game controls are unchanged
(L3 / `Esc` opens the return-to-launcher prompt).

### Settings: 3D shelf or classic list

**Settings** (Y / Tab) now has a **Launcher view** row to switch between the *3D Shelf*
and the *Classic list* (the original carousel, which now also shows a version-count badge
and lists the versions). The choice is saved in `prefs.json`. If the 3D renderer can't
start on some GPU, the launcher falls back to the classic list by itself.

### Your own box art (optional)

Any game or version in `db.json` can use a real box scan instead of the generated front:

```json
"zelda3": {
  "title": "A Link to the Past",
  "cover": "roms/Z3/box_front.png",
  "entries": [
    { "stem": "zeldatriforce", "title": "ALTTP Redux", "cover": "roms/Z3/redux_box.png" }
  ]
}
```

A version's `cover` is used as-is; a game-level `cover` is shared by all its versions
(each one still gets its version ribbon on top). The image's shape picks the box
orientation, so a portrait scan of a Super Famicom box gets a portrait box. The `maker/` editor has fields for both.
Box art is built in the background (during the boot logo), so large libraries don't slow
the launcher down.

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

Asset paths are relative to the content root, same as `db.json`'s image paths (the
optional per-game / per-version `cover` images of the 3D shelf work the same way). Put
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
- On the 3D shelf, **A** on the game opens its four weeks with the current one in front.
  Locked weeks show a padlock; a completed week can be replayed at any time (progress
  never moves backwards).

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

`-DEL_GLES=ON` builds the desktop version against **OpenGL ES 3.0** instead (needs a
GLES driver such as Mesa's): the exact renderer path the Switch uses, shaders included,
so Switch rendering can be checked on a PC.

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
