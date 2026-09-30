#include "pause.h"
#include "i18n.h"
#include "renderer.h"
#include "savestate.h"
#include "sfx.h"
#include "ui.h"
#include <SDL2/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

enum Row { R_RESUME, R_SAVE, R_LOAD, R_SHOT, R_SHADER, R_CONTROLS, R_PERF, R_NEXT_WEEK, R_RESET, R_QUIT };

PauseInfo        s_info;
bool             s_open = false;
bool             s_confirm = false;       /* yes / no question on top of the menu */
bool             s_confirm_only = false;  /* opened straight into the question    */
int              s_confirm_yes = 0;       /* 0 = yes selected, 1 = no              */
PauseAction      s_confirm_action = PAUSE_NONE;
std::string      s_confirm_text;
std::vector<Row> s_rows;
int              s_sel = 0;
int              s_shader = 0;
Uint32           s_open_tick = 0;

bool s_week_done = false, s_week_next = false;
int  s_week_entry = 0;

const float PANEL_W = 470.0f;
const float OFF_R = 0.90f, OFF_G = 0.90f, OFF_B = 0.86f;

void build_rows(void) {
    s_rows.clear();
    s_rows.push_back(R_RESUME);
    if (s_info.states) { s_rows.push_back(R_SAVE); s_rows.push_back(R_LOAD); }
    s_rows.push_back(R_SHOT);
    s_rows.push_back(R_SHADER);
    if (s_info.controls)  s_rows.push_back(R_CONTROLS);
    s_rows.push_back(R_PERF);
    if (s_info.next_week) s_rows.push_back(R_NEXT_WEEK);
    if (s_info.can_reset) s_rows.push_back(R_RESET);
    s_rows.push_back(R_QUIT);
    if (s_sel >= (int)s_rows.size()) s_sel = (int)s_rows.size() - 1;
}

bool row_enabled(Row r) {
    return r != R_LOAD || s_info.state_time > 0;
}

std::string row_label(Row r) {
    char buf[160];
    switch (r) {
    case R_RESUME:    return tr("Resume");
    case R_SAVE:      return tr("Save state");
    case R_LOAD:      return tr("Load state");
    case R_SHOT:      return tr("Take screenshot");
    case R_SHADER:
        snprintf(buf, sizeof(buf), tr("Shader: %s"), tr(renderer_shader_name(s_shader)));
        return buf;
    case R_CONTROLS:  return tr("Controls");
    case R_PERF:
        snprintf(buf, sizeof(buf), tr("Performance info: %s"), s_info.perf ? tr("On") : tr("Off"));
        return buf;
    case R_NEXT_WEEK: return tr("Next week");
    case R_RESET:     return tr("Reset game");
    case R_QUIT:      return tr("Return to launcher");
    }
    return "";
}

void ask(PauseAction action, const std::string &text) {
    s_confirm = true;
    s_confirm_yes = 0;
    s_confirm_action = action;
    s_confirm_text = text;
}

float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

void diamond(float cx, float cy, float s, float r, float g, float b, float a) {
    ui_triangle(cx, cy - s, cx - s, cy, cx + s, cy, r, g, b, a);
    ui_triangle(cx - s, cy, cx, cy + s, cx + s, cy, r, g, b, a);
}

int fit_px(const char *s, float max_w, int hi, int lo) {
    for (int px = hi; px > lo; px -= 2)
        if ((float)ui_text_width(px, s) <= max_w) return px;
    return lo;
}

void draw_state_card(float a) {
    const Row r = s_rows[(size_t)s_sel];
    if (r != R_SAVE && r != R_LOAD) return;
    const float x = PANEL_W + 70.0f, y = 150.0f, w = 1280.0f - x - 70.0f, h = 400.0f;
    ui_round_rect(x - 3.0f, y - 3.0f, w + 6.0f, h + 6.0f, 10.0f, GOLD_R, GOLD_G, GOLD_B, 0.55f * a);
    ui_round_rect(x, y, w, h, 8.0f, PANEL_R, PANEL_G, PANEL_B, 0.92f * a);
    const int px = 18;
    if (s_info.state_time > 0) {
        int iw = 0, ih = 0;
        const char *thumb = s_info.state_thumb.c_str();
        if (ui_image_size(thumb, &iw, &ih))
            ui_image_ex(x + 16.0f, y + 16.0f, w - 32.0f, h - 70.0f, thumb, UI_IMG_FIT | UI_IMG_SMOOTH,
                        1.0f, 1.0f, 1.0f, a);
        char buf[128];
        snprintf(buf, sizeof(buf), tr("Saved %s"), time_ago(s_info.state_time).c_str());
        const float tw = (float)ui_text_width(px, buf);
        ui_text_px(x + (w - tw) * 0.5f, y + h - 44.0f, px, buf, GOLD_R, GOLD_G, GOLD_B, a);
    } else {
        const char *t = tr("No saved state yet");
        const float tw = (float)ui_text_width(px, t);
        ui_text_px(x + (w - tw) * 0.5f, y + h * 0.5f - 10.0f, px, t, 0.62f, 0.64f, 0.60f, a);
    }
}

