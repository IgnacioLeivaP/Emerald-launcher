#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    OVERLAY_HIDDEN = 0,
    OVERLAY_WEEK_NEXT,       /* R3/TAB: "Start next week? Yes / No" */
    OVERLAY_RETURN_LAUNCHER, /* L3/ESC: "Return to launcher? Yes / No" */
    OVERLAY_HELP             /* Shows help text */
} OverlayState;

typedef enum {
    OVERLAY_ACTION_NONE = 0,
    OVERLAY_ACTION_NEXT_WEEK,     /* confirmed start-next-week */
    OVERLAY_ACTION_GO_LAUNCHER    /* confirmed return-to-launcher */
} OverlayAction;

void          overlay_init(void);
/* Call every frame while in-game: reads wram to decide corner HUD */
void          overlay_update(const uint8_t *wram, int week_complete_mask, int current_entry_idx,
                              int total_entries, bool has_next_week);
/* Handle SDL key/button. Returns action if user confirmed something. */
OverlayAction overlay_key(int sdl_key, bool down);
OverlayAction overlay_button(int sdl_btn, bool down);
/* Draw overlay elements on top of the game frame */
void          overlay_draw(void);
/* Force-open a state (called from main when triggering from input) */
void          overlay_open(OverlayState state);
void          overlay_close(void);
OverlayState  overlay_get_state(void);
bool          overlay_week_done(void);  /* true when week_complete_mask bits are all set */

#ifdef __cplusplus
}
#endif
