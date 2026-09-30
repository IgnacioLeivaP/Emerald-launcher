#include "launcher.h"
#include "boxart.h"
#include "boxshape.h"
#include "controls.h"
#include "perf.h"
#include "renderer.h"
#include "fsutil.h"
#include "i18n.h"
#include "savestate.h"
#include "stats.h"
#include "status.h"
#include "toast.h"
#include "ui.h"
#include "sfx.h"
#include "prefs.h"
#include <cctype>
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

static const int NUM_SHADERS = RENDERER_SHADER_COUNT;
/* Settings rows: this game's shader and saves, then options for all games. */
enum {
    ROW_CLEAR = NUM_SHADERS,
    ROW_VIEW,
    ROW_SORT,
    ROW_SHOW,
    ROW_LANG,
    ROW_MUSIC,
    ROW_ATTRACT,
    ROW_PERF,
    ROW_CONTROLS,
    CONFIG_ROWS
};
static const char *LANG_PREFS[] = { "auto", "en", "es" };
static const char *SORT_PREFS[] = { "default", "az", "year", "platform", "recent", "played" };
static const char *SORT_NAMES[] = { "db.json order", "A-Z", "Year", "Platform", "Recently played", "Most played" };
static const int   NUM_SORTS = 6;

/* Layout constants (1280x720) */
static const float WIN_W  = UI_W;
static const float WIN_H  = UI_H;
static const float LEFT_W =  320.0f;
static const float PAD    =   20.0f;

/* Card sizes */
static const float CARD_W = 220.0f;
static const float CARD_H = 140.0f;

/* SDL game-controller buttons are positional (Xbox layout): the Switch's
   A (right) is SDL's B and its B (bottom) is SDL's A. */
#ifdef __SWITCH__
static const int BTN_A = SDL_CONTROLLER_BUTTON_B, BTN_B = SDL_CONTROLLER_BUTTON_A;
#else
static const int BTN_A = SDL_CONTROLLER_BUTTON_A, BTN_B = SDL_CONTROLLER_BUTTON_B;
#endif
static const int BTN_LEFT_FACE = SDL_CONTROLLER_BUTTON_X;   /* Nintendo Y / Xbox X: settings    */
static const int BTN_TOP_FACE  = SDL_CONTROLLER_BUTTON_Y;   /* Nintendo X / Xbox Y: look at box */
static const int BTN_START = SDL_CONTROLLER_BUTTON_START;
static const int BTN_L = SDL_CONTROLLER_BUTTON_LEFTSHOULDER, BTN_R = SDL_CONTROLLER_BUTTON_RIGHTSHOULDER;
static const int BTN_UP = SDL_CONTROLLER_BUTTON_DPAD_UP, BTN_DOWN = SDL_CONTROLLER_BUTTON_DPAD_DOWN;
static const int BTN_LEFT = SDL_CONTROLLER_BUTTON_DPAD_LEFT, BTN_RIGHT = SDL_CONTROLLER_BUTTON_DPAD_RIGHT;

static const int STICK_DEAD = 8000;

void Launcher::load(std::vector<GameGroup> groups) {
    m_all      = std::move(groups);
    m_submenu  = -1;
    m_sub_sel  = 0;
    m_use_3d   = prefs_get_view_3d();
    rebuild_view(prefs_get_last_group());      /* resume where the user left off */
}

std::string Launcher::current_key(void) const {
    if (m_groups.empty()) return "";
    int g = in_3d() ? m_shelf.selected() : (m_submenu >= 0 ? m_submenu : m_selected);
    g = std::max(0, std::min(g, (int)m_groups.size() - 1));
    return m_groups[(size_t)g].key;
}

/* The games shown, in the chosen order (Settings): all of them or only the
   favorites, by db.json order, title, year, platform, last played or play
   time. Rebuilt when any of that changes; keeps `keep_key` selected. */
