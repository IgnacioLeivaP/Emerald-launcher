#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <cstdio>
#include <cstring>
#include <string>

#include "gl.h"
#include "core.h"
#include "renderer.h"
#include "ui.h"
#include "audio.h"
#include "controls.h"
#include "input.h"
#include "perf.h"
#include "pause.h"
#include "savestate.h"
#include "stats.h"
#include "toast.h"
#include "i18n.h"
#include "db.h"
#include "launcher.h"
#include "sfx.h"
#include "prefs.h"
#include "paths.h"
#include "branding.h"
#include "fsutil.h"

#ifdef __SWITCH__
#  include <switch.h>   /* envSetNextLoad for NRO chainloading */
#elif defined(_WIN32)
#  include <direct.h>   /* _chdir */
#else
#  include <cerrno>
#  include <spawn.h>
#  include <sys/wait.h>
#  include <unistd.h>
#  include <vector>
extern char **environ;
#endif

/* ── Configuration ──────────────────────────────────────────────────── */
static const int   WIN_W       = 1280;
static const int   WIN_H       = 720;
static const char *DB_PATH       = DATA("db.json");
static const char *ROMS_DIR      = DATA("roms");
static const char *SAVES_DIR     = DATA("saves");
static const char *CORES_DIR     = DATA("cores");
static const char *BRANDING_PATH = DATA("branding.json");

/* ── State ───────────────────────────────────────────────────────────── */
typedef enum { STATE_SPLASH, STATE_LAUNCHER, STATE_PLAYING } AppState;
static AppState s_state          = STATE_SPLASH;
static Uint32   s_splash_start   = 0;
static Uint32   s_launcher_start = 0;

static Branding      s_branding;
static SDL_Window   *s_window   = nullptr;
static SDL_GLContext s_glctx    = nullptr;

static Launcher s_launcher;
static LaunchRequest s_current;   /* game being played */
static int  s_autosave_timer   = 0;
static bool s_week_notified    = false;  /* prevent repeat of youcangotonextweek */
static bool s_quit_requested   = false;  /* set when chainloading an external app */
static std::string s_pending_resume;     /* state to load after the first frame */
static PadStyle s_pad_style    = STYLE_KEYBOARD;   /* last device used in-game */

/* Present viewport (16:9 letterboxed into the real window) + maximize state */
static bool s_maximized        = false;
static int  s_vpx = 0, s_vpy = 0, s_vpw = WIN_W, s_vph = WIN_H;

/* Recompute the letterboxed 16:9 region for the current window size. */
static void recompute_viewport(void) {
    int dw = WIN_W, dh = WIN_H;
    if (s_window) SDL_GL_GetDrawableSize(s_window, &dw, &dh);
    if (dw <= 0 || dh <= 0) { dw = WIN_W; dh = WIN_H; }
    const float target = (float)WIN_W / (float)WIN_H;          /* 16:9 */
    if ((float)dw / (float)dh > target) {                      /* window wider */
        s_vph = dh; s_vpw = (int)((float)dh * target + 0.5f);
    } else {                                                   /* window taller */
        s_vpw = dw; s_vph = (int)((float)dw / target + 0.5f);
    }
    s_vpx = (dw - s_vpw) / 2;
    s_vpy = (dh - s_vph) / 2;
    renderer_set_screen_viewport(s_vpx, s_vpy, s_vpw, s_vph);
}

/* Window (mouse) coordinates → the logical 1280x720 space of the UI. */
static void window_to_logical(int wx, int wy, float *lx, float *ly) {
    int ww = WIN_W, wh = WIN_H, dw = WIN_W, dh = WIN_H;
    if (s_window) {
        SDL_GetWindowSize(s_window, &ww, &wh);
        SDL_GL_GetDrawableSize(s_window, &dw, &dh);
    }
    float px = (float)wx * (float)dw / (float)(ww > 0 ? ww : 1);   /* HiDPI */
    float py = (float)wy * (float)dh / (float)(wh > 0 ? wh : 1);
    *lx = (px - (float)s_vpx) * (float)WIN_W / (float)(s_vpw > 0 ? s_vpw : 1);
    *ly = (py - (float)s_vpy) * (float)WIN_H / (float)(s_vph > 0 ? s_vph : 1);
}

/* F11 = maximize / restore. True fullscreen (SDL_SetWindowFullscreen and
   borderless-cover) both oscillate against Windows' fullscreen optimizations on
   this setup, so we use plain maximize, which is reliable. */
static void toggle_fullscreen(void) {
    s_maximized = !s_maximized;
    if (s_maximized) SDL_MaximizeWindow(s_window);
    else             SDL_RestoreWindow(s_window);
    recompute_viewport();
}

/* ── Frame timing ────────────────────────────────────────────────────── */
static Uint64  s_perf_freq        = 0;
static Uint64  s_last_frame_tick  = 0;
static double  s_frame_target_us  = 16666.667; /* default: 60 fps */

