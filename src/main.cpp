#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <cstdio>
#include <cstring>
#include <string>

#include "gl.h"
#include "core.h"
#include "renderer.h"
#include "ui.h"
#include "overlay.h"
#include "db.h"
#include "launcher.h"
#include "sfx.h"
#include "prefs.h"
#include "paths.h"
#include "branding.h"

#ifdef __SWITCH__
#  include <switch.h>   /* envSetNextLoad for NRO chainloading */
#endif

/* ── Configuration ──────────────────────────────────────────────────── */
static const int   WIN_W       = 1280;
static const int   WIN_H       = 720;
#ifdef __SWITCH__
static const char *DB_PATH       = "sdmc:/emerald/db.json";
static const char *ROMS_DIR      = "sdmc:/emerald/roms";
static const char *SAVES_DIR     = "sdmc:/emerald/saves";
static const char *CORES_DIR     = "sdmc:/emerald/cores";
static const char *BRANDING_PATH = "sdmc:/emerald/branding.json";
#else
static const char *DB_PATH       = "db.json";
static const char *ROMS_DIR      = "roms";
static const char *SAVES_DIR     = "saves";
static const char *CORES_DIR     = "cores";
static const char *BRANDING_PATH = "branding.json";
#endif

/* ── State ───────────────────────────────────────────────────────────── */
typedef enum { STATE_SPLASH, STATE_LAUNCHER, STATE_PLAYING } AppState;
static AppState s_state          = STATE_SPLASH;
static Uint32   s_splash_start   = 0;
static Uint32   s_launcher_start = 0;

static Branding      s_branding;
static SDL_Window   *s_window   = nullptr;
static SDL_GLContext s_glctx    = nullptr;
static SDL_AudioDeviceID s_audio_dev = 0;
static int s_audio_drop_bytes = 12000;  /* skip batches once the queue exceeds this */

static Launcher s_launcher;
static LaunchRequest s_current;   /* game being played */
static int  s_autosave_timer   = 0;
static bool s_week_notified    = false;  /* prevent repeat of youcangotonextweek */
static bool s_quit_requested   = false;  /* set when chainloading an external app */

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
    if (s_audio_dev) {
        /* Drop audio if the queue is backing up — keeps latency low. The cap is
           scaled to the core's sample rate (set on device open) so high-rate
           cores aren't starved into crackling. */
        if (SDL_GetQueuedAudioSize(s_audio_dev) < (Uint32)s_audio_drop_bytes)
            SDL_QueueAudio(s_audio_dev, data, (Uint32)(frames * 4));
    }
    return frames;
}

/* ── Input ───────────────────────────────────────────────────────────── */
static bool s_keys[512]   = {};
static bool s_btns[32]    = {};
static int  s_axes[8]     = {};   /* SDL_CONTROLLER_AXIS_* values */
static int  s_stick_dir   = 0;    /* launcher menu Y: -1 up / 1 down / 0 center */
static int  s_stick_dir_x = 0;    /* overlay menu X:  -1 left / 1 right / 0 center */
#define STICK_DEAD 12000

