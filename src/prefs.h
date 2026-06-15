#pragma once
#include <string>

void prefs_load(void);
void prefs_save(void);

/* Shader IDs: 0=None, 1=ScaleFX-9x, 2=Scanlines, 3=CRT, 4=LCD, 5=Bloom */
int  prefs_get_shader(const std::string &group_key);
void prefs_set_shader(const std::string &group_key, int shader_id);