void draw_confirm(float a) {
    const float w = 660.0f, h = 210.0f, x = (1280.0f - w) * 0.5f, y = (720.0f - h) * 0.5f - 20.0f;
    ui_rect(0.0f, 0.0f, 1280.0f, 720.0f, 0.0f, 0.0f, 0.0f, 0.35f * a);
    ui_round_rect(x - 3.0f, y - 3.0f, w + 6.0f, h + 6.0f, 12.0f, GOLD_R, GOLD_G, GOLD_B, 0.75f * a);
    ui_round_rect(x, y, w, h, 10.0f, PANEL_R, PANEL_G, PANEL_B, 0.97f * a);
    ui_text_wrap(x + 36.0f, y + 34.0f, w - 72.0f, 20, 27.0f, s_confirm_text.c_str(), UI_ALIGN_CENTER, 3,
                 OFF_R, OFF_G, OFF_B, a);
    const char *labels[2] = { tr("Yes"), tr("No") };
    const float bw = 170.0f, bh = 44.0f, gap = 40.0f;
    float bx = x + (w - (bw * 2.0f + gap)) * 0.5f;
    const float by = y + h - bh - 30.0f;
    for (int i = 0; i < 2; i++, bx += bw + gap) {
        const bool sel = s_confirm_yes == i;
        if (sel) ui_round_rect(bx, by, bw, bh, 8.0f, GOLD_R, GOLD_G, GOLD_B, 0.95f * a);
        else     ui_round_rect_outline(bx, by, bw, bh, 8.0f, 1.5f, GOLD_R, GOLD_G, GOLD_B, 0.65f * a);
        const int px = 20;
        const float tw = (float)ui_text_width(px, labels[i]);
        ui_text_px(bx + (bw - tw) * 0.5f, by + (bh - (float)ui_text_line_height(px)) * 0.5f, px, labels[i],
                   sel ? 0.08f : OFF_R, sel ? 0.07f : OFF_G, sel ? 0.03f : OFF_B, a);
    }
}

} // namespace

void pause_open(const PauseInfo &info) {
    s_info = info;
    s_shader = info.shader;
    s_sel = 0;
    s_confirm = s_confirm_only = false;
    build_rows();
    s_open = true;
    s_open_tick = SDL_GetTicks();
}

void pause_open_next_week(const PauseInfo &info) {
    pause_open(info);
    char buf[160];
    snprintf(buf, sizeof(buf), tr("Go to week %d? Your save carries over."), info.week + 1);
    ask(PAUSE_NEXT_WEEK, buf);
    s_confirm_only = true;
}

void pause_close(void) { s_open = false; s_confirm = false; }
bool pause_is_open(void) { return s_open; }
int  pause_shader(void) { return s_shader; }

void pause_set_info(const PauseInfo &info) {
    const Row cur = s_rows.empty() ? R_RESUME : s_rows[(size_t)s_sel];
    s_info = info;
    s_shader = info.shader;
    build_rows();
    for (size_t i = 0; i < s_rows.size(); i++)
        if (s_rows[i] == cur) s_sel = (int)i;
}