static void input_poll_cb(void) {}
static int16_t input_state_cb(unsigned port, unsigned dev, unsigned idx, unsigned id) {
    if (port != 0 || dev != RETRO_DEVICE_JOYPAD) return 0;
    switch (id) {
    /* D-pad OR left analog stick (axis 1 = LEFTY, axis 0 = LEFTX). */
    case RETRO_DEVICE_ID_JOYPAD_UP:     return (s_keys[SDL_SCANCODE_UP]    || s_btns[11] || s_axes[1] < -STICK_DEAD) ? 1:0;
    case RETRO_DEVICE_ID_JOYPAD_DOWN:   return (s_keys[SDL_SCANCODE_DOWN]  || s_btns[12] || s_axes[1] >  STICK_DEAD) ? 1:0;
    case RETRO_DEVICE_ID_JOYPAD_LEFT:   return (s_keys[SDL_SCANCODE_LEFT]  || s_btns[13] || s_axes[0] < -STICK_DEAD) ? 1:0;
    case RETRO_DEVICE_ID_JOYPAD_RIGHT:  return (s_keys[SDL_SCANCODE_RIGHT] || s_btns[14] || s_axes[0] >  STICK_DEAD) ? 1:0;
#ifdef __SWITCH__
    /* Nintendo A (right)=SDL btn 1, B (bottom)=SDL btn 0 — opposite of Xbox. */
    case RETRO_DEVICE_ID_JOYPAD_A:      return s_btns[1] ? 1:0;
    case RETRO_DEVICE_ID_JOYPAD_B:      return s_btns[0] ? 1:0;
#else
    case RETRO_DEVICE_ID_JOYPAD_A:      return (s_keys[SDL_SCANCODE_X]     || s_btns[0])  ? 1:0;
    case RETRO_DEVICE_ID_JOYPAD_B:      return (s_keys[SDL_SCANCODE_Z]     || s_btns[1])  ? 1:0;
#endif
    case RETRO_DEVICE_ID_JOYPAD_X:      return (s_keys[SDL_SCANCODE_S]     || s_btns[3])  ? 1:0;
    case RETRO_DEVICE_ID_JOYPAD_Y:      return (s_keys[SDL_SCANCODE_A]     || s_btns[2])  ? 1:0;
    case RETRO_DEVICE_ID_JOYPAD_L:      return (s_keys[SDL_SCANCODE_Q]     || s_btns[9])  ? 1:0;
    case RETRO_DEVICE_ID_JOYPAD_R:      return (s_keys[SDL_SCANCODE_W]     || s_btns[10]) ? 1:0;
    case RETRO_DEVICE_ID_JOYPAD_START:  return (s_keys[SDL_SCANCODE_RETURN]|| s_btns[6])  ? 1:0;
    case RETRO_DEVICE_ID_JOYPAD_SELECT: return (s_keys[SDL_SCANCODE_RSHIFT]|| s_btns[4])  ? 1:0;
    default: return 0;
    }
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
static bool copy_file(const std::string &src, const std::string &dst) {
    FILE *in = fopen(src.c_str(),"rb");
    if (!in) return false;
    FILE *out = fopen(dst.c_str(),"wb");
    if (!out) { fclose(in); return false; }
    char buf[4096]; size_t n;
    while ((n=fread(buf,1,sizeof(buf),in))>0) fwrite(buf,1,n,out);
    fclose(in); fclose(out);
    return true;
}

/* Carry save from previous week into current week's srm slot. */
static void carry_save_if_needed(const LaunchRequest &req) {
    if (req.carry_srm_from.empty()) return;
    FILE *test = fopen(req.srm_path.c_str(),"rb");
    if (test) { fclose(test); return; } /* already have a save for this week */
    if (!copy_file(req.carry_srm_from, req.srm_path))
        fprintf(stderr,"carry: failed to copy %s -> %s\n",
                req.carry_srm_from.c_str(), req.srm_path.c_str());
    else
        printf("carry: %s -> %s\n", req.carry_srm_from.c_str(), req.srm_path.c_str());
}

/* ── External launch (chainload NRO on Switch / spawn exe on PC) ──────── */
/* Launches an external program. SoH-style entries close the launcher; RetroArch
   chainload entries (keep_open) leave the PC launcher running in the background.
   On Switch we always exit (envSetNextLoad replaces the running NRO). */
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
    }
#else
    if (system(req.exec_path.c_str()) >= 0 && !req.keep_open)
        s_quit_requested = true;
#endif
}

/* ── Game launch/stop ────────────────────────────────────────────────── */
static bool launch_game(const LaunchRequest &req) {
    if (req.external) { launch_external(req); return true; }

    carry_save_if_needed(req);

    if (!core_load(req.core_dll.c_str())) {
        fprintf(stderr,"Cannot load core: %s\n", req.core_dll.c_str());
        return false;
    }
    core_set_video_cb(video_refresh_cb);
    core_set_audio_cb(audio_batch_cb);
    core_set_input_poll_cb(input_poll_cb);
    core_set_input_state_cb(input_state_cb);

    if (!core_load_game(req.rom_path.c_str(),
                        req.srm_path.empty() ? nullptr : req.srm_path.c_str())) {
        fprintf(stderr,"Cannot load game: %s\n", req.rom_path.c_str());
        core_unload();
        return false;
    }

    if (!renderer_init()) {
        fprintf(stderr,"renderer_init failed\n");
        core_unload();
        return false;
    }
    renderer_set_shader(prefs_get_shader(req.group_key));

    /* Hardware-rendering cores (N64 etc.): create the FBO and let the core
       initialise its GL resources via context_reset before the first frame. */
    if (core_hw_enabled()) core_hw_setup();

    /* Open audio at the core's native sample rate */
    CoreAVInfo av = core_get_avinfo();
    {
        int sr = (av.sample_rate > 0) ? (int)av.sample_rate : 44100;
        if (s_audio_dev) { SDL_CloseAudioDevice(s_audio_dev); s_audio_dev = 0; }
        sfx_suspend();   /* free the SFX device — some backends allow only one */
        SDL_AudioSpec want{}, have{};
        want.freq     = sr;
        want.format   = AUDIO_S16SYS;
        want.channels = 2;
#ifdef __SWITCH__
        want.samples  = 1024;   /* 512 underran on Switch → crackle */
#else
        want.samples  = 512;
#endif
        /* Allow ~125 ms of queued audio before dropping (scaled to the rate). */
        s_audio_drop_bytes = (sr / 8) * 4;
        if (s_audio_drop_bytes < 12000) s_audio_drop_bytes = 12000;
        s_audio_dev = SDL_OpenAudioDevice(nullptr,0,&want,&have,0);
        if (s_audio_dev) SDL_PauseAudioDevice(s_audio_dev,0);
        else fprintf(stderr,"audio: FAILED to open game device: %s\n", SDL_GetError());
        fprintf(stderr,"audio: opened %d Hz (core %.1f Hz) dev=%u have.freq=%d\n",
                sr, av.sample_rate, (unsigned)s_audio_dev, have.freq);
    }
    double fps = (av.fps > 0.0) ? av.fps : 60.0;
    s_frame_target_us = 1000000.0 / fps;
    s_last_frame_tick = SDL_GetPerformanceCounter();

    s_current        = req;
    s_state          = STATE_PLAYING;
    s_autosave_timer = 0;
    s_week_notified  = false;
    overlay_init();
    return true;
}

