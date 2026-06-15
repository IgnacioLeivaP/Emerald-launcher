#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool ui_init(int screen_w, int screen_h);
void ui_shutdown(void);
/* Load a TTF font for all text rendering. Call once after ui_init. */
bool ui_load_font(const char *ttf_path);
/* Load a background image (JPG/PNG) rendered tiled behind all UI. */
bool ui_load_bg(const char *img_path);
/* Enable/disable background rendering in ui_end (disable during gameplay). */
void ui_set_draw_bg(bool enable);

void ui_begin(void);
void ui_end(void);

void ui_rect(float x, float y, float w, float h, float r, float g, float b, float a);
void ui_text(float x, float y, float scale, const char *str, float r, float g, float b);
/* Draw image fit-scaled within a box, centered. brightness 0.0-1.0. Cached after first load. */
void ui_image(float x, float y, float max_w, float max_h, const char *path, float brightness);
/* Returns pixel dimensions of a (cached) image. */
bool ui_image_size(const char *path, int *out_w, int *out_h);
/* Clip all put_pixel calls to a sub-rect; ui_clear_clip restores full canvas. */
void ui_set_clip(float x, float y, float w, float h);
void ui_clear_clip(void);

#ifdef __cplusplus
}
#endif
