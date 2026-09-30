#include "launcher.h"
#include "ui.h"
#include "sfx.h"
#include "prefs.h"
#include "paths.h"
#include <SDL.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cmath>

#ifdef _WIN32
#  include <windows.h>
static bool delete_file(const char *path) { return DeleteFileA(path) != 0; }
#else
#  include <cstdio>
static bool delete_file(const char *path) { return remove(path) == 0; }
#endif

static const char *SHADER_NAMES[] = {
    "None (sharp pixels)",
    "Smooth (ScaleFX-9x)",
    "Scanlines",
    "CRT (scanlines + vignette)",
    "LCD Grid (handheld)",
    "Bloom (glow on brights)"
};
static const int NUM_SHADERS = 6;
/* Settings rows: the shaders, then "Clear Save Data", then the launcher view. */
static const int ROW_CLEAR   = NUM_SHADERS;
static const int ROW_VIEW    = NUM_SHADERS + 1;
static const int CONFIG_ROWS = NUM_SHADERS + 2;

/* Layout constants (1280x720) */
static const float WIN_W  = UI_W;
static const float WIN_H  = UI_H;
static const float LEFT_W =  320.0f;
static const float PAD    =   20.0f;

/* Card sizes */
static const float CARD_W = 220.0f;
static const float CARD_H = 140.0f;

/* SDL game-controller buttons (positional, Xbox-style numbering). Nintendo
   A (right) = SDL 1, B (bottom) = SDL 0 — the opposite of Xbox. */
#ifdef __SWITCH__
static const int BTN_A = 1, BTN_B = 0;
#else
static const int BTN_A = 0, BTN_B = 1;
#endif
static const int BTN_LEFT_FACE = 2;   /* Nintendo Y / Xbox X: settings */
static const int BTN_TOP_FACE  = 3;   /* Nintendo X / Xbox Y: look at box */
static const int BTN_START = 6, BTN_L = 9, BTN_R = 10;
static const int BTN_UP = 11, BTN_DOWN = 12, BTN_LEFT = 13, BTN_RIGHT = 14;

static const int STICK_DEAD = 8000;

void Launcher::load(std::vector<GameGroup> groups) {
    m_groups   = std::move(groups);
    m_selected = 0;
    m_submenu  = -1;
    m_sub_sel  = 0;
    m_use_3d   = prefs_get_view_3d();

    /* Resume where the user left off. */
    const std::string last = prefs_get_last_group();
    for (size_t i = 0; i < m_groups.size(); i++)
        if (m_groups[i].key == last) { m_selected = (int)i; break; }

    m_shelf.attach(&m_groups);
    for (size_t i = 0; i < m_groups.size(); i++) {
        int e = prefs_get_last_entry(m_groups[i].key);
        if (e >= 0 && e < (int)m_groups[i].entries.size()) m_shelf.set_front((int)i, e);
    }
    m_shelf.select(m_selected);
}

LaunchRequest Launcher::poll_launch(void) {
    LaunchRequest r = m_pending;
    m_pending = LaunchRequest{};
    return r;
}

bool Launcher::ensure_3d(void) {
    if (!m_3d_tried) {
        m_3d_tried = true;
        m_3d_ok = m_shelf.init();
        if (!m_3d_ok) fprintf(stderr, "launcher: 3D shelf unavailable, using the classic view\n");
    }
    return m_3d_ok;
}

void Launcher::set_view_3d(bool on) {
    if (on && !ensure_3d()) on = false;
    if (on && !in_3d()) {
        m_shelf.select(m_submenu >= 0 ? m_submenu : m_selected);
    } else if (!on && in_3d()) {
        m_selected = m_shelf.selected();
        m_submenu  = -1;
    }
    m_use_3d = on;
    prefs_set_view_3d(on);
    prefs_save();
}

void Launcher::prewarm(void) {
    if (m_groups.empty() || !m_use_3d || !ensure_3d()) return;
    m_shelf.prewarm(12.0);
}

void Launcher::on_game_start(void) {
    if (m_3d_ok) m_shelf.release_gpu();
}

void Launcher::on_quit(void) {
    if (m_groups.empty()) return;
    int g = in_3d() ? m_shelf.selected() : (m_submenu >= 0 ? m_submenu : m_selected);
    if (g >= 0 && g < (int)m_groups.size()) {
        prefs_set_last_group(m_groups[(size_t)g].key);
        prefs_save();
    }
}

LaunchRequest Launcher::make_request(int gi, int idx) const {
    const GameGroup &g = m_groups[(size_t)gi];
    const GameEntry &e = g.entries[(size_t)idx];
    LaunchRequest r;
    r.valid              = true;
    r.rom_path           = e.rom_path;
    r.core_dll           = e.core_dll;
    r.srm_path           = e.srm_path;
    r.carry_srm_from     = e.carry_srm_from;
    r.group_key          = g.key;
    r.entry_idx          = idx;
    r.is_sequential      = g.sequential;
    r.week_complete_mask = g.sequential ? e.week_complete_mask : 0;
    r.has_next_week      = g.sequential && (idx + 1 < (int)g.entries.size());
    r.external           = e.external;
    r.exec_path          = e.exec_path;
    r.exec_argv          = e.exec_argv;
    r.keep_open          = e.keep_open;
    return r;
}