void Launcher::rebuild_view(const std::string &keep_key) {
    const bool fav_only = prefs_get_show() == "favorites";
    std::vector<GameGroup> v;
    for (const auto &g : m_all)
        if (!fav_only || prefs_is_favorite(g.key)) v.push_back(g);
    if (v.empty()) v = m_all;                           /* no favorites yet: show everything */

    auto lower = [](std::string t) { for (auto &c : t) c = (char)tolower((unsigned char)c); return t; };
    auto by_title = [&](const GameGroup &a, const GameGroup &b) { return lower(db_title(a)) < lower(db_title(b)); };
    const std::string mode = prefs_get_sort();
    if (mode == "az") {
        std::stable_sort(v.begin(), v.end(), by_title);
    } else if (mode == "year") {
        std::stable_sort(v.begin(), v.end(), [](const GameGroup &a, const GameGroup &b) {
            return (a.year > 0 ? a.year : 99999) < (b.year > 0 ? b.year : 99999);
        });
    } else if (mode == "platform") {
        auto fam = [](const GameGroup &g) {
            int f = g.entries.empty() ? FAM_DEFAULT : (int)boxfamily_of(g.entries[0].platform.c_str());
            return f == FAM_DEFAULT ? FAM_COUNT : f;          /* unknown platforms last */
        };
        std::stable_sort(v.begin(), v.end(), [&](const GameGroup &a, const GameGroup &b) {
            const int fa = fam(a), fb = fam(b);
            return fa != fb ? fa < fb : (a.year > 0 ? a.year : 99999) < (b.year > 0 ? b.year : 99999);
        });
    } else if (mode == "recent" || mode == "played") {
        std::vector<std::pair<double, long long>> key(v.size());
        for (size_t i = 0; i < v.size(); i++) group_play(v[i], key[i].first, key[i].second);
        std::vector<size_t> idx(v.size());
        for (size_t i = 0; i < idx.size(); i++) idx[i] = i;
        std::stable_sort(idx.begin(), idx.end(), [&](size_t a, size_t b) {
            return mode == "recent" ? key[a].second > key[b].second : key[a].first > key[b].first;
        });
        std::vector<GameGroup> sorted;
        for (size_t i : idx) sorted.push_back(std::move(v[i]));
        v = std::move(sorted);
    }

    boxart_drop_queue();              /* queued box art points into the old list */
    const std::string config_key = (m_config_group >= 0 && m_config_group < (int)m_groups.size())
                                   ? m_groups[(size_t)m_config_group].key : keep_key;
    m_groups = std::move(v);
    /* The settings modal follows its game to its new place. */
    m_config_group = 0;
    for (size_t i = 0; i < m_groups.size(); i++)
        if (m_groups[i].key == config_key) m_config_group = (int)i;
    m_selected = 0;
    for (size_t i = 0; i < m_groups.size(); i++)
        if (m_groups[i].key == keep_key) { m_selected = (int)i; break; }
    m_submenu = -1;
    m_shelf.attach(&m_groups);
    for (size_t i = 0; i < m_groups.size(); i++) {
        int e = prefs_get_last_entry(m_groups[i].key);
        if (e >= 0 && e < (int)m_groups[i].entries.size()) m_shelf.set_front((int)i, e);
    }
    m_shelf.select(m_selected);
}

void Launcher::toggle_favorite(int g) {
    if (g < 0 || g >= (int)m_groups.size()) return;
    const std::string key = m_groups[(size_t)g].key;
    const bool on = !prefs_is_favorite(key);
    prefs_set_favorite(key, on);
    prefs_save();
    toast_show(on ? tr("Added to favorites") : tr("Removed from favorites"));
    sfx_play_confirm();
    if (!on && prefs_get_show() == "favorites") rebuild_view(key);   /* it leaves the list */
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
    prefs_set_last_group(current_key());
    prefs_save();
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
    r.stem               = e.stem;
    r.title              = db_title(g);
    r.version            = g.entries.size() > 1 || g.sequential ? db_entry_title(e) : std::string();
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
    case 'f': dispatch(IN_FAVORITE); break;
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
    else if (btn == SDL_CONTROLLER_BUTTON_BACK) in = IN_FAVORITE;
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
    if (controls_is_open()) {
        if (down && button == 3) { controls_close(); sfx_play_back(); }
        return;
    }
    if (m_resume_open) {
        if (down && button == 3) { m_resume_open = false; sfx_play_back(); }
        if (!down && button == 1)
            for (int i = 0; i < 2; i++) {
                const float *r = m_resume_btn[i];
                if (x >= r[0] && x <= r[0] + r[2] && y >= r[1] && y <= r[1] + r[3]) {
                    m_resume_sel = i;
                    resume_input(IN_CONFIRM);
                    break;
                }
            }
        return;
    }
    if (m_config_open) {
        if (down && button == 3) { close_config(); sfx_play_back(); }
        return;
    }
    if (in_3d()) m_shelf.mouse_button(x, y, button, down);
}

