#include "launcher.h"
#include "ui.h"
#include "sfx.h"
#include "prefs.h"
#include "paths.h"
#include <SDL.h>
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

/* Layout constants (1280x720) */
static const float WIN_W  = 1280.0f;
static const float WIN_H  =  720.0f;
static const float LEFT_W =  320.0f;
static const float PAD    =   20.0f;

/* Card sizes */
static const float CARD_W = 220.0f;
static const float CARD_H = 140.0f;

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

void Launcher::load(std::vector<GameGroup> groups) {
    m_groups   = std::move(groups);
    m_selected = 0;
    m_submenu  = -1;
    m_sub_sel  = 0;
}

LaunchRequest Launcher::poll_launch(void) {
    LaunchRequest r = m_pending;
    m_pending = LaunchRequest{};
    return r;
}

/* ── Input ──────────────────────────────────────────────────────────── */
void Launcher::handle_key(int key, bool down) {
    if (!down) return;

    if (m_config_open) {
        if (m_clear_confirm) {
            if (key==13) {
                /* Confirm clear: delete all SRM files for the group */
                const GameGroup &g = m_groups[m_config_group];
                for (auto &e : g.entries)
                    if (!e.srm_path.empty()) delete_file(e.srm_path.c_str());
                if (g.sequential) db_progress_set(g.key, 0);
                m_clear_confirm = false;
                sfx_play_confirm();
            }
            if (key==27 || key==8) { m_clear_confirm = false; sfx_play_back(); }
            return;
        }
        int n = NUM_SHADERS + 1; /* shader rows + clear row */
        if (key==1073741906 || key=='w') { m_config_sel=(m_config_sel-1+n)%n; sfx_play_nav(); }
        if (key==1073741905 || key=='s') { m_config_sel=(m_config_sel+1)%n;   sfx_play_nav(); }
        if (key==13) {
            if (m_config_sel < NUM_SHADERS) {
                prefs_set_shader(m_groups[m_config_group].key, m_config_sel);
                prefs_save();
                sfx_play_confirm();
            } else {
                m_clear_confirm = true;
                sfx_play_confirm();
            }
        }
        if (key==27 || key==8) { close_config(); sfx_play_back(); }
        return;
    }

    if (m_submenu < 0) {
        if (key==1073741906 || key=='w') { m_selected=(m_selected-1+(int)m_groups.size())%(int)m_groups.size(); m_arrow_dir=+1; m_arrow_time=SDL_GetTicks(); sfx_play_nav(); }
        if (key==1073741905 || key=='s') { m_selected=(m_selected+1)%(int)m_groups.size();                      m_arrow_dir=-1; m_arrow_time=SDL_GetTicks(); sfx_play_nav(); }
        if (key==13) { confirm_selection(); m_pending.valid ? sfx_play_enter_game() : sfx_play_confirm(); }
        if (key==9) { open_config(); sfx_play_confirm(); }  /* Tab */
    } else {
        const GameGroup &g = m_groups[m_submenu];
        if (key==1073741906 || key=='w') { m_sub_sel=(m_sub_sel-1+(int)g.entries.size())%(int)g.entries.size(); m_arrow_dir=+1; m_arrow_time=SDL_GetTicks(); sfx_play_nav(); }
        if (key==1073741905 || key=='s') { m_sub_sel=(m_sub_sel+1)%(int)g.entries.size();                       m_arrow_dir=-1; m_arrow_time=SDL_GetTicks(); sfx_play_nav(); }
        if (key==13) { confirm_selection(); m_pending.valid ? sfx_play_enter_game() : sfx_play_confirm(); }
        if (key==27 || key==8) { m_submenu=-1; sfx_play_back(); }
        if (key==9) { open_config(); sfx_play_confirm(); }  /* Tab */
    }
}

