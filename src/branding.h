#pragma once
#include <string>

/* App-level branding/customization, loaded from an optional branding.json
   that lives next to db.json (same content-root convention: sdmc:/emerald/
   on Switch, the working dir on PC).

   Every asset field is empty by default, meaning "use the built-in asset
   shipped with the launcher" (imgs/BOOTLOGO.png, imgs/fabric-green.jpg,
   alagard.ttf — see paths.h's ASSET()). A non-empty field is a path the
   user supplied, resolved the same way db.json resolves logo/screenshot
   paths: relative to the content root, not to the bundled romfs. */
struct Branding {
    std::string app_name = "Emerald Launcher";
    std::string icon_path;         /* runtime window icon (PNG); .exe/NACP icons are build-time, untouched */
    std::string splash_path;
    std::string background_path;
    std::string font_path;
    std::string music_path;        /* menu music, .ogg or .wav (none by default) */
    float       music_volume = 0.6f;
};

/* "theme": { "accent": "#F2C726", "accent_dim": ..., "panel": ...,
   "spot": ..., "floor": ..., "glow": ... } → g_theme (uikit.h). Colors
   that are missing or malformed keep their default. */
void branding_apply_theme(const char *path);

/* Reads branding.json at `path`; returns defaults if the file is missing,
   unreadable, or missing individual fields. Never throws. */
Branding branding_load(const char *path);
