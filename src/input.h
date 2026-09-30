#pragma once
/* Game input: up to four players and remappable buttons.

   Every controller that connects takes the lowest free player slot and
   keeps it until it's unplugged; the keyboard always plays as player 1.
   Each libretro button has a controller binding and a keyboard key, edited
   in the Controls screen and kept in input.json next to db.json. The d-pad
   directions also follow the left stick. */
#include <SDL2/SDL.h>
#include <cstdint>
#include <string>

#define INPUT_PLAYERS 4

/* libretro joypad buttons, in RETRO_DEVICE_ID_JOYPAD_* order. */
enum RetroButton {
    RB_B, RB_Y, RB_SELECT, RB_START, RB_UP, RB_DOWN, RB_LEFT, RB_RIGHT,
    RB_A, RB_X, RB_L, RB_R, RB_L2, RB_R2, RB_COUNT
};

/* A controller input: a button, or one direction of an axis (triggers). */
struct PadBind {
    enum Type { NONE, BUTTON, AXIS_POS, AXIS_NEG } type = NONE;
    int index = 0;             /* SDL_GameControllerButton / SDL_GameControllerAxis */
};

struct Binding {
    PadBind pad;
    int     key = 0;           /* SDL_Scancode, 0 = none */
};

void input_init(const std::string &path);   /* load input.json, open pads */
/* Track keys, buttons, axes and controllers coming and going. */
void input_handle_event(const SDL_Event &ev);
int16_t input_state(unsigned port, unsigned id);   /* retro_input_state (joypad) */
bool input_any_held(void);
bool input_key_held(int scancode);

const Binding &input_binding(int rb);
void input_set_pad(int rb, PadBind b);
void input_set_key(int rb, int scancode);
void input_reset_defaults(void);
bool input_save(void);

/* Labels for the Controls screen ("A", "ZL", "Right Shift"...). */
std::string input_pad_label(PadBind b);
std::string input_key_label(int scancode);
const char *input_button_name(int rb);      /* "A", "Start", "Up"... */

/* Players: the controller in each slot (empty name = none). */
int         input_player_count(void);
std::string input_player_name(int player);