/* Remember the game and the version played (it becomes the front of its stack). */
void Launcher::remember(int g, int entry) {
    const GameGroup &grp = m_groups[(size_t)g];
    prefs_set_last_group(grp.key);
    if (!grp.sequential) {
        prefs_set_last_entry(grp.key, entry);
        m_shelf.set_front(g, entry);
    }
    prefs_save();
}

/* ── Input ──────────────────────────────────────────────────────────── */
void Launcher::handle_key(int key, bool down) {
    if (!down) return;
    m_style = STYLE_KEYBOARD;
    switch (key) {
    case SDLK_UP:    case 'w': dispatch(IN_UP);    break;
    case SDLK_DOWN:  case 's': dispatch(IN_DOWN);  break;
    case SDLK_LEFT:  case 'a': dispatch(IN_LEFT);  break;
    case SDLK_RIGHT: case 'd': dispatch(IN_RIGHT); break;
    case SDLK_RETURN: case SDLK_KP_ENTER: dispatch(IN_CONFIRM); break;
    case SDLK_ESCAPE: case SDLK_BACKSPACE: dispatch(IN_BACK); break;
    case SDLK_TAB:   dispatch(IN_SETTINGS); break;
    case SDLK_SPACE: case 'i': dispatch(IN_INSPECT); break;
    case SDLK_PAGEUP:   case 'q': dispatch(IN_PAGE_PREV); break;
    case SDLK_PAGEDOWN: case 'e': dispatch(IN_PAGE_NEXT); break;
    default: break;
    }
}

void Launcher::handle_button(int btn, bool down) {
#ifdef __SWITCH__
    m_style = STYLE_NINTENDO;
#else
    m_style = STYLE_XBOX;
#endif
    if (!down) {
        if (btn == m_hold_btn) m_hold_btn = -1;
        return;
    }
    UiInput in;
    bool repeat = false;
    if      (btn == BTN_UP)    { in = IN_UP;    repeat = true; }
    else if (btn == BTN_DOWN)  { in = IN_DOWN;  repeat = true; }
    else if (btn == BTN_LEFT)  { in = IN_LEFT;  repeat = true; }
    else if (btn == BTN_RIGHT) { in = IN_RIGHT; repeat = true; }
    else if (btn == BTN_A)     in = IN_CONFIRM;
    else if (btn == BTN_B)     in = IN_BACK;
    else if (btn == BTN_LEFT_FACE || btn == BTN_START) in = IN_SETTINGS;
    else if (btn == BTN_TOP_FACE) in = IN_INSPECT;
    else if (btn == BTN_L)     in = IN_PAGE_PREV;
    else if (btn == BTN_R)     in = IN_PAGE_NEXT;
    else return;
    if (repeat) {
        m_hold_btn  = btn;
        m_hold_next = SDL_GetTicks() + 380;
    }
    dispatch(in);
}

void Launcher::handle_axis(int axis, int value) {
    float v = 0.0f;
    if (value > STICK_DEAD)       v = (float)(value - STICK_DEAD) / (32767.0f - STICK_DEAD);
    else if (value < -STICK_DEAD) v = (float)(value + STICK_DEAD) / (32768.0f - STICK_DEAD);
    if (v > 1.0f) v = 1.0f;
    if (v < -1.0f) v = -1.0f;
    if (axis == SDL_CONTROLLER_AXIS_RIGHTX) m_rs_x = v;
    if (axis == SDL_CONTROLLER_AXIS_RIGHTY) m_rs_y = v;
    if (v != 0.0f) {
#ifdef __SWITCH__
        m_style = STYLE_NINTENDO;
#else
        m_style = STYLE_XBOX;
#endif
    }
}

void Launcher::handle_mouse_button(float x, float y, int button, bool down) {
    if (m_groups.empty()) return;
    if (m_config_open) {
        if (down && button == 3) { close_config(); sfx_play_back(); }
        return;
    }
    if (in_3d()) m_shelf.mouse_button(x, y, button, down);
}

void Launcher::handle_mouse_motion(float x, float y) {
    if (!m_groups.empty() && !m_config_open && in_3d()) m_shelf.mouse_motion(x, y);
}

void Launcher::handle_mouse_wheel(int dy) {
    if (m_groups.empty() || dy == 0) return;
    if (m_config_open)  { dispatch(dy > 0 ? IN_UP : IN_DOWN); return; }
    if (in_3d())        { m_shelf.mouse_wheel(dy); return; }
    dispatch(dy > 0 ? IN_UP : IN_DOWN);
}

void Launcher::dispatch(UiInput in) {
    if (m_groups.empty()) return;
    if (m_config_open) { config_input(in); return; }
    if (in_3d())       { m_shelf.input(in); return; }
    classic_input(in);
}

