#pragma once
/* Short notifications ("State saved", "Couldn't start ...") that slide in at
   the top of the screen for a few seconds, in the launcher and in-game. */
#include <string>

void toast_show(const std::string &text, bool error = false);
/* Draw the active toasts (between ui_begin and ui_end). */
void toast_draw(void);
bool toast_active(void);
