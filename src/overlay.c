#include "overlay.h"
#include "ui.h"
#include <string.h>
#include <stdio.h>

/* ── Internal state ──────────────────────────────────────────────────── */
static OverlayState s_state           = OVERLAY_HIDDEN;
static int          s_menu_sel        = 0;   /* 0 = Yes, 1 = No */
static bool         s_week_done       = false;
static int          s_cur_entry       = 0;
static int          s_total_entries   = 0;
static bool         s_has_next_week   = false;

/* Corner HUD label, e.g. "W1>W2" */
static char         s_corner_label[16] = "";

/* ── Public API ──────────────────────────────────────────────────────── */
void overlay_init(void) {
    s_state      = OVERLAY_HIDDEN;
    s_menu_sel   = 0;
    s_week_done  = false;
    s_corner_label[0] = '\0';
}

void overlay_update(const uint8_t *wram, int week_complete_mask,
                    int current_entry_idx, int total_entries, bool has_next_week) {
    s_cur_entry    = current_entry_idx;
    s_total_entries = total_entries;
    s_has_next_week = has_next_week;

    if (!wram || week_complete_mask == 0) {
        s_week_done = false;
        s_corner_label[0] = '\0';
        return;
    }

    /* WRAM[0xF37C] = tablets collected bitmask */
    uint8_t tablets = wram[0xF37C];
    bool done = (tablets & week_complete_mask) == (uint8_t)week_complete_mask;
    s_week_done = done;

    if (done && has_next_week) {
        snprintf(s_corner_label, sizeof(s_corner_label),
                 "R3:W%d>W%d", current_entry_idx+1, current_entry_idx+2);
    } else {
        s_corner_label[0] = '\0';
    }
}

void overlay_open(OverlayState state) {
    s_state    = state;
    s_menu_sel = 1; /* default to "No" for safety */
}

void overlay_close(void) {
    s_state = OVERLAY_HIDDEN;
}

OverlayState overlay_get_state(void) { return s_state; }
bool         overlay_week_done(void)  { return s_week_done; }

/* ── Input ───────────────────────────────────────────────────────────── */
static OverlayAction handle_input(bool left_right_changed, bool confirm, bool cancel) {
    if (s_state == OVERLAY_HIDDEN) return OVERLAY_ACTION_NONE;

    if (s_state == OVERLAY_HELP) {
        if (confirm || cancel) { s_state = OVERLAY_HIDDEN; }
        return OVERLAY_ACTION_NONE;
    }

    /* WEEK_NEXT / RETURN_LAUNCHER: two-option menus */
    if (left_right_changed) {
        s_menu_sel = 1 - s_menu_sel; /* toggle Yes/No */
    }
    if (cancel) {
        s_state = OVERLAY_HIDDEN;
        return OVERLAY_ACTION_NONE;
    }
    if (confirm) {
        OverlayState st = s_state;
        s_state = OVERLAY_HIDDEN;
        if (s_menu_sel == 0) { /* Yes */
            if (st == OVERLAY_WEEK_NEXT)        return OVERLAY_ACTION_NEXT_WEEK;
            if (st == OVERLAY_RETURN_LAUNCHER)  return OVERLAY_ACTION_GO_LAUNCHER;
        }
    }
    return OVERLAY_ACTION_NONE;
}

OverlayAction overlay_key(int k, bool down) {
    if (!down || s_state == OVERLAY_HIDDEN) return OVERLAY_ACTION_NONE;
    /* SDLK: LEFT=1073741904, RIGHT=1073741903, RETURN=13, ESCAPE=27 */
    bool lr      = (k == 1073741904 || k == 1073741903);
    bool confirm = (k == 13);
    bool cancel  = (k == 27);
    return handle_input(lr, confirm, cancel);
}

OverlayAction overlay_button(int btn, bool down) {
    if (!down || s_state == OVERLAY_HIDDEN) return OVERLAY_ACTION_NONE;
    /* SDL_CONTROLLER_BUTTON: A=0, B=1, DPAD_LEFT=13, DPAD_RIGHT=14.
       Switch labels A/B opposite to SDL's Xbox layout, so swap confirm/cancel. */
    bool lr      = (btn == 13 || btn == 14);
#ifdef __SWITCH__
    bool confirm = (btn == 1);
    bool cancel  = (btn == 0);
#else
    bool confirm = (btn == 0);
    bool cancel  = (btn == 1);
#endif
    return handle_input(lr, confirm, cancel);
}