void Launcher::config_input(UiInput in) {
    if (m_clear_confirm) {
        if (in == IN_CONFIRM) {
            /* Confirm clear: delete all SRM files for the group */
            const GameGroup &g = m_groups[(size_t)m_config_group];
            for (auto &e : g.entries)
                if (!e.srm_path.empty()) delete_file(e.srm_path.c_str());
            if (g.sequential) db_progress_set(g.key, 0);
            m_clear_confirm = false;
            sfx_play_confirm();
        } else if (in == IN_BACK || in == IN_SETTINGS) {
            m_clear_confirm = false;
            sfx_play_back();
        }
        return;
    }
    switch (in) {
    case IN_UP:   m_config_sel = (m_config_sel - 1 + CONFIG_ROWS) % CONFIG_ROWS; sfx_play_nav(); break;
    case IN_DOWN: m_config_sel = (m_config_sel + 1) % CONFIG_ROWS;               sfx_play_nav(); break;
    case IN_LEFT: case IN_RIGHT:
        if (m_config_sel == ROW_VIEW) { set_view_3d(!in_3d()); sfx_play_confirm(); }
        break;
    case IN_CONFIRM:
        if (m_config_sel < NUM_SHADERS) {
            prefs_set_shader(m_groups[(size_t)m_config_group].key, m_config_sel);
            prefs_save();
        } else if (m_config_sel == ROW_CLEAR) {
            m_clear_confirm = true;
        } else {
            set_view_3d(!in_3d());
        }
        sfx_play_confirm();
        break;
    case IN_BACK: case IN_SETTINGS:
        close_config();
        sfx_play_back();
        break;
    default: break;
    }
}

void Launcher::classic_input(UiInput in) {
    const int n = (int)m_groups.size();
    if (m_submenu < 0) {
        switch (in) {
        case IN_UP:   m_selected = (m_selected - 1 + n) % n; m_arrow_dir = +1; m_arrow_time = SDL_GetTicks(); sfx_play_nav(); break;
        case IN_DOWN: m_selected = (m_selected + 1) % n;     m_arrow_dir = -1; m_arrow_time = SDL_GetTicks(); sfx_play_nav(); break;
        case IN_CONFIRM: confirm_selection(); m_pending.valid ? sfx_play_enter_game() : sfx_play_confirm(); break;
        case IN_SETTINGS: open_config(m_selected); sfx_play_confirm(); break;
        default: break;
        }
    } else {
        const GameGroup &g = m_groups[(size_t)m_submenu];
        const int e = (int)g.entries.size();
        switch (in) {
        case IN_UP:   m_sub_sel = (m_sub_sel - 1 + e) % e; m_arrow_dir = +1; m_arrow_time = SDL_GetTicks(); sfx_play_nav(); break;
        case IN_DOWN: m_sub_sel = (m_sub_sel + 1) % e;     m_arrow_dir = -1; m_arrow_time = SDL_GetTicks(); sfx_play_nav(); break;
        case IN_CONFIRM: confirm_selection(); m_pending.valid ? sfx_play_enter_game() : sfx_play_confirm(); break;
        case IN_BACK: m_submenu = -1; sfx_play_back(); break;
        case IN_SETTINGS: open_config(m_submenu); sfx_play_confirm(); break;
        default: break;
        }
    }
}

void Launcher::open_config(int group) {
    m_config_group  = group;
    m_config_sel    = 0;
    m_clear_confirm = false;
    m_config_open   = true;
}

void Launcher::close_config(void) {
    m_config_open   = false;
    m_clear_confirm = false;
}

void Launcher::confirm_selection(void) {
    if (m_groups.empty()) return;
    const GameGroup &g = m_groups[(size_t)m_selected];

    if (m_submenu < 0) {
        if (g.sequential) {
            int idx = db_progress_get(g.key);
            if (idx >= (int)g.entries.size()) idx = (int)g.entries.size()-1;
            m_pending = make_request(m_selected, idx);
            remember(m_selected, idx);
        } else if (g.entries.size()==1) {
            m_pending = make_request(m_selected, 0);
            remember(m_selected, 0);
        } else {
            m_submenu = m_selected;
            int last = prefs_get_last_entry(g.key);
            m_sub_sel = (last >= 0 && last < (int)g.entries.size()) ? last : 0;
        }
    } else {
        if (!db_entry_unlocked(g, m_sub_sel)) return;
        m_pending = make_request(m_selected, m_sub_sel);
        remember(m_selected, m_sub_sel);
        m_submenu = -1;
    }
}

/* ── Drawing ────────────────────────────────────────────────────────── */
void Launcher::draw(void) {
    const uint64_t now = SDL_GetPerformanceCounter();
    double dt = m_last_frame ? (double)(now - m_last_frame) / (double)SDL_GetPerformanceFrequency() : 0.0;
    m_last_frame = now;
    const bool resumed = dt > 0.5;      /* first frame back after a game */
    if (dt > 0.1) dt = 0.1;
    if (resumed) { m_hold_btn = -1; m_rs_x = m_rs_y = 0.0f; }

    /* Auto-repeat while a d-pad direction (or the stick) is held. */
    if (m_hold_btn >= 0) {
        uint32_t t = SDL_GetTicks();
        if ((int32_t)(t - m_hold_next) >= 0) {
            m_hold_next = t + 110;
            int b = m_hold_btn;
            dispatch(b == BTN_UP ? IN_UP : b == BTN_DOWN ? IN_DOWN : b == BTN_LEFT ? IN_LEFT : IN_RIGHT);
        }
    }

    if (m_groups.empty()) {
        ui_begin();
        ui_rect(0,0,WIN_W,WIN_H, PANEL_R,PANEL_G,PANEL_B,0.88f);
        ui_text(PAD, WIN_H/2-16, 2.0f, "No ROMs found. Place roms + db.json and restart.",
                1.0f,1.0f,1.0f);
        ui_end();
        return;
    }

    if (m_use_3d) ensure_3d();
    if (!in_3d()) { draw_classic(); return; }

    if (resumed) m_shelf.on_resume();
    m_shelf.set_right_stick(m_rs_x, m_rs_y);
    m_shelf.update((float)dt);
    ShelfAction act;
    while (m_shelf.poll_action(act)) {
        if (act.kind == ShelfAction::SETTINGS) {
            open_config(act.group);
            sfx_play_confirm();
        } else if (act.kind == ShelfAction::LAUNCH) {
            m_pending = make_request(act.group, act.entry);
            remember(act.group, act.entry);
            m_shelf.on_launched();
        }
    }

    m_shelf.draw_scene();
    ui_set_draw_bg(false);
    ui_begin();
    m_shelf.draw_overlay(m_style, m_app_name, m_config_open);
    if (m_config_open) {
        draw_config_modal();
        draw_config_hints();
    }
    m_shelf.draw_fade();
    ui_end();
    ui_set_draw_bg(true);
}