PauseAction pause_input(UiInput in) {
    if (!s_open) return PAUSE_NONE;
    if (s_confirm) {
        switch (in) {
        case IN_LEFT: case IN_RIGHT: case IN_UP: case IN_DOWN:
            s_confirm_yes ^= 1;
            sfx_play_nav();
            return PAUSE_NONE;
        case IN_CONFIRM:
            if (s_confirm_yes == 0) {
                s_confirm = false;
                pause_close();
                sfx_play_confirm();
                return s_confirm_action;
            }
            [[fallthrough]];                       /* "No" */
        case IN_BACK: case IN_SETTINGS: case IN_INSPECT:
            s_confirm = false;
            sfx_play_back();
            if (s_confirm_only) { pause_close(); return PAUSE_RESUME; }
            return PAUSE_NONE;
        default:
            return PAUSE_NONE;
        }
    }
    const int n = (int)s_rows.size();
    switch (in) {
    case IN_UP: case IN_DOWN: {
        const int d = in == IN_UP ? -1 : 1;
        int s = s_sel;
        for (int k = 0; k < n; k++) {
            s = (s + d + n) % n;
            if (row_enabled(s_rows[(size_t)s])) break;
        }
        s_sel = s;
        sfx_play_nav();
        return PAUSE_NONE;
    }
    case IN_LEFT: case IN_RIGHT:
        if (s_rows[(size_t)s_sel] == R_SHADER) {
            s_shader = (s_shader + (in == IN_LEFT ? -1 : 1) + RENDERER_SHADER_COUNT) % RENDERER_SHADER_COUNT;
            sfx_play_nav();
            return PAUSE_SHADER;
        }
        return PAUSE_NONE;
    case IN_BACK: case IN_SETTINGS:
        pause_close();
        sfx_play_back();
        return PAUSE_RESUME;
    case IN_CONFIRM: {
        const Row r = s_rows[(size_t)s_sel];
        if (!row_enabled(r)) { sfx_play_back(); return PAUSE_NONE; }
        switch (r) {
        case R_RESUME: pause_close(); sfx_play_back(); return PAUSE_RESUME;
        case R_SAVE:   sfx_play_confirm(); return PAUSE_SAVE_STATE;
        case R_LOAD:
            ask(PAUSE_LOAD_STATE, tr("Load the saved state? Progress since then will be lost."));
            sfx_play_confirm();
            return PAUSE_NONE;
        case R_SHOT:   return PAUSE_SCREENSHOT;
        case R_SHADER:
            s_shader = (s_shader + 1) % RENDERER_SHADER_COUNT;
            sfx_play_nav();
            return PAUSE_SHADER;
        case R_CONTROLS: sfx_play_confirm(); return PAUSE_CONTROLS;
        case R_PERF:     sfx_play_nav(); return PAUSE_PERF;
        case R_NEXT_WEEK: {
            char buf[160];
            snprintf(buf, sizeof(buf), tr("Go to week %d? Your save carries over."), s_info.week + 1);
            ask(PAUSE_NEXT_WEEK, buf);
            sfx_play_confirm();
            return PAUSE_NONE;
        }
        case R_RESET:
            ask(PAUSE_RESET, tr("Reset the game? Unsaved progress will be lost."));
            sfx_play_confirm();
            return PAUSE_NONE;
        case R_QUIT:   pause_close(); return PAUSE_QUIT;
        }
        return PAUSE_NONE;
    }
    default:
        return PAUSE_NONE;
    }
}

