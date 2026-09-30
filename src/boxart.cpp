#include "boxart.h"
#include "i18n.h"
#include "boxshape.h"
#include "scene3d.h"
#include "ui.h"
#include <SDL.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <unordered_map>
#include <vector>

/* Texture budget (an atlas is ≈ 1.2 MB with mipmaps, a back ≈ 2.5 MB). */
#ifdef __SWITCH__
static const int MAX_ATLASES = 32;
#else
static const int MAX_ATLASES = 64;
#endif
static const int MAX_BACKS = 3;

namespace {

/* ── Palettes ──────────────────────────────────────────────────────────── */
struct Palette {
    float banner[3];   /* platform color: banner, spine, top */
    float accent[3];   /* stripes and tabs                   */
    float text[3];     /* text on the banner                 */
    float dark[3];     /* base behind the art                */
    const char *tag;   /* short platform name                */
};

/* Indexed by BoxFamily. */
const Palette PALETTES[FAM_COUNT] = {
    /* DEFAULT */ {{0.03f, 0.22f, 0.14f}, {0.95f, 0.78f, 0.15f}, {0.95f, 0.80f, 0.25f}, {0.02f, 0.08f, 0.05f}, nullptr},
    /* NES     */ {{0.06f, 0.06f, 0.07f}, {0.80f, 0.10f, 0.16f}, {0.96f, 0.96f, 0.96f}, {0.04f, 0.04f, 0.05f}, "NES"},
    /* SNES    */ {{0.79f, 0.78f, 0.83f}, {0.42f, 0.30f, 0.66f}, {0.22f, 0.16f, 0.36f}, {0.10f, 0.09f, 0.15f}, "SNES"},
    /* N64     */ {{0.04f, 0.04f, 0.04f}, {0.13f, 0.56f, 0.26f}, {1.00f, 1.00f, 1.00f}, {0.04f, 0.05f, 0.04f}, "N64"},
    /* GB      */ {{0.36f, 0.42f, 0.22f}, {0.72f, 0.82f, 0.36f}, {0.97f, 0.98f, 0.90f}, {0.07f, 0.09f, 0.05f}, "GB"},
    /* GBC     */ {{0.30f, 0.18f, 0.52f}, {0.97f, 0.77f, 0.12f}, {1.00f, 1.00f, 1.00f}, {0.08f, 0.05f, 0.14f}, "GBC"},
    /* GBA     */ {{0.16f, 0.20f, 0.46f}, {0.56f, 0.66f, 1.00f}, {1.00f, 1.00f, 1.00f}, {0.05f, 0.06f, 0.14f}, "GBA"},
    /* CD-i    */ {{0.12f, 0.24f, 0.46f}, {0.86f, 0.86f, 0.92f}, {1.00f, 1.00f, 1.00f}, {0.04f, 0.07f, 0.13f}, "CD-i"},
    /* PC      */ {{0.02f, 0.24f, 0.16f}, {0.95f, 0.78f, 0.15f}, {0.95f, 0.80f, 0.25f}, {0.02f, 0.08f, 0.05f}, "PC"},
};

const float GOLD[3]    = {0.95f, 0.78f, 0.15f};
const float GOLD_LT[3] = {1.00f, 0.90f, 0.52f};
const float INK[3]     = {0.20f, 0.12f, 0.03f};
const float PAPER[3]   = {0.93f, 0.92f, 0.88f};
const float BOARD[3]   = {0.86f, 0.84f, 0.78f};   /* bare cardboard under worn ink */
const float PLASTIC[3] = {0.035f, 0.035f, 0.04f};
const float WHITE[3]   = {1.0f, 1.0f, 1.0f};
/* Letters of the N64-style platform label cycle through these. */
const float N64_COLORS[4][3] = {{0.88f, 0.18f, 0.16f}, {0.16f, 0.64f, 0.30f},
                                {0.20f, 0.40f, 0.90f}, {0.97f, 0.77f, 0.12f}};

/* ── Small helpers ─────────────────────────────────────────────────────── */
unsigned rng_next(unsigned &s) { s = s * 1664525u + 1013904223u; return s >> 8; }

unsigned hash_str(const std::string &s) {
    unsigned h = 2166136261u;
    for (unsigned char c : s) { h ^= c; h *= 16777619u; }
    return h;
}

inline unsigned hash3(int x, int y, unsigned seed) {
    unsigned h = (unsigned)x * 374761393u + (unsigned)y * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}
inline float hashf(int x, int y, unsigned seed) { return (float)(hash3(x, y, seed) & 0xFFFFFFu) / 16777216.0f; }

/* Smooth value noise in [0,1). */
float vnoise(float x, float y, unsigned seed) {
    int xi = (int)floorf(x), yi = (int)floorf(y);
    float fx = x - (float)xi, fy = y - (float)yi;
    fx = fx * fx * (3.0f - 2.0f * fx);
    fy = fy * fy * (3.0f - 2.0f * fy);
    float a = hashf(xi, yi, seed), b = hashf(xi + 1, yi, seed);
    float c = hashf(xi, yi + 1, seed), d = hashf(xi + 1, yi + 1, seed);
    return (a + (b - a) * fx) + ((c + (d - c) * fx) - (a + (b - a) * fx)) * fy;
}

void mix3(float out[3], const float a[3], const float b[3], float t) {
    for (int k = 0; k < 3; k++) out[k] = a[k] + (b[k] - a[k]) * t;
}

float lh(int px) { return (float)ui_text_line_height(px); }

/* Largest font size in [lo, hi] for which `s` fits in max_w. */
int fit_px(const char *s, float max_w, int hi, int lo) {
    for (int px = hi; px > lo; px -= 2)
        if ((float)ui_text_width(px, s) <= max_w) return px;
    return lo;
}

void text_centered(float cx, float y, int px, const char *s, const float c[3], float a) {
    ui_text_px(cx - (float)ui_text_width(px, s) * 0.5f, y, px, s, c[0], c[1], c[2], a);
}

/* Text in a band, vertically centered. */
void text_in_band(float x, float y, float h, int px, const char *s, const float c[3], float a) {
    ui_text_px(x, y + (h - lh(px)) * 0.5f + 1.0f, px, s, c[0], c[1], c[2], a);
}

/* Emerald-cut gem (the launcher's little "seal of quality"). */
void gem(float cx, float cy, float r) {
    float ox[8], oy[8], ix[8], iy[8];
    for (int k = 0; k < 8; k++) {
        float a = (22.5f + 45.0f * (float)k) * 3.14159265f / 180.0f;
        ox[k] = cx + cosf(a) * r;          oy[k] = cy + sinf(a) * r * 1.15f;
        ix[k] = cx + cosf(a) * r * 0.58f;  iy[k] = cy + sinf(a) * r * 0.58f * 1.15f;
    }
    for (int k = 0; k < 8; k++) {
        int j = (k + 1) % 8;
        float shade = 0.55f + 0.45f * (0.5f + 0.5f * sinf((float)k * 0.785f + 2.2f));
        ui_triangle(ox[k], oy[k], ox[j], oy[j], ix[j], iy[j], 0.02f * shade, 0.45f * shade, 0.22f * shade, 1.0f);
        ui_triangle(ox[k], oy[k], ix[j], iy[j], ix[k], iy[k], 0.02f * shade, 0.45f * shade, 0.22f * shade, 1.0f);
    }
    for (int k = 0; k < 8; k++) {
        int j = (k + 1) % 8;
        ui_triangle(cx, cy, ix[k], iy[k], ix[j], iy[j], 0.10f, 0.72f, 0.40f, 1.0f);
    }
    ui_triangle(ix[4], iy[4], ix[5], iy[5], cx, cy, 1.0f, 1.0f, 1.0f, 0.45f);
    ui_triangle(ix[5], iy[5], ix[6], iy[6], cx, cy, 1.0f, 1.0f, 1.0f, 0.25f);
}

/* Gem + "EMERALD": the seal in a box corner. */
void seal(float x, float cy, float r, const float text[3]) {
    gem(x + r, cy, r);
    int px = std::max(10, (int)(r * 1.5f));
    ui_text_px(x + 2.0f * r + 5.0f, cy - lh(px) * 0.5f, px, "EMERALD", text[0], text[1], text[2], 0.9f);
}

/* Soft radial glow built from stacked translucent discs. */
void glow(float cx, float cy, float r, const float c[3], float a) {
    for (int i = 0; i < 8; i++) {
        float t = (float)(i + 1) / 8.0f;
        ui_circle(cx, cy, r * (1.0f - t * 0.85f), c[0], c[1], c[2], a / 8.0f);
    }
}

/* ── Direct pixel effects on an RGBA buffer ────────────────────────────── */
struct Canvas {
    unsigned char *px;
    int w, h;
    unsigned char *at(int x, int y) const { return px + ((size_t)y * (size_t)w + (size_t)x) * 4; }
};

/* Diagonal diamond lattice. */
void lattice(const Canvas &c, int x0, int y0, int w, int h, const float col[3], float a) {
    const int period = 26;
    unsigned ca = (unsigned)(a * 255.0f);
    for (int y = std::max(0, y0); y < std::min(c.h, y0 + h); y++)
        for (int x = std::max(0, x0); x < std::min(c.w, x0 + w); x++) {
            int d1 = (x + y) % period, d2 = (x - y + 4096 * period) % period;
            if (d1 != 0 && d2 != 0) continue;
            unsigned char *p = c.at(x, y);
            for (int k = 0; k < 3; k++)
                p[k] = (unsigned char)((p[k] * (255u - ca) + (unsigned)(col[k] * 255.0f) * ca) / 255u);
        }
}

/* Printed cardboard: fine fibers plus blotchy ink density. */
void grain(const Canvas &c, int x0, int y0, int w, int h, unsigned seed, float amount) {
    for (int y = y0; y < y0 + h; y++)
        for (int x = x0; x < x0 + w; x++) {
            float n = (hashf(x, y, seed) - 0.5f) * 0.55f
                    + (vnoise((float)x / 6.0f, (float)y / 6.0f, seed + 11u) - 0.5f) * 0.9f;
            float k = 1.0f + n * amount;
            unsigned char *p = c.at(x, y);
            for (int i = 0; i < 3; i++) {
                float v = (float)p[i] * k;
                p[i] = (unsigned char)(v < 0.0f ? 0.0f : (v > 255.0f ? 255.0f : v));
            }
        }
}

/* Worn edges and corners: the ink rubbed off in patches along the folds,
   showing the lighter board underneath. */
void wear(const Canvas &c, int x0, int y0, int w, int h, unsigned seed, float strength,
          const float board[3]) {
    const float R = 7.0f, RC = 18.0f;
    for (int y = y0; y < y0 + h; y++)
        for (int x = x0; x < x0 + w; x++) {
            float dx = (float)std::min(x - x0, x0 + w - 1 - x);
            float dy = (float)std::min(y - y0, y0 + h - 1 - y);
            float d = std::min(dx, dy);
            float corner = std::max(0.0f, 1.0f - sqrtf(dx * dx + dy * dy) / RC);
            if (d > R && corner <= 0.0f) continue;
            float edge = std::max(0.0f, 1.0f - d / R);
            /* Some stretches of a fold stay crisp, others are scuffed. */
            float patch = (vnoise((float)x / 34.0f, (float)y / 34.0f, seed + 23u) - 0.38f) * 2.6f;
            patch = patch < 0.0f ? 0.0f : (patch > 1.0f ? 1.0f : patch);
            float amt = std::max(edge * edge * patch, corner * corner * 1.2f) * strength;
            float nz = vnoise((float)x / 3.2f, (float)y / 3.2f, seed) * 0.7f + hashf(x, y, seed + 5u) * 0.3f;
            float t = (amt - nz * 0.9f) * 2.2f;
            if (t <= 0.0f) continue;
            if (t > 0.7f) t = 0.7f;
            unsigned char *p = c.at(x, y);
            for (int i = 0; i < 3; i++)
                p[i] = (unsigned char)((float)p[i] + ((board[i] * 255.0f) - (float)p[i]) * t);
        }
}

/* Clear plastic sleeve over a case insert: a soft diagonal reflection. */
void sleeve_gloss(const Canvas &c, int x0, int y0, int w, int h) {
    for (int y = y0; y < y0 + h; y++)
        for (int x = x0; x < x0 + w; x++) {
            float t = ((float)(x - x0) + (float)(y - y0) * 0.55f) / ((float)w + (float)h * 0.55f);
            float g = expf(-powf((t - 0.30f) / 0.07f, 2.0f)) * 0.11f
                    + expf(-powf((t - 0.40f) / 0.018f, 2.0f)) * 0.06f;
            unsigned char *p = c.at(x, y);
            for (int i = 0; i < 3; i++) p[i] = (unsigned char)((float)p[i] + (255.0f - (float)p[i]) * g);
        }
}

/* Text rendered on its own so it can be blitted rotated (vertical labels). */
struct Sprite {
    std::vector<unsigned char> px;
    int w = 0, h = 0;
};

/* Must be called while no other ui target is active. */
Sprite text_sprite(const char *s, int size, const float rgb[3]) {
    Sprite sp;
    sp.w = ui_text_width(size, s) + 6;
    sp.h = (int)lh(size) + 4;
    sp.px.assign((size_t)sp.w * (size_t)sp.h * 4, 0);
    ui_target_begin(sp.px.data(), sp.w, sp.h);
    ui_text_px(3.0f, 2.0f, size, s, rgb[0], rgb[1], rgb[2], 1.0f);
    ui_target_end();
    return sp;
}

/* rot: 0 as is, +1 turned 90° counter-clockwise (reads bottom→top),
   -1 clockwise (reads top→bottom). (x, y) = top-left of the result. */
void blit(const Canvas &dst, const Sprite &sp, int x, int y, int rot) {
    const int ow = rot ? sp.h : sp.w, oh = rot ? sp.w : sp.h;
    for (int oy = 0; oy < oh; oy++)
        for (int ox = 0; ox < ow; ox++) {
            int sx, sy;
            if (rot == 0)     { sx = ox;            sy = oy; }
            else if (rot > 0) { sx = sp.w - 1 - oy; sy = ox; }
            else              { sx = oy;            sy = sp.h - 1 - ox; }
            const unsigned char *s = &sp.px[((size_t)sy * (size_t)sp.w + (size_t)sx) * 4];
            if (!s[3]) continue;
            int dx = x + ox, dy = y + oy;
            if (dx < 0 || dy < 0 || dx >= dst.w || dy >= dst.h) continue;
            unsigned char *d = dst.at(dx, dy);
            unsigned ia = 255u - s[3];
            for (int i = 0; i < 4; i++) d[i] = (unsigned char)(s[i] + (d[i] * ia + 127u) / 255u);
        }
}

/* ── Content lookup ────────────────────────────────────────────────────── */
/* First usable screenshot for this version (falls back to its siblings'). */
const std::string *pick_shot(const GameGroup &g, int idx) {
    const GameEntry &e = g.entries[(size_t)idx];
    for (const auto &s : e.screenshots)
        if (ui_image_size(s.c_str(), nullptr, nullptr)) return &s;
    for (const auto &o : g.entries)
        for (const auto &s : o.screenshots)
            if (ui_image_size(s.c_str(), nullptr, nullptr)) return &s;
    return nullptr;
}

const std::string *pick_logo(const GameGroup &g, int idx) {
    const GameEntry &e = g.entries[(size_t)idx];
    const std::string &l = e.logo_path.empty() ? g.logo_path : e.logo_path;
    if (!l.empty() && ui_image_size(l.c_str(), nullptr, nullptr)) return &l;
    return nullptr;
}

/* Everything a face painter needs about one box. */
struct Art {
    const GameGroup *g;
    const GameEntry *e;
    int              idx;
    BoxFamily        fam;
    const Palette   *pal;
    const BoxShape  *sh;
    const std::string *shot;
    const std::string *logo;
    std::string      version;
    int              year;
    Canvas           atlas;
};

/* Screenshot art (crisp pixels), or an emerald lattice when there's none. */
void art_fill(const Art &a, float x, float y, float w, float h, float bright) {
    if (a.shot) {
        ui_image_ex(x, y, w, h, a.shot->c_str(), UI_IMG_COVER, bright, bright, bright, 1.0f);
        return;
    }
    const float *d = a.pal->dark;
    ui_gradient_v(x, y, w, h, d[0] * 2.6f + 0.02f, d[1] * 2.6f + 0.03f, d[2] * 2.6f + 0.02f, 1.0f,
                  d[0], d[1], d[2], 1.0f);
    lattice(a.atlas, (int)x, (int)y, (int)w, (int)h, GOLD, 0.07f);
    glow(x + w * 0.5f, y + h * 0.40f, std::min(w, h) * 0.55f, GOLD, 0.26f);
}

/* Game title as lettering when there's no logo image. */
void title_text(const std::string &title, float x, float y, float w, float h, bool light_bg) {
    int px = 48;
    for (; px > 20; px -= 4) {
        int n = ui_text_wrap_lines(w, px, title.c_str(), 0);
        if (n <= 3 && (float)n * (float)px * 1.1f <= h) break;
    }
    int lines = ui_text_wrap_lines(w, px, title.c_str(), 3);
    float lh1 = (float)px * 1.1f;
    float ty = y + (h - lh1 * (float)lines) * 0.5f;
    if (light_bg) {
        ui_text_wrap(x, ty, w, px, lh1, title.c_str(), UI_ALIGN_CENTER, 3, 0.12f, 0.10f, 0.14f, 1.0f);
        return;
    }
    for (int dy = -3; dy <= 3; dy += 3)
        for (int dx = -3; dx <= 3; dx += 3)
            if (dx || dy)
                ui_text_wrap(x + (float)dx, ty + (float)dy, w, px, lh1, title.c_str(), UI_ALIGN_CENTER, 3,
                             0.10f, 0.06f, 0.02f, 0.9f);
    ui_text_wrap(x, ty, w, px, lh1, title.c_str(), UI_ALIGN_CENTER, 3, GOLD[0], GOLD[1], GOLD[2], 1.0f);
}

/* The logo (drop shadow) or the lettered title. */
void logo_block(const Art &a, float x, float y, float w, float h, bool light_bg) {
    if (a.logo) {
        ui_image_ex(x + 3.0f, y + 5.0f, w, h, a.logo->c_str(),
                    UI_IMG_FIT | UI_IMG_SMOOTH | UI_IMG_SILHOUETTE, 0, 0, 0, light_bg ? 0.30f : 0.60f);
        ui_image_ex(x, y, w, h, a.logo->c_str(), UI_IMG_FIT | UI_IMG_SMOOTH, 1, 1, 1, 1);
    } else {
        title_text(db_title(*a.g), x, y, w, h, light_bg);
    }
}

/* Translucent band with the version name on the left and the year on the right. */
void version_band(const Art &a, float x, float y, float w, float h, const float bg[3], float bg_a,
                  const float fg[3]) {
    ui_rect(x, y, w, h, bg[0], bg[1], bg[2], bg_a);
    int px = fit_px(a.version.c_str(), w - 96.0f, 22, 12);
    text_in_band(x + 14.0f, y, h, px, a.version.c_str(), fg, 1.0f);
    if (a.year > 0) {
        char yb[16];
        snprintf(yb, sizeof(yb), "%d", a.year);
        int ypx = std::min(px, 20);
        text_in_band(x + w - 14.0f - (float)ui_text_width(ypx, yb), y, h, ypx, yb, fg, 0.9f);
    }
}

const char *platform_label(const Art &a) {
    return a.e->platform.empty() ? tr("Emerald Collection") : a.e->platform.c_str();
}

/* Platform label with one color per letter (N64 style). */
void colored_label(float x, float y, int px, const char *s) {
    char ch[2] = { 0, 0 };
    int ci = 0;
    for (const char *p = s; *p; p++) {
        ch[0] = *p;
        if (*p != ' ') {
            const float *c = N64_COLORS[ci++ % 4];
            ui_text_px(x + 2.0f, y + 2.0f, px, ch, 0, 0, 0, 0.6f);
            ui_text_px(x, y, px, ch, c[0], c[1], c[2], 1.0f);
        }
        x += (float)ui_text_width(px, ch);
    }
}

/* ── Fronts, one per family ────────────────────────────────────────────── */
/* NES "black box": black grid, the art in a framed window. Also the default. */
void front_nes(const Art &a, float W, float H) {
    const Palette &p = *a.pal;
    ui_rect(0, 0, W, H, 0.035f, 0.035f, 0.04f, 1.0f);
    for (float gx = 6.0f; gx < W; gx += 12.0f) ui_rect(gx, 0, 1.0f, H, 0.13f, 0.13f, 0.14f, 0.55f);
    for (float gy = 6.0f; gy < H; gy += 12.0f) ui_rect(0, gy, W, 1.0f, 0.13f, 0.13f, 0.14f, 0.55f);
    /* Platform tab, top left. */
    const char *plat = platform_label(a);
    int tpx = fit_px(plat, W * 0.55f, 22, 12);
    float tw = (float)ui_text_width(tpx, plat) + 28.0f;
    ui_triangle(12.0f, 12.0f, 12.0f + tw + 12.0f, 12.0f, 12.0f + tw, 44.0f, p.accent[0], p.accent[1], p.accent[2], 1.0f);
    ui_triangle(12.0f, 12.0f, 12.0f + tw, 44.0f, 12.0f, 44.0f, p.accent[0], p.accent[1], p.accent[2], 1.0f);
    text_in_band(24.0f, 12.0f, 32.0f, tpx, plat, WHITE, 1.0f);
    /* Title, art window, version band, seal. */
    logo_block(a, 20.0f, 52.0f, W - 40.0f, H * 0.23f, false);
    const float wx = 28.0f, wy = 60.0f + H * 0.23f, ww = W - 56.0f, wh = H * 0.44f;
    ui_rect(wx - 5.0f, wy - 5.0f, ww + 10.0f, wh + 10.0f, 0.52f, 0.52f, 0.55f, 1.0f);
    ui_rect(wx - 2.0f, wy - 2.0f, ww + 4.0f, wh + 4.0f, 0.0f, 0.0f, 0.0f, 1.0f);
    art_fill(a, wx, wy, ww, wh, 1.0f);
    version_band(a, wx - 5.0f, wy + wh + 12.0f, ww + 10.0f, 32.0f, p.accent, 1.0f, WHITE);
    seal(20.0f, H - 24.0f, 9.0f, WHITE);
}

/* SNES: light gray band down the left with purple stripes, art on the right. */
void front_snes(const Art &a, float W, float H, const Sprite &label) {
    const Palette &p = *a.pal;
    const float band = std::max(64.0f, W * 0.16f);
    art_fill(a, band, 0, W - band, H, 0.95f);
    ui_gradient_v(band, 0, W - band, H * 0.42f, 0, 0, 0, 0.62f, 0, 0, 0, 0.0f);
    ui_gradient_v(band, H * 0.70f, W - band, H * 0.30f, 0, 0, 0, 0.0f, 0, 0, 0, 0.55f);
    ui_gradient_v(0, 0, band, H, p.banner[0] * 1.08f, p.banner[1] * 1.08f, p.banner[2] * 1.08f, 1.0f,
                  p.banner[0] * 0.90f, p.banner[1] * 0.90f, p.banner[2] * 0.90f, 1.0f);
    for (int i = 0; i < 3; i++)
        ui_rect(band - 16.0f + (float)i * 5.0f, 0, 2.0f, H, p.accent[0], p.accent[1], p.accent[2], 0.9f);
    blit(a.atlas, label, (int)((band - 16.0f - (float)label.h) * 0.5f), (int)(H * 0.5f - (float)label.w * 0.5f), 1);
    gem(band * 0.40f, H - 26.0f, 9.0f);
    logo_block(a, band + 18.0f, 12.0f, W - band - 36.0f, H * 0.38f, false);
    version_band(a, band, H - 64.0f, W - band, 34.0f, PLASTIC, 0.55f, WHITE);
    ui_rect(band, H - 30.0f, W - band, 3.0f, p.accent[0], p.accent[1], p.accent[2], 0.9f);
}

/* N64: black frame along the top and left, colored platform lettering. */
void front_n64(const Art &a, float W, float H) {
    const float top = std::max(48.0f, H * 0.16f), left = 16.0f;
    art_fill(a, left, top, W - left, H - top, 0.95f);
    ui_gradient_v(left, top, W - left, H * 0.36f, 0, 0, 0, 0.55f, 0, 0, 0, 0.0f);
    ui_rect(0, 0, W, top, 0.035f, 0.035f, 0.04f, 1.0f);
    ui_rect(0, top, left, H - top, 0.035f, 0.035f, 0.04f, 1.0f);
    const char *plat = platform_label(a);
    int px = fit_px(plat, W * 0.55f, 34, 14);
    colored_label(20.0f, (top - lh(px)) * 0.5f, px, plat);
    seal(W - 132.0f, top * 0.5f, 10.0f, WHITE);
    logo_block(a, left + 20.0f, top + 10.0f, W - left - 40.0f, H * 0.36f, false);
    version_band(a, left, H - 46.0f, W - left, 32.0f, PLASTIC, 0.60f, WHITE);
}

/* Game Boy / Game Boy Color: white board, platform band, art window. */
void front_gb(const Art &a, float W, float H, bool color) {
    const Palette &p = *a.pal;
    ui_rect(0, 0, W, H, PAPER[0], PAPER[1], PAPER[2], 1.0f);
    const float band = 38.0f;
    ui_rect(0, 0, W, band, p.banner[0], p.banner[1], p.banner[2], 1.0f);
    const char *plat = platform_label(a);
    text_in_band(12.0f, 0, band, fit_px(plat, W - 60.0f, 20, 12), plat, p.text, 1.0f);
    gem(W - 20.0f, band * 0.5f, 8.0f);
    float y = band;
    if (color) {           /* a strip of colors under the band */
        static const float C[5][3] = {{0.90f, 0.20f, 0.20f}, {0.97f, 0.55f, 0.10f}, {0.97f, 0.82f, 0.12f},
                                      {0.20f, 0.70f, 0.30f}, {0.20f, 0.42f, 0.90f}};
        for (int i = 0; i < 5; i++) ui_rect(W * (float)i / 5.0f, y, W / 5.0f + 1.0f, 5.0f, C[i][0], C[i][1], C[i][2], 1.0f);
        y += 5.0f;
    }
    const float wx = 12.0f, wy = y + 10.0f, ww = W - 24.0f, wh = H * 0.52f;
    ui_rect(wx - 3.0f, wy - 3.0f, ww + 6.0f, wh + 6.0f, 0.16f, 0.16f, 0.18f, 1.0f);
    art_fill(a, wx, wy, ww, wh, 1.0f);
    logo_block(a, wx, wy + wh + 6.0f, ww, H - (wy + wh + 6.0f) - 38.0f, true);
    version_band(a, 0, H - 30.0f, W, 30.0f, p.banner, 1.0f, p.text);
}

/* Game Boy Advance: indigo band on the left, full art. */
void front_gba(const Art &a, float W, float H, const Sprite &label) {
    const Palette &p = *a.pal;
    const float band = 40.0f;
    art_fill(a, band, 0, W - band, H, 0.95f);
    ui_gradient_v(band, 0, W - band, H * 0.40f, 0, 0, 0, 0.60f, 0, 0, 0, 0.0f);
    ui_gradient_v(0, 0, band, H, p.banner[0] * 1.2f, p.banner[1] * 1.2f, p.banner[2] * 1.2f, 1.0f,
                  p.banner[0] * 0.8f, p.banner[1] * 0.8f, p.banner[2] * 0.8f, 1.0f);
    ui_rect(band - 4.0f, 0, 2.0f, H, p.accent[0], p.accent[1], p.accent[2], 0.9f);
    blit(a.atlas, label, (int)((band - 4.0f - (float)label.h) * 0.5f), (int)(H * 0.42f - (float)label.w * 0.5f), 1);
    gem(band * 0.45f, H - 22.0f, 7.0f);
    logo_block(a, band + 12.0f, 12.0f, W - band - 24.0f, H * 0.30f, false);
    version_band(a, band, H - 56.0f, W - band, 30.0f, PLASTIC, 0.60f, WHITE);
}

/* CD-i: an insert behind a black plastic clamshell (Mega Drive / VHS style). */
void front_case(const Art &a, float W, float H) {
    const Palette &p = *a.pal;
    const float m = 14.0f, band = 44.0f;
    ui_rect(0, 0, W, H, PLASTIC[0], PLASTIC[1], PLASTIC[2], 1.0f);
    const float ix = m, iy = m, iw = W - 2.0f * m, ih = H - 2.0f * m;
    ui_rect(ix, iy, iw, band, p.banner[0], p.banner[1], p.banner[2], 1.0f);
    ui_rect(ix, iy + band - 3.0f, iw, 3.0f, p.accent[0], p.accent[1], p.accent[2], 1.0f);
    const char *plat = platform_label(a);
    text_in_band(ix + 12.0f, iy, band - 3.0f, fit_px(plat, iw - 60.0f, 22, 12), plat, p.text, 1.0f);
    gem(ix + iw - 20.0f, iy + (band - 3.0f) * 0.5f, 9.0f);
    art_fill(a, ix, iy + band, iw, ih - band, 0.95f);
    ui_gradient_v(ix, iy + band, iw, ih * 0.35f, 0, 0, 0, 0.60f, 0, 0, 0, 0.0f);
    logo_block(a, ix + 14.0f, iy + band + 10.0f, iw - 28.0f, ih * 0.27f, false);
    version_band(a, ix, iy + ih - 44.0f, iw, 34.0f, PLASTIC, 0.60f, WHITE);
}

/* PC big box: full-bleed art, emerald band along the bottom. */
void front_pc(const Art &a, float W, float H) {
    const Palette &p = *a.pal;
    art_fill(a, 0, 0, W, H, 0.95f);
    ui_gradient_v(0, 0, W, H * 0.42f, 0, 0, 0, 0.70f, 0, 0, 0, 0.0f);
    const float band = 50.0f;
    ui_rect(0, H - band, W, band, p.banner[0], p.banner[1], p.banner[2], 1.0f);
    ui_rect(0, H - band, W, 3.0f, p.accent[0], p.accent[1], p.accent[2], 1.0f);
    const char *plat = platform_label(a);
    text_in_band(16.0f, H - band + 3.0f, band - 3.0f, fit_px(plat, W - 170.0f, 22, 12), plat, p.text, 1.0f);
    seal(W - 124.0f, H - band * 0.5f + 1.5f, 9.0f, p.text);
    logo_block(a, 24.0f, 20.0f, W - 48.0f, H * 0.33f, false);
    version_band(a, 0, H - band - 42.0f, W, 34.0f, PLASTIC, 0.55f, WHITE);
}

/* A real box scan from db.json. A cover shared by several versions still says
   which one this is. */
void front_cover(const Art &a, float W, float H, const std::string &cover, bool shared) {
    ui_rect(0, 0, W, H, a.pal->dark[0], a.pal->dark[1], a.pal->dark[2], 1.0f);
    ui_image_ex(0, 0, W, H, cover.c_str(), UI_IMG_COVER | UI_IMG_SMOOTH, 1, 1, 1, 1);
    if (shared) version_band(a, 0, H - 52.0f, W, 34.0f, PLASTIC, 0.62f, WHITE);
}

/* ── Spine (drawn horizontally: fh long x sd thick) and top (fw x sd) ───── */
void spine_strip(const Art &a, float L, float T) {
    const Palette &p = *a.pal;
    const bool light = a.fam == FAM_SNES;
    const bool is_case = a.sh->style == BOXSTYLE_CASE;
    const char *title = db_title(*a.g).c_str();
    float x0 = 12.0f, x1 = L - 34.0f;
    const float *tc = p.text;

    if (is_case) {
        ui_rect(0, 0, L, T, PLASTIC[0], PLASTIC[1], PLASTIC[2], 1.0f);
        ui_rect(10.0f, 6.0f, L - 20.0f, T - 12.0f, p.banner[0], p.banner[1], p.banner[2], 1.0f);
        x0 = 20.0f; x1 = L - 44.0f;
    } else if (a.fam == FAM_N64 || a.fam == FAM_NES) {
        ui_rect(0, 0, L, T, 0.035f, 0.035f, 0.04f, 1.0f);
    } else {
        ui_gradient_v(0, 0, L, T, p.banner[0] * 1.10f, p.banner[1] * 1.10f, p.banner[2] * 1.10f, 1.0f,
                      p.banner[0] * 0.88f, p.banner[1] * 0.88f, p.banner[2] * 0.88f, 1.0f);
        if (light) {
            ui_rect(0, T - 12.0f, L, 2.0f, p.accent[0], p.accent[1], p.accent[2], 0.9f);
            ui_rect(0, T - 8.0f, L, 2.0f, p.accent[0], p.accent[1], p.accent[2], 0.9f);
        }
    }

    /* Platform tag at the top end. */
    const char *tag = p.tag ? p.tag : (a.e->platform.empty() ? "EL" : a.e->platform.c_str());
    int tpx = fit_px(tag, T * 1.2f, std::min(24, (int)(T * 0.40f)), 11);
    float tag_w = (float)ui_text_width(tpx, tag);
    float ty = (T - lh(tpx)) * 0.5f;
    if (a.fam == FAM_NES) {
        ui_rect(x0 - 2.0f, ty - 3.0f, tag_w + 14.0f, lh(tpx) + 6.0f, p.accent[0], p.accent[1], p.accent[2], 1.0f);
        ui_text_px(x0 + 5.0f, ty, tpx, tag, 1, 1, 1, 1);
    } else if (a.fam == FAM_N64) {
        colored_label(x0, ty, tpx, tag);
    } else {
        ui_text_px(x0, ty, tpx, tag, tc[0], tc[1], tc[2], 0.95f);
    }
    x0 += tag_w + 26.0f;

    /* Title, centered in the remaining length. */
    int px = fit_px(title, x1 - x0, std::min(40, (int)(T * 0.52f)), 12);
    float w = (float)ui_text_width(px, title);
    float yy = (T - lh(px)) * 0.5f;
    float xx = x0 + (x1 - x0 - w) * 0.5f;
    if (!light) ui_text_px(xx + 1.5f, yy + 2.0f, px, title, 0, 0, 0, 0.45f);
    ui_text_px(xx, yy, px, title, tc[0], tc[1], tc[2], 1.0f);
    gem(L - 20.0f, T * 0.5f, std::min(10.0f, T * 0.16f));
}

void top_strip(const Art &a, float x, float y, float W, float T) {
    const Palette &p = *a.pal;
    if (a.sh->style == BOXSTYLE_CASE) {
        ui_rect(x, y, W, T, PLASTIC[0], PLASTIC[1], PLASTIC[2], 1.0f);
        ui_rect(x, y + T * 0.5f, W, 1.0f, 0.16f, 0.16f, 0.18f, 1.0f);     /* hinge line */
        return;
    }
    const bool dark_box = a.fam == FAM_NES || a.fam == FAM_N64;
    const float *bg = dark_box ? PLASTIC : p.banner;
    ui_rect(x, y, W, T, bg[0], bg[1], bg[2], 1.0f);
    const char *title = db_title(*a.g).c_str();
    int px = fit_px(title, W * 0.62f, std::min(26, (int)(T * 0.42f)), 10);
    const float *tc = p.text;
    ui_text_px(x + (W - (float)ui_text_width(px, title)) * 0.5f, y + (T - 7.0f - lh(px)) * 0.5f, px, title,
               tc[0], tc[1], tc[2], 0.95f);
    const char *tag = p.tag ? p.tag : "EL";
    int tpx = std::min(px, 16);
    ui_text_px(x + 12.0f, y + (T - 7.0f - lh(tpx)) * 0.5f, tpx, tag, tc[0], tc[1], tc[2], 0.8f);
    /* The tuck flap's fold, near the front edge. */
    ui_rect(x, y + T - 7.0f, W, 1.5f, 0, 0, 0, 0.35f);
}

/* ── Back cover ────────────────────────────────────────────────────────── */
void barcode(float x, float y, float w, float h, unsigned seed) {
    ui_rect(x, y, w, h, 0.97f, 0.97f, 0.94f, 1.0f);
    float bx = x + 8.0f;
    const float top = y + 6.0f, bh = h - 22.0f;
    while (bx < x + w - 9.0f) {
        int bw = 1 + (int)(rng_next(seed) % 3u);
        if (rng_next(seed) & 1u) ui_rect(bx, top, (float)bw, bh, 0.05f, 0.05f, 0.05f, 1.0f);
        bx += (float)bw;
    }
    char digits[24];
    snprintf(digits, sizeof(digits), "0 %05u %05u", rng_next(seed) % 100000u, rng_next(seed) % 100000u);
    const float ink[3] = {0.05f, 0.05f, 0.05f};
    text_centered(x + w * 0.5f, y + h - 16.0f, 12, digits, ink, 1.0f);
}

void draw_back(const Art &a, const Canvas &c) {
    const Palette &p = *a.pal;
    const float W = (float)c.w, H = (float)c.h;
    const bool land = a.sh->landscape;
    const bool is_case = a.sh->style == BOXSTYLE_CASE;
    const float M = 28.0f + (is_case ? 14.0f : 0.0f);
    const float band_h = 90.0f;

    const float *d = p.dark;
    ui_gradient_v(0, 0, W, H, d[0] * 2.4f + 0.02f, d[1] * 2.4f + 0.03f, d[2] * 2.4f + 0.02f, 1.0f,
                  d[0], d[1], d[2], 1.0f);
    lattice(c, 0, 0, c.w, c.h - (int)band_h, GOLD, 0.05f);

    /* Screenshots: a column of three on landscape boxes, a row on portrait. */
    std::vector<const std::string *> shots;
    for (const auto &s : a.e->screenshots)
        if (shots.size() < 3 && ui_image_size(s.c_str(), nullptr, nullptr)) shots.push_back(&s);
    if (shots.empty())
        for (const auto &o : a.g->entries)
            for (const auto &s : o.screenshots)
                if (shots.size() < 3 && ui_image_size(s.c_str(), nullptr, nullptr)) shots.push_back(&s);

    float tx = M, tw = W - 2.0f * M, y = 24.0f + (is_case ? 14.0f : 0.0f);
    if (land && !shots.empty()) {
        const float col_w = W * 0.34f;
        const float gap = 12.0f;
        const float ch = std::min((H - band_h - 2.0f * y - gap * 2.0f) / 3.0f, col_w * 0.75f);
        const float cw = ch / 0.75f;
        float sx = W - M - cw, sy = y + 4.0f;
        for (size_t i = 0; i < shots.size(); i++, sy += ch + gap) {
            ui_rect(sx - 3.0f, sy - 3.0f, cw + 6.0f, ch + 6.0f, GOLD[0], GOLD[1], GOLD[2], 0.85f);
            ui_rect(sx, sy, cw, ch, 0, 0, 0, 1);
            ui_image_ex(sx, sy, cw, ch, shots[i]->c_str(), UI_IMG_COVER, 1, 1, 1, 1);
        }
        tw = W - 2.0f * M - cw - 24.0f;
    }

    int tpx = fit_px(db_title(*a.g).c_str(), tw, 38, 24);
    y = ui_text_wrap(tx, y, tw, tpx, (float)tpx * 1.15f, db_title(*a.g).c_str(), UI_ALIGN_CENTER, 2,
                     GOLD[0], GOLD[1], GOLD[2], 1.0f);
    {
        std::string meta;
        if (a.year > 0) meta += std::to_string(a.year);
        if (!a.e->platform.empty()) { if (!meta.empty()) meta += "  -  "; meta += a.e->platform; }
        if (!a.version.empty())     { if (!meta.empty()) meta += "  -  "; meta += a.version; }
        const float off[3] = {0.84f, 0.86f, 0.82f};
        int mpx = fit_px(meta.c_str(), tw, 19, 12);
        text_centered(tx + tw * 0.5f, y + 4.0f, mpx, meta.c_str(), off, 1.0f);
        y += 34.0f;
    }
    ui_rect(tx, y, tw, 2.0f, GOLD[0], GOLD[1], GOLD[2], 0.55f);
    y += 18.0f;

    if (!land && !shots.empty()) {
        const float gap = 12.0f;
        const float cw = (tw - gap * 2.0f) / 3.0f, ch = cw * 0.75f;
        float total = cw * (float)shots.size() + gap * (float)(shots.size() - 1);
        float sx = tx + (tw - total) * 0.5f;
        for (size_t i = 0; i < shots.size(); i++) {
            float cx = sx + (float)i * (cw + gap);
            ui_rect(cx - 3.0f, y - 3.0f, cw + 6.0f, ch + 6.0f, GOLD[0], GOLD[1], GOLD[2], 0.85f);
            ui_rect(cx, y, cw, ch, 0, 0, 0, 1);
            ui_image_ex(cx, y, cw, ch, shots[i]->c_str(), UI_IMG_COVER, 1, 1, 1, 1);
        }
        y += ch + 26.0f;
    }

    const float body[3] = {0.93f, 0.93f, 0.90f};
    const float dim[3]  = {0.82f, 0.82f, 0.76f};
    const float bottom  = H - band_h - 16.0f;
    auto section = [&](const char *label, const std::string &text, const float col[3], int max_lines) {
        if (text.empty() || y > bottom - 40.0f) return;
        ui_text_px(tx, y, 16, label, GOLD_LT[0], GOLD_LT[1], GOLD_LT[2], 0.95f);
        y += 24.0f;
        int avail = (int)((bottom - y) / 28.0f);
        if (avail < 1) return;
        if (max_lines > avail) max_lines = avail;
        y = ui_text_wrap(tx, y, tw, 21, 28.0f, text.c_str(), UI_ALIGN_LEFT, max_lines, col[0], col[1], col[2], 1.0f);
        y += 14.0f;
    };
    section(tr("THE ADVENTURE"), db_description(*a.g), body, 7);
    if (a.g->sequential) {
        char wk[64];
        snprintf(wk, sizeof(wk), tr("Week %d of %d of the original broadcast."), a.idx + 1, (int)a.g->entries.size());
        section(tr("THIS WEEK"), db_version_desc(*a.e).empty() ? std::string(wk) : db_version_desc(*a.e), dim, 5);
    } else {
        section(tr("THIS VERSION"), db_version_desc(*a.e), dim, 6);
    }
    if (bottom - y > 80.0f && a.logo) {
        float lh2 = std::min(bottom - y - 16.0f, 130.0f);
        ui_image_ex(tx + 40.0f, y + (bottom - y - lh2) * 0.5f, tw - 80.0f, lh2, a.logo->c_str(),
                    UI_IMG_FIT | UI_IMG_SMOOTH, 1, 1, 1, 0.85f);
    }

    /* Bottom band: platform, seal, barcode. */
    const float by = H - band_h - (is_case ? 14.0f : 0.0f);
    ui_rect(0, by, W, band_h, p.banner[0], p.banner[1], p.banner[2], 1.0f);
    ui_rect(0, by, W, 5.0f, p.accent[0], p.accent[1], p.accent[2], 1.0f);
    const char *plat = platform_label(a);
    int px = fit_px(plat, W - 240.0f, 24, 14);
    ui_text_px(M, by + 20.0f, px, plat, p.text[0], p.text[1], p.text[2], 1.0f);
    seal(M, by + 64.0f, 8.0f, p.text);
    barcode(W - M - 150.0f, by + 14.0f, 150.0f, 62.0f, hash_str(a.g->key + a.version));

    if (is_case) {                          /* plastic frame around the insert */
        const float m = 14.0f;
        ui_rect(0, 0, W, m, PLASTIC[0], PLASTIC[1], PLASTIC[2], 1.0f);
        ui_rect(0, H - m, W, m, PLASTIC[0], PLASTIC[1], PLASTIC[2], 1.0f);
        ui_rect(0, 0, m, H, PLASTIC[0], PLASTIC[1], PLASTIC[2], 1.0f);
        ui_rect(W - m, 0, m, H, PLASTIC[0], PLASTIC[1], PLASTIC[2], 1.0f);
    }
}

/* ── Building textures ─────────────────────────────────────────────────── */
Art make_art(const GameGroup &g, int idx, int shape) {
    Art a;
    a.g = &g;
    a.e = &g.entries[(size_t)idx];
    a.idx = idx;
    a.sh = boxshape_get(shape);
    a.fam = a.sh->family;
    a.pal = &PALETTES[a.fam];
    a.shot = pick_shot(g, idx);
    a.logo = pick_logo(g, idx);
    a.version = db_entry_title(*a.e).empty() ? std::string(tr("Original")) : db_entry_title(*a.e);
    a.year = a.e->year > 0 ? a.e->year : g.year;
    a.atlas = Canvas{nullptr, 0, 0};
    return a;
}

void edge_color(const Art &a, float out[3]) {
    if (a.sh->style == BOXSTYLE_CASE) { out[0] = PLASTIC[0]; out[1] = PLASTIC[1]; out[2] = PLASTIC[2]; return; }
    const float *base = (a.fam == FAM_NES || a.fam == FAM_N64) ? PLASTIC : a.pal->banner;
    mix3(out, base, BOARD, 0.45f);
}

unsigned build_atlas(const GameGroup &g, int idx, int shape) {
    Art a = make_art(g, idx, shape);
    const BoxShape &sh = *a.sh;
    std::vector<unsigned char> buf((size_t)sh.atlas_w * (size_t)sh.atlas_h * 4, 0);
    std::vector<unsigned char> strip((size_t)sh.fh * (size_t)sh.sd * 4, 0);
    a.atlas = Canvas{buf.data(), sh.atlas_w, sh.atlas_h};
    const float W = (float)sh.fw, H = (float)sh.fh;
    const unsigned seed = hash_str(g.key) + (unsigned)idx * 7919u;

    /* Vertical labels need their own buffers, so render them first. */
    Sprite label;
    if (a.fam == FAM_SNES || a.fam == FAM_GBA) {
        const char *plat = platform_label(a);
        int px = fit_px(plat, H * 0.8f, a.fam == FAM_SNES ? 30 : 22, 12);
        label = text_sprite(plat, px, a.fam == FAM_SNES ? a.pal->text : a.pal->text);
    }

    const std::string &cover = !a.e->cover_path.empty() ? a.e->cover_path : g.cover_path;
    const bool has_cover = !cover.empty() && ui_image_size(cover.c_str(), nullptr, nullptr);

    ui_target_begin(buf.data(), sh.atlas_w, sh.atlas_h);
    ui_set_clip(0, 0, W, H);
    if (has_cover) front_cover(a, W, H, cover, a.e->cover_path.empty() && g.entries.size() > 1);
    else switch (a.fam) {
        case FAM_SNES: front_snes(a, W, H, label); break;
        case FAM_N64:  front_n64(a, W, H); break;
        case FAM_GB:   front_gb(a, W, H, false); break;
        case FAM_GBC:  front_gb(a, W, H, true); break;
        case FAM_GBA:  front_gba(a, W, H, label); break;
        case FAM_CDI:  front_case(a, W, H); break;
        case FAM_PC:   front_pc(a, W, H); break;
        default:       front_nes(a, W, H); break;
    }
    ui_set_clip(0, H, W, (float)sh.sd);
    top_strip(a, 0, H, W, (float)sh.sd);
    float ec[3];
    edge_color(a, ec);
    ui_set_clip(W, H, (float)sh.sd, (float)sh.sd);
    ui_rect(W, H, (float)sh.sd, (float)sh.sd, ec[0], ec[1], ec[2], 1.0f);
    ui_target_end();

    ui_target_begin(strip.data(), sh.fh, sh.sd);
    spine_strip(a, H, (float)sh.sd);
    ui_target_end();
    /* Rotate the strip 90° clockwise into place: the title then runs top to
       bottom with the letters' tops toward the front of the box. */
    for (int s = 0; s < sh.fh; s++)
        for (int t = 0; t < sh.sd; t++)
            memcpy(&buf[((size_t)s * (size_t)sh.atlas_w + (size_t)sh.fw + (size_t)(sh.sd - 1 - t)) * 4],
                   &strip[((size_t)t * (size_t)sh.fh + (size_t)s) * 4], 4);

    /* Age the print: cardboard grain and worn edges (not on a real scan). */
    if (sh.style == BOXSTYLE_CARDBOARD) {
        grain(a.atlas, 0, 0, sh.atlas_w, sh.atlas_h, seed, has_cover ? 0.03f : 0.075f);
        if (!has_cover) wear(a.atlas, 0, 0, sh.fw, sh.fh, seed + 1u, 0.85f, BOARD);
        wear(a.atlas, sh.fw, 0, sh.sd, sh.fh, seed + 2u, 0.85f, BOARD);
        wear(a.atlas, 0, sh.fh, sh.fw, sh.sd, seed + 3u, 0.85f, BOARD);
        grain(a.atlas, sh.fw, sh.fh, sh.sd, sh.sd, seed + 4u, 0.25f);
    } else if (!has_cover) {
        grain(a.atlas, 14, 14, sh.fw - 28, sh.fh - 28, seed, 0.03f);
        sleeve_gloss(a.atlas, 14, 14, sh.fw - 28, sh.fh - 28);
    }
    return scene3d_texture_rgba(buf.data(), sh.atlas_w, sh.atlas_h, true);
}

unsigned build_back(const GameGroup &g, int idx, int shape) {
    Art a = make_art(g, idx, shape);
    const BoxShape &sh = *a.sh;
    std::vector<unsigned char> buf((size_t)sh.back_w * (size_t)sh.back_h * 4, 0);
    Canvas c{buf.data(), sh.back_w, sh.back_h};
    a.atlas = c;
    ui_target_begin(buf.data(), sh.back_w, sh.back_h);
    draw_back(a, c);
    ui_target_end();
    const unsigned seed = hash_str(g.key) + (unsigned)idx * 7919u + 99u;
    if (sh.style == BOXSTYLE_CARDBOARD) {
        grain(c, 0, 0, c.w, c.h, seed, 0.06f);
        wear(c, 0, 0, c.w, c.h, seed + 1u, 0.85f, BOARD);
    } else {
        sleeve_gloss(c, 14, 14, c.w - 28, c.h - 28);
    }
    return scene3d_texture_rgba(buf.data(), sh.back_w, sh.back_h, true);
}

/* ── Cache ─────────────────────────────────────────────────────────────── */
struct Item {
    const GameGroup *g = nullptr;
    int      idx       = 0;
    bool     back      = false;
    bool     queued    = false;
    unsigned tex       = 0;
    unsigned long last_used = 0;
};

std::unordered_map<std::string, Item> s_items;
std::unordered_map<std::string, int>  s_shapes;
std::deque<std::string> s_queue;        /* atlases, in request order */
std::deque<std::string> s_queue_back;   /* backs: served first        */
unsigned long s_frame = 1;

std::string make_key(const GameGroup &g, int idx, const char *kind) {
    return g.key + '\x1f' + std::to_string(idx) + kind;
}

unsigned request(const GameGroup &g, int idx, bool back) {
    if (idx < 0 || idx >= (int)g.entries.size()) return 0;
    std::string key = make_key(g, idx, back ? "b" : "a");
    auto it = s_items.find(key);
    if (it != s_items.end()) {
        it->second.g = &g;
        it->second.last_used = s_frame;
        return it->second.tex;
    }
    Item item;
    item.g = &g; item.idx = idx; item.back = back; item.queued = true; item.last_used = s_frame;
    s_items.emplace(key, item);
    (back ? s_queue_back : s_queue).push_back(key);
    return 0;
}

void evict(void) {
    int atlases = 0, backs = 0;
    for (auto &kv : s_items)
        if (kv.second.tex) (kv.second.back ? backs : atlases)++;
    while (atlases > MAX_ATLASES || backs > MAX_BACKS) {
        bool want_back = backs > MAX_BACKS;
        auto victim = s_items.end();
        for (auto it = s_items.begin(); it != s_items.end(); ++it) {
            if (!it->second.tex || it->second.back != want_back) continue;
            if (it->second.last_used + 1 >= s_frame) continue;      /* on screen now */
            if (victim == s_items.end() || it->second.last_used < victim->second.last_used) victim = it;
        }
        if (victim == s_items.end()) break;
        scene3d_texture_free(victim->second.tex);
        s_items.erase(victim);
        (want_back ? backs : atlases)--;
    }
}

} // namespace