/* Filled triangle centered at (cx,cy), height h pixels.
   point_up=true → ▲  point_up=false → ▼ */
static void draw_tri(float cx, float cy, float h, bool point_up,
                     float r, float g, float b, float a) {
    int rows = (int)(h + 0.5f);
    for (int k = 0; k < rows; k++) {
        int kk  = point_up ? k : (rows - 1 - k);
        float w = (float)(2 * kk + 1);
        ui_rect(cx - w * 0.5f, cy - h * 0.5f + (float)k, w, 1.0f, r, g, b, a);
    }
}

static const char *ingame_hint(PadStyle style) {
#ifdef __SWITCH__
    (void)style;
    return "In-game: L3 = launcher";
#else
    return style == STYLE_KEYBOARD ? "In-game: Esc = launcher" : "In-game: L3 = launcher";
#endif
}

void Launcher::draw_config_hints(void) {
    Hint h[4];
    int n = 0;
    if (m_clear_confirm) {
        h[n++] = {HB_CONFIRM, "Delete saves"};
        h[n++] = {HB_BACK, "Cancel"};
    } else {
        h[n++] = {HB_DPAD_V, "Navigate"};
        h[n++] = {HB_CONFIRM, m_config_sel == ROW_VIEW ? "Switch view" : "Apply"};
        h[n++] = {HB_BACK, "Close"};
    }
    uikit_hint_bar(h, n, m_style, nullptr);
}

void Launcher::draw_classic(void) {
    ui_begin();

    /* Left panel — semi-transparent dark overlay over the fabric */
    ui_rect(0,0,LEFT_W,WIN_H, PANEL_R,PANEL_G,PANEL_B,0.82f);

    /* Gold vertical separator */
    ui_rect(LEFT_W,0,2,WIN_H, GOLD_R,GOLD_G,GOLD_B,0.90f);

    /* Right panel subtle overlay */
    ui_rect(LEFT_W+2,0, WIN_W-LEFT_W-2,WIN_H-28, PANEL_R,PANEL_G,PANEL_B,0.55f);

    if (m_submenu >= 0)
        draw_submenu();
    else
        draw_carousel();

    if (m_config_open) {
        draw_config_modal();
        draw_config_hints();
    } else {
        Hint h[5];
        int n = 0;
        char confirm_lbl[40];
        if (m_submenu < 0) {
            const GameGroup &g = m_groups[(size_t)m_selected];
            if (g.sequential) {
                int w = std::min(db_progress_get(g.key), (int)g.entries.size() - 1);
                snprintf(confirm_lbl, sizeof(confirm_lbl), "Play %s", g.entries[(size_t)w].title.c_str());
            } else if (g.entries.size() > 1) {
                snprintf(confirm_lbl, sizeof(confirm_lbl), "Versions (%d)", (int)g.entries.size());
            } else {
                snprintf(confirm_lbl, sizeof(confirm_lbl), "Play");
            }
            h[n++] = {HB_DPAD_V, "Navigate"};
            h[n++] = {HB_CONFIRM, confirm_lbl};
        } else {
            const GameGroup &g = m_groups[(size_t)m_submenu];
            h[n++] = {HB_DPAD_V, "Navigate"};
            h[n++] = {HB_CONFIRM, db_entry_unlocked(g, m_sub_sel) ? "Play" : "Locked"};
            h[n++] = {HB_BACK, "Back"};
        }
        h[n++] = {HB_SETTINGS, "Settings"};
#ifndef __SWITCH__
        if (m_style == STYLE_KEYBOARD) h[n++] = {HB_FULLSCREEN, "Maximize"};
#endif
        uikit_hint_bar(h, n, m_style, ingame_hint(m_style));
    }

    ui_end();
}

