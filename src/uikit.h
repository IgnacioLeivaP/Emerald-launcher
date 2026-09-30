#pragma once
/* Launcher-level UI building blocks shared by the classic list and the 3D
   shelf: palette, semantic inputs, button glyphs and the bottom hint bar. */

/* Colors: emerald and gold by default, overridable by the "theme" section
   of branding.json (see branding.h). */
struct UiTheme {
    float accent[3]     = {0.95f, 0.78f, 0.15f};    /* gold: titles, focus, chips  */
    float accent_dim[3] = {0.70f, 0.56f, 0.10f};    /* section labels              */
    float panel[3]      = {0.03f, 0.07f, 0.05f};    /* panels and the hint bar     */
    float spot[3]       = {0.20f, 0.52f, 0.32f};    /* 3D stage spotlight          */
    float floor[3]      = {0.004f, 0.022f, 0.015f}; /* 3D stage floor              */
    float glow[3]       = {0.95f, 0.70f, 0.18f};    /* glow around the focused box */
};
extern UiTheme g_theme;

#define GOLD_R  (g_theme.accent[0])
#define GOLD_G  (g_theme.accent[1])
#define GOLD_B  (g_theme.accent[2])
#define GOLD_DIM_R (g_theme.accent_dim[0])
#define GOLD_DIM_G (g_theme.accent_dim[1])
#define GOLD_DIM_B (g_theme.accent_dim[2])
#define PANEL_R (g_theme.panel[0])
#define PANEL_G (g_theme.panel[1])
#define PANEL_B (g_theme.panel[2])

/* Logical screen (everything is laid out in 1280x720). */
#define UI_W 1280.0f
#define UI_H  720.0f
#define HINT_BAR_H 28.0f

/* Device-independent inputs the views react to. */
enum UiInput {
    IN_UP, IN_DOWN, IN_LEFT, IN_RIGHT,
    IN_CONFIRM, IN_BACK, IN_INSPECT, IN_SETTINGS,
    IN_PAGE_PREV, IN_PAGE_NEXT,
    IN_FAVORITE
};

/* Which glyphs to show in hints: the device the user touched last. */
enum PadStyle { STYLE_KEYBOARD, STYLE_XBOX, STYLE_NINTENDO };

enum HintBtn {
    HB_CONFIRM, HB_BACK, HB_INSPECT, HB_SETTINGS,
    HB_DPAD_H, HB_DPAD_V, HB_DPAD, HB_FULLSCREEN, HB_STICK_R,
    HB_FAVORITE          /* Select / Back / - (F on the keyboard) */
};

struct Hint {
    HintBtn     btn;
    const char *label;
};

/* Draw a button glyph with its left edge at x, centered on cy; returns its width. */
float uikit_glyph(float x, float cy, HintBtn btn, PadStyle style, float alpha);
/* Bottom hint bar: glyph+label pairs from the left, optional text on the right. */
void  uikit_hint_bar(const Hint *hints, int count, PadStyle style, const char *right_text);
/* Small padlock icon centered at (cx, cy). */
void  uikit_padlock(float cx, float cy, float size, float alpha);
/* Rounded "chip" with centered text; returns its width. filled = gold chip. */
float uikit_chip(float x, float y, const char *text, int px, bool filled, float alpha);
float uikit_chip_width(const char *text, int px);
/* Five-pointed star (favorites) centered at (cx, cy). */
void  uikit_star(float cx, float cy, float r, float red, float green, float blue, float alpha);