static void stop_game(void) {
    core_save_sram();
    core_unload();
    renderer_shutdown();
    if (s_audio_dev) {
        SDL_CloseAudioDevice(s_audio_dev);   /* release so the SFX device can reopen */
        s_audio_dev = 0;
    }
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

    /* Save progress so launcher picks up next week */
    db_progress_set(s_current.group_key, next_idx);

    /* Unload current core */
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
        req.entry_idx      = next_idx;
        req.is_sequential  = true;
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

/* ── Event handling ──────────────────────────────────────────────────── */
/* Returns false if app should quit */
static bool handle_event(const SDL_Event &ev) {
    if (ev.type == SDL_QUIT) return false;

    /* Fullscreen toggle (F11 / Alt+Enter) and window resize — all states.
       Ignore key auto-repeat so a held key doesn't toggle repeatedly. */
    if (ev.type == SDL_KEYDOWN && ev.key.repeat == 0) {
        SDL_Keycode k = ev.key.keysym.sym;
        if (k == SDLK_F11 ||
            (k == SDLK_RETURN && (ev.key.keysym.mod & KMOD_ALT))) {
            toggle_fullscreen();
            return true;
        }
    }
    if (ev.type == SDL_WINDOWEVENT &&
        (ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
         ev.window.event == SDL_WINDOWEVENT_RESIZED)) {
        recompute_viewport();
        return true;
    }

    /* Left analog stick — store axis values (used in-game) and drive menu
       navigation (edge-triggered up/down) in the launcher. */
    if (ev.type == SDL_CONTROLLERAXISMOTION) {
        if (ev.caxis.axis >= 0 && ev.caxis.axis < 8)
            s_axes[ev.caxis.axis] = ev.caxis.value;
        if (s_state == STATE_LAUNCHER && ev.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
            int nd = (ev.caxis.value < -STICK_DEAD) ? -1
                   : (ev.caxis.value >  STICK_DEAD) ?  1 : 0;
            if (nd != s_stick_dir) {
                s_stick_dir = nd;
                if      (nd == -1) s_launcher.handle_button(11, true);  /* up   */
                else if (nd ==  1) s_launcher.handle_button(12, true);  /* down */
            }
        }
        /* In-game overlay (Yes/No) navigates left/right with the stick. */
        if (s_state == STATE_PLAYING && overlay_get_state() != OVERLAY_HIDDEN
                && ev.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX) {
            int nd = (ev.caxis.value < -STICK_DEAD) ? -1
                   : (ev.caxis.value >  STICK_DEAD) ?  1 : 0;
            if (nd != s_stick_dir_x) {
                s_stick_dir_x = nd;
                if      (nd == -1) overlay_button(13, true);  /* dpad left  */
                else if (nd ==  1) overlay_button(14, true);  /* dpad right */
            }
        }
        return true;
    }

    if (s_state == STATE_SPLASH) return true;

    if (s_state == STATE_LAUNCHER) {
        if (ev.type == SDL_KEYDOWN || ev.type == SDL_KEYUP) {
            bool down = (ev.type == SDL_KEYDOWN);
            s_keys[ev.key.keysym.scancode] = down;
            s_launcher.handle_key((int)ev.key.keysym.sym, down);
        }
        if (ev.type == SDL_CONTROLLERBUTTONDOWN || ev.type == SDL_CONTROLLERBUTTONUP) {
            bool down = (ev.type == SDL_CONTROLLERBUTTONDOWN);
            s_btns[ev.cbutton.button] = down;
            s_launcher.handle_button(ev.cbutton.button, down);
        }
        return true;
    }

    /* STATE_PLAYING */
    OverlayAction action = OVERLAY_ACTION_NONE;

    if (ev.type == SDL_KEYDOWN || ev.type == SDL_KEYUP) {
        bool down = (ev.type == SDL_KEYDOWN);
        s_keys[ev.key.keysym.scancode] = down;

        if (down) {
            int sym = (int)ev.key.keysym.sym;
            if (overlay_get_state() != OVERLAY_HIDDEN) {
                action = overlay_key(sym, true);
            } else {
                /* TAB = R3: next week prompt — only when week is complete */
                if (sym == SDLK_TAB && s_current.is_sequential
                        && s_current.has_next_week && overlay_week_done()) {
                    overlay_open(OVERLAY_WEEK_NEXT);
                }
                /* ESCAPE = L3: return to launcher prompt */
                if (sym == SDLK_ESCAPE) {
                    sfx_play_open_menu();
                    overlay_open(OVERLAY_RETURN_LAUNCHER);
                }
            }
        }
    }
    if (ev.type == SDL_CONTROLLERBUTTONDOWN || ev.type == SDL_CONTROLLERBUTTONUP) {
        bool down = (ev.type == SDL_CONTROLLERBUTTONDOWN);
        s_btns[ev.cbutton.button] = down;

        if (down) {
            int btn = ev.cbutton.button;
            if (overlay_get_state() != OVERLAY_HIDDEN) {
                action = overlay_button(btn, true);
            } else {
                /* RIGHTSTICK (R3) = 8, LEFTSTICK (L3) = 7 */
                if (btn == 8 && s_current.is_sequential
                        && s_current.has_next_week && overlay_week_done()) {
                    overlay_open(OVERLAY_WEEK_NEXT);
                }
                if (btn == 7) {
                    sfx_play_open_menu();
                    overlay_open(OVERLAY_RETURN_LAUNCHER);
                }
            }
        }
    }

    if (action == OVERLAY_ACTION_NEXT_WEEK) {
        if (s_current.is_sequential)
            advance_week();
        else
            overlay_close();
    }
    if (action == OVERLAY_ACTION_GO_LAUNCHER) {
        sfx_play_back_to_launcher();
        stop_game();
    }

    if (ev.type == SDL_CONTROLLERDEVICEADDED)
        SDL_GameControllerOpen(ev.cdevice.which);

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

/* ── Main loop tick ──────────────────────────────────────────────────── */
static void tick_playing(void) {
    frame_wait();   /* throttle to core's target FPS */

    /* Pause emulation while any overlay dialog is open */
    if (overlay_get_state() == OVERLAY_HIDDEN) {
        core_run();
        s_autosave_timer++;
        if (s_autosave_timer >= 3600) {
            core_save_sram();
            s_autosave_timer = 0;
        }
        const uint8_t *wram = core_get_wram();
        overlay_update(wram, s_current.week_complete_mask,
                       s_current.entry_idx, 0, s_current.has_next_week);

        /* Play youcangotonextweek exactly once when completion is first detected */
        if (!s_week_notified && s_current.is_sequential
                && s_current.has_next_week && overlay_week_done()) {
            sfx_play_next_week();
            s_week_notified = true;
        }
    }

    renderer_draw();

    ui_begin();
    overlay_draw();
    ui_end();
}

/* ── Entry point ─────────────────────────────────────────────────────── */
int main(int argc, char *argv[]) {
    (void)argc; (void)argv;

    setvbuf(stderr, NULL, _IONBF, 0);  /* unbuffered: don't lose logs on crash */

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

#ifdef __SWITCH__
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
#ifdef _WIN32
    CreateDirectoryA(SAVES_DIR, nullptr);
#else
    { char cmd[256]; snprintf(cmd,sizeof(cmd),"mkdir -p %s",SAVES_DIR); system(cmd); }
#endif

    /* Load game database */
    auto groups = db_load(DB_PATH, ROMS_DIR, SAVES_DIR, CORES_DIR);
    s_launcher.load(std::move(groups));

    /* Open any connected gamepads */
    for (int i=0; i<SDL_NumJoysticks(); i++)
        if (SDL_IsGameController(i)) SDL_GameControllerOpen(i);

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

        if (s_quit_requested) running = false;  /* external app launched */
    }

    if (s_state == STATE_PLAYING) stop_game();
    if (s_audio_dev) SDL_CloseAudioDevice(s_audio_dev);
    sfx_shutdown();
    ui_shutdown();
    SDL_GL_DeleteContext(s_glctx);
    SDL_DestroyWindow(s_window);
    SDL_Quit();
    return 0;
}
