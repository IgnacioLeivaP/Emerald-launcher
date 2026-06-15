#pragma once
#include "db.h"
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

class Launcher {
public:
    void load(std::vector<GameGroup> groups);
    void handle_key(int key, bool down);
    void handle_button(int btn, bool down);
    void draw(void);

    /* Returns a filled LaunchRequest when the user confirms a game, then clears it. */
    LaunchRequest poll_launch(void);

private:
    std::vector<GameGroup> m_groups;
    int m_selected = 0;    /* selected group index */
    int m_submenu  = -1;   /* -1 = carousel, >= 0 = open submenu for that group */
    int m_sub_sel  = 0;    /* selected entry inside submenu */
    LaunchRequest m_pending;

    /* Arrow animation: +1=up pressed, -1=down pressed, 0=none */
    int      m_arrow_dir  = 0;
    uint32_t m_arrow_time = 0;

    /* Screenshot strip scroll state */
    float    m_shot_scroll = 0.0f;
    uint32_t m_shot_last_t = 0;

    /* Config modal state */
    bool     m_config_open    = false;
    int      m_config_sel     = 0;    /* 0-4 shader options, 5 = clear data */
    int      m_config_group   = -1;   /* index into m_groups being configured */
    bool     m_clear_confirm  = false;

    void draw_carousel(void);
    void draw_submenu(void);
    void draw_detail(const GameGroup &g, int entry_hint);
    void draw_config_modal(void);
    void confirm_selection(void);
    void open_config(void);
    void close_config(void);
};