void Launcher::draw_carousel(void) {
    const float cx      = LEFT_W / 2.0f;
    const float cy      = WIN_H  / 2.0f;
    const float spacing = CARD_H - 10.0f;   /* 130px — 5 cards fit with divider room */

    int n = (int)m_groups.size();

    /* ── Wrap-around divider ──────────────────────────────────────────
       Scan pairs in the visible range (plus one slot beyond each edge)
       to find where the list goes from index n-1 back to 0.
       Draw a gold line with a small center diamond at that gap.       */
    for (int i = -3; i <= 2; i++) {
        int idx_a = (m_selected + i     + n * 10) % n;
        int idx_b = (m_selected + i + 1 + n * 10) % n;
        if (!(idx_a == n - 1 && idx_b == 0)) continue;

        float mid_y;
        if (i == -3) {
            /* gap is above the topmost visible card */
            float h2 = CARD_H * 0.60f;
            mid_y = cy - 2.0f * spacing - h2 * 0.5f - 5.0f;
        } else if (i == 2) {
            /* gap is below the bottommost visible card */
            float h2 = CARD_H * 0.60f;
            mid_y = cy + 2.0f * spacing + h2 * 0.5f + 5.0f;
        } else {
            float sa = (i == 0) ? 1.0f : (abs(i) == 1 ? 0.78f : 0.60f);
            float ha = CARD_H * sa;
            float bot_a = cy + (float)i * spacing + ha * 0.5f;

            float sb = ((i + 1) == 0) ? 1.0f : (abs(i + 1) == 1 ? 0.78f : 0.60f);
            float hb = CARD_H * sb;
            float top_b = cy + (float)(i + 1) * spacing - hb * 0.5f;

            mid_y = (bot_a + top_b) * 0.5f;
        }

        if (mid_y > 4.0f && mid_y < WIN_H - 32.0f) {
            float lx = 10.0f, lw = LEFT_W - 20.0f;
            ui_rect(lx,        mid_y,      lw,   1.0f, GOLD_R, GOLD_G, GOLD_B, 0.50f);
            /* small diamond at center */
            float mx = LEFT_W * 0.5f;
            ui_rect(mx - 1.0f, mid_y - 2.0f, 2.0f, 2.0f, GOLD_R, GOLD_G, GOLD_B, 0.80f);
            ui_rect(mx - 2.0f, mid_y,         4.0f, 1.0f, GOLD_R, GOLD_G, GOLD_B, 0.80f);
            ui_rect(mx - 1.0f, mid_y + 1.0f,  2.0f, 2.0f, GOLD_R, GOLD_G, GOLD_B, 0.80f);
        }
        break;
    }

    /* ── Arrow animation ────────────────────────────────────────────── */
    uint32_t elapsed = SDL_GetTicks() - m_arrow_time;
    float pulse = 0.0f;
    if (elapsed < 260) {
        float t = (float)elapsed / 260.0f;
        pulse = sinf(t * 3.14159f);   /* smooth 0→peak→0 over 260ms */
    }

    /* Centered in the gap between card right edge (270) and separator (320) → x=295 */
    const float ax      = (LEFT_W / 2.0f + CARD_W / 2.0f + LEFT_W) * 0.5f;
    const float ay_up   = cy - 50.0f;   /* 100px total gap between the two arrows */
    const float ay_down = cy + 50.0f;

    /* Up arrow */
    {
        float extra  = (m_arrow_dir > 0) ? pulse : 0.0f;
        float h      = 12.0f + extra * 5.0f;
        float bright = 0.70f + extra * 0.30f;
        float yoff   = -(extra * 4.0f);   /* moves upward during animation */
        draw_tri(ax, ay_up + yoff, h, true,
                 GOLD_R * bright, GOLD_G * bright, GOLD_B * bright, 1.0f);
    }

    /* Down arrow */
    {
        float extra  = (m_arrow_dir < 0) ? pulse : 0.0f;
        float h      = 12.0f + extra * 5.0f;
        float bright = 0.70f + extra * 0.30f;
        float yoff   = extra * 4.0f;    /* moves downward during animation */
        draw_tri(ax, ay_down + yoff, h, false,
                 GOLD_R * bright, GOLD_G * bright, GOLD_B * bright, 1.0f);
    }

    /* ── Cards ──────────────────────────────────────────────────────── */
    for (int offset = -2; offset <= 2; offset++) {
        int idx = (m_selected + offset + n * 10) % n;
        float scale = (offset == 0) ? 1.0f : (abs(offset) == 1 ? 0.78f : 0.60f);
        float w = CARD_W * scale, h = CARD_H * scale;
        float x = cx - w * 0.5f;
        float y = cy + (float)offset * spacing - h * 0.5f;

        const GameGroup &g = m_groups[idx];

        ui_rect(x, y, w, h, PANEL_R, PANEL_G, PANEL_B, 0.88f);

        float bright = (offset == 0) ? 1.0f : (abs(offset) == 1 ? 0.52f : 0.30f);

        if (!g.logo_path.empty()) {
            float pad = 10.0f * scale;
            ui_image(x + pad, y + pad, w - pad * 2.0f, h - pad * 2.0f,
                     g.logo_path.c_str(), bright);
        } else {
            float ts = scale * 1.2f;
            if (ts < 1.0f) ts = 1.0f;
            char buf[48];
            snprintf(buf, sizeof(buf), "%.24s", g.title.c_str());
            ui_text(x + 6, y + h * 0.5f - 4.0f * ts, ts, buf, bright, bright, bright);
        }

        if (offset == 0) {
            float bw = 2.0f;
            ui_rect(x,       y,       w,  bw, GOLD_R, GOLD_G, GOLD_B, 1.0f);
            ui_rect(x,       y+h-bw,  w,  bw, GOLD_R, GOLD_G, GOLD_B, 1.0f);
            ui_rect(x,       y,       bw, h,  GOLD_R, GOLD_G, GOLD_B, 1.0f);
            ui_rect(x+w-bw,  y,       bw, h,  GOLD_R, GOLD_G, GOLD_B, 1.0f);
        }

        /* Version count badge: tells from the list whether there's a choice. */
        if (!g.sequential && g.entries.size() > 1) {
            float r  = 12.0f * scale;
            float bx = x + w - r - 4.0f * scale, by = y + r + 4.0f * scale;
            float ba = (offset == 0) ? 1.0f : 0.75f * bright + 0.2f;
            ui_circle(bx, by, r, GOLD_R * ba, GOLD_G * ba, GOLD_B * ba, 1.0f);
            char num[8];
            snprintf(num, sizeof(num), "%d", (int)g.entries.size());
            int px = (int)(16.0f * scale + 0.5f);
            ui_text_px(bx - (float)ui_text_width(px, num) * 0.5f,
                       by - (float)ui_text_line_height(px) * 0.5f, px, num, 0.14f, 0.09f, 0.02f, 1.0f);
        }

        if (g.sequential && offset == 0) {
            int cur = db_progress_get(g.key);
            float sz = 18.0f;
            float gap = 4.0f;
            float total_w = g.entries.size() * (sz + gap) - gap;
            float dx = x + (w - total_w) * 0.5f;
            for (int i = 0; i < (int)g.entries.size(); i++) {
                float bright = (i < cur) ? 1.0f : 0.25f;
                ui_image(dx, y + h - sz - 6, sz, sz, ASSET("imgs/tablet.png"), bright);
                dx += sz + gap;
            }
        }
    }

    if (m_selected < n)
        draw_detail(m_groups[m_selected], -1);
}

