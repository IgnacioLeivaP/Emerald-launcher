#pragma once
/* Save states and screenshots of the running game.

   States live in saves/states/: "<stem>.state" is the slot the pause menu
   saves and loads (same name RetroArch uses for slot 0), "<stem>.auto.state"
   is written when leaving a game so it can continue from there next time.
   Each state has a "<state>.png" thumbnail of the moment it was saved.

   Screenshots go to a "captures" folder next to the ROM, named
   "<stem>-YYYYMMDD-HHMMSS.png"; db_load picks them up as extra screenshots
   of that version (and as its box art when db.json lists none). */
#include <string>

std::string state_path(const std::string &saves_dir, const std::string &stem, bool autosave);
std::string state_thumb(const std::string &state_path);

bool      state_save(const std::string &path);     /* running core → file (+ thumbnail) */
bool      state_load(const std::string &path);     /* file → running core */
long long state_time(const std::string &path);     /* when it was saved, 0 = none */
void      state_delete(const std::string &path);

bool png_save(const std::string &path, const unsigned char *rgba, int w, int h);
/* Current game frame → <captures_dir>/<stem>-YYYYMMDD-HHMMSS.png. Returns the
   file written, or "" on failure. */
std::string screenshot_save(const std::string &captures_dir, const std::string &stem);
std::string captures_dir_for(const std::string &rom_path);

/* "just now", "5 min ago", "yesterday"... (translated). */
std::string time_ago(long long when);