/* ── Audio ───────────────────────────────────────────────────────────── */
static size_t audio_batch_cb(const int16_t *data, size_t frames) {
    audio_push(data, frames);    /* resampled with dynamic rate control (audio.cpp) */
    return frames;
}

/* ── Input ───────────────────────────────────────────────────────────── */
/* Players, bindings and the pad / keyboard state live in input.cpp. */
static int  s_stick_dir   = 0;    /* launcher menu Y: -1 up / 1 down / 0 center */
static int  s_stick_lx    = 0;    /* launcher menu X: -1 left / 1 right / 0 center */
static int  s_stick_dir_x = 0;    /* pause menu X:    -1 left / 1 right / 0 center */
static int  s_stick_dir_y = 0;    /* pause menu Y:    -1 up / 1 down / 0 center */
#define STICK_DEAD 12000

/* After the pause menu closes, the buttons that closed it are still down:
   keep them from reaching the game until everything is released. */
static bool   s_input_masked = false;
static Uint32 s_mask_tick = 0;

static void input_poll_cb(void) {
    /* (A key whose release got lost can't block the game for long.) */
    if (s_input_masked && (!input_any_held() || SDL_GetTicks() - s_mask_tick > 1000))
        s_input_masked = false;
}

static int16_t input_state_cb(unsigned port, unsigned dev, unsigned idx, unsigned id) {
    (void)idx;
    if (s_input_masked || dev != RETRO_DEVICE_JOYPAD) return 0;
    if (id == 256) {                               /* RETRO_DEVICE_ID_JOYPAD_MASK */
        int16_t mask = 0;
        for (unsigned b = 0; b < RB_COUNT; b++)
            if (input_state(port, b)) mask |= (int16_t)(1 << b);
        return mask;
    }
    return input_state(port, id);
}

/* ── Video ───────────────────────────────────────────────────────────── */
static void video_refresh_cb(const void *data, unsigned w, unsigned h, size_t pitch) {
    if (data == RETRO_HW_FRAME_BUFFER_VALID) {
        /* Core hardware-rendered into its FBO — draw that texture directly. */
        renderer_set_hw_frame(core_hw_texture(), w, h,
                              core_hw_max_w(), core_hw_max_h(), core_hw_bottom_left());
    } else if (data) {
        renderer_set_frame(data, w, h, pitch, core_get_pixel_format());
    }
    /* data == NULL → duplicated frame, nothing to update */
}

/* ── Save helpers ─────────────────────────────────────────────────────── */
/* Carry save from previous week into current week's srm slot. */
static void carry_save_if_needed(const LaunchRequest &req) {
    if (req.carry_srm_from.empty()) return;
    if (fs_exists(req.srm_path)) return;   /* already have a save for this week */
    if (!fs_copy(req.carry_srm_from, req.srm_path))
        fprintf(stderr,"carry: failed to copy %s -> %s\n",
                req.carry_srm_from.c_str(), req.srm_path.c_str());
    else
        printf("carry: %s -> %s\n", req.carry_srm_from.c_str(), req.srm_path.c_str());
}

/* ── External launch (chainload NRO on Switch / spawn exe on PC) ──────── */
#if !defined(__SWITCH__) && !defined(_WIN32)
/* Split an argv string the way a shell would for simple cases: spaces
   separate arguments, double quotes group them ("C:/My Games/rom.sfc"). */
static std::vector<std::string> split_args(const std::string &s) {
    std::vector<std::string> out;
    std::string cur;
    bool quoted = false, have = false;
    for (char c : s) {
        if (c == '"') { quoted = !quoted; have = true; continue; }
        if ((c == ' ' || c == '\t') && !quoted) {
            if (have) out.push_back(cur);
            cur.clear();
            have = false;
            continue;
        }
        cur += c;
        have = true;
    }
    if (have) out.push_back(cur);
    return out;
}

/* Collect finished child processes (RetroArch sessions launched with the
   launcher kept open) so they don't linger as zombies. */
static void reap_children(void) {
    while (waitpid(-1, nullptr, WNOHANG) > 0) {}
}
#endif

/* Launches an external program. SoH-style entries close the launcher; RetroArch
   chainload entries (keep_open) leave the PC launcher running in the background.
   On Switch we always exit (envSetNextLoad replaces the running NRO). If the
   program can't be started the launcher stays open. */