void Launcher::handle_mouse_motion(float x, float y) {
    if (m_groups.empty()) return;
    if (m_resume_open) {
        for (int i = 0; i < 2; i++) {
            const float *r = m_resume_btn[i];
            if (x >= r[0] && x <= r[0] + r[2] && y >= r[1] && y <= r[1] + r[3]) m_resume_sel = i;
        }
        return;
    }
    if (!m_config_open && in_3d()) m_shelf.mouse_motion(x, y);
}

void Launcher::handle_mouse_wheel(int dy) {
    if (m_groups.empty() || dy == 0) return;
    if (modal_open())   { dispatch(dy > 0 ? IN_UP : IN_DOWN); return; }
    if (in_3d())        { m_shelf.mouse_wheel(dy); return; }
    dispatch(dy > 0 ? IN_UP : IN_DOWN);
}

void Launcher::dispatch(UiInput in) {
    if (m_groups.empty()) return;
    if (controls_is_open()) { controls_ui_input(in); return; }
    if (m_resume_open) { resume_input(in); return; }
    if (m_config_open) { config_input(in); return; }
    if (in_3d())       { m_shelf.input(in); return; }
    classic_input(in);
}

void Launcher::config_input(UiInput in) {
    if (m_clear_confirm) {
        if (in == IN_CONFIRM) {
            /* Confirm clear: delete the group's saves and save states */
            const GameGroup &g = m_groups[(size_t)m_config_group];
            for (auto &e : g.entries) {
                if (e.srm_path.empty()) continue;
                delete_file(e.srm_path.c_str());
                const std::string saves = fs_dirname(e.srm_path);
                state_delete(state_path(saves, e.stem, false));
                state_delete(state_path(saves, e.stem, true));
                entry_status_forget(g.key, e.stem);
            }
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
        if (m_config_sel >= ROW_VIEW && m_config_sel != ROW_CONTROLS) {
            change_option(m_config_sel, in == IN_LEFT ? -1 : 1);
            sfx_play_nav();
        }
        break;
    case IN_CONFIRM:
        if (m_config_sel < NUM_SHADERS) {
            prefs_set_shader(m_groups[(size_t)m_config_group].key, m_config_sel);
            prefs_save();
        } else if (m_config_sel == ROW_CLEAR) {
            m_clear_confirm = true;
        } else {
            change_option(m_config_sel, +1);
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

/* Options for all games: view, language, performance overlay. */
void Launcher::change_option(int row, int d) {
    if (row == ROW_VIEW) {
        set_view_3d(!in_3d());
    } else if (row == ROW_LANG) {
        int cur = 0;
        for (int i = 0; i < 3; i++) if (prefs_get_language() == LANG_PREFS[i]) cur = i;
        cur = (cur + d + 3) % 3;
        prefs_set_language(LANG_PREFS[cur]);
        i18n_init(LANG_PREFS[cur]);
        boxart_release();                      /* the boxes print text too */
        prefs_save();
    } else if (row == ROW_SORT) {
        int cur = 0;
        for (int i = 0; i < NUM_SORTS; i++) if (prefs_get_sort() == SORT_PREFS[i]) cur = i;
        prefs_set_sort(SORT_PREFS[(cur + d + NUM_SORTS) % NUM_SORTS]);
        prefs_save();
        rebuild_view(current_key());
    } else if (row == ROW_SHOW) {
        const bool fav = prefs_get_show() != "favorites";
        prefs_set_show(fav ? "favorites" : "all");
        prefs_save();
        rebuild_view(current_key());
    } else if (row == ROW_MUSIC) {
        prefs_set_music(!prefs_get_music());
        sfx_music_play(prefs_get_music());
        prefs_save();
    } else if (row == ROW_ATTRACT) {
        prefs_set_attract(!prefs_get_attract());
        prefs_save();
    } else if (row == ROW_PERF) {
        perf_set_enabled(!perf_enabled());
        prefs_set_perf_hud(perf_enabled());
        prefs_save();
    } else if (row == ROW_CONTROLS) {
        controls_open();
    }
}

void Launcher::classic_input(UiInput in) {
    const int n = (int)m_groups.size();
    if (m_submenu < 0) {
        switch (in) {
        case IN_UP:   m_selected = (m_selected - 1 + n) % n; m_arrow_dir = +1; m_arrow_time = SDL_GetTicks(); sfx_play_nav(); break;
        case IN_DOWN: m_selected = (m_selected + 1) % n;     m_arrow_dir = -1; m_arrow_time = SDL_GetTicks(); sfx_play_nav(); break;
        case IN_CONFIRM: confirm_selection(); break;
        case IN_FAVORITE: toggle_favorite(m_selected); break;
        case IN_SETTINGS: open_config(m_selected); sfx_play_confirm(); break;
        default: break;
        }
    } else {
        const GameGroup &g = m_groups[(size_t)m_submenu];
        const int e = (int)g.entries.size();
        switch (in) {
        case IN_UP:   m_sub_sel = (m_sub_sel - 1 + e) % e; m_arrow_dir = +1; m_arrow_time = SDL_GetTicks(); sfx_play_nav(); break;
        case IN_DOWN: m_sub_sel = (m_sub_sel + 1) % e;     m_arrow_dir = -1; m_arrow_time = SDL_GetTicks(); sfx_play_nav(); break;
        case IN_CONFIRM: confirm_selection(); break;
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
            request_play(m_selected, idx);
        } else if (g.entries.size()==1) {
            request_play(m_selected, 0);
        } else {
            m_submenu = m_selected;
            int last = prefs_get_last_entry(g.key);
            m_sub_sel = (last >= 0 && last < (int)g.entries.size()) ? last : 0;
            sfx_play_confirm();
        }
    } else {
        if (!db_entry_unlocked(g, m_sub_sel)) { sfx_play_back(); return; }
        request_play(m_selected, m_sub_sel);
    }
}

bool Launcher::modal_open(void) const { return m_config_open || m_resume_open || controls_is_open(); }

/* The user picked a version: ask first if it can continue from where they
   left it, then start it (the shelf plays its launch animation first). */
void Launcher::request_play(int g, int j) {
    if (entry_status(m_groups[(size_t)g], j).resume > 0) {
        m_resume_open = true;
        m_resume_sel  = 0;
        m_resume_g    = g;
        m_resume_j    = j;
        sfx_play_confirm();
        return;
    }
    start_play(g, j, false);
}

void Launcher::start_play(int g, int j, bool resume) {
    if (in_3d()) {
        m_launch_g = g;
        m_launch_j = j;
        m_launch_resume = resume;
        m_shelf.launch(g, j);
        return;
    }
    m_pending = make_request(g, j);
    m_pending.resume = resume;
    remember(g, j);
    m_submenu = -1;
    sfx_play_enter_game();
}

void Launcher::resume_input(UiInput in) {
    switch (in) {
    case IN_UP: case IN_DOWN: case IN_LEFT: case IN_RIGHT:
        m_resume_sel ^= 1;
        sfx_play_nav();
        break;
    case IN_CONFIRM:
        m_resume_open = false;
        start_play(m_resume_g, m_resume_j, m_resume_sel == 0);
        break;
    case IN_BACK: case IN_SETTINGS:
        m_resume_open = false;
        sfx_play_back();
        break;
    default:
        break;
    }
}

void Launcher::add_capture(const std::string &key, int entry, const std::string &path) {
    for (auto *list : { &m_all, &m_groups })
        for (auto &g : *list) {
            if (g.key != key || entry < 0 || entry >= (int)g.entries.size()) continue;
            GameEntry &e = g.entries[(size_t)entry];
            const size_t pos = std::min((size_t)std::max(e.shots_explicit, 0), e.screenshots.size());
            e.screenshots.insert(e.screenshots.begin() + (std::ptrdiff_t)pos, path);   /* newest capture first */
            if (list == &m_groups) boxart_invalidate(g, entry);
        }
}

void Launcher::on_game_end(const std::string &key, int entry) {
    for (auto &g : m_all)
        if (g.key == key && entry >= 0 && entry < (int)g.entries.size())
            entry_status_forget(g.key, g.entries[(size_t)entry].stem);
    /* Orders that depend on play time change after every game. */
    const std::string mode = prefs_get_sort();
    if (mode == "recent" || mode == "played") rebuild_view(key);
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
        ui_text(PAD, WIN_H/2-16, 2.0f, tr("No ROMs found. Place roms + db.json and restart."),
                1.0f,1.0f,1.0f);
        toast_draw();
        ui_end();
        return;
    }

    if (m_use_3d) ensure_3d();
    if (!in_3d()) { draw_classic(); return; }

    if (resumed) m_shelf.on_resume();
    m_shelf.set_attract_allowed(prefs_get_attract() && !modal_open());
    m_shelf.set_right_stick(m_rs_x, m_rs_y);
    m_shelf.update((float)dt);
    ShelfAction act;
    while (m_shelf.poll_action(act)) {
        if (act.kind == ShelfAction::SETTINGS) {
            open_config(act.group);
            sfx_play_confirm();
        } else if (act.kind == ShelfAction::FAVORITE) {
            toggle_favorite(act.group);
        } else if (act.kind == ShelfAction::PLAY) {
            request_play(act.group, act.entry);
        } else if (act.kind == ShelfAction::LAUNCH) {
            m_pending = make_request(act.group, act.entry);
            m_pending.resume = m_launch_resume && act.group == m_launch_g && act.entry == m_launch_j;
            m_launch_resume = false;
            remember(act.group, act.entry);
            m_shelf.on_launched();
        }
    }

    perf_section_begin(PERF_RENDER);
    m_shelf.draw_scene();
    perf_section_end(PERF_RENDER);
    perf_section_begin(PERF_UI);
    ui_set_draw_bg(false);
    ui_begin();
    m_shelf.draw_overlay(m_style, m_app_name, modal_open());
    if (m_config_open) {
        draw_config_modal();
        if (!controls_is_open()) draw_config_hints();
    }
    if (m_resume_open) draw_resume_prompt();
    controls_draw(m_style);
    m_shelf.draw_fade();
    toast_draw();
    perf_section_end(PERF_UI);
    perf_draw(false);
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
    return tr("In-game: L3 = menu");
#else
    return style == STYLE_KEYBOARD ? tr("In-game: Esc = menu") : tr("In-game: L3 = menu");
#endif
}

void Launcher::draw_config_hints(void) {
    Hint h[4];
    int n = 0;
    if (m_clear_confirm) {
        h[n++] = {HB_CONFIRM, tr("Delete saves")};
        h[n++] = {HB_BACK, tr("Cancel")};
    } else {
        h[n++] = {HB_DPAD_V, tr("Navigate")};
        if (m_config_sel == ROW_CONTROLS)  h[n++] = {HB_CONFIRM, tr("Open")};
        else if (m_config_sel >= ROW_VIEW) h[n++] = {HB_DPAD_H, tr("Change")};
        else h[n++] = {HB_CONFIRM, m_config_sel == ROW_CLEAR ? tr("Delete saves") : tr("Apply")};
        h[n++] = {HB_BACK, tr("Close")};
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
        if (!controls_is_open()) draw_config_hints();
        controls_draw(m_style);
    } else if (m_resume_open) {
        draw_resume_prompt();
    } else {
        Hint h[5];
        int n = 0;
        char confirm_lbl[40];
        if (m_submenu < 0) {
            const GameGroup &g = m_groups[(size_t)m_selected];
            if (g.sequential) {
                int w = std::min(db_progress_get(g.key), (int)g.entries.size() - 1);
                snprintf(confirm_lbl, sizeof(confirm_lbl), tr("Play %s"), db_entry_title(g.entries[(size_t)w]).c_str());
            } else if (g.entries.size() > 1) {
                snprintf(confirm_lbl, sizeof(confirm_lbl), "Versions (%d)", (int)g.entries.size());
            } else {
                snprintf(confirm_lbl, sizeof(confirm_lbl), "Play");
            }
            h[n++] = {HB_DPAD_V, tr("Navigate")};
            h[n++] = {HB_CONFIRM, confirm_lbl};
        } else {
            const GameGroup &g = m_groups[(size_t)m_submenu];
            h[n++] = {HB_DPAD_V, tr("Navigate")};
            h[n++] = {HB_CONFIRM, db_entry_unlocked(g, m_sub_sel) ? tr("Play") : tr("Locked")};
            h[n++] = {HB_BACK, tr("Back")};
        }
        h[n++] = {HB_SETTINGS, tr("Settings")};
#ifndef __SWITCH__
        if (m_style == STYLE_KEYBOARD) h[n++] = {HB_FULLSCREEN, tr("Maximize")};
#endif
        uikit_hint_bar(h, n, m_style, ingame_hint(m_style));
    }
    toast_draw();
    perf_draw(false);

    ui_end();
}

/* "Continue where you left off?": the automatic state's picture on the left,
   Continue / Start game on the right. */
void Launcher::draw_resume_prompt(void) {
    const GameGroup &g = m_groups[(size_t)m_resume_g];
    const GameEntry &e = g.entries[(size_t)m_resume_j];
    const EntryStatus &st = entry_status(g, m_resume_j);
    const float w = 820.0f, h = 420.0f, x = (WIN_W - w) * 0.5f, y = (WIN_H - h) * 0.5f - 16.0f;
    ui_rect(0.0f, 0.0f, WIN_W, WIN_H, 0.0f, 0.0f, 0.0f, 0.55f);
    ui_round_rect(x - 3.0f, y - 3.0f, w + 6.0f, h + 6.0f, 14.0f, GOLD_R, GOLD_G, GOLD_B, 0.80f);
    ui_round_rect(x, y, w, h, 12.0f, PANEL_R, PANEL_G, PANEL_B, 0.97f);

    const char *title = tr("Continue where you left off?");
    ui_text_px(x + 36.0f, y + 26.0f, 26, title, GOLD_R, GOLD_G, GOLD_B, 1.0f);
    std::string sub = db_title(g);
    if (g.entries.size() > 1 || g.sequential) sub += "  -  " + db_entry_title(e);
    ui_text_px(x + 36.0f, y + 64.0f, 18, sub.c_str(), 0.86f, 0.88f, 0.84f, 1.0f);

    /* The moment it was left. */
    const float tx = x + 36.0f, ty = y + 104.0f, tw = 400.0f, th = 290.0f;
    ui_rect(tx - 2.0f, ty - 2.0f, tw + 4.0f, th + 4.0f, GOLD_R, GOLD_G, GOLD_B, 0.55f);
    ui_rect(tx, ty, tw, th, 0.0f, 0.0f, 0.0f, 1.0f);
    if (ui_image_size(st.resume_thumb.c_str(), nullptr, nullptr))
        ui_image_ex(tx, ty, tw, th, st.resume_thumb.c_str(), UI_IMG_FIT | UI_IMG_SMOOTH, 1, 1, 1, 1);

    char left[96];
    snprintf(left, sizeof(left), tr("Left %s"), time_ago(st.resume).c_str());
    const char *labels[2] = { tr("Continue"), tr("Start game") };
    const char *notes[2]  = { left, tr("Loads your in-game save") };
    const float bx = x + 470.0f, bw = w - 470.0f - 36.0f, bh = 96.0f;
    for (int i = 0; i < 2; i++) {
        const float by = y + 120.0f + (float)i * (bh + 22.0f);
        const bool sel = m_resume_sel == i;
        if (sel) ui_round_rect(bx, by, bw, bh, 10.0f, GOLD_R, GOLD_G, GOLD_B, 0.95f);
        else     ui_round_rect_outline(bx, by, bw, bh, 10.0f, 1.5f, GOLD_R, GOLD_G, GOLD_B, 0.65f);
        const float ink = sel ? 0.08f : 0.92f;
        ui_text_px(bx + 22.0f, by + 18.0f, 24, labels[i], ink, sel ? 0.07f : 0.90f, sel ? 0.03f : 0.84f, 1.0f);
        ui_text_px(bx + 22.0f, by + 56.0f, 16, notes[i], sel ? 0.20f : 0.62f, sel ? 0.16f : 0.66f,
                   sel ? 0.06f : 0.60f, 1.0f);
        m_resume_btn[i][0] = bx; m_resume_btn[i][1] = by; m_resume_btn[i][2] = bw; m_resume_btn[i][3] = bh;
    }

    Hint hints[3] = { {HB_DPAD_V, tr("Select")}, {HB_CONFIRM, tr("OK")}, {HB_BACK, tr("Back")} };
    uikit_hint_bar(hints, 3, m_style, nullptr);
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
            snprintf(buf, sizeof(buf), "%.24s", db_title(g).c_str());
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
        snprintf(buf,sizeof(buf),"%s%s", sel?"> ":"  ", db_entry_title(e).c_str());
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
        ui_text(x, y, 2.5f, db_title(g).c_str(), GOLD_R,GOLD_G,GOLD_B);
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

    /* Saves and play time. */
    {
        std::string line;
        double played = 0.0;
        long long last = 0;
        if (entry_hint >= 0 && entry_hint < (int)g.entries.size()) {
            const EntryStatus &st = entry_status(g, entry_hint);
            char buf[128];
            if (st.resume > 0)     snprintf(buf, sizeof(buf), tr("Can continue - left %s"), time_ago(st.resume).c_str());
            else if (st.has_save)  snprintf(buf, sizeof(buf), "%s", tr("Has a saved game"));
            else                   buf[0] = '\0';
            line = buf;
            played = st.seconds;
        } else {
            group_play(g, played, last);
        }
        if (played >= 60.0) {
            char buf[96];
            snprintf(buf, sizeof(buf), tr("%s played"), stats_format_duration(played).c_str());
            line += (line.empty() ? "" : "  -  ") + std::string(buf);
        }
        if (last > 0) line += (line.empty() ? "" : "  -  ") + time_ago(last);
        if (!line.empty()) {
            ui_text_px(x, y, 18, line.c_str(), 0.55f, 0.92f, 0.62f, 1.0f);
            y += 26;
        }
    }

    /* In the list, name the versions a game contains before it's opened. */
    if (entry_hint < 0 && !g.sequential && g.entries.size() > 1) {
        char head[64];
        snprintf(head, sizeof(head), tr("%d versions: "), (int)g.entries.size());
        std::string line = head;
        for (size_t i = 0; i < g.entries.size(); i++) {
            if (i) line += ", ";
            line += db_entry_title(g.entries[i]);
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

    draw_wrapped(db_description(g), 0.92f,0.92f,0.92f);

    /* Version-specific description (only when an entry is selected) */
    if (entry_hint >= 0 && entry_hint < (int)g.entries.size()) {
        const std::string &vdesc = db_version_desc(g.entries[entry_hint]);
        if (!vdesc.empty()) {
            y += 6;
            ui_rect(x,y,w,1, GOLD_R,GOLD_G,GOLD_B,0.30f);
            y += 10;
            ui_text(x, y, 1.2f, tr("This version:"), GOLD_DIM_R,GOLD_DIM_G,GOLD_DIM_B);
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
        ui_text(x,y,1.2f,tr("In-game: R3 = advance week (when complete)"), 0.65f,0.65f,0.65f);
#else
        ui_text(x,y,1.2f,tr("In-game: Tab = advance week (when complete)"), 0.65f,0.65f,0.65f);
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
    const float MW   = 540.0f;
    const float ROW  = 30.0f;
    const int   NOPT = CONFIG_ROWS - ROW_VIEW;
    const float MH   = 16.0f + 38.0f + 26.0f + 12.0f + 24.0f + NUM_SHADERS * ROW
                     + 12.0f + ROW + 14.0f + 24.0f + (float)NOPT * ROW + 18.0f;
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
    ui_text_px(tx, ty, 32, tr("SETTINGS"), GOLD_R, GOLD_G, GOLD_B, 1.0f);
    ty += 38.0f;
    {
        char title[48];
        snprintf(title, sizeof(title), "%.40s", db_title(g).c_str());
        ui_text_px(tx, ty, 18, title, 0.80f, 0.80f, 0.75f, 1.0f);
    }
    ty += 26.0f;

    /* Separator */
    ui_rect(tx, ty, MW - 40.0f, 1.0f, GOLD_R, GOLD_G, GOLD_B, 0.40f);
    ty += 12.0f;

    if (!m_clear_confirm) {
        /* Shader section label */
        ui_text_px(tx, ty, 15, tr("DISPLAY SHADER  (this game)"), GOLD_DIM_R, GOLD_DIM_G, GOLD_DIM_B, 1.0f);
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

            char row_buf[96];
            snprintf(row_buf, sizeof(row_buf), "%s %s",
                     (i == active_shader) ? "[x]" : "[ ]", tr(renderer_shader_name(i)));
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
        ui_text_px(tx + 4.0f, ty + 2.0f, 20, tr("Clear Save Data"), cr, 0.28f, 0.28f, 1.0f);
        ty += ROW;

        /* Options for all games */
        ty += 4.0f;
        ui_rect(tx, ty, MW - 40.0f, 1.0f, GOLD_R, GOLD_G, GOLD_B, 0.30f);
        ty += 10.0f;
        ui_text_px(tx, ty, 15, tr("ALL GAMES"), GOLD_DIM_R, GOLD_DIM_G, GOLD_DIM_B, 1.0f);
        ty += 24.0f;
        for (int row = ROW_VIEW; row < CONFIG_ROWS; row++) {
            const bool hov = m_config_sel == row;
            row_bg(ty, hov, false);
            const char *label = "";
            const char *opts[3] = { nullptr, nullptr, nullptr };
            int active = 0, nopts = 0;
            if (row == ROW_VIEW) {
                label = tr("View");
                opts[0] = tr("3D Shelf"); opts[1] = tr("Classic list"); nopts = 2;
                active = in_3d() ? 0 : 1;
            } else if (row == ROW_LANG) {
                label = tr("Language");
                opts[0] = tr("Auto"); opts[1] = "English"; opts[2] = "Español"; nopts = 3;
                for (int i = 0; i < 3; i++) if (prefs_get_language() == LANG_PREFS[i]) active = i;
            } else if (row == ROW_SORT) {
                label = tr("Order");
                int cur = 0;
                for (int i = 0; i < NUM_SORTS; i++) if (prefs_get_sort() == SORT_PREFS[i]) cur = i;
                opts[0] = tr(SORT_NAMES[cur]); nopts = 1;     /* ◀ value ▶ */
                active = 0;
            } else if (row == ROW_SHOW) {
                label = tr("Show");
                opts[0] = tr("All games"); opts[1] = tr("Favorites"); nopts = 2;
                active = prefs_get_show() == "favorites" ? 1 : 0;
            } else if (row == ROW_MUSIC) {
                label = tr("Menu music");
                opts[0] = tr("Off"); opts[1] = tr("On"); nopts = 2;
                active = prefs_get_music() ? 1 : 0;
                if (!sfx_music_loaded()) { opts[0] = tr("None (branding.json)"); nopts = 1; active = -1; }
            } else if (row == ROW_ATTRACT) {
                label = tr("Attract mode");
                opts[0] = tr("Off"); opts[1] = tr("On"); nopts = 2;
                active = prefs_get_attract() ? 1 : 0;
            } else if (row == ROW_PERF) {
                label = tr("Performance info");
                opts[0] = tr("Off"); opts[1] = tr("On"); nopts = 2;
                active = perf_enabled() ? 1 : 0;
            } else if (row == ROW_CONTROLS) {
                label = tr("Controls...");
            }
            float vr = hov ? GOLD_R : 0.85f, vg = hov ? GOLD_G : 0.85f, vb = hov ? GOLD_B : 0.80f;
            ui_text_px(tx + 4.0f, ty + 2.0f, 20, label, vr, vg, vb, 1.0f);
            float cx = tx + 230.0f;
            if (row == ROW_SORT) {                   /* many values: ◀ one ▶ */
                const float cy = ty + ROW * 0.5f - 3.0f;
                ui_triangle(cx, cy, cx + 8.0f, cy - 6.0f, cx + 8.0f, cy + 6.0f, vr, vg, vb, hov ? 1.0f : 0.5f);
                cx += 14.0f;
                cx += uikit_chip(cx, ty - 1.0f, opts[0], 14, true, 1.0f) + 6.0f;
                ui_triangle(cx + 8.0f, cy, cx, cy - 6.0f, cx, cy + 6.0f, vr, vg, vb, hov ? 1.0f : 0.5f);
            } else {
                for (int i = 0; i < nopts; i++)
                    cx += uikit_chip(cx, ty - 1.0f, opts[i], 14, i == active, i == active ? 1.0f : 0.6f) + 8.0f;
            }
            ty += ROW;
        }
    } else {
        /* Confirmation sub-step */
        ty += 60.0f;
        ui_text_px(tx, ty, 22, tr("Delete ALL saves for this game?"), 1.0f, 0.4f, 0.3f, 1.0f);
        ty += 32.0f;
        ui_text_px(tx, ty, 18, tr("This cannot be undone."), 0.80f, 0.55f, 0.50f, 1.0f);
        ty += 40.0f;
        float gx = tx;
        gx += uikit_glyph(gx, ty + 10.0f, HB_CONFIRM, m_style, 1.0f) + 8.0f;
        ui_text_px(gx, ty, 18, tr("Confirm"), GOLD_R, GOLD_G, GOLD_B, 1.0f);
        gx += (float)ui_text_width(18, tr("Confirm")) + 24.0f;
        gx += uikit_glyph(gx, ty + 10.0f, HB_BACK, m_style, 1.0f) + 8.0f;
        ui_text_px(gx, ty, 18, tr("Cancel"), GOLD_R, GOLD_G, GOLD_B, 1.0f);
    }
}
