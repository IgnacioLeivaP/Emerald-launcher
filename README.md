# Emerald Launcher

[![CI](https://github.com/IgnacioLeivaP/Emerald-launcher/actions/workflows/ci.yml/badge.svg)](https://github.com/IgnacioLeivaP/Emerald-launcher/actions/workflows/ci.yml)

A small, controller-driven game launcher for a curated *Legend of Zelda* collection,
running libretro cores **in-process**. Built in C/C++ with SDL2 + OpenGL, it runs on
**Windows/Linux desktop** and as **Nintendo Switch homebrew** (`.nro`) from the same
source tree.

- **Emerald Launcher 2.0 — the 3D shelf**: every game is a game box you can browse, pick
  up and turn around, shaped like its platform's real retail box (NES, SNES and N64
  cardboard boxes, small Game Boy boxes, a CD-i plastic case). Games with several
  versions show up as a *stack* of boxes, so you can see what's inside before pressing
  anything ([details below](#emerald-launcher-20--the-3d-shelf)).
- Browse games grouped by title, with per-version logos, screenshots and descriptions
  (the original list view is still available as **Classic list**). Mark **favorites**,
  sort by title, year, platform or what you played last, and let the shelf browse by
  itself when idle (**attract mode**).
- Light systems (NES, SNES, Game Boy/Color, GBA) emulate **inside the launcher** via
  libretro cores; heavy systems (N64) are delegated to **RetroArch** via chainload.
- An **in-game menu** with **save states**, **Continue where you left off**,
  **screenshots**, live shader switching and **button remapping**; up to **four
  players**; audio with **dynamic rate control** (no pops or crackles).
- **Play time and saves** shown on every game; **English and Spanish** UI (and
  per-language descriptions in `db.json`); colors and **menu music** configurable in
  `branding.json`.
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
the game you were on. The info panel also shows how long you've played a game and when;
version chips carry a dot when that version has a saved game and a ▶ when it can
**continue** from where you left it.

**Favorites and order.** `F` (keyboard), `−` (Switch) or `Back`/`View` (Xbox) marks the
focused game as a favorite (★ next to its title). **Settings → Order** sorts the shelf by
db.json order, title, year, platform, recently played or most played; **Show** switches
between all games and favorites only.

**Attract mode.** After a minute without input the shelf browses by itself, turning each
box, until any button is pressed (on by default, **Settings → Attract mode**).

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
| Favorite | − | Back / View | F | — |
| Settings | Y or + | X or Start | Tab | — |
| Performance info | (Settings) | (Settings) | F3 | — |

On the Switch the touch screen works too (tap a box).

In-game:

| Action | Switch | Xbox pad (PC) | Keyboard |
|--------|--------|---------------|----------|
| In-game menu | L3 | L3 | Esc |
| Next week (Ancient Stone Tablets, once complete) | R3 | R3 | Tab |
| Screenshot | (menu) | (menu) | F12 |

The game's own buttons can be changed in **Controls** (see below).

### In-game menu

L3 / `Esc` pauses the game and opens its menu:

- **Resume**
- **Save state / Load state** — one slot per version (`saves/states/<stem>.state`), with a
  picture of the saved moment and how long ago it was saved.
- **Take screenshot** — saved at the game's display aspect in a `captures` folder next to
  the ROM (`roms/Z1/captures/Zelda1original-20260930-101500.png`). New screenshots show
  up with that version's screenshots right away, and become its box art when `db.json`
  lists none.
- **Shader** — change it live with ◀ ▶ (remembered per game).
- **Controls**, **Performance info**, **Reset game**, **Next week** (sequential games) and
  **Return to launcher**.

**Continue where you left off.** Leaving a game (from the menu, or closing the window)
keeps an automatic state. The next time you play that version the launcher asks:
*Continue* (from that moment) or *Start game* (boot normally with your in-game save).

### Controls and players

**Settings → Controls…** (or **Controls** in the in-game menu) lists every game button
with its controller button and keyboard key: pick one, press the new button (L3 / `Esc`
cancel), or clear it. The mapping is saved in `input.json` next to `db.json`.

Every controller that connects becomes the next player (P1–P4, shown in the Controls
screen); the keyboard always plays as player 1. Two-player NES and SNES games work with a
second controller.

### Settings

**Settings** (Y / Tab) has the focused game's **shader** and **Clear Save Data**
(saves and save states), and options for all games:

| Row | Choices |
|-----|---------|
| View | *3D Shelf* or *Classic list* (the original carousel, with a version-count badge) |
| Order | db.json order, A-Z, year, platform, recently played, most played |
| Show | all games, or favorites only |
| Language | Auto (the system's), English, Español |
| Menu music | on / off (when `branding.json` provides a track) |
| Attract mode | on / off |
| Performance info | frame rate, core / render time, audio and memory, for testing on hardware |
| Controls… | the button mapping (see above) |

Everything is saved in `prefs.json` next to `db.json`. If the 3D renderer can't start on
some GPU, the launcher falls back to the classic list by itself.

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

Releases are built by CI: pushing a tag such as `v2.1.0` builds the NRO and the Windows
zip and publishes them. Every other push also builds them (Actions → the run →
*Artifacts*), together with screenshots of the main screens.

---

## Repository layout

```
src/            C/C++ source (shared across PC and Switch; platform code behind #ifdef)
include/        Vendored deps: nlohmann/json (MIT), stb_vorbis (public domain)
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
tests/          Unit tests (ctest)
tools/          check-i18n.py, check-shaders.py, build-switch-cores.sh, visual-test/
.github/        CI and release workflows
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
    "font":       "branding/font.ttf",
    "music":      "branding/menu.ogg"
  },
  "music_volume": 0.6,
  "theme": {
    "accent": "#F2C726", "accent_dim": "#B38F1A", "panel": "#08120D",
    "spot": "#338552", "floor": "#010604", "glow": "#F2B32E"
  }
}
```

`music` is an `.ogg` (streamed) or `.wav` that loops under the menus and fades out when a
game starts. `theme` recolors the UI (`accent`: titles, focus and chips; `accent_dim`:
section labels; `panel`: panels and the hint bar) and the 3D stage (`spot`: the light
behind the focused box, `floor`, `glow`: the focused box's outline); the values above are
the defaults.

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
roms/**/captures/  screenshots taken in-game, next to each ROM
saves/           .srm save files (created automatically)
saves/states/    save states and "continue" states, with their pictures
system/          BIOS files some cores need (e.g. Satellaview BIOS for BS-Zelda)
prefs.json       settings, favorites, last game (created automatically)
stats.json       play time per version
input.json       the button mapping
```

Texts in `db.json` can have a Spanish version next to the English one, shown when the
launcher is in Spanish: `title_es` and `description_es` on a game, `title_es` and
`version_desc_es` on a version (the `maker/` editor has fields for them).

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
  that week's stone tablets** (via each entry's `week_complete_mask`). A *Week 1 complete!*
  notice then appears in the corner.
- Press **R3** (or `Tab`) to advance to the next week (or pick **Next week** in the in-game
  menu) — your progress is **carried forward** automatically (`carry_save_from` copies the
  previous week's save into the new one), just like the original broadcast continued your
  file.
- Press **L3** (or `Esc`) any time for the in-game menu, and **Return to launcher** from it.
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

If the launcher is started from another folder (a shortcut, a file manager), it moves to
the executable's folder when `db.json` is there.

### Linux: external programs and RetroArch

External entries can name a Linux program next to the Windows `exe` and the Switch `nro`:

```json
"external": { "nro": "sdmc:/switch/soh/soh.nro", "exe": "C:/Games/SoH/soh.exe",
              "linux": "/home/me/soh/soh.elf", "argv": "" }
```

It's started with its arguments (quotes group them) without blocking; if it can't start,
the launcher says so and stays open. For N64 through RetroArch, `db.json`'s `retroarch`
section takes `exe_linux` (default `retroarch`, from the PATH) and `cores_linux` (default:
`~/.config/retroarch/cores`, then the usual system folders); cores are `<core>.so`.

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
`.a`. One script clones gambatte, snes9x, fceumm and mGBA into `cores-src/`, builds them
with `make platform=libnx` and copies the `<core>_libretro_libnx.a` files into
`switch/cores/`:

```bash
tools/build-switch-cores.sh            # or e.g. tools/build-switch-cores.sh snes9x
```

(`DEVKITPRO` defaults to `/opt/devkitpro`; set `SNES9X_REF=<commit>` etc. to pin a core.)

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

### Testing on the console

**Settings → Performance info** (also in the in-game menu) shows the frame rate and the
slowest frame, the time spent in the emulator core and in rendering (3D and 2D in the
launcher), the audio queue and its rate correction, and the memory in use. The log goes
to `sdmc:/emerald/emerald_log.txt`.

---

## Development

- **Unit tests** — built with the desktop version; run `ctest --test-dir build`. They
  cover `db.json` loading (platform filters, missing ROMs, captures, external and
  RetroArch entries), week unlocking, prefs / stats / input round trips, the audio rate
  control, translations and the box shapes.
- **Visual test** — `tools/visual-test/run.sh build/emerald_launcher out/` runs the
  launcher on placeholder content in a virtual display and saves screenshots of its main
  screens ([details](tools/visual-test/README.md)).
- **Checks** — `tools/check-shaders.py` compiles every shader as GLSL ES 3.00 (what the
  Switch runs); `tools/check-i18n.py` makes sure every UI string has a Spanish
  translation with matching `%` placeholders.
- **CI** — every push builds Linux (OpenGL and OpenGL ES), Windows (MSYS2) and the Switch
  NRO (devkitPro, with the static cores), runs the tests and checks, and uploads the
  Windows build, the NRO and the screenshots as artifacts. Pushing a `v*` tag publishes a
  GitHub Release with the NRO and the Windows zip.

---

## Legal

This project is a frontend only. It does not contain or distribute game ROMs, BIOS images,
or emulator cores. Game titles and trademarks belong to their respective owners. Provide
your own legally-obtained content, and obtain the libretro cores from their official
sources under their respective licenses.
