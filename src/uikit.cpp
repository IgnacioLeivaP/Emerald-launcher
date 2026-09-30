#include "uikit.h"
#include <cmath>
#include "i18n.h"
#include "ui.h"

UiTheme g_theme;

static const char *face_label(HintBtn b, PadStyle s) {
    /* Same physical positions everywhere (confirm, back, top = inspect,
       left = settings); only the printed letters differ per controller. */
    switch (b) {
    case HB_CONFIRM:  return "A";
    case HB_BACK:     return "B";
    case HB_INSPECT:  return s == STYLE_NINTENDO ? "X" : "Y";
    case HB_SETTINGS: return s == STYLE_NINTENDO ? "Y" : "X";
    case HB_FAVORITE: return s == STYLE_NINTENDO ? "-" : "=";
    default:          return "";
    }
}

static const char *key_label(HintBtn b) {
    switch (b) {
    case HB_CONFIRM:    return tr("Enter");
    case HB_BACK:       return "Esc";
    case HB_INSPECT:    return tr("Space");
    case HB_SETTINGS:   return "Tab";
    case HB_FULLSCREEN: return "F11";
    case HB_STICK_R:    return tr("Mouse");
    case HB_FAVORITE:   return "F";
    default:            return "";
    }
}

static float text_top(float cy, int px) {
    return cy - (float)ui_text_line_height(px) * 0.5f;
}

static float keycap(float x, float cy, const char *label, float a) {
    const int px = 14;
    float w = (float)ui_text_width(px, label) + 14.0f, h = 20.0f;
    ui_round_rect(x, cy - h * 0.5f, w, h, 4.0f, 0.62f, 0.62f, 0.58f, a);
    ui_round_rect(x + 1.0f, cy - h * 0.5f + 1.0f, w - 2.0f, h - 4.0f, 3.0f, 0.96f, 0.96f, 0.92f, a);
    ui_text_px(x + 7.0f, text_top(cy, px) - 2.0f, px, label, 0.12f, 0.12f, 0.12f, a);
    return w;
}

/* Keycap holding a small arrow (dir: 0 left, 1 right, 2 up, 3 down). */
static float arrow_key(float x, float cy, int dir, float a) {
    const float w = 20.0f, h = 20.0f;
    ui_round_rect(x, cy - h * 0.5f, w, h, 4.0f, 0.62f, 0.62f, 0.58f, a);
    ui_round_rect(x + 1.0f, cy - h * 0.5f + 1.0f, w - 2.0f, h - 4.0f, 3.0f, 0.96f, 0.96f, 0.92f, a);
    float cx = x + w * 0.5f, c = cy - 2.0f, s = 4.5f;
    const float r = 0.12f, g = 0.12f, b = 0.12f;
    switch (dir) {
    case 0: ui_triangle(cx - s, c, cx + s * 0.7f, c - s, cx + s * 0.7f, c + s, r, g, b, a); break;
    case 1: ui_triangle(cx + s, c, cx - s * 0.7f, c - s, cx - s * 0.7f, c + s, r, g, b, a); break;
    case 2: ui_triangle(cx, c - s, cx - s, c + s * 0.7f, cx + s, c + s * 0.7f, r, g, b, a); break;
    default: ui_triangle(cx, c + s, cx - s, c - s * 0.7f, cx + s, c - s * 0.7f, r, g, b, a); break;
    }
    return w;
}

/* Controller d-pad; lit arms show which directions the hint is about. */
static float dpad(float x, float cy, bool horiz, bool vert, float a) {
    const float arm = 7.0f, t = 6.0f;
    float cx = x + arm + t * 0.5f;
    ui_round_rect(cx - arm - t * 0.5f, cy - t * 0.5f, arm * 2.0f + t, t, 1.5f, 0.55f, 0.55f, 0.52f, a);
    ui_round_rect(cx - t * 0.5f, cy - arm - t * 0.5f, t, arm * 2.0f + t, 1.5f, 0.55f, 0.55f, 0.52f, a);
    if (horiz) {
        ui_round_rect(cx - arm - t * 0.5f, cy - t * 0.5f, arm, t, 1.5f, GOLD_R, GOLD_G, GOLD_B, a);
        ui_round_rect(cx + t * 0.5f, cy - t * 0.5f, arm, t, 1.5f, GOLD_R, GOLD_G, GOLD_B, a);
    }
    if (vert) {
        ui_round_rect(cx - t * 0.5f, cy - arm - t * 0.5f, t, arm, 1.5f, GOLD_R, GOLD_G, GOLD_B, a);
        ui_round_rect(cx - t * 0.5f, cy + t * 0.5f, t, arm, 1.5f, GOLD_R, GOLD_G, GOLD_B, a);
    }
    return arm * 2.0f + t;
}

