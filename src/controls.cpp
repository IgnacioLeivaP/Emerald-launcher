#include "controls.h"
#include "i18n.h"
#include "input.h"
#include "sfx.h"
#include "toast.h"
#include "ui.h"
#include <cmath>
#include <cstdio>
#include <string>

namespace {

/* Display order: directions, face buttons, shoulders, start / select. */
const int ORDER[] = { RB_UP, RB_DOWN, RB_LEFT, RB_RIGHT, RB_A, RB_B, RB_X, RB_Y,
                      RB_L, RB_R, RB_L2, RB_R2, RB_START, RB_SELECT };
const int NROWS = (int)(sizeof(ORDER) / sizeof(ORDER[0])) + 1;     /* + "Reset" */
const Uint32 CAPTURE_MS = 6000;

bool   s_open = false;
int    s_row = 0;
int    s_col = 0;               /* 0 = controller, 1 = keyboard */
bool   s_capture = false;
Uint32 s_capture_tick = 0;

bool is_reset_row(void) { return s_row == NROWS - 1; }

void stop_capture(void) { s_capture = false; }

void bound(void) {
    input_save();
    stop_capture();
    sfx_play_confirm();
}

bool reserved_key(SDL_Scancode sc) {
    switch (sc) {
    case SDL_SCANCODE_ESCAPE: case SDL_SCANCODE_TAB: case SDL_SCANCODE_F3:
    case SDL_SCANCODE_F11: case SDL_SCANCODE_F12:
        return true;
    default:
        return false;
    }
}

} // namespace

void controls_open(void) {
    s_open = true;
    s_row = 0;
    s_col = 0;
    s_capture = false;
}

void controls_close(void) { s_open = false; s_capture = false; }
bool controls_is_open(void) { return s_open; }

bool controls_capturing(void) {
    if (s_capture && SDL_GetTicks() - s_capture_tick > CAPTURE_MS) stop_capture();
    return s_open && s_capture;
}

bool controls_event(const SDL_Event &ev) {
    if (!controls_capturing()) return false;
    const int rb = ORDER[s_row];
    switch (ev.type) {
    case SDL_CONTROLLERBUTTONDOWN: {
        const int b = ev.cbutton.button;
        if (b == SDL_CONTROLLER_BUTTON_LEFTSTICK) { stop_capture(); sfx_play_back(); return true; }
        if (s_col != 0) return true;                           /* waiting for a key */
        PadBind p;
        p.type = PadBind::BUTTON;
        p.index = b;
        input_set_pad(rb, p);
        bound();
        return true;
    }
    case SDL_CONTROLLERAXISMOTION: {
        const int a = ev.caxis.axis;
        if (s_col != 0) return true;
        if ((a == SDL_CONTROLLER_AXIS_TRIGGERLEFT || a == SDL_CONTROLLER_AXIS_TRIGGERRIGHT) &&
            ev.caxis.value > 24000) {
            PadBind p;
            p.type = PadBind::AXIS_POS;
            p.index = a;
            input_set_pad(rb, p);
            bound();
        }
        return true;                                           /* sticks don't bind */
    }
    case SDL_KEYDOWN: {
        if (ev.key.repeat) return true;
        const SDL_Scancode sc = ev.key.keysym.scancode;
        if (sc == SDL_SCANCODE_ESCAPE) { stop_capture(); sfx_play_back(); return true; }
        if (s_col != 1) return true;
        if (reserved_key(sc)) {
            toast_show(tr("That key is used by the launcher"), true);
            return true;
        }
        input_set_key(rb, (int)sc);
        bound();
        return true;
    }
    case SDL_CONTROLLERBUTTONUP: case SDL_KEYUP:
        return true;
    default:
        return false;
    }
}

void controls_ui_input(UiInput in) {
    if (!s_open || controls_capturing()) return;
    switch (in) {
    case IN_UP:    s_row = (s_row - 1 + NROWS) % NROWS; sfx_play_nav(); break;
    case IN_DOWN:  s_row = (s_row + 1) % NROWS;         sfx_play_nav(); break;
    case IN_LEFT: case IN_RIGHT:
        if (!is_reset_row()) { s_col ^= 1; sfx_play_nav(); }
        break;
    case IN_CONFIRM:
        if (is_reset_row()) {
            input_reset_defaults();
            input_save();
            toast_show(tr("Default controls restored"));
            sfx_play_confirm();
        } else {
            s_capture = true;
            s_capture_tick = SDL_GetTicks();
            sfx_play_confirm();
        }
        break;
    case IN_INSPECT:                                          /* clear the cell */
        if (!is_reset_row()) {
            if (s_col == 0) input_set_pad(ORDER[s_row], PadBind{});
            else            input_set_key(ORDER[s_row], 0);
            input_save();
            sfx_play_back();
        }
        break;
    case IN_BACK: case IN_SETTINGS:
        controls_close();
        sfx_play_back();
        break;
    default:
        break;
    }
}

