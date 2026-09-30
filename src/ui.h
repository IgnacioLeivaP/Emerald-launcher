#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── 2D UI layer ─────────────────────────────────────────────────────────
   Everything is drawn in software into a 1280x720 canvas that holds
   PREMULTIPLIED RGBA and is alpha-blended over the frame by ui_end().
   All primitives composite "source over" (so text on a translucent panel,
   a dimming rect over content, etc. blend the way you'd expect).

   The same primitives can draw into any other RGBA buffer via
   ui_target_begin()/ui_target_end() — used to compose the 3D box art. */

bool ui_init(int screen_w, int screen_h);
void ui_shutdown(void);
/* Load a TTF font for all text rendering. Call once after ui_init. */
bool ui_load_font(const char *ttf_path);
/* Load a background image (JPG/PNG) rendered tiled behind all UI. */
bool ui_load_bg(const char *img_path);
/* Enable/disable background rendering in ui_end (disable during gameplay). */
void ui_set_draw_bg(bool enable);
/* Draw the tiled background into the current GL framebuffer + viewport now
   (the 3D shelf renders it inside its own offscreen pass). */
void ui_draw_bg(void);

void ui_begin(void);
void ui_end(void);

/* Redirect drawing into `rgba` (w*h*4 bytes, premultiplied) until
   ui_target_end(). Clip is reset to the whole target. Not nestable. */
void ui_target_begin(unsigned char *rgba, int w, int h);
void ui_target_end(void);

void ui_rect(float x, float y, float w, float h, float r, float g, float b, float a);
/* Vertical gradient from (r0,g0,b0,a0) at the top to (r1,g1,b1,a1) at the bottom. */
void ui_gradient_v(float x, float y, float w, float h,
                   float r0, float g0, float b0, float a0,
                   float r1, float g1, float b1, float a1);
/* Anti-aliased shapes. */
void ui_round_rect(float x, float y, float w, float h, float radius,
                   float r, float g, float b, float a);
void ui_round_rect_outline(float x, float y, float w, float h, float radius, float thickness,
                           float r, float g, float b, float a);
void ui_circle(float cx, float cy, float radius, float r, float g, float b, float a);
void ui_ring(float cx, float cy, float radius, float thickness,
             float r, float g, float b, float a);
/* Filled triangle given its three corners (anti-aliased edges). */
void ui_triangle(float x0, float y0, float x1, float y1, float x2, float y2,
                 float r, float g, float b, float a);

/* Legacy text API: scale 1.0 = 16px (snapped to the preloaded sizes). */
void ui_text(float x, float y, float scale, const char *str, float r, float g, float b);

/* Text at an exact pixel size with opacity; (x,y) = top-left. */
void ui_text_px(float x, float y, int px, const char *str,
                float r, float g, float b, float a);
int  ui_text_width(int px, const char *str);
int  ui_text_line_height(int px);

enum { UI_ALIGN_LEFT = 0, UI_ALIGN_CENTER = 1, UI_ALIGN_RIGHT = 2 };
/* Word-wrapped text inside a column of width max_w starting at y.
   Stops after max_lines (0 = unlimited), ending the last line with "..." if
   the text didn't fit. Returns the y just below the last line drawn. */
float ui_text_wrap(float x, float y, float max_w, int px, float line_h, const char *str,
                   int align, int max_lines, float r, float g, float b, float a);
/* Same layout as ui_text_wrap without drawing: returns how many lines it takes. */
int   ui_text_wrap_lines(float max_w, int px, const char *str, int max_lines);

/* Draw image fit-scaled within a box, centered. brightness 0.0-1.0. Cached after first load. */
void ui_image(float x, float y, float max_w, float max_h, const char *path, float brightness);

enum {
    UI_IMG_FIT        = 0,   /* contain inside the box, centered (default) */
    UI_IMG_COVER      = 1,   /* fill the box, cropping the overflow        */
    UI_IMG_STRETCH    = 2,   /* fill the box, ignoring aspect ratio        */
    UI_IMG_SMOOTH     = 4,   /* filtered scaling (box filter / bilinear)   */
    UI_IMG_SILHOUETTE = 8    /* draw only the alpha mask, in (r,g,b)       */
};
/* General image draw: (r,g,b) multiply the image color (or ARE the color with
   UI_IMG_SILHOUETTE); a = opacity. */
void ui_image_ex(float x, float y, float w, float h, const char *path, int flags,
                 float r, float g, float b, float a);
/* Returns pixel dimensions of a (cached) image. */
bool ui_image_size(const char *path, int *out_w, int *out_h);

/* Clip all drawing to a sub-rect; ui_clear_clip restores the full target. */
void ui_set_clip(float x, float y, float w, float h);
void ui_clear_clip(void);

#ifdef __cplusplus
}
#endif