void Launcher::handle_button(int btn, bool down) {
    if (!down) return;
    /* Switch labels A/B by physical position opposite to SDL's Xbox layout:
       Nintendo A (right) = SDL button 1, Nintendo B (bottom) = SDL button 0. */
#ifdef __SWITCH__
    const int BTN_A=1, BTN_B=0, BTN_Y=2, BTN_UP=11, BTN_DOWN=12;
#else
    const int BTN_A=0, BTN_B=1, BTN_Y=2, BTN_UP=11, BTN_DOWN=12;
#endif

    if (m_config_open) {
        if (m_clear_confirm) {
            if (btn==BTN_A) {
                const GameGroup &g = m_groups[m_config_group];
                for (auto &e : g.entries)
                    if (!e.srm_path.empty()) delete_file(e.srm_path.c_str());
                if (g.sequential) db_progress_set(g.key, 0);
                m_clear_confirm = false;
                sfx_play_confirm();
            }
            if (btn==BTN_B) { m_clear_confirm = false; sfx_play_back(); }
            return;
        }
        int n = NUM_SHADERS + 1;
        if (btn==BTN_UP)   { m_config_sel=(m_config_sel-1+n)%n; sfx_play_nav(); }
        if (btn==BTN_DOWN) { m_config_sel=(m_config_sel+1)%n;   sfx_play_nav(); }
        if (btn==BTN_A) {
            if (m_config_sel < NUM_SHADERS) {
                prefs_set_shader(m_groups[m_config_group].key, m_config_sel);
                prefs_save();
                sfx_play_confirm();
            } else {
                m_clear_confirm = true;
                sfx_play_confirm();
            }
        }
        if (btn==BTN_B || btn==BTN_Y) { close_config(); sfx_play_back(); }
        return;
    }

    if (m_submenu < 0) {
        if (btn==BTN_UP)   { m_selected=(m_selected-1+(int)m_groups.size())%(int)m_groups.size(); m_arrow_dir=+1; m_arrow_time=SDL_GetTicks(); sfx_play_nav(); }
        if (btn==BTN_DOWN) { m_selected=(m_selected+1)%(int)m_groups.size();                      m_arrow_dir=-1; m_arrow_time=SDL_GetTicks(); sfx_play_nav(); }
        if (btn==BTN_A)    { confirm_selection(); m_pending.valid ? sfx_play_enter_game() : sfx_play_confirm(); }
        if (btn==BTN_Y)    { open_config(); sfx_play_confirm(); }
    } else {
        const GameGroup &g = m_groups[m_submenu];
        if (btn==BTN_UP)   { m_sub_sel=(m_sub_sel-1+(int)g.entries.size())%(int)g.entries.size(); m_arrow_dir=+1; m_arrow_time=SDL_GetTicks(); sfx_play_nav(); }
        if (btn==BTN_DOWN) { m_sub_sel=(m_sub_sel+1)%(int)g.entries.size();                       m_arrow_dir=-1; m_arrow_time=SDL_GetTicks(); sfx_play_nav(); }
        if (btn==BTN_A)    { confirm_selection(); m_pending.valid ? sfx_play_enter_game() : sfx_play_confirm(); }
        if (btn==BTN_B)    { m_submenu=-1; sfx_play_back(); }
        if (btn==BTN_Y)    { open_config(); sfx_play_confirm(); }
    }
}