int boxart_shape(const GameGroup &g, int idx) {
    if (idx < 0 || idx >= (int)g.entries.size()) return boxshape_id(FAM_DEFAULT, false);
    std::string key = make_key(g, idx, "s");
    auto it = s_shapes.find(key);
    if (it != s_shapes.end()) return it->second;
    const GameEntry &e = g.entries[(size_t)idx];
    BoxFamily fam = boxfamily_of(e.platform.c_str());
    bool land = boxfamily_landscape(fam);
    /* A real box scan decides the orientation (e.g. a portrait Super Famicom box). */
    const std::string &cover = !e.cover_path.empty() ? e.cover_path : g.cover_path;
    int w = 0, h = 0;
    if (!cover.empty() && ui_image_size(cover.c_str(), &w, &h) && h > 0) {
        float ar = (float)w / (float)h;
        if (ar > 1.15f) land = true;
        else if (ar < 0.87f) land = false;
    }
    int id = boxshape_id(fam, land);
    s_shapes.emplace(key, id);
    return id;
}

unsigned boxart_atlas(const GameGroup &g, int idx) { return request(g, idx, false); }
unsigned boxart_back(const GameGroup &g, int idx)  { return request(g, idx, true); }

void boxart_pump(double budget_ms) {
    const Uint64 t0   = SDL_GetPerformanceCounter();
    const double freq = (double)SDL_GetPerformanceFrequency();
    int built = 0;
    while (!s_queue_back.empty() || !s_queue.empty()) {
        if (built > 0 && (double)(SDL_GetPerformanceCounter() - t0) * 1000.0 / freq >= budget_ms) break;
        std::deque<std::string> &q = !s_queue_back.empty() ? s_queue_back : s_queue;
        std::string key = q.front();
        q.pop_front();
        auto it = s_items.find(key);
        if (it == s_items.end() || !it->second.queued) continue;
        Item &item = it->second;
        if (item.last_used + 90 < s_frame) { s_items.erase(it); continue; }   /* no longer wanted */
        const int shape = boxart_shape(*item.g, item.idx);
        item.tex = item.back ? build_back(*item.g, item.idx, shape) : build_atlas(*item.g, item.idx, shape);
        item.queued = false;
        built++;
    }
    evict();
    s_frame++;
}