void pause_draw(PadStyle style) {
    if (!s_open) return;
    const float a = clamp01((float)(SDL_GetTicks() - s_open_tick) / 140.0f);

    if (s_confirm_only) {                 /* just the question over the game */
        draw_confirm(a);
    } else {
        /* Dim the frozen game, panel on the left. */
        ui_rect(0.0f, 0.0f, 1280.0f, 720.0f, 0.0f, 0.0f, 0.0f, 0.50f * a);
        ui_gradient_v(0.0f, 0.0f, PANEL_W, 720.0f, PANEL_R, PANEL_G, PANEL_B, 0.96f * a,
                      PANEL_R * 0.6f, PANEL_G * 0.6f, PANEL_B * 0.6f, 0.96f * a);
        ui_rect(PANEL_W, 0.0f, 2.0f, 720.0f, GOLD_R, GOLD_G, GOLD_B, 0.85f * a);

        const float lx = 48.0f, maxw = PANEL_W - 80.0f;
        float y = 46.0f;
        diamond(lx + 6.0f, y + 10.0f, 6.0f, GOLD_R, GOLD_G, GOLD_B, a);
        ui_text_px(lx + 22.0f, y, 18, tr("Paused"), GOLD_DIM_R + 0.1f, GOLD_DIM_G + 0.1f, GOLD_DIM_B, a);
        y += 34.0f;
        int tpx = fit_px(s_info.title.c_str(), maxw, 32, 20);
        ui_text_px(lx, y, tpx, s_info.title.c_str(), GOLD_R, GOLD_G, GOLD_B, a);
        y += (float)ui_text_line_height(tpx) + 4.0f;
        if (!s_info.version.empty()) {
            int vpx = fit_px(s_info.version.c_str(), maxw, 20, 14);
            ui_text_px(lx, y, vpx, s_info.version.c_str(), OFF_R, OFF_G, OFF_B, a);
            y += (float)ui_text_line_height(vpx) + 2.0f;
        }
        if (!s_info.played.empty()) {
            ui_text_px(lx, y, 16, s_info.played.c_str(), 0.60f, 0.64f, 0.58f, a);
            y += 24.0f;
        }
        y = std::max(y + 18.0f, 196.0f);
        ui_rect(lx, y - 12.0f, maxw, 1.0f, GOLD_R, GOLD_G, GOLD_B, 0.35f * a);

        const float row_h = 46.0f;
        for (size_t i = 0; i < s_rows.size(); i++) {
            const Row r = s_rows[i];
            const bool sel = (int)i == s_sel, en = row_enabled(r);
            const float ry = y + (float)i * row_h;
            if (sel) {
                ui_round_rect(lx - 16.0f, ry, maxw + 24.0f, row_h - 6.0f, 8.0f, GOLD_R, GOLD_G, GOLD_B, 0.16f * a);
                ui_rect(lx - 16.0f, ry + 6.0f, 3.0f, row_h - 18.0f, GOLD_R, GOLD_G, GOLD_B, a);
            }
            std::string label = row_label(r);
            const int px = 22;
            const float ty = ry + (row_h - 6.0f - (float)ui_text_line_height(px)) * 0.5f;
            float cr = OFF_R, cg = OFF_G, cb = OFF_B;
            if (sel) { cr = GOLD_R; cg = GOLD_G; cb = GOLD_B; }
            if (!en) { cr = 0.42f; cg = 0.44f; cb = 0.42f; }
            ui_text_px(lx, ty, px, label.c_str(), cr, cg, cb, a);
            if (sel && r == R_SHADER) {           /* ◀ ▶: left / right change it */
                const float cy = ry + (row_h - 6.0f) * 0.5f, rx = lx + maxw;
                ui_triangle(rx - 30.0f, cy, rx - 22.0f, cy - 7.0f, rx - 22.0f, cy + 7.0f, GOLD_R, GOLD_G, GOLD_B, a);
                ui_triangle(rx, cy, rx - 8.0f, cy - 7.0f, rx - 8.0f, cy + 7.0f, GOLD_R, GOLD_G, GOLD_B, a);
            }
        }
        draw_state_card(a);
        if (s_confirm) draw_confirm(a);
    }

    Hint h[4];
    int n = 0;
    if (s_confirm) {
        h[n++] = {HB_DPAD_H, tr("Select")};
        h[n++] = {HB_CONFIRM, tr("OK")};
        h[n++] = {HB_BACK, tr("Back")};
    } else {
        h[n++] = {HB_DPAD_V, tr("Select")};
        if (s_rows[(size_t)s_sel] == R_SHADER) h[n++] = {HB_DPAD_H, tr("Change")};
        h[n++] = {HB_CONFIRM, tr("OK")};
        h[n++] = {HB_BACK, tr("Resume")};
    }
    uikit_hint_bar(h, n, style, nullptr);
}

/* ── Week completion (sequential games) ─────────────────────────────── */
void pause_week_reset(void) {
    s_week_done = s_week_next = false;
    s_week_entry = 0;
}

void pause_week_update(const uint8_t *wram, int mask, int entry_idx, bool has_next_week) {
    s_week_entry = entry_idx;
    s_week_next = has_next_week;
    if (!wram || mask == 0) { s_week_done = false; return; }
    /* Ancient Stone Tablets: WRAM[0xF37C] = bitmask of the tablets collected. */
    const uint8_t tablets = wram[0xF37C];
    s_week_done = (tablets & mask) == (uint8_t)mask;
}

bool pause_week_done(void) { return s_week_done; }

void pause_draw_hud(PadStyle style) {
    if (!s_week_done || !s_week_next || s_open) return;
    char title[96];
    snprintf(title, sizeof(title), tr("Week %d complete!"), s_week_entry + 1);
    const char *hint = style == STYLE_KEYBOARD ? tr("Tab: next week") : tr("R3: next week");
    const int px = 16;
    const float w = (float)std::max(ui_text_width(px, title), ui_text_width(px, hint)) + 36.0f;
    const float h = 52.0f, x = 1280.0f - w - 14.0f, y = 720.0f - h - 14.0f;
    const float pulse = 0.75f + 0.25f * sinf((float)SDL_GetTicks() * 0.004f);
    ui_round_rect(x - 2.0f, y - 2.0f, w + 4.0f, h + 4.0f, 9.0f, GOLD_R, GOLD_G, GOLD_B, 0.70f * pulse);
    ui_round_rect(x, y, w, h, 8.0f, PANEL_R, PANEL_G, PANEL_B, 0.92f);
    ui_text_px(x + 18.0f, y + 7.0f, px, title, GOLD_R, GOLD_G, GOLD_B, 1.0f);
    ui_text_px(x + 18.0f, y + 27.0f, px, hint, OFF_R, OFF_G, OFF_B, 0.9f);
}
