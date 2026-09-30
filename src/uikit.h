#pragma once
/* Launcher-level UI building blocks shared by the classic list and the 3D
   shelf: palette, semantic inputs, button glyphs and the bottom hint bar. */

/* Color palette */
#define GOLD_R  0.95f
#define GOLD_G  0.78f
#define GOLD_B  0.15f
#define GOLD_DIM_R 0.70f
#define GOLD_DIM_G 0.56f
#define GOLD_DIM_B 0.10f
#define PANEL_R 0.03f
#define PANEL_G 0.07f
#define PANEL_B 0.05f

/* Logical screen (everything is laid out in 1280x720). */
#define UI_W 1280.0f
#define UI_H  720.0f
#define HINT_BAR_H 28.0f

/* Device-independent inputs the views react to. */
enum UiInput {
    IN_UP, IN_DOWN, IN_LEFT, IN_RIGHT,
    IN_CONFIRM, IN_BACK, IN_INSPECT, IN_SETTINGS,
    IN_PAGE_PREV, IN_PAGE_NEXT
};

/* Which glyphs to show in hints: the device the user touched last. */
enum PadStyle { STYLE_KEYBOARD, STYLE_XBOX, STYLE_NINTENDO };

enum HintBtn {
    HB_CONFIRM, HB_BACK, HB_INSPECT, HB_SETTINGS,
    HB_DPAD_H, HB_DPAD_V, HB_DPAD, HB_FULLSCREEN, HB_STICK_R
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