static void launch_external(const LaunchRequest &req) {
    if (req.exec_path.empty()) {
        fprintf(stderr, "external: no executable for this platform — not launching\n");
        return; /* keep launcher open; nothing to run here */
    }
    printf("external: launching %s %s\n", req.exec_path.c_str(), req.exec_argv.c_str());

#ifdef __SWITCH__
    std::string argv = req.exec_argv.empty() ? req.exec_path : req.exec_argv;
    envSetNextLoad(req.exec_path.c_str(), argv.c_str());
    s_quit_requested = true;   /* exit loop → hbloader chainloads the NRO */
#elif defined(_WIN32)
    STARTUPINFOA si{}; PROCESS_INFORMATION pi{};
    si.cb = sizeof(si);
    std::string cmd = "\"" + req.exec_path + "\"";
    if (!req.exec_argv.empty()) cmd += " " + req.exec_argv;
    if (CreateProcessA(NULL, cmd.data(), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        if (!req.keep_open) s_quit_requested = true;   /* close unless told to stay */
    } else {
        fprintf(stderr, "external: CreateProcess failed for %s\n", req.exec_path.c_str());
        char msg[600];
        snprintf(msg, sizeof(msg), tr("Couldn't start %s"), fs_stem(req.exec_path).c_str());
        toast_show(msg, true);
    }
#else
    /* Linux / other POSIX: spawn it with its arguments, without a shell and
       without waiting for it. posix_spawnp searches PATH ("retroarch"). */
    std::vector<std::string> args = split_args(req.exec_argv);
    std::vector<char *> argv;
    argv.push_back(const_cast<char *>(req.exec_path.c_str()));
    for (auto &a : args) argv.push_back(const_cast<char *>(a.c_str()));
    argv.push_back(nullptr);
    pid_t pid = 0;
    int rc = posix_spawnp(&pid, req.exec_path.c_str(), nullptr, nullptr, argv.data(), environ);
    if (rc == 0) {
        if (!req.keep_open) s_quit_requested = true;   /* close unless told to stay */
    } else {
        fprintf(stderr, "external: couldn't start %s: %s\n", req.exec_path.c_str(), strerror(rc));
        char msg[600];
        snprintf(msg, sizeof(msg), tr("Couldn't start %s"), fs_stem(req.exec_path).c_str());
        toast_show(msg, true);
    }
#endif
}

/* ── Game launch/stop ────────────────────────────────────────────────── */
static bool launch_game(const LaunchRequest &req) {
    if (req.external) { launch_external(req); return true; }

    carry_save_if_needed(req);
    s_launcher.on_game_start();   /* free the 3D shelf's render targets */

    if (!core_load(req.core_dll.c_str())) {
        fprintf(stderr,"Cannot load core: %s\n", req.core_dll.c_str());
        toast_show(tr("Couldn't load the core for this game"), true);
        return false;
    }
    core_set_video_cb(video_refresh_cb);
    core_set_audio_cb(audio_batch_cb);
    core_set_input_poll_cb(input_poll_cb);
    core_set_input_state_cb(input_state_cb);

    if (!core_load_game(req.rom_path.c_str(),
                        req.srm_path.empty() ? nullptr : req.srm_path.c_str())) {
        fprintf(stderr,"Cannot load game: %s\n", req.rom_path.c_str());
        toast_show(tr("Couldn't load the game"), true);
        core_unload();
        return false;
    }

    /* Two players on the standard ports (a second controller joins as P2). */
    core_set_port_device(0, RETRO_DEVICE_JOYPAD);
    core_set_port_device(1, RETRO_DEVICE_JOYPAD);

    if (!renderer_init()) {
        fprintf(stderr,"renderer_init failed\n");
        core_unload();
        return false;
    }
    renderer_set_shader(prefs_get_shader(req.group_key));

    /* Hardware-rendering cores (N64 etc.): create the FBO and let the core
       initialise its GL resources via context_reset before the first frame. */
    if (core_hw_enabled()) core_hw_setup();

    /* Game audio at the device's rate, resampled from the core's. */
    CoreAVInfo av = core_get_avinfo();
    sfx_suspend();   /* free the SFX device — some backends allow only one */
    audio_open(av.sample_rate);
    double fps = (av.fps > 0.0) ? av.fps : 60.0;
    s_frame_target_us = 1000000.0 / fps;
    s_last_frame_tick = SDL_GetPerformanceCounter();

    s_current        = req;
    s_state          = STATE_PLAYING;
    s_autosave_timer = 0;
    s_week_notified  = false;
    s_pending_resume = req.resume ? state_path(SAVES_DIR, req.stem, true) : std::string();
    pause_close();
    pause_week_reset();
    stats_begin(stats_key(req.group_key, req.stem));
    return true;
}

/* keep_resume: leave an automatic state so the game can continue from here. */
static void stop_game(bool keep_resume) {
    if (keep_resume && !s_current.stem.empty() && core_state_size() > 0) {
        if (!state_save(state_path(SAVES_DIR, s_current.stem, true)))
            fprintf(stderr, "resume: couldn't save the automatic state\n");
    }
    stats_end();
    s_launcher.on_game_end(s_current.group_key, s_current.entry_idx);
    pause_close();
    core_save_sram();
    core_unload();
    renderer_shutdown();
    audio_close();   /* release so the SFX device can reopen */
    sfx_resume();
    s_state = STATE_LAUNCHER;
    /* Sequential progress is only advanced via advance_week() (R3 confirm),
       never on a plain exit — the player might return to replay the same week. */
    s_current = LaunchRequest{};
}

/* Advance to next week inside AST without returning to launcher */
static void advance_week(void) {
    /* Save current week */
    core_save_sram();

    /* Build next-week request */
    /* Progress was already >= entry_idx, so next idx = entry_idx+1 */
    int next_idx = s_current.entry_idx + 1;

    /* Re-load db to get the next entry (simpler: clone from current + bump idx) */
    /* We stored enough in s_current to reconstruct; db.h gives us db_load
       but we don't want to re-read disk here. Instead, stop and re-launch. */

    /* Save progress so launcher picks up next week. Replaying an earlier week
       (picked from the week list) must not move progress backwards. */
    if (next_idx > db_progress_get(s_current.group_key))
        db_progress_set(s_current.group_key, next_idx);

    /* Unload current core */
    stats_end();
    core_save_sram();
    core_unload();
    renderer_shutdown();

    /* Re-load db and find the next entry */
    auto groups = db_load(DB_PATH, ROMS_DIR, SAVES_DIR, CORES_DIR);
    for (auto &g : groups) {
        if (g.key != s_current.group_key) continue;
        if (next_idx >= (int)g.entries.size()) {
            /* Last week complete — back to launcher */
            fprintf(stdout,"All weeks complete! Returning to launcher.\n");
            s_state = STATE_LAUNCHER;
            s_current = LaunchRequest{};
            return;
        }
        const GameEntry &e = g.entries[next_idx];
        LaunchRequest req;
        req.valid          = true;
        req.rom_path       = e.rom_path;
        req.core_dll       = e.core_dll;
        req.srm_path       = e.srm_path;
        req.carry_srm_from = e.carry_srm_from;
        req.group_key      = g.key;
        req.stem           = e.stem;
        req.title          = g.title;
        req.version        = e.title;
        req.entry_idx      = next_idx;
        req.is_sequential  = true;
        /* Keep watching for "week done" in the new week too, so the chain
           continues in-game (week 2 → 3 → 4) without a trip to the launcher. */
        req.week_complete_mask = e.week_complete_mask;
        req.has_next_week      = next_idx + 1 < (int)g.entries.size();
        if (!launch_game(req)) {
            fprintf(stderr,"Failed to launch week %d\n", next_idx+1);
            s_state = STATE_LAUNCHER;
        }
        return;
    }
    /* Group not found — back to launcher */
    s_state = STATE_LAUNCHER;
    s_current = LaunchRequest{};
}

/* ── Pause menu ──────────────────────────────────────────────────────── */
/* SDL numbers controller buttons by position (Xbox layout): the Switch's
   A (right) is SDL's B and its B (bottom) is SDL's A. */
#ifdef __SWITCH__
static const int PAD_CONFIRM = SDL_CONTROLLER_BUTTON_B, PAD_CANCEL = SDL_CONTROLLER_BUTTON_A;
static const PadStyle PAD_STYLE = STYLE_NINTENDO;
#else
static const int PAD_CONFIRM = SDL_CONTROLLER_BUTTON_A, PAD_CANCEL = SDL_CONTROLLER_BUTTON_B;
static const PadStyle PAD_STYLE = STYLE_XBOX;
#endif

static PauseInfo pause_info(void) {
    PauseInfo pi;
    pi.title   = s_current.title;
    pi.version = s_current.version;
    PlayStats ps = stats_get(stats_key(s_current.group_key, s_current.stem));
    if (ps.seconds >= 60.0) {
        char buf[96];
        snprintf(buf, sizeof(buf), tr("%s played"), stats_format_duration(ps.seconds).c_str());
        pi.played = buf;
    }
    pi.states = !s_current.stem.empty() && core_state_size() > 0;
    if (pi.states) {
        const std::string sp = state_path(SAVES_DIR, s_current.stem, false);
        pi.state_time  = state_time(sp);
        pi.state_thumb = state_thumb(sp);
    }
    pi.can_reset = core_can_reset();
    pi.perf      = perf_enabled();
    pi.controls  = true;
    pi.shader    = prefs_get_shader(s_current.group_key);
    pi.next_week = s_current.is_sequential && s_current.has_next_week && pause_week_done();
    pi.week      = s_current.entry_idx + 1;
    return pi;
}

static void open_pause_menu(bool next_week) {
    if (next_week) {
        /* R3 / Tab only means something once the week is complete. */
        if (!(s_current.is_sequential && s_current.has_next_week && pause_week_done())) return;
        sfx_play_open_menu();
        pause_open_next_week(pause_info());
        return;
    }
    sfx_play_open_menu();
    pause_open(pause_info());
}

static void take_screenshot(void) {
    std::string path = screenshot_save(captures_dir_for(s_current.rom_path),
                                       s_current.stem.empty() ? fs_stem(s_current.rom_path) : s_current.stem);
    if (path.empty()) {
        toast_show(tr("Couldn't save the screenshot"), true);
        return;
    }
    sfx_play_confirm();
    toast_show(tr("Screenshot saved"));
    s_launcher.add_capture(s_current.group_key, s_current.entry_idx, path);
}

static void pause_ui_input(UiInput in) {
    if (controls_is_open()) {                 /* the Controls screen is on top */
        controls_ui_input(in);
        return;
    }
    const PauseAction action = pause_input(in);
    if (!pause_is_open()) { s_input_masked = true; s_mask_tick = SDL_GetTicks(); }
    switch (action) {
    case PAUSE_SAVE_STATE: {
        bool ok = state_save(state_path(SAVES_DIR, s_current.stem, false));
        toast_show(ok ? tr("State saved") : tr("Couldn't save the state"), !ok);
        pause_set_info(pause_info());
        break;
    }
    case PAUSE_LOAD_STATE: {
        bool ok = state_load(state_path(SAVES_DIR, s_current.stem, false));
        toast_show(ok ? tr("State loaded") : tr("Couldn't load the state"), !ok);
        break;
    }
    case PAUSE_SCREENSHOT:
        take_screenshot();
        break;
    case PAUSE_SHADER:
        renderer_set_shader(pause_shader());
        prefs_set_shader(s_current.group_key, pause_shader());
        prefs_save();
        break;
    case PAUSE_RESET:
        core_reset();
        toast_show(tr("Game reset"));
        break;
    case PAUSE_CONTROLS:
        controls_open();
        break;
    case PAUSE_PERF:
        perf_set_enabled(!perf_enabled());
        prefs_set_perf_hud(perf_enabled());
        prefs_save();
        pause_set_info(pause_info());
        break;
    case PAUSE_NEXT_WEEK:
        if (s_current.is_sequential) advance_week();
        break;
    case PAUSE_QUIT:
        sfx_play_back_to_launcher();
        stop_game(true);
        break;
    default:
        break;
    }
}

/* ── Event handling ──────────────────────────────────────────────────── */
/* Returns false if app should quit */
static bool handle_event(const SDL_Event &ev) {
    if (ev.type == SDL_QUIT) return false;

    input_handle_event(ev);                   /* pad / key state, players */
    /* Waiting for a new button in the Controls screen: it gets everything. */
    if (controls_capturing() && controls_event(ev)) return true;

    /* Fullscreen toggle (F11 / Alt+Enter) and window resize — all states.
       Ignore key auto-repeat so a held key doesn't toggle repeatedly. */
    if (ev.type == SDL_KEYDOWN && ev.key.repeat == 0) {
        SDL_Keycode k = ev.key.keysym.sym;
        if (k == SDLK_F11 ||
            (k == SDLK_RETURN && (ev.key.keysym.mod & KMOD_ALT))) {
            toggle_fullscreen();
            return true;
        }
        if (k == SDLK_F3) {                        /* performance overlay */
            perf_set_enabled(!perf_enabled());
            prefs_set_perf_hud(perf_enabled());
            prefs_save();
            return true;
        }
    }
    if (ev.type == SDL_WINDOWEVENT &&
        (ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
         ev.window.event == SDL_WINDOWEVENT_RESIZED)) {
        recompute_viewport();
        return true;
    }

    /* Left analog stick: menu navigation (edge-triggered) in the launcher
       and the pause menu. In-game it's read through input.cpp. */
    if (ev.type == SDL_CONTROLLERAXISMOTION) {
        if (s_state == STATE_LAUNCHER) {
            /* Left stick acts as the d-pad (press + release, so holding it
               auto-repeats); the right stick turns the focused 3D box. */
            int nd = (ev.caxis.value < -STICK_DEAD) ? -1
                   : (ev.caxis.value >  STICK_DEAD) ?  1 : 0;
            const int up = SDL_CONTROLLER_BUTTON_DPAD_UP, down = SDL_CONTROLLER_BUTTON_DPAD_DOWN;
            const int left = SDL_CONTROLLER_BUTTON_DPAD_LEFT, right = SDL_CONTROLLER_BUTTON_DPAD_RIGHT;
            if (ev.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY && nd != s_stick_dir) {
                if (s_stick_dir) s_launcher.handle_button(s_stick_dir < 0 ? up : down, false);
                s_stick_dir = nd;
                if (nd) s_launcher.handle_button(nd < 0 ? up : down, true);
            }
            if (ev.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX && nd != s_stick_lx) {
                if (s_stick_lx) s_launcher.handle_button(s_stick_lx < 0 ? left : right, false);
                s_stick_lx = nd;
                if (nd) s_launcher.handle_button(nd < 0 ? left : right, true);
            }
            if (ev.caxis.axis == SDL_CONTROLLER_AXIS_RIGHTX ||
                ev.caxis.axis == SDL_CONTROLLER_AXIS_RIGHTY)
                s_launcher.handle_axis(ev.caxis.axis, ev.caxis.value);
        }
        /* The pause menu navigates with the stick too. */
        if (s_state == STATE_PLAYING && pause_is_open() &&
            (ev.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX || ev.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY)) {
            int nd = (ev.caxis.value < -STICK_DEAD) ? -1
                   : (ev.caxis.value >  STICK_DEAD) ?  1 : 0;
            int &cur = ev.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX ? s_stick_dir_x : s_stick_dir_y;
            if (nd != cur) {
                cur = nd;
                if (nd) {
                    s_pad_style = PAD_STYLE;
                    if (ev.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX) pause_ui_input(nd < 0 ? IN_LEFT : IN_RIGHT);
                    else                                             pause_ui_input(nd < 0 ? IN_UP : IN_DOWN);
                }
            }
        }
        return true;
    }


    if (s_state == STATE_SPLASH) return true;

    if (s_state == STATE_LAUNCHER) {
        if (ev.type == SDL_MOUSEBUTTONDOWN || ev.type == SDL_MOUSEBUTTONUP) {
            float lx, ly;
            window_to_logical(ev.button.x, ev.button.y, &lx, &ly);
            s_launcher.handle_mouse_button(lx, ly, ev.button.button,
                                           ev.type == SDL_MOUSEBUTTONDOWN);
        }
        if (ev.type == SDL_MOUSEMOTION) {
            float lx, ly;
            window_to_logical(ev.motion.x, ev.motion.y, &lx, &ly);
            s_launcher.handle_mouse_motion(lx, ly);
        }
        if (ev.type == SDL_MOUSEWHEEL)
            s_launcher.handle_mouse_wheel(ev.wheel.y);
        if (ev.type == SDL_KEYDOWN || ev.type == SDL_KEYUP)
            s_launcher.handle_key((int)ev.key.keysym.sym, ev.type == SDL_KEYDOWN);
        if (ev.type == SDL_CONTROLLERBUTTONDOWN || ev.type == SDL_CONTROLLERBUTTONUP)
            s_launcher.handle_button(ev.cbutton.button, ev.type == SDL_CONTROLLERBUTTONDOWN);
        return true;
    }

    /* STATE_PLAYING */
    if (ev.type == SDL_KEYDOWN || ev.type == SDL_KEYUP) {
        bool down = (ev.type == SDL_KEYDOWN);
        if (down) {
            s_pad_style = STYLE_KEYBOARD;
            const SDL_Keycode sym = ev.key.keysym.sym;
            if (pause_is_open()) {
                switch (sym) {
                case SDLK_UP:    pause_ui_input(IN_UP);    break;
                case SDLK_DOWN:  pause_ui_input(IN_DOWN);  break;
                case SDLK_LEFT:  pause_ui_input(IN_LEFT);  break;
                case SDLK_RIGHT: pause_ui_input(IN_RIGHT); break;
                case SDLK_RETURN: case SDLK_KP_ENTER: pause_ui_input(IN_CONFIRM); break;
                case SDLK_SPACE: pause_ui_input(controls_is_open() ? IN_INSPECT : IN_CONFIRM); break;
                case SDLK_ESCAPE: case SDLK_BACKSPACE: pause_ui_input(IN_BACK); break;
                case SDLK_DELETE: pause_ui_input(IN_INSPECT); break;
                default: break;
                }
            } else if (ev.key.repeat == 0) {
                if (sym == SDLK_ESCAPE) open_pause_menu(false);           /* = L3 */
                else if (sym == SDLK_TAB) open_pause_menu(true);          /* = R3 */
                else if (sym == SDLK_F12) take_screenshot();
            }
        }
    }
    if (ev.type == SDL_CONTROLLERBUTTONDOWN || ev.type == SDL_CONTROLLERBUTTONUP) {
        bool down = (ev.type == SDL_CONTROLLERBUTTONDOWN);
        if (down) {
            s_pad_style = PAD_STYLE;
            const int btn = ev.cbutton.button;
            if (pause_is_open()) {
                switch (btn) {
                case SDL_CONTROLLER_BUTTON_DPAD_UP:    pause_ui_input(IN_UP);    break;
                case SDL_CONTROLLER_BUTTON_DPAD_DOWN:  pause_ui_input(IN_DOWN);  break;
                case SDL_CONTROLLER_BUTTON_DPAD_LEFT:  pause_ui_input(IN_LEFT);  break;
                case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: pause_ui_input(IN_RIGHT); break;
                case SDL_CONTROLLER_BUTTON_LEFTSTICK:
                case SDL_CONTROLLER_BUTTON_START:      pause_ui_input(IN_BACK);  break;
                case SDL_CONTROLLER_BUTTON_Y:          pause_ui_input(IN_INSPECT); break;   /* top */
                default:
                    if (btn == PAD_CONFIRM)     pause_ui_input(IN_CONFIRM);
                    else if (btn == PAD_CANCEL) pause_ui_input(IN_BACK);
                    break;
                }
            } else {
                if (btn == SDL_CONTROLLER_BUTTON_LEFTSTICK)       open_pause_menu(false);
                else if (btn == SDL_CONTROLLER_BUTTON_RIGHTSTICK) open_pause_menu(true);
            }
        }
    }
    return true;
}

/* ── Frame wait: sleep until target frame time has elapsed ───────────── */
static void frame_wait(void) {
    /* SDL_Delay has ~1ms granularity; spin-wait for the last millisecond. */
    double budget = s_frame_target_us;
    for (;;) {
        Uint64 now     = SDL_GetPerformanceCounter();
        double elapsed = (double)(now - s_last_frame_tick) * 1000000.0 / (double)s_perf_freq;
        double remain  = budget - elapsed;
        if (remain <= 0.0) break;
        /* SDL_INIT_TIMER enables 1ms precision via timeBeginPeriod on Windows.
           Only spin-wait for the last 2ms to minimize CPU usage. */
        if (remain > 2000.0) SDL_Delay(1);
    }
    s_last_frame_tick = SDL_GetPerformanceCounter();
}

/* Menus (splash + launcher) run at 60 fps instead of spinning the GPU. */
static void menu_frame_wait(void) {
    static Uint64 last = 0;
    const double budget = 1000000.0 / 60.0;
    for (;;) {
        Uint64 now = SDL_GetPerformanceCounter();
        double elapsed = last ? (double)(now - last) * 1000000.0 / (double)s_perf_freq : budget;
        double remain = budget - elapsed;
        if (remain <= 0.0) break;
        if (remain > 2000.0) SDL_Delay(1);
    }
    last = SDL_GetPerformanceCounter();
}

/* ── Main loop tick ──────────────────────────────────────────────────── */
static void tick_playing(void) {
    frame_wait();   /* throttle to core's target FPS */

    /* The game is frozen while the pause menu is open. */
    if (!pause_is_open()) {
        perf_section_begin(PERF_CORE);
        core_run();
        perf_section_end(PERF_CORE);
        if (!s_pending_resume.empty()) {        /* "Continue": after the first frame */
            if (!state_load(s_pending_resume)) toast_show(tr("Couldn't load the state"), true);
            s_pending_resume.clear();
        }
        stats_add(s_frame_target_us / 1000000.0);
        s_autosave_timer++;
        if (s_autosave_timer >= 3600) {
            core_save_sram();
            s_autosave_timer = 0;
        }
        const uint8_t *wram = core_get_wram();
        pause_week_update(wram, s_current.week_complete_mask, s_current.entry_idx,
                          s_current.has_next_week);

        /* Play youcangotonextweek exactly once when completion is first detected */
        if (!s_week_notified && s_current.is_sequential
                && s_current.has_next_week && pause_week_done()) {
            sfx_play_next_week();
            s_week_notified = true;
        }
    }
    if (s_state != STATE_PLAYING) return;     /* a menu action left the game */

    perf_section_begin(PERF_RENDER);
    renderer_draw();
    perf_section_end(PERF_RENDER);

    ui_begin();
    pause_draw_hud(s_pad_style);
    pause_draw(s_pad_style);
    controls_draw(s_pad_style);
    toast_draw();
    perf_draw(true);
    ui_end();
}

/* ── Entry point ─────────────────────────────────────────────────────── */
/* PC: everything (db.json, assets, saves) is relative to the working
   directory. When started from elsewhere (a desktop shortcut, a file
   manager, a terminal in another folder), move to the executable's folder
   if that's where db.json lives. */
static void find_data_dir(void) {
#ifndef __SWITCH__
    if (fs_exists(DB_PATH)) return;
    char *base = SDL_GetBasePath();
    if (!base) return;
    std::string dir = base;
    SDL_free(base);
    if (!fs_exists(dir + DB_PATH)) return;
#  ifdef _WIN32
    int rc = _chdir(dir.c_str());
#  else
    int rc = chdir(dir.c_str());
#  endif
    if (rc == 0) fprintf(stderr, "data: using %s\n", dir.c_str());
#endif
}

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;

    setvbuf(stderr, NULL, _IONBF, 0);  /* unbuffered: don't lose logs on crash */
    find_data_dir();

#ifdef __SWITCH__
    if (R_FAILED(romfsInit()))
        fprintf(stderr, "romfsInit failed — bundled assets unavailable\n");
    /* No console on Switch — capture logs to the SD so we can debug. */
    freopen("sdmc:/emerald/emerald_log.txt", "w", stderr);
    setvbuf(stderr, NULL, _IONBF, 0);   /* freopen reset buffering — disable again */
    fprintf(stderr, "=== Emerald Launcher (Switch) log ===\n");
#endif

    if (SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO|SDL_INIT_GAMECONTROLLER|SDL_INIT_TIMER) < 0) {
        fprintf(stderr,"SDL_Init: %s\n",SDL_GetError());
        return 1;
    }

