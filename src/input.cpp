#include "input.h"
#include "fsutil.h"
#include "i18n.h"
#include "toast.h"
#include <nlohmann/json.hpp>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace {

const int STICK_DEAD = 12000;
const int TRIGGER_ON = 16000;

struct Player {
    SDL_GameController *gc = nullptr;
    SDL_JoystickID      id = -1;
    bool                btn[SDL_CONTROLLER_BUTTON_MAX] = {};
    int16_t             axis[SDL_CONTROLLER_AXIS_MAX] = {};
};

Player      s_players[INPUT_PLAYERS];
bool        s_keys[SDL_NUM_SCANCODES] = {};
Binding     s_map[RB_COUNT];
std::string s_path;

/* Names used in input.json, in RetroButton order. */
const char *RB_KEYS[RB_COUNT] = { "b", "y", "select", "start", "up", "down", "left", "right",
                                   "a", "x", "l", "r", "l2", "r2" };
const char *RB_NAMES[RB_COUNT] = { "B", "Y", "Select", "Start", "Up", "Down", "Left", "Right",
                                    "A", "X", "L", "R", "L2", "R2" };

PadBind btn(int b) { PadBind p; p.type = PadBind::BUTTON; p.index = b; return p; }
PadBind axis_pos(int a) { PadBind p; p.type = PadBind::AXIS_POS; p.index = a; return p; }

