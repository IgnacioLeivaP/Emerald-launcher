#include "toast.h"
#include "ui.h"
#include "uikit.h"
#include <SDL2/SDL.h>
#include <deque>

namespace {

struct Toast {
    std::string text;
    bool        error;
    Uint32      start;
};

const Uint32 LIFE_MS = 2600, FADE_MS = 220;
const int    MAX_TOASTS = 3;
std::deque<Toast> s_toasts;

float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

} // namespace

void toast_show(const std::string &text, bool error) {
    if (text.empty()) return;
    if (!s_toasts.empty() && s_toasts.back().text == text) {   /* repeated: restart it */
        s_toasts.back().start = SDL_GetTicks();
        return;
    }
    s_toasts.push_back(Toast{text, error, SDL_GetTicks()});
    while ((int)s_toasts.size() > MAX_TOASTS) s_toasts.pop_front();
}

bool toast_active(void) {
    const Uint32 now = SDL_GetTicks();
    while (!s_toasts.empty() && now - s_toasts.front().start > LIFE_MS) s_toasts.pop_front();
    return !s_toasts.empty();
}

void toast_draw(void) {
    if (!toast_active()) return;
    const Uint32 now = SDL_GetTicks();
    const int px = 18;
    float y = 18.0f;
    for (const Toast &t : s_toasts) {
        const float age = (float)(now - t.start);
        const float in  = clamp01(age / (float)FADE_MS);
        const float out = clamp01(((float)LIFE_MS - age) / (float)FADE_MS);
        const float a = in * out;
        const float slide = (1.0f - in) * -16.0f;
        const float tw = (float)ui_text_width(px, t.text.c_str());
        const float w = tw + 56.0f, h = 40.0f;
        const float x = (UI_W - w) * 0.5f, yy = y + slide;
        const float br = t.error ? 0.90f : GOLD_R, bg = t.error ? 0.36f : GOLD_G, bb = t.error ? 0.28f : GOLD_B;
        ui_round_rect(x - 2.0f, yy - 2.0f, w + 4.0f, h + 4.0f, 12.0f, br, bg, bb, 0.85f * a);
        ui_round_rect(x, yy, w, h, 10.0f, PANEL_R, PANEL_G, PANEL_B, 0.96f * a);
        /* A small gem (or a warning dot) before the text. */
        const float cx = x + 22.0f, cy = yy + h * 0.5f;
        ui_triangle(cx, cy - 6.0f, cx - 6.0f, cy, cx + 6.0f, cy, br, bg, bb, a);
        ui_triangle(cx - 6.0f, cy, cx, cy + 6.0f, cx + 6.0f, cy, br, bg, bb, a);
        ui_text_px(x + 38.0f, cy - (float)ui_text_line_height(px) * 0.5f, px, t.text.c_str(),
                   0.95f, 0.94f, 0.88f, a);
        y += (h + 10.0f) * a;
    }
}