#if defined(__SWITCH__) || defined(EL_GLES)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#endif
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

#ifdef __SWITCH__
    const Uint32 s_win_flags = SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN;
#else
    const Uint32 s_win_flags = SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE;
#endif
    s_branding = branding_load(BRANDING_PATH);
    s_window = SDL_CreateWindow(s_branding.app_name.c_str(),
                                SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                WIN_W, WIN_H, s_win_flags);
    if (!s_window) {
        fprintf(stderr,"SDL_CreateWindow: %s\n",SDL_GetError());
        return 1;
    }
    if (!s_branding.icon_path.empty()) {
        SDL_Surface *icon = IMG_Load(s_branding.icon_path.c_str());
        if (icon) { SDL_SetWindowIcon(s_window, icon); SDL_FreeSurface(icon); }
        else fprintf(stderr, "branding: couldn't load window icon '%s': %s\n",
                     s_branding.icon_path.c_str(), IMG_GetError());
    }

    s_glctx = SDL_GL_CreateContext(s_window);
    if (!s_glctx) {

        fprintf(stderr,"SDL_GL_CreateContext: %s\n",SDL_GetError());
        return 1;
    }
    SDL_GL_SetSwapInterval(0); /* vsync off — frame limiter handles timing */
    s_perf_freq = SDL_GetPerformanceFrequency();

    if (!gl_load()) {
        fprintf(stderr,"gl_load failed\n");
        return 1;
    }

    recompute_viewport();   /* initial 16:9 present region */

    /* Enables alpha blending for the UI overlay */
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    ui_init(WIN_W, WIN_H);
    sfx_init();
    prefs_load();
    i18n_init(prefs_get_language());
    perf_set_enabled(prefs_get_perf_hud());
    stats_load(DATA("stats.json"));

    const std::string font_path = s_branding.font_path.empty()
        ? ASSET("alagard.ttf") : s_branding.font_path;
    if (!ui_load_font(font_path.c_str()))
        fprintf(stderr, "ui: %s not found — using fallback bitmap font\n", font_path.c_str());

    const std::string bg_path = s_branding.background_path.empty()
        ? ASSET("imgs/fabric-green.jpg") : s_branding.background_path;
    if (!ui_load_bg(bg_path.c_str()))
        fprintf(stderr, "ui: %s not found — no background image\n", bg_path.c_str());

    const std::string splash_path = s_branding.splash_path.empty()
        ? ASSET("imgs/BOOTLOGO.png") : s_branding.splash_path;

    /* Ensure saves directory exists (best-effort) */
    if (!fs_mkdirs(SAVES_DIR)) fprintf(stderr, "saves: couldn't create %s\n", SAVES_DIR);

    /* Load game database */
    auto groups = db_load(DB_PATH, ROMS_DIR, SAVES_DIR, CORES_DIR);
    s_launcher.set_app_name(s_branding.app_name);
    s_launcher.load(std::move(groups));

    /* Controllers (players 1-4) and the button mapping */
    input_init(DATA("input.json"));

    s_splash_start = SDL_GetTicks();
    sfx_play_boot();

    bool running = true;
    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (!handle_event(ev)) running = false;
        }

        /* Draw the 16:9 content into the letterboxed region of the window.
           glClear (called per-state below) still clears the whole window,
           so the bars stay black. */
        glViewport(s_vpx, s_vpy, s_vpw, s_vph);

        if (s_state == STATE_SPLASH) {
            Uint32 e = SDL_GetTicks() - s_splash_start;
            /* fade-in 0→500ms, hold 500→2500ms, fade-out 2500→3000ms */
            float dark = 0.0f;
            if (e < 500)        dark = 1.0f - (float)e / 500.0f;
            else if (e > 2500)  dark = (float)(e - 2500) / 500.0f;
            if (dark > 1.0f) dark = 1.0f;

            ui_set_draw_bg(true);
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            ui_begin();
            ui_image(WIN_W * 0.25f, WIN_H * 0.25f, WIN_W * 0.5f, WIN_H * 0.5f,
                     splash_path.c_str(), 1.0f);
            if (dark > 0.0f)
                ui_rect(0.0f, 0.0f, (float)WIN_W, (float)WIN_H, 0.0f, 0.0f, 0.0f, dark);
            ui_end();
            s_launcher.prewarm();   /* build the 3D box art while the logo shows */

            if (e >= 3000) {
                s_state          = STATE_LAUNCHER;
                s_launcher_start = SDL_GetTicks();
            }
        } else if (s_state == STATE_LAUNCHER) {
            ui_set_draw_bg(true);
            glClearColor(0.0f,0.0f,0.0f,1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            s_launcher.draw();

            /* fade-in from black when entering from splash */
            Uint32 fd = SDL_GetTicks() - s_launcher_start;
            if (fd < 500) {
                float a = 1.0f - (float)fd / 500.0f;
                ui_set_draw_bg(false);
                ui_begin();
                ui_rect(0.0f, 0.0f, (float)WIN_W, (float)WIN_H, 0.0f, 0.0f, 0.0f, a);
                ui_end();
                ui_set_draw_bg(true);
            }

            LaunchRequest req = s_launcher.poll_launch();
            if (req.valid) launch_game(req);
        } else {
            ui_set_draw_bg(false);
            tick_playing();
        }

        SDL_GL_SwapWindow(s_window);
        perf_frame_end();
        if (s_state != STATE_PLAYING) menu_frame_wait();

        if (s_quit_requested) running = false;  /* external app launched */
#if !defined(__SWITCH__) && !defined(_WIN32)
        reap_children();
#endif
    }

    s_launcher.on_quit();
    if (s_state == STATE_PLAYING) stop_game(true);
    audio_close();
    sfx_shutdown();
    ui_shutdown();
    SDL_GL_DeleteContext(s_glctx);
    SDL_DestroyWindow(s_window);
    SDL_Quit();
    return 0;
}