void Launcher::open_config(void) {
    m_config_group  = (m_submenu >= 0) ? m_submenu : m_selected;
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
    GameGroup &g = m_groups[m_selected];

    if (m_submenu < 0) {
        if (g.sequential) {
            int idx = db_progress_get(g.key);
            if (idx >= (int)g.entries.size()) idx = (int)g.entries.size()-1;
            const GameEntry &e = g.entries[idx];
            m_pending.valid             = true;
            m_pending.rom_path          = e.rom_path;
            m_pending.core_dll          = e.core_dll;
            m_pending.srm_path          = e.srm_path;
            m_pending.carry_srm_from    = e.carry_srm_from;
            m_pending.group_key         = g.key;
            m_pending.entry_idx         = idx;
            m_pending.is_sequential     = true;
            m_pending.week_complete_mask = e.week_complete_mask;
            m_pending.has_next_week     = (idx+1 < (int)g.entries.size());
            m_pending.external          = e.external;
            m_pending.exec_path         = e.exec_path;
            m_pending.exec_argv         = e.exec_argv;
            m_pending.keep_open         = e.keep_open;
        } else if (g.entries.size()==1) {
            const GameEntry &e = g.entries[0];
            m_pending.valid             = true;
            m_pending.rom_path          = e.rom_path;
            m_pending.core_dll          = e.core_dll;
            m_pending.srm_path          = e.srm_path;
            m_pending.carry_srm_from    = e.carry_srm_from;
            m_pending.group_key         = g.key;
            m_pending.entry_idx         = 0;
            m_pending.is_sequential     = false;
            m_pending.week_complete_mask = 0;
            m_pending.has_next_week     = false;
            m_pending.external          = e.external;
            m_pending.exec_path         = e.exec_path;
            m_pending.exec_argv         = e.exec_argv;
            m_pending.keep_open         = e.keep_open;
        } else {
            m_submenu = m_selected;
            m_sub_sel = 0;
        }
    } else {
        if (!db_entry_unlocked(g, m_sub_sel)) return;
        const GameEntry &e = g.entries[m_sub_sel];
        m_pending.valid             = true;
        m_pending.rom_path          = e.rom_path;
        m_pending.core_dll          = e.core_dll;
        m_pending.srm_path          = e.srm_path;
        m_pending.carry_srm_from    = e.carry_srm_from;
        m_pending.group_key         = g.key;
        m_pending.entry_idx         = m_sub_sel;
        m_pending.is_sequential     = g.sequential;
        m_pending.week_complete_mask = e.week_complete_mask;
        m_pending.has_next_week     = g.sequential && (m_sub_sel+1 < (int)g.entries.size());
        m_pending.external          = e.external;
        m_pending.exec_path         = e.exec_path;
        m_pending.exec_argv         = e.exec_argv;
        m_pending.keep_open         = e.keep_open;
        m_submenu = -1;
    }
}

/* ── Drawing helpers ────────────────────────────────────────────────── */

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

