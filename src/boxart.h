#pragma once
/* Procedural box art for the 3D shelf.

   Every version (GameEntry) gets its own box. Its textures are composed on
   the CPU with the ui_* primitives from what db.json already provides —
   first screenshot as cover art, the (version) logo, platform, version name,
   year — and uploaded as GL textures:
     - atlas: front cover + spine (see scene3d.h for the layout)
     - back:  description, screenshots, barcode… (only built when inspected)
   An optional "cover" image in db.json replaces the generated front.

   Generation is queued and spread over frames (boxart_pump) so the UI never
   stalls; callers get 0 until a texture is ready and draw a plain box. */
#include "db.h"
#include <string>

unsigned boxart_atlas(const GameGroup &g, int entry_idx);
unsigned boxart_back(const GameGroup &g, int entry_idx);

/* Build queued textures for up to `budget_ms` (always at least one). */
void boxart_pump(double budget_ms);
/* True while textures are still waiting to be generated. */
bool boxart_busy(void);
/* Free every texture (e.g. while a game runs, or when the db is reloaded). */
void boxart_release(void);

/* Banner / placeholder color for a platform string ("SNES (Satellaview)"…). */
void boxart_platform_color(const std::string &platform, float rgb[3]);