void Launcher::draw_submenu(void) {
    const GameGroup &g = m_groups[m_submenu];

    int n = (int)g.entries.size();
    float row_h   = 28.0f;
    float total_h = n * row_h;
    float avail   = WIN_H - 28.0f; /* minus bottom bar */
    float ly      = (avail - total_h) / 2.0f;
    float lx      = PAD;

    for (int i=0; i<n; i++) {
        const GameEntry &e = g.entries[i];
        bool unlocked = db_entry_unlocked(g, i);
        bool sel      = (i == m_sub_sel);

        if (sel)
            ui_rect(lx-4, ly-3, LEFT_W-PAD, row_h,
                    GOLD_DIM_R*0.25f, GOLD_DIM_G*0.20f, 0.02f, 0.85f);

        float cr, cg, cb;
        if (!unlocked)  { cr=0.40f; cg=0.40f; cb=0.40f; }
        else if (sel)   { cr=GOLD_R; cg=GOLD_G; cb=GOLD_B; }
        else            { cr=0.90f;  cg=0.90f;  cb=0.90f; }

        char buf[64];
        snprintf(buf,sizeof(buf),"%s%s", sel?"> ":"  ", e.title.c_str());
        if (!unlocked) strncat(buf," [locked]",sizeof(buf)-strlen(buf)-1);
        ui_text(lx, ly+4, 1.5f, buf, cr,cg,cb);

        if (g.sequential) {
            int prog = db_progress_get(g.key);
            if (i < prog)
                ui_text(LEFT_W-PAD*2, ly+4, 1.5f, "*", GOLD_R,GOLD_G,GOLD_B);
        }
        ly += row_h;
    }

    draw_detail(g, m_sub_sel);
}

