/* Button mapping: defaults, one binding per button, input.json round trip
   and what the core reads for each player. */
#include "check.h"
#include "scratch.h"
#include "input.h"

namespace {

SDL_Event key_event(SDL_Scancode sc, bool down) {
    SDL_Event ev;
    SDL_memset(&ev, 0, sizeof(ev));
    ev.type = down ? SDL_KEYDOWN : SDL_KEYUP;
    ev.key.keysym.scancode = sc;
    return ev;
}

} // namespace

TEST(input_defaults_and_keyboard) {
    Scratch sc("input_defaults");
    input_init("input.json");                        /* missing: defaults */
    CHECK_EQ(input_binding(RB_A).key, (int)SDL_SCANCODE_X);
    CHECK_EQ(input_binding(RB_START).key, (int)SDL_SCANCODE_RETURN);
    CHECK(input_binding(RB_L2).pad.type == PadBind::AXIS_POS);
    CHECK_EQ(input_state(0, RB_A), 0);
    input_handle_event(key_event(SDL_SCANCODE_X, true));
    CHECK_EQ(input_state(0, RB_A), 1);
    CHECK_EQ(input_state(1, RB_A), 0);               /* the keyboard is player 1 only */
    CHECK(input_any_held());
    input_handle_event(key_event(SDL_SCANCODE_X, false));
    CHECK_EQ(input_state(0, RB_A), 0);
    CHECK(!input_any_held());
    CHECK_EQ(input_state(7, RB_A), 0);               /* out of range: nothing */
    CHECK_EQ(input_state(0, 99), 0);
}

TEST(input_rebind_and_save) {
    Scratch sc("input_rebind");
    input_init("input.json");
    input_set_key(RB_B, SDL_SCANCODE_X);            /* X was A's key: A loses it */
    CHECK_EQ(input_binding(RB_B).key, (int)SDL_SCANCODE_X);
    CHECK_EQ(input_binding(RB_A).key, 0);
    PadBind lb;
    lb.type = PadBind::BUTTON;
    lb.index = SDL_CONTROLLER_BUTTON_LEFTSHOULDER;
    input_set_pad(RB_A, lb);                         /* L's button moves to A */
    CHECK(input_binding(RB_L).pad.type == PadBind::NONE);
    CHECK(input_save());
    CHECK(Scratch::read("input.json").find("\"leftshoulder\"") != std::string::npos);

    input_reset_defaults();
    CHECK_EQ(input_binding(RB_A).key, (int)SDL_SCANCODE_X);
    input_init("input.json");                        /* back from the file */
    CHECK_EQ(input_binding(RB_B).key, (int)SDL_SCANCODE_X);
    CHECK_EQ(input_binding(RB_A).key, 0);
    CHECK(input_binding(RB_A).pad.type == PadBind::BUTTON);
    CHECK_EQ(input_binding(RB_A).pad.index, (int)SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
    input_reset_defaults();
    input_save();
}

TEST(input_labels) {
    PadBind lt;
    lt.type = PadBind::AXIS_POS;
    lt.index = SDL_CONTROLLER_AXIS_TRIGGERLEFT;
    PadBind start;
    start.type = PadBind::BUTTON;
    start.index = SDL_CONTROLLER_BUTTON_START;
#ifdef __SWITCH__
    CHECK_EQ(input_pad_label(lt), std::string("ZL"));
    CHECK_EQ(input_pad_label(start), std::string("+"));
#else
    CHECK_EQ(input_pad_label(lt), std::string("LT"));
    CHECK_EQ(input_pad_label(start), std::string("Start"));
#endif
    CHECK_EQ(input_pad_label(PadBind{}), std::string("-"));
    CHECK_EQ(input_key_label(SDL_SCANCODE_RETURN), std::string("Return"));
    CHECK_EQ(input_key_label(0), std::string("-"));
    CHECK_EQ(std::string(input_button_name(RB_A)), std::string("A"));
}
