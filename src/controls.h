#pragma once
/* The Controls screen: which controller button and keyboard key play each
   game button, and who is player 1-4. Opened from the pause menu and from
   Settings. Pick a cell and press the new button (L3 / Esc cancel). */
#include "uikit.h"
#include <SDL2/SDL.h>

void controls_open(void);
void controls_close(void);
bool controls_is_open(void);
/* Waiting for the new button / key: raw events go to controls_event(). */
bool controls_capturing(void);
bool controls_event(const SDL_Event &ev);     /* true = it used the event */
void controls_ui_input(UiInput in);
void controls_draw(PadStyle style);