void Launcher::draw_detail(const GameGroup &g, int entry_hint) {
    float x = LEFT_W + PAD*2;
    float y = 16.0f;
    float w = WIN_W - x - PAD;

    /* Determine logo: entry-specific if available, else group logo */
    const std::string *logo = &g.logo_path;
    if (entry_hint >= 0 && entry_hint < (int)g.entries.size()
            && !g.entries[entry_hint].logo_path.empty())
        logo = &g.entries[entry_hint].logo_path;

    if (!logo->empty()) {
        ui_image(x, y, w, 150.0f, logo->c_str(), 1.0f);
        y += 158.0f;
    } else {
        ui_text(x, y, 2.5f, g.title.c_str(), GOLD_R,GOLD_G,GOLD_B);
        y += 40;
    }

    /* Year and platform in off-white */
    if (g.year > 0) {
        char buf[32]; snprintf(buf,sizeof(buf),"%d",g.year);
        ui_text(x, y, 1.5f, buf, 0.80f,0.80f,0.78f);
        y += 24;
    }

    {
        std::string plat;
        if (entry_hint>=0 && entry_hint<(int)g.entries.size())
            plat = g.entries[entry_hint].platform;
        else if (!g.entries.empty())
            plat = g.entries[0].platform;
        if (!plat.empty()) {
            ui_text(x, y, 1.5f, plat.c_str(), 0.80f,0.80f,0.78f);
            y += 24;
        }
    }

    /* In the list, name the versions a game contains before it's opened. */
    if (entry_hint < 0 && !g.sequential && g.entries.size() > 1) {
        std::string line = std::to_string(g.entries.size()) + " versions: ";
        for (size_t i = 0; i < g.entries.size(); i++) {
            if (i) line += ", ";
            line += g.entries[i].title;
        }
        y = ui_text_wrap(x, y, w, 18, 22.0f, line.c_str(), UI_ALIGN_LEFT, 2,
                         GOLD_R, GOLD_G, GOLD_B, 1.0f) + 2.0f;
    }

    /* Gold separator line */
    ui_rect(x,y,w,1, GOLD_R,GOLD_G,GOLD_B,0.65f);
    y += 14;

    /* Group description in white */
    /* Screenshots: pick entry 0 in carousel mode, selected entry in submenu */
    const std::vector<std::string> *shots = nullptr;
    {
        int eidx = (entry_hint >= 0 && entry_hint < (int)g.entries.size()) ? entry_hint : 0;
        if (eidx < (int)g.entries.size() && !g.entries[eidx].screenshots.empty())
            shots = &g.entries[eidx].screenshots;
    }
    const float shot_h     = 200.0f;
    const float shot_sy    = WIN_H - 28.0f - shot_h - 8.0f;   /* y of strip */
    const float wrap_limit = shots ? shot_sy - 16.0f : WIN_H - 70.0f;

    auto draw_wrapped = [&](const std::string &text, float r, float g2, float b2) {
        size_t pos=0;
        while (pos<text.size() && y<wrap_limit) {
            size_t end = pos+58;
            if (end < text.size()) {
                size_t sp = text.rfind(' ', end);
                if (sp!=std::string::npos && sp>pos) end=sp;
            } else end=text.size();
            ui_text(x, y, 1.5f, text.substr(pos, end-pos).c_str(), r, g2, b2);
            y+=22; pos=end;
            while (pos<text.size() && text[pos]==' ') pos++;
        }
    };

    draw_wrapped(g.description, 0.92f,0.92f,0.92f);

    /* Version-specific description (only when an entry is selected) */
    if (entry_hint >= 0 && entry_hint < (int)g.entries.size()) {
        const std::string &vdesc = g.entries[entry_hint].version_desc;
        if (!vdesc.empty()) {
            y += 6;
            ui_rect(x,y,w,1, GOLD_R,GOLD_G,GOLD_B,0.30f);
            y += 10;
            ui_text(x, y, 1.2f, "This version:", GOLD_DIM_R,GOLD_DIM_G,GOLD_DIM_B);
            y += 18;
            draw_wrapped(vdesc, 0.80f,0.80f,0.75f);
        }
    }

    if (g.sequential) {
        y += 12;
        int cur = db_progress_get(g.key);
        char buf[64];
        snprintf(buf,sizeof(buf),"Progress: Week %d / %d", cur+1, (int)g.entries.size());
        ui_text(x,y,1.5f,buf, GOLD_R,GOLD_G,GOLD_B);
        y+=24;
#ifdef __SWITCH__
        ui_text(x,y,1.2f,"In-game: R3 = advance week (when complete)", 0.65f,0.65f,0.65f);
#else
        ui_text(x,y,1.2f,"In-game: Tab = advance week (when complete)", 0.65f,0.65f,0.65f);
#endif
    }

    /* Scrolling screenshot strip */
    if (shots && !shots->empty()) {
        struct ShotInfo { float dw; const char *path; };
        ShotInfo infos[32];
        int   count   = 0;
        float total_w = 0.0f;
        const float gap = 10.0f;

        for (int i = 0; i < (int)shots->size() && i < 32; i++) {
            int iw = 1, ih = 1;
            ui_image_size((*shots)[i].c_str(), &iw, &ih);
            float dw = shot_h * (float)iw / (float)ih;
            infos[count++] = { dw, (*shots)[i].c_str() };
            total_w += dw + gap;
        }

        /* Advance scroll */
        Uint32 now = SDL_GetTicks();
        if (m_shot_last_t == 0) m_shot_last_t = now;
        float dt = (float)(now - m_shot_last_t) / 1000.0f;
        if (dt > 0.1f) dt = 0.1f;
        m_shot_last_t = now;
        if (total_w > 0.0f)
            m_shot_scroll = fmodf(m_shot_scroll + 50.0f * dt, total_w);

        /* Draw clipped to the panel strip */
        ui_set_clip(x, shot_sy, w, shot_h);
        ui_rect(x, shot_sy, w, shot_h, PANEL_R, PANEL_G, PANEL_B, 0.85f);

        int reps = (int)(w / total_w) + 2;
        float start_x = x - floorf(m_shot_scroll);   /* floor → pixel-aligned, no jitter */
        for (int rep = 0; rep < reps; rep++) {
            float cx = start_x + rep * total_w;
            for (int i = 0; i < count; i++) {
                if (cx + infos[i].dw >= x && cx < x + w)
                    ui_image(cx, shot_sy, infos[i].dw, shot_h, infos[i].path, 1.0f);
                cx += infos[i].dw + gap;
            }
        }
        ui_clear_clip();
    }
}

