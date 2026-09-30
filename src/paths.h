#pragma once

/* App assets (imgs, sounds, font) are bundled in the NRO's romfs on Switch and
   sit relative to the working dir on PC. ASSET() prefixes a string LITERAL at
   compile time (adjacent string-literal concatenation), so:
       ui_load_font(ASSET("alagard.ttf"))
   becomes "romfs:/alagard.ttf" on Switch, "alagard.ttf" on PC.

   User content (db.json, roms, saves, and the image paths db.json references)
   lives at sdmc:/emerald/ on Switch — handled in main.cpp / db.cpp, not here. */

#ifdef __SWITCH__
#  define ASSET_PREFIX "romfs:/"
#else
#  define ASSET_PREFIX ""
#endif

#define ASSET(p) (ASSET_PREFIX p)

/* User data root: db.json and everything next to it (prefs, progress, stats,
   roms, saves, cores, screenshots, save states). DATA("db.json") becomes
   "sdmc:/emerald/db.json" on Switch; on PC it is relative to the working
   directory, which main() moves to the executable's folder if db.json isn't
   in the current one. */
#ifdef __SWITCH__
#  define DATA_PREFIX "sdmc:/emerald/"
#else
#  define DATA_PREFIX ""
#endif

#define DATA(p) (DATA_PREFIX p)