bool boxart_busy(void) { return !s_queue.empty() || !s_queue_back.empty(); }

void boxart_release(void) {
    for (auto &kv : s_items)
        if (kv.second.tex) scene3d_texture_free(kv.second.tex);
    s_items.clear();
    s_shapes.clear();
    s_queue.clear();
    s_queue_back.clear();
}

void boxart_drop_queue(void) {
    for (auto it = s_items.begin(); it != s_items.end();) {
        if (it->second.queued) it = s_items.erase(it);
        else { it->second.g = nullptr; ++it; }      /* rebuilt boxes get a fresh pointer */
    }
    s_queue.clear();
    s_queue_back.clear();
}

void boxart_invalidate(const GameGroup &g, int idx) {
    for (const char *kind : {"a", "b"}) {
        auto it = s_items.find(make_key(g, idx, kind));
        if (it == s_items.end()) continue;
        if (it->second.tex) scene3d_texture_free(it->second.tex);
        s_items.erase(it);        /* a queued key without an item is skipped */
    }
}

void boxart_platform_color(const std::string &platform, float rgb[3]) {
    const Palette &p = PALETTES[boxfamily_of(platform.c_str())];
    rgb[0] = p.banner[0]; rgb[1] = p.banner[1]; rgb[2] = p.banner[2];
}
