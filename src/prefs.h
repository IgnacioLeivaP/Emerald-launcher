#pragma once
#include <string>

void prefs_load(void);
void prefs_save(void);

/* Shader IDs: 0=None, 1=ScaleFX-9x, 2=Scanlines, 3=CRT, 4=LCD, 5=Bloom */
int  prefs_get_shader(const std::string &group_key);
void prefs_set_shader(const std::string &group_key, int shader_id);

/* Launcher view: true = 3D shelf (default), false = classic list. */
bool prefs_get_view_3d(void);
void prefs_set_view_3d(bool on);

/* Where the user left off: last selected game, last version played per game. */
std::string prefs_get_last_group(void);
void        prefs_set_last_group(const std::string &group_key);
int         prefs_get_last_entry(const std::string &group_key);   /* -1 = none */
void        prefs_set_last_entry(const std::string &group_key, int entry_idx);

/* UI language: "auto" (system), "en" or "es". */
std::string prefs_get_language(void);
void        prefs_set_language(const std::string &code);
