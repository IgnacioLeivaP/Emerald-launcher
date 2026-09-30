#pragma once
#include "db.h"
#include "shelf.h"
#include "uikit.h"
#include <vector>
#include <string>
#include <cstdint>

struct LaunchRequest {
    bool        valid             = false;
    std::string rom_path;
    std::string core_dll;
    std::string srm_path;
    std::string carry_srm_from;
    std::string group_key;
    std::string stem;                     /* save / state / screenshot file names */
    std::string title;                    /* game and version, for the pause menu */
    std::string version;
    bool        resume            = false; /* continue from the automatic state   */
    int         entry_idx         = 0;
    bool        is_sequential     = false;
    int         week_complete_mask = 0;   /* WRAM mask for "week done" detection */
    bool        has_next_week     = false;

    /* External launch (chainload NRO / spawn exe, then close launcher) */
    bool        external          = false;
    std::string exec_path;
    std::string exec_argv;
    bool        keep_open         = false; /* PC: keep launcher running (RetroArch) */
};

/* The launcher UI. Two views over the same catalog:
     - the 3D shelf (default, see shelf.h), and
     - the classic list (carousel + version submenu),
   switchable from the settings modal. Keyboard, controller and mouse input
   are translated into UiInput actions here. */
class Launcher {
public:
    void load(std::vector<GameGroup> groups);
    void set_app_name(const std::string &name) { m_app_name = name; }
    void handle_key(int key, bool down);
    void handle_button(int btn, bool down);
    /* Right analog stick (SDL axis 2/3), raw value: turns the focused box. */
    void handle_axis(int axis, int value);
    /* Mouse, in the logical 1280x720 space (button 1 = left, 3 = right). */
    void handle_mouse_button(float x, float y, int button, bool down);
    void handle_mouse_motion(float x, float y);
    void handle_mouse_wheel(int dy);
    void draw(void);

    /* Build 3D box art ahead of time (called while the splash is showing). */
    void prewarm(void);
    /* An in-launcher game is about to run: free the big 3D render targets. */
    void on_game_start(void);
    /* App is exiting: remember the selected game for next time. */
    void on_quit(void);

    /* Returns a filled LaunchRequest when the user confirms a game, then clears it. */
    LaunchRequest poll_launch(void);

    /* A screenshot was taken in-game: list it with that version. */
    void add_capture(const std::string &group_key, int entry, const std::string &path);
    /* A game just ended: its saves / play time / "continue" state changed. */
    void on_game_end(const std::string &group_key, int entry);

private:
    std::vector<GameGroup> m_groups;
    std::string m_app_name = "Emerald Launcher";
    int m_selected = 0;    /* selected group index (classic view) */
    int m_submenu  = -1;   /* -1 = carousel, >= 0 = open submenu for that group */
    int m_sub_sel  = 0;    /* selected entry inside submenu */
    LaunchRequest m_pending;

    /* View */
    bool  m_use_3d   = true;    /* preference */
    bool  m_3d_ok    = false;   /* 3D renderer initialized */
    bool  m_3d_tried = false;
    Shelf m_shelf;

    /* Input */
    PadStyle m_style     = STYLE_KEYBOARD;
    int      m_hold_btn  = -1;      /* d-pad button held → auto-repeat */
    uint32_t m_hold_next = 0;
    float    m_rs_x = 0.0f, m_rs_y = 0.0f;
    uint64_t m_last_frame = 0;

    /* Arrow animation: +1=up pressed, -1=down pressed, 0=none */
    int      m_arrow_dir  = 0;
    uint32_t m_arrow_time = 0;

    /* Screenshot strip scroll state */
    float    m_shot_scroll = 0.0f;
    uint32_t m_shot_last_t = 0;

    /* Config modal state */
    bool     m_config_open    = false;
    int      m_config_sel     = 0;    /* shader rows, then clear data, then view */
    int      m_config_group   = -1;   /* index into m_groups being configured */
    bool     m_clear_confirm  = false;

    /* "Continue where you left off?" (the version has an automatic state) */
    bool     m_resume_open  = false;
    int      m_resume_sel   = 0;          /* 0 = continue, 1 = start the game */
    int      m_resume_g = -1, m_resume_j = -1;
    float    m_resume_btn[2][4] = {};     /* button rects, for the mouse      */
    /* Choice for the launch that's in flight (the shelf animates first). */
    bool     m_launch_resume = false;
    int      m_launch_g = -1, m_launch_j = -1;

    bool in_3d(void) const { return m_use_3d && m_3d_ok; }
    void request_play(int group, int entry);
    void start_play(int group, int entry, bool resume);
    void resume_input(UiInput in);
    void draw_resume_prompt(void);
    bool modal_open(void) const { return m_config_open || m_resume_open; }
    bool ensure_3d(void);
    void set_view_3d(bool on);
    void dispatch(UiInput in);
    void classic_input(UiInput in);
    void config_input(UiInput in);
    LaunchRequest make_request(int group, int entry) const;
    void remember(int group, int entry);

    void draw_classic(void);
    void draw_carousel(void);
    void draw_submenu(void);
    void draw_detail(const GameGroup &g, int entry_hint);
    void draw_config_modal(void);
    void draw_config_hints(void);
    void confirm_selection(void);
    void open_config(int group);
    void close_config(void);
};
