#pragma once
/* Procedural box art for the 3D shelf.

   Every version (GameEntry) gets its own box, shaped like its platform's real
   retail box (see boxshape.h): cardboard for the cartridge systems (SNES and
   N64 landscape, NES / Game Boy portrait…), a plastic clamshell for CD-i.
   Its textures are composed on the CPU with the ui_* primitives from what
   db.json already provides — first screenshot as cover art, the (version)
   logo, platform, version name, year — then aged a little (cardboard grain,
   worn edges) and uploaded as GL textures:
     - atlas: front, spine, top and edge color (layout in boxshape.h)
     - back:  description, screenshots, barcode… (only built when inspected)
   An optional "cover" image in db.json replaces the generated front, and its
   aspect ratio picks the box orientation.

   Generation is queued and spread over frames (boxart_pump) so the UI never
   stalls; callers get 0 until a texture is ready and draw a plain box. */
#include "db.h"
#include <string>

/* Box shape id (boxshape.h) for entry `entry_idx` of a game. */
int      boxart_shape(const GameGroup &g, int entry_idx);
unsigned boxart_atlas(const GameGroup &g, int entry_idx);
unsigned boxart_back(const GameGroup &g, int entry_idx);

/* Build queued textures for up to `budget_ms` (always at least one). */
void boxart_pump(double budget_ms);
/* True while textures are still waiting to be generated. */
bool boxart_busy(void);
/* Free every texture (e.g. when the db is reloaded or the language changes). */
void boxart_release(void);

/* Banner / placeholder color for a platform string ("SNES (Satellaview)"…). */
void boxart_platform_color(const std::string &platform, float rgb[3]);