float uikit_glyph(float x, float cy, HintBtn btn, PadStyle style, float a) {
    if (style == STYLE_KEYBOARD) {
        switch (btn) {
        case HB_DPAD_H: { float w = arrow_key(x, cy, 0, a); return w + 3.0f + arrow_key(x + w + 3.0f, cy, 1, a); }
        case HB_DPAD_V: { float w = arrow_key(x, cy, 2, a); return w + 3.0f + arrow_key(x + w + 3.0f, cy, 3, a); }
        case HB_DPAD:   return keycap(x, cy, tr("Arrows"), a);
        default:        return keycap(x, cy, key_label(btn), a);
        }
    }
    switch (btn) {
    case HB_DPAD_H: return dpad(x, cy, true, false, a);
    case HB_DPAD_V: return dpad(x, cy, false, true, a);
    case HB_DPAD:   return dpad(x, cy, true, true, a);
    case HB_FULLSCREEN: return keycap(x, cy, "F11", a);
    case HB_STICK_R: {
        const float r = 10.0f;
        ui_circle(x + r, cy, r, 0.30f, 0.30f, 0.28f, a);
        ui_ring(x + r, cy, r, 2.0f, 0.85f, 0.85f, 0.80f, a);
        ui_text_px(x + r - (float)ui_text_width(13, "R") * 0.5f, text_top(cy, 13), 13, "R",
                   0.95f, 0.95f, 0.92f, a);
        return r * 2.0f;
    }
    default: {
        const float r = 10.0f;
        const char *l = face_label(btn, style);
        bool primary = btn == HB_CONFIRM;
        if (primary) ui_circle(x + r, cy, r, GOLD_R, GOLD_G, GOLD_B, a);
        else         ui_circle(x + r, cy, r, 0.92f, 0.92f, 0.88f, a);
        ui_text_px(x + r - (float)ui_text_width(14, l) * 0.5f, text_top(cy, 14), 14, l,
                   0.10f, 0.08f, 0.04f, a);
        return r * 2.0f;
    }
    }
}

void uikit_hint_bar(const Hint *hints, int count, PadStyle style, const char *right_text) {
    const float y = UI_H - HINT_BAR_H;
    const float cy = y + HINT_BAR_H * 0.5f;
    ui_rect(0.0f, y, UI_W, HINT_BAR_H, PANEL_R, PANEL_G, PANEL_B, 0.92f);
    ui_rect(0.0f, y - 1.0f, UI_W, 1.0f, GOLD_R, GOLD_G, GOLD_B, 0.60f);
    float x = 20.0f;
    for (int i = 0; i < count; i++) {
        x += uikit_glyph(x, cy, hints[i].btn, style, 1.0f) + 7.0f;
        ui_text_px(x, text_top(cy, 16), 16, hints[i].label, 0.82f, 0.82f, 0.78f, 1.0f);
        x += (float)ui_text_width(16, hints[i].label) + 22.0f;
    }
    if (right_text && right_text[0]) {
        float w = (float)ui_text_width(16, right_text);
        ui_text_px(UI_W - 20.0f - w, text_top(cy, 16), 16, right_text, 0.62f, 0.62f, 0.60f, 1.0f);
    }
}

void uikit_padlock(float cx, float cy, float s, float a) {
    ui_ring(cx, cy - s * 0.10f, s * 0.30f, s * 0.11f, 0.86f, 0.86f, 0.82f, a);
    ui_round_rect(cx - s * 0.42f, cy - s * 0.06f, s * 0.84f, s * 0.60f, s * 0.10f,
                  GOLD_R, GOLD_G, GOLD_B, a);
    ui_circle(cx, cy + s * 0.18f, s * 0.08f, 0.22f, 0.13f, 0.02f, a);
    ui_rect(cx - s * 0.03f, cy + s * 0.20f, s * 0.06f, s * 0.14f, 0.22f, 0.13f, 0.02f, a);
}

float uikit_chip_width(const char *text, int px) {
    return (float)ui_text_width(px, text) + 24.0f;
}

float uikit_chip(float x, float y, const char *text, int px, bool filled, float a) {
    float w = uikit_chip_width(text, px), h = (float)px + 12.0f;
    float ty = y + (h - (float)ui_text_line_height(px)) * 0.5f;
    if (filled) {
        ui_round_rect(x, y, w, h, h * 0.5f, GOLD_R, GOLD_G, GOLD_B, a);
        ui_text_px(x + 12.0f, ty, px, text, 0.16f, 0.10f, 0.02f, a);
    } else {
        ui_round_rect(x, y, w, h, h * 0.5f, 0.02f, 0.06f, 0.04f, 0.78f * a);
        ui_round_rect_outline(x, y, w, h, h * 0.5f, 1.5f, GOLD_R, GOLD_G, GOLD_B, 0.75f * a);
        ui_text_px(x + 12.0f, ty, px, text, 0.93f, 0.93f, 0.90f, a);
    }
    return w;
}

void uikit_star(float cx, float cy, float r, float red, float green, float blue, float a) {
    /* Five spikes around a pentagon core. */
    const float PI = 3.14159265f, ri = r * 0.42f;
    float ox[5], oy[5], ix[5], iy[5];
    for (int i = 0; i < 5; i++) {
        const float ao = -PI * 0.5f + (float)i * 2.0f * PI / 5.0f;
        const float ai = ao + PI / 5.0f;
        ox[i] = cx + cosf(ao) * r;  oy[i] = cy + sinf(ao) * r;
        ix[i] = cx + cosf(ai) * ri; iy[i] = cy + sinf(ai) * ri;
    }
    for (int i = 0; i < 5; i++) {
        const int p = (i + 4) % 5;                      /* inner point before this spike */
        ui_triangle(ox[i], oy[i], ix[p], iy[p], ix[i], iy[i], red, green, blue, a);
        ui_triangle(cx, cy, ix[p], iy[p], ix[i], iy[i], red, green, blue, a);
    }
}