/* ── Drawing ────────────────────────────────────────────────────────── */
void Launcher::draw(void) {
    if (m_groups.empty()) {
        ui_begin();
        ui_rect(0,0,WIN_W,WIN_H, PANEL_R,PANEL_G,PANEL_B,0.88f);
        ui_text(PAD, WIN_H/2-16, 2.0f, "No ROMs found. Place roms + db.json and restart.",
                1.0f,1.0f,1.0f);
        ui_end();
        return;
    }

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

    if (m_config_open)
        draw_config_modal();

    /* Bottom hint bar */
    ui_rect(0,WIN_H-28,WIN_W,28, PANEL_R,PANEL_G,PANEL_B,0.90f);
    ui_rect(0,WIN_H-29,WIN_W,1, GOLD_R,GOLD_G,GOLD_B,0.60f);
    const char *nav_hint;
    const char *game_hint = "In-game: Esc = launcher";
#ifdef __SWITCH__
    game_hint = "In-game: L3 = launcher";
    if (m_config_open)
        nav_hint = "D-pad: navigate   A: select   B: close";
    else if (m_submenu < 0)
        nav_hint = "D-pad: navigate   A: select   Y: settings";
    else
        nav_hint = "D-pad: navigate   A: launch   B: back   Y: settings";
#else
    if (m_config_open)
        nav_hint = "Up/Down: navigate   Enter: apply   Esc: close";
    else if (m_submenu < 0)
        nav_hint = "Up/Down: navigate   Enter: select   Tab: settings   F11: maximize";
    else
        nav_hint = "Up/Down: navigate   Enter: launch   Esc: back   Tab: settings   F11: maximize";
#endif
    ui_text(PAD,        WIN_H-20, 1.0f, nav_hint,  0.65f,0.65f,0.65f);
    ui_text(900,        WIN_H-20, 1.0f, game_hint, 0.65f,0.65f,0.65f);

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

    /* Arrows sit in the gap between card right-edge (~270) and separator (320) */
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
        float alpha = (offset == 0) ? 1.0f : (abs(offset) == 1 ? 0.55f : 0.30f);
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
    const float MW   = 460.0f;
    const float MX   = LEFT_W + (WIN_W - LEFT_W - MW) * 0.5f;
    const float ROW  = 28.0f;
    const float MH   = 50.0f + 20.0f + 20.0f + NUM_SHADERS * ROW + 14.0f + ROW + 40.0f;
    const float MY   = (WIN_H - 28.0f - MH) * 0.5f;

    /* Dim everything behind the modal */
    ui_rect(0.0f, 0.0f, WIN_W, WIN_H - 28.0f, 0.0f, 0.0f, 0.0f, 0.55f);

    /* Modal card */
    ui_rect(MX, MY, MW, MH, 0.04f, 0.09f, 0.06f, 0.97f);
    /* Gold border */
    ui_rect(MX,          MY,          MW,   2.0f, GOLD_R,GOLD_G,GOLD_B,0.90f);
    ui_rect(MX,          MY+MH-2.0f,  MW,   2.0f, GOLD_R,GOLD_G,GOLD_B,0.90f);
    ui_rect(MX,          MY,          2.0f, MH,   GOLD_R,GOLD_G,GOLD_B,0.90f);
    ui_rect(MX+MW-2.0f,  MY,          2.0f, MH,   GOLD_R,GOLD_G,GOLD_B,0.90f);

    float tx = MX + 18.0f;
    float ty = MY + 14.0f;

    /* Title */
    ui_text(tx, ty, 1.8f, "SETTINGS", GOLD_R, GOLD_G, GOLD_B);
    ty += 26.0f;
    {
        char title[48];
        snprintf(title, sizeof(title), "%.36s", g.title.c_str());
        ui_text(tx, ty, 1.3f, title, 0.80f, 0.80f, 0.75f);
    }
    ty += 20.0f;

    /* Separator */
    ui_rect(tx, ty, MW - 36.0f, 1.0f, GOLD_R, GOLD_G, GOLD_B, 0.40f);
    ty += 12.0f;

    /* Shader section label */
    ui_text(tx, ty, 1.2f, "DISPLAY SHADER", GOLD_DIM_R, GOLD_DIM_G, GOLD_DIM_B);
    ty += 20.0f;

    if (!m_clear_confirm) {
        for (int i = 0; i < NUM_SHADERS; i++) {
            bool hov = (m_config_sel == i);
            if (hov)
                ui_rect(MX + 4.0f, ty - 3.0f, MW - 8.0f, ROW,
                        GOLD_R*0.10f, GOLD_G*0.08f, 0.02f, 0.90f);

            /* Active shader bullet */
            float br, bg, bb;
            if (hov)       { br=GOLD_R;  bg=GOLD_G;  bb=GOLD_B; }
            else if (i==active_shader) { br=0.85f; bg=0.85f; bb=0.80f; }
            else           { br=0.55f;   bg=0.55f;   bb=0.50f; }

            char row_buf[64];
            snprintf(row_buf, sizeof(row_buf), "%s %s",
                     (i == active_shader) ? "[x]" : "[ ]", SHADER_NAMES[i]);
            ui_text(tx + 4.0f, ty + 4.0f, 1.4f, row_buf, br, bg, bb);
            ty += ROW;
        }

        /* Separator */
        ui_rect(tx, ty, MW - 36.0f, 1.0f, GOLD_R, GOLD_G, GOLD_B, 0.30f);
        ty += 14.0f;

        /* Clear data row */
        bool hov_clear = (m_config_sel == NUM_SHADERS);
        if (hov_clear)
            ui_rect(MX + 4.0f, ty - 3.0f, MW - 8.0f, ROW,
                    0.25f, 0.04f, 0.04f, 0.85f);
        float cr = hov_clear ? 1.0f : 0.70f;
        ui_text(tx + 4.0f, ty + 4.0f, 1.4f, "Clear Save Data", cr, 0.28f, 0.28f);
    } else {
        /* Confirmation sub-step */
        ty += NUM_SHADERS * ROW + 28.0f;
        ui_text(tx, ty, 1.4f, "Delete ALL saves for this game?", 1.0f, 0.4f, 0.3f);
        ty += 22.0f;
        ui_text(tx, ty, 1.3f, "This cannot be undone.", 0.80f, 0.55f, 0.50f);
        ty += 28.0f;
#ifdef __SWITCH__
        ui_text(tx, ty, 1.3f, "A: Confirm   B: Cancel", GOLD_R, GOLD_G, GOLD_B);
#else
        ui_text(tx, ty, 1.3f, "Enter: Confirm   Esc: Cancel", GOLD_R, GOLD_G, GOLD_B);
#endif
    }
}