void Launcher::draw_config_modal(void) {
    if (m_config_group < 0 || m_config_group >= (int)m_groups.size()) return;
    const GameGroup &g = m_groups[m_config_group];
    int active_shader = prefs_get_shader(g.key);

    /* Modal dimensions */
    const float MW   = 480.0f;
    const float ROW  = 30.0f;
    const float MH   = 16.0f + 38.0f + 26.0f + 12.0f + 24.0f + NUM_SHADERS * ROW
                     + 12.0f + ROW + 14.0f + 24.0f + ROW + 18.0f;
    const float MX   = in_3d() ? (WIN_W - MW) * 0.5f : LEFT_W + (WIN_W - LEFT_W - MW) * 0.5f;
    const float MY   = (WIN_H - HINT_BAR_H - MH) * 0.5f;

    /* Dim everything behind the modal */
    ui_rect(0.0f, 0.0f, WIN_W, WIN_H - HINT_BAR_H, 0.0f, 0.0f, 0.0f, 0.55f);

    /* Modal card with a gold border */
    ui_round_rect(MX, MY, MW, MH, 10.0f, 0.04f, 0.09f, 0.06f, 0.97f);
    ui_round_rect_outline(MX, MY, MW, MH, 10.0f, 2.0f, GOLD_R, GOLD_G, GOLD_B, 0.90f);

    float tx = MX + 20.0f;
    float ty = MY + 16.0f;

    /* Title */
    ui_text_px(tx, ty, 32, "SETTINGS", GOLD_R, GOLD_G, GOLD_B, 1.0f);
    ty += 38.0f;
    {
        char title[48];
        snprintf(title, sizeof(title), "%.40s", g.title.c_str());
        ui_text_px(tx, ty, 18, title, 0.80f, 0.80f, 0.75f, 1.0f);
    }
    ty += 26.0f;

    /* Separator */
    ui_rect(tx, ty, MW - 40.0f, 1.0f, GOLD_R, GOLD_G, GOLD_B, 0.40f);
    ty += 12.0f;

    if (!m_clear_confirm) {
        /* Shader section label */
        ui_text_px(tx, ty, 15, "DISPLAY SHADER  (this game)", GOLD_DIM_R, GOLD_DIM_G, GOLD_DIM_B, 1.0f);
        ty += 24.0f;

        auto row_bg = [&](float y, bool hov, bool danger) {
            if (!hov) return;
            if (danger) ui_round_rect(MX + 6.0f, y - 3.0f, MW - 12.0f, ROW, 6.0f, 0.25f, 0.04f, 0.04f, 0.85f);
            else        ui_round_rect(MX + 6.0f, y - 3.0f, MW - 12.0f, ROW, 6.0f,
                                      GOLD_R * 0.12f, GOLD_G * 0.10f, 0.02f, 0.95f);
        };

        for (int i = 0; i < NUM_SHADERS; i++) {
            bool hov = (m_config_sel == i);
            row_bg(ty, hov, false);

            /* Active shader bullet */
            float br, bg, bb;
            if (hov)       { br=GOLD_R;  bg=GOLD_G;  bb=GOLD_B; }
            else if (i==active_shader) { br=0.85f; bg=0.85f; bb=0.80f; }
            else           { br=0.55f;   bg=0.55f;   bb=0.50f; }

            char row_buf[64];
            snprintf(row_buf, sizeof(row_buf), "%s %s",
                     (i == active_shader) ? "[x]" : "[ ]", SHADER_NAMES[i]);
            ui_text_px(tx + 4.0f, ty + 2.0f, 20, row_buf, br, bg, bb, 1.0f);
            ty += ROW;
        }

        /* Separator */
        ty += 4.0f;
        ui_rect(tx, ty, MW - 40.0f, 1.0f, GOLD_R, GOLD_G, GOLD_B, 0.30f);
        ty += 8.0f;

        /* Clear data row */
        bool hov_clear = (m_config_sel == ROW_CLEAR);
        row_bg(ty, hov_clear, true);
        float cr = hov_clear ? 1.0f : 0.70f;
        ui_text_px(tx + 4.0f, ty + 2.0f, 20, "Clear Save Data", cr, 0.28f, 0.28f, 1.0f);
        ty += ROW;

        /* Launcher view (global) */
        ty += 4.0f;
        ui_rect(tx, ty, MW - 40.0f, 1.0f, GOLD_R, GOLD_G, GOLD_B, 0.30f);
        ty += 10.0f;
        ui_text_px(tx, ty, 15, "LAUNCHER VIEW  (all games)", GOLD_DIM_R, GOLD_DIM_G, GOLD_DIM_B, 1.0f);
        ty += 24.0f;
        bool hov_view = (m_config_sel == ROW_VIEW);
        row_bg(ty, hov_view, false);
        const bool is3d = in_3d();
        float vr = hov_view ? GOLD_R : 0.85f, vg = hov_view ? GOLD_G : 0.85f, vb = hov_view ? GOLD_B : 0.80f;
        ui_text_px(tx + 4.0f, ty + 2.0f, 20, "View:", vr, vg, vb, 1.0f);
        float cx = tx + 90.0f;
        cx += uikit_chip(cx, ty - 1.0f, "3D Shelf", 14, is3d, is3d ? 1.0f : 0.6f) + 8.0f;
        uikit_chip(cx, ty - 1.0f, "Classic list", 14, !is3d, !is3d ? 1.0f : 0.6f);
    } else {
        /* Confirmation sub-step */
        ty += 60.0f;
        ui_text_px(tx, ty, 22, "Delete ALL saves for this game?", 1.0f, 0.4f, 0.3f, 1.0f);
        ty += 32.0f;
        ui_text_px(tx, ty, 18, "This cannot be undone.", 0.80f, 0.55f, 0.50f, 1.0f);
        ty += 40.0f;
        float gx = tx;
        gx += uikit_glyph(gx, ty + 10.0f, HB_CONFIRM, m_style, 1.0f) + 8.0f;
        ui_text_px(gx, ty, 18, "Confirm", GOLD_R, GOLD_G, GOLD_B, 1.0f);
        gx += (float)ui_text_width(18, "Confirm") + 24.0f;
        gx += uikit_glyph(gx, ty + 10.0f, HB_BACK, m_style, 1.0f) + 8.0f;
        ui_text_px(gx, ty, 18, "Cancel", GOLD_R, GOLD_G, GOLD_B, 1.0f);
    }
}