void controls_draw(PadStyle style) {
    if (!s_open) return;
    const float W = 940.0f, H = 640.0f, X = (UI_W - W) * 0.5f, Y = (UI_H - HINT_BAR_H - H) * 0.5f;
    ui_rect(0.0f, 0.0f, UI_W, UI_H, 0.0f, 0.0f, 0.0f, 0.55f);
    ui_round_rect(X - 3.0f, Y - 3.0f, W + 6.0f, H + 6.0f, 12.0f, GOLD_R, GOLD_G, GOLD_B, 0.80f);
    ui_round_rect(X, Y, W, H, 10.0f, PANEL_R, PANEL_G, PANEL_B, 0.98f);

    float y = Y + 20.0f;
    ui_text_px(X + 32.0f, y, 30, tr("Controls"), GOLD_R, GOLD_G, GOLD_B, 1.0f);
    y += 42.0f;

    /* Who's playing. */
    float px = X + 32.0f;
    for (int p = 0; p < INPUT_PLAYERS; p++) {
        std::string name = input_player_name(p);
        char buf[160];
        if (name.empty() && p == 0) snprintf(buf, sizeof(buf), tr("P%d: keyboard"), p + 1);
        else if (name.empty())      snprintf(buf, sizeof(buf), tr("P%d: -"), p + 1);
        else                        snprintf(buf, sizeof(buf), "P%d: %.28s", p + 1, name.c_str());
        const bool on = !name.empty() || p == 0;
        ui_text_px(px, y, 15, buf, on ? 0.55f : 0.45f, on ? 0.92f : 0.48f, on ? 0.62f : 0.46f, 1.0f);
        px += (float)ui_text_width(15, buf) + 28.0f;
    }
    y += 30.0f;

    const float c0 = X + 40.0f, c1 = X + 330.0f, c2 = X + 620.0f, row_h = 29.0f;
    ui_text_px(c1, y, 15, tr("CONTROLLER"), GOLD_DIM_R, GOLD_DIM_G, GOLD_DIM_B, 1.0f);
    ui_text_px(c2, y, 15, tr("KEYBOARD"), GOLD_DIM_R, GOLD_DIM_G, GOLD_DIM_B, 1.0f);
    y += 24.0f;
    ui_rect(X + 28.0f, y - 4.0f, W - 56.0f, 1.0f, GOLD_R, GOLD_G, GOLD_B, 0.35f);

    const bool capturing = controls_capturing();
    const float pulse = 0.55f + 0.45f * sinf((float)SDL_GetTicks() * 0.012f);
    for (int r = 0; r < NROWS; r++) {
        const float ry = y + (float)r * row_h;
        const bool sel = r == s_row;
        if (sel) ui_round_rect(X + 24.0f, ry - 2.0f, W - 48.0f, row_h - 2.0f, 6.0f,
                               GOLD_R * 0.12f, GOLD_G * 0.10f, 0.02f, 0.95f);
        if (r == NROWS - 1) {
            ui_text_px(c0, ry + 2.0f, 18, tr("Reset to defaults"), sel ? GOLD_R : 0.85f,
                       sel ? GOLD_G : 0.85f, sel ? GOLD_B : 0.80f, 1.0f);
            continue;
        }
        const int rb = ORDER[r];
        ui_text_px(c0, ry + 2.0f, 18, input_button_name(rb), sel ? GOLD_R : 0.90f, sel ? GOLD_G : 0.90f,
                   sel ? GOLD_B : 0.86f, 1.0f);
        const Binding &b = input_binding(rb);
        for (int c = 0; c < 2; c++) {
            const bool cell = sel && s_col == c;
            std::string label = c == 0 ? input_pad_label(b.pad) : input_key_label(b.key);
            if (cell && capturing) label = c == 0 ? tr("Press a button...") : tr("Press a key...");
            const float cx = c == 0 ? c1 : c2;
            uikit_chip(cx, ry, label.c_str(), 15, cell, cell && capturing ? pulse : (cell ? 1.0f : 0.75f));
        }
    }

    const char *note = capturing ? (s_col == 0 ? tr("L3 cancels") : tr("Esc cancels"))
                                 : tr("L3 opens the menu in-game, so it can't be used by games.");
    ui_text_px(X + 32.0f, Y + H - 34.0f, 15, note, 0.62f, 0.66f, 0.60f, 1.0f);

    Hint h[5];
    int n = 0;
    h[n++] = {HB_DPAD, tr("Select")};
    h[n++] = {HB_CONFIRM, is_reset_row() ? tr("Reset") : tr("Change")};
    if (!is_reset_row()) h[n++] = {HB_INSPECT, tr("Clear")};
    h[n++] = {HB_BACK, tr("Back")};
    uikit_hint_bar(h, n, style, nullptr);
}