/* ── Draw ─────────────────────────────────────────────────────────────── */
void overlay_draw(void) {
    /* Corner HUD — gold on dark */
    if (s_corner_label[0] != '\0') {
        float cw = 80.0f, ch = 22.0f;
        float cx = 1280.0f - cw - 10.0f;
        float cy = 720.0f  - ch - 10.0f;
        ui_rect(cx-2, cy-2, cw+4, ch+4, 0.90f,0.72f,0.10f,0.55f); /* gold glow border */
        ui_rect(cx, cy, cw, ch, 0.03f,0.07f,0.05f,0.92f);
        ui_text(cx+5, cy+5, 1.4f, s_corner_label, 0.95f,0.78f,0.15f);
    }

    if (s_state == OVERLAY_HIDDEN) return;

    /* Dimmed backdrop */
    ui_rect(0,0,1280,720, 0.0f,0.0f,0.0f,0.60f);

    float bx=320.0f, by=240.0f, bw=640.0f, bh=220.0f;
    /* Dialog box */
    ui_rect(bx,by,bw,bh, 0.03f,0.07f,0.05f,0.96f);
    /* Gold top accent */
    ui_rect(bx,by,bw,3, 0.95f,0.78f,0.15f,1.0f);
    /* Gold bottom accent */
    ui_rect(bx,by+bh-3,bw,3, 0.95f,0.78f,0.15f,1.0f);

    if (s_state == OVERLAY_HELP) {
        ui_text(bx+20, by+16, 2.0f, "CONTROLS",          0.95f,0.78f,0.15f);
        ui_text(bx+20, by+56, 1.5f, "R3 / Tab  : Next week? (when week is complete)",0.92f,0.92f,0.92f);
        ui_text(bx+20, by+80, 1.5f, "L3 / Esc  : Return to Emerald Launcher",        0.92f,0.92f,0.92f);
        ui_text(bx+20, by+116,1.5f, "W1>W2 icon: current week is complete",           0.70f,0.70f,0.70f);
        ui_text(bx+20, by+170,1.2f, "Press A / Enter to close",                       0.55f,0.55f,0.55f);
        return;
    }

    /* Prompt text */
    const char *prompt = (s_state == OVERLAY_WEEK_NEXT)
        ? "Start next week?" : "Return to Emerald Launcher?";
    ui_text(bx+20, by+28, 2.0f, prompt, 1.0f,1.0f,1.0f);

    /* Separator */
    ui_rect(bx+20, by+72, bw-40, 1, 0.95f,0.78f,0.15f,0.50f);

    /* Yes / No buttons */
    float btn_y = by + 110.0f;
    float yes_x = bx + bw/2 - 130.0f;
    float no_x  = bx + bw/2 +  30.0f;
    float btn_w = 100.0f, btn_h = 44.0f;

    /* "Yes" — gold when selected */
    if (s_menu_sel==0) {
        ui_rect(yes_x, btn_y, btn_w, btn_h, 0.28f,0.20f,0.02f,1.0f);
        /* gold border */
        ui_rect(yes_x,   btn_y,     btn_w,2,   0.95f,0.78f,0.15f,1.0f);
        ui_rect(yes_x,   btn_y+btn_h-2,btn_w,2,0.95f,0.78f,0.15f,1.0f);
        ui_rect(yes_x,   btn_y,     2,btn_h,   0.95f,0.78f,0.15f,1.0f);
        ui_rect(yes_x+btn_w-2,btn_y,2,btn_h,   0.95f,0.78f,0.15f,1.0f);
        ui_text(yes_x+22, btn_y+14, 2.0f, "YES", 0.95f,0.78f,0.15f);
    } else {
        ui_rect(yes_x, btn_y, btn_w, btn_h, 0.08f,0.10f,0.08f,0.85f);
        ui_text(yes_x+22, btn_y+14, 2.0f, "YES", 0.55f,0.55f,0.55f);
    }

    /* "No" — white/red tint when selected */
    if (s_menu_sel==1) {
        ui_rect(no_x, btn_y, btn_w, btn_h, 0.30f,0.08f,0.06f,1.0f);
        ui_rect(no_x,    btn_y,     btn_w,2,   0.90f,0.30f,0.25f,1.0f);
        ui_rect(no_x,    btn_y+btn_h-2,btn_w,2,0.90f,0.30f,0.25f,1.0f);
        ui_rect(no_x,    btn_y,     2,btn_h,   0.90f,0.30f,0.25f,1.0f);
        ui_rect(no_x+btn_w-2,btn_y, 2,btn_h,   0.90f,0.30f,0.25f,1.0f);
        ui_text(no_x+28, btn_y+14, 2.0f, "NO", 1.0f,0.60f,0.58f);
    } else {
        ui_rect(no_x, btn_y, btn_w, btn_h, 0.08f,0.10f,0.08f,0.85f);
        ui_text(no_x+28, btn_y+14, 2.0f, "NO", 0.55f,0.55f,0.55f);
    }

    ui_text(bx+20, by+bh-28, 1.2f, "Left/Right: select   A/Enter: confirm   B/Esc: cancel",
            0.50f,0.50f,0.50f);
}
