#pragma once
/* In-game pause menu (L3 / Esc): resume, save / load state, screenshot,
   shader, controls, reset, next week (sequential games) and back to the
   launcher. The game is frozen while it's open.

   Also tracks the "week complete" flag of sequential games (Ancient Stone
   Tablets reads it from WRAM) and draws the corner hint that offers the
   next week (R3 / Tab). */
#include "uikit.h"
#include <cstdint>
#include <string>

enum PauseAction {
    PAUSE_NONE,
    PAUSE_RESUME,
    PAUSE_SAVE_STATE,
    PAUSE_LOAD_STATE,
    PAUSE_SCREENSHOT,
    PAUSE_SHADER,          /* pause_shader() has the new shader id */
    PAUSE_CONTROLS,
    PAUSE_RESET,
    PAUSE_NEXT_WEEK,
    PAUSE_QUIT
};

struct PauseInfo {
    std::string title;             /* game                                  */
    std::string version;           /* version or week name                  */
    std::string played;            /* e.g. "12 h 40 min played" (optional)   */
    bool        states = false;    /* the core can save states              */
    long long   state_time = 0;    /* when the slot was saved (0 = empty)   */
    std::string state_thumb;
    bool        can_reset = false;
    bool        controls = false;  /* show the Controls row                 */
    int         shader = 0;
    bool        next_week = false; /* the current week is done, more follow */
    int         week = 0;          /* current week, 1-based                 */
};

void        pause_open(const PauseInfo &info);
/* Straight to the "go to the next week?" question (R3 / Tab). */
void        pause_open_next_week(const PauseInfo &info);
void        pause_close(void);
bool        pause_is_open(void);
/* Refresh what the menu shows (e.g. after saving a state). */
void        pause_set_info(const PauseInfo &info);
PauseAction pause_input(UiInput in);
int         pause_shader(void);
void        pause_draw(PadStyle style);

void pause_week_reset(void);
void pause_week_update(const uint8_t *wram, int week_complete_mask, int entry_idx,
                       bool has_next_week);
bool pause_week_done(void);
/* Corner hint when a week is complete (call every frame while playing). */
void pause_draw_hud(PadStyle style);