void defaults(void) {
    for (auto &b : s_map) b = Binding{};
#ifdef __SWITCH__
    /* SDL numbers the Switch's buttons by position (Xbox layout): the
       right button, labelled A, is SDL's B. */
    s_map[RB_A].pad = btn(SDL_CONTROLLER_BUTTON_B);
    s_map[RB_B].pad = btn(SDL_CONTROLLER_BUTTON_A);
#else
    s_map[RB_A].pad = btn(SDL_CONTROLLER_BUTTON_A);
    s_map[RB_B].pad = btn(SDL_CONTROLLER_BUTTON_B);
#endif
    s_map[RB_X].pad      = btn(SDL_CONTROLLER_BUTTON_Y);
    s_map[RB_Y].pad      = btn(SDL_CONTROLLER_BUTTON_X);
    s_map[RB_L].pad      = btn(SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
    s_map[RB_R].pad      = btn(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
    s_map[RB_L2].pad     = axis_pos(SDL_CONTROLLER_AXIS_TRIGGERLEFT);
    s_map[RB_R2].pad     = axis_pos(SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
    s_map[RB_START].pad  = btn(SDL_CONTROLLER_BUTTON_START);
    s_map[RB_SELECT].pad = btn(SDL_CONTROLLER_BUTTON_BACK);
    s_map[RB_UP].pad     = btn(SDL_CONTROLLER_BUTTON_DPAD_UP);
    s_map[RB_DOWN].pad   = btn(SDL_CONTROLLER_BUTTON_DPAD_DOWN);
    s_map[RB_LEFT].pad   = btn(SDL_CONTROLLER_BUTTON_DPAD_LEFT);
    s_map[RB_RIGHT].pad  = btn(SDL_CONTROLLER_BUTTON_DPAD_RIGHT);

    s_map[RB_A].key      = SDL_SCANCODE_X;
    s_map[RB_B].key      = SDL_SCANCODE_Z;
    s_map[RB_X].key      = SDL_SCANCODE_S;
    s_map[RB_Y].key      = SDL_SCANCODE_A;
    s_map[RB_L].key      = SDL_SCANCODE_Q;
    s_map[RB_R].key      = SDL_SCANCODE_W;
    s_map[RB_START].key  = SDL_SCANCODE_RETURN;
    s_map[RB_SELECT].key = SDL_SCANCODE_RSHIFT;
    s_map[RB_UP].key     = SDL_SCANCODE_UP;
    s_map[RB_DOWN].key   = SDL_SCANCODE_DOWN;
    s_map[RB_LEFT].key   = SDL_SCANCODE_LEFT;
    s_map[RB_RIGHT].key  = SDL_SCANCODE_RIGHT;
}

std::string pad_to_string(PadBind b) {
    switch (b.type) {
    case PadBind::BUTTON: {
        const char *n = SDL_GameControllerGetStringForButton((SDL_GameControllerButton)b.index);
        return n ? n : "";
    }
    case PadBind::AXIS_POS: case PadBind::AXIS_NEG: {
        const char *n = SDL_GameControllerGetStringForAxis((SDL_GameControllerAxis)b.index);
        return n ? std::string(b.type == PadBind::AXIS_POS ? "+" : "-") + n : "";
    }
    default: return "";
    }
}

PadBind pad_from_string(const std::string &s) {
    PadBind p;
    if (s.empty()) return p;
    if (s[0] == '+' || s[0] == '-') {
        SDL_GameControllerAxis a = SDL_GameControllerGetAxisFromString(s.c_str() + 1);
        if (a != SDL_CONTROLLER_AXIS_INVALID) {
            p.type = s[0] == '+' ? PadBind::AXIS_POS : PadBind::AXIS_NEG;
            p.index = a;
        }
        return p;
    }
    SDL_GameControllerButton b = SDL_GameControllerGetButtonFromString(s.c_str());
    if (b != SDL_CONTROLLER_BUTTON_INVALID) { p.type = PadBind::BUTTON; p.index = b; }
    return p;
}

void load(void) {
    defaults();
    std::ifstream f(s_path);
    if (!f) return;
    try {
        auto j = nlohmann::json::parse(f);
        for (int i = 0; i < RB_COUNT; i++) {
            if (j.contains("pad") && j["pad"].contains(RB_KEYS[i]) && j["pad"][RB_KEYS[i]].is_string())
                s_map[i].pad = pad_from_string(j["pad"][RB_KEYS[i]].get<std::string>());
            if (j.contains("keys") && j["keys"].contains(RB_KEYS[i]) && j["keys"][RB_KEYS[i]].is_string()) {
                const std::string name = j["keys"][RB_KEYS[i]].get<std::string>();
                s_map[i].key = name.empty() ? 0 : (int)SDL_GetScancodeFromName(name.c_str());
            }
        }
    } catch (...) {
        fprintf(stderr, "input: couldn't read %s, using the default buttons\n", s_path.c_str());
        defaults();
    }
}

bool nintendo_labels(const Player *p) {
#ifdef __SWITCH__
    (void)p;
    return true;
#else
#if SDL_VERSION_ATLEAST(2, 0, 12)
    if (p && p->gc) {
        switch (SDL_GameControllerGetType(p->gc)) {
        case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_PRO: return true;
        default: break;
        }
    }
#endif
    return false;
#endif
}

Player *player_by_id(SDL_JoystickID id) {
    for (auto &p : s_players) if (p.gc && p.id == id) return &p;
    return nullptr;
}

void add_controller(int device_index) {
    if (!SDL_IsGameController(device_index)) return;
    SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(device_index);
    if (player_by_id(id)) return;                              /* already ours */
    for (int i = 0; i < INPUT_PLAYERS; i++) {
        Player &p = s_players[i];
        if (p.gc) continue;
        p = Player{};
        p.gc = SDL_GameControllerOpen(device_index);
        if (!p.gc) return;
        p.id = id;
        const char *name = SDL_GameControllerName(p.gc);
        fprintf(stderr, "input: player %d = %s\n", i + 1, name ? name : "?");
        if (i > 0) {                                          /* player 1 is expected */
            char buf[160];
            snprintf(buf, sizeof(buf), tr("Player %d: %s"), i + 1, name ? name : tr("Controller"));
            toast_show(buf);
        }
        return;
    }
    /* More than four controllers: still usable for the menus. */
    SDL_GameControllerOpen(device_index);
}

void remove_controller(SDL_JoystickID id) {
    for (int i = 0; i < INPUT_PLAYERS; i++) {
        Player &p = s_players[i];
        if (!p.gc || p.id != id) continue;
        SDL_GameControllerClose(p.gc);
        p = Player{};
        char buf[96];
        snprintf(buf, sizeof(buf), tr("Player %d disconnected"), i + 1);
        toast_show(buf);
        return;
    }
}

bool pad_active(const Player &p, PadBind b) {
    switch (b.type) {
    case PadBind::BUTTON:   return b.index >= 0 && b.index < SDL_CONTROLLER_BUTTON_MAX && p.btn[b.index];
    case PadBind::AXIS_POS: return b.index >= 0 && b.index < SDL_CONTROLLER_AXIS_MAX && p.axis[b.index] > TRIGGER_ON;
    case PadBind::AXIS_NEG: return b.index >= 0 && b.index < SDL_CONTROLLER_AXIS_MAX && p.axis[b.index] < -TRIGGER_ON;
    default:                return false;
    }
}

} // namespace

void input_init(const std::string &path) {
    s_path = path;
    load();
    for (int i = 0; i < SDL_NumJoysticks(); i++) add_controller(i);
}

void input_handle_event(const SDL_Event &ev) {
    switch (ev.type) {
    case SDL_KEYDOWN: case SDL_KEYUP:
        if (ev.key.keysym.scancode < SDL_NUM_SCANCODES)
            s_keys[ev.key.keysym.scancode] = ev.type == SDL_KEYDOWN;
        break;
    case SDL_CONTROLLERBUTTONDOWN: case SDL_CONTROLLERBUTTONUP:
        if (Player *p = player_by_id(ev.cbutton.which))
            if (ev.cbutton.button < SDL_CONTROLLER_BUTTON_MAX)
                p->btn[ev.cbutton.button] = ev.type == SDL_CONTROLLERBUTTONDOWN;
        break;
    case SDL_CONTROLLERAXISMOTION:
        if (Player *p = player_by_id(ev.caxis.which))
            if (ev.caxis.axis < SDL_CONTROLLER_AXIS_MAX) p->axis[ev.caxis.axis] = ev.caxis.value;
        break;
    case SDL_CONTROLLERDEVICEADDED:
        add_controller(ev.cdevice.which);
        break;
    case SDL_CONTROLLERDEVICEREMOVED:
        remove_controller(ev.cdevice.which);
        break;
    case SDL_WINDOWEVENT:
        if (ev.window.event == SDL_WINDOWEVENT_FOCUS_LOST)       /* key-ups may never come */
            memset(s_keys, 0, sizeof(s_keys));
        break;
    default:
        break;
    }
}

int16_t input_state(unsigned port, unsigned id) {
    if (port >= INPUT_PLAYERS || id >= RB_COUNT) return 0;
    const Binding &b = s_map[id];
    if (port == 0 && b.key > 0 && b.key < SDL_NUM_SCANCODES && s_keys[b.key]) return 1;
    const Player &p = s_players[port];
    if (!p.gc) return 0;
    if (pad_active(p, b.pad)) return 1;
    switch (id) {                                            /* left stick = d-pad */
    case RB_UP:    return p.axis[SDL_CONTROLLER_AXIS_LEFTY] < -STICK_DEAD;
    case RB_DOWN:  return p.axis[SDL_CONTROLLER_AXIS_LEFTY] >  STICK_DEAD;
    case RB_LEFT:  return p.axis[SDL_CONTROLLER_AXIS_LEFTX] < -STICK_DEAD;
    case RB_RIGHT: return p.axis[SDL_CONTROLLER_AXIS_LEFTX] >  STICK_DEAD;
    default:       return 0;
    }
}

bool input_any_held(void) {
    for (bool k : s_keys) if (k) return true;
    for (const auto &p : s_players) {
        if (!p.gc) continue;
        for (bool b : p.btn) if (b) return true;
        if (p.axis[SDL_CONTROLLER_AXIS_TRIGGERLEFT] > TRIGGER_ON ||
            p.axis[SDL_CONTROLLER_AXIS_TRIGGERRIGHT] > TRIGGER_ON) return true;
    }
    return false;
}

bool input_key_held(int sc) { return sc > 0 && sc < SDL_NUM_SCANCODES && s_keys[sc]; }

const Binding &input_binding(int rb) {
    static const Binding none;
    return rb >= 0 && rb < RB_COUNT ? s_map[rb] : none;
}

void input_set_pad(int rb, PadBind b) {
    if (rb < 0 || rb >= RB_COUNT) return;
    /* One button does one thing: take it away from whoever had it. */
    if (b.type != PadBind::NONE)
        for (auto &m : s_map)
            if (m.pad.type == b.type && m.pad.index == b.index) m.pad = PadBind{};
    s_map[rb].pad = b;
}

void input_set_key(int rb, int sc) {
    if (rb < 0 || rb >= RB_COUNT) return;
    if (sc > 0)
        for (auto &m : s_map) if (m.key == sc) m.key = 0;
    s_map[rb].key = sc;
}

void input_reset_defaults(void) { defaults(); }

bool input_save(void) {
    nlohmann::json j;
    j["pad"] = nlohmann::json::object();
    j["keys"] = nlohmann::json::object();
    for (int i = 0; i < RB_COUNT; i++) {
        j["pad"][RB_KEYS[i]] = pad_to_string(s_map[i].pad);
        j["keys"][RB_KEYS[i]] = s_map[i].key ? SDL_GetScancodeName((SDL_Scancode)s_map[i].key) : "";
    }
    if (s_path.empty()) return false;
    return fs_write_atomic(s_path, j.dump(2) + "\n");
}

std::string input_pad_label(PadBind b) {
    const bool nin = nintendo_labels(&s_players[0]);
    if (b.type == PadBind::AXIS_POS || b.type == PadBind::AXIS_NEG) {
        switch (b.index) {
        case SDL_CONTROLLER_AXIS_TRIGGERLEFT:  return nin ? "ZL" : "LT";
        case SDL_CONTROLLER_AXIS_TRIGGERRIGHT: return nin ? "ZR" : "RT";
        default: return pad_to_string(b);
        }
    }
    if (b.type != PadBind::BUTTON) return "-";
    switch (b.index) {
#ifdef __SWITCH__
    case SDL_CONTROLLER_BUTTON_A: return "B";          /* positional: bottom = B */
    case SDL_CONTROLLER_BUTTON_B: return "A";
    case SDL_CONTROLLER_BUTTON_X: return "Y";
    case SDL_CONTROLLER_BUTTON_Y: return "X";
#else
    case SDL_CONTROLLER_BUTTON_A: return "A";
    case SDL_CONTROLLER_BUTTON_B: return "B";
    case SDL_CONTROLLER_BUTTON_X: return "X";
    case SDL_CONTROLLER_BUTTON_Y: return "Y";
#endif
    case SDL_CONTROLLER_BUTTON_BACK:          return nin ? "-" : "Back";
    case SDL_CONTROLLER_BUTTON_START:         return nin ? "+" : "Start";
    case SDL_CONTROLLER_BUTTON_GUIDE:         return "Home";
    case SDL_CONTROLLER_BUTTON_LEFTSTICK:     return "L3";
    case SDL_CONTROLLER_BUTTON_RIGHTSTICK:    return "R3";
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:  return nin ? "L" : "LB";
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return nin ? "R" : "RB";
    case SDL_CONTROLLER_BUTTON_DPAD_UP:       return tr("D-pad Up");
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN:     return tr("D-pad Down");
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT:     return tr("D-pad Left");
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:    return tr("D-pad Right");
    default: return pad_to_string(b);
    }
}

std::string input_key_label(int sc) {
    if (sc <= 0) return "-";
    const char *n = SDL_GetScancodeName((SDL_Scancode)sc);
    return n && n[0] ? n : "?";
}

const char *input_button_name(int rb) {
    if (rb < 0 || rb >= RB_COUNT) return "";
    switch (rb) {
    case RB_UP: case RB_DOWN: case RB_LEFT: case RB_RIGHT: case RB_START: case RB_SELECT:
        return tr(RB_NAMES[rb]);
    default:
        return RB_NAMES[rb];
    }
}

int input_player_count(void) {
    int n = 0;
    for (const auto &p : s_players) if (p.gc) n++;
    return n;
}

std::string input_player_name(int i) {
    if (i < 0 || i >= INPUT_PLAYERS || !s_players[i].gc) return "";
    const char *n = SDL_GameControllerName(s_players[i].gc);
    return n ? n : tr("Controller");
}
