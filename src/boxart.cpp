#include "boxart.h"
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

/* Texture budget (atlas ≈ 1.2 MB with mipmaps, back ≈ 2.2 MB). */
#ifdef __SWITCH__
static const int MAX_ATLASES = 32;
#else
static const int MAX_ATLASES = 64;
#endif
static const int MAX_BACKS = 3;

/* Back cover resolution: 1.5x the front so its text stays readable when the
   box is picked up and fills most of the screen. */
static const int BACK_W = 540;
static const int BACK_H = 756;

/* ── Platform palettes ─────────────────────────────────────────────────── */
namespace {

struct Palette {
    float banner[3];   /* platform banner / spine          */
    float accent[3];   /* thin stripe under the banner     */
    float text[3];     /* text on the banner               */
    float dark[3];     /* cover base color                 */
    const char *tag;   /* short platform name for the spine */
};

enum Family { FAM_DEFAULT, FAM_NES, FAM_SNES, FAM_GB, FAM_GBC, FAM_GBA, FAM_N64, FAM_CDI, FAM_PC };

const Palette PALETTES[] = {
    /* default: emerald */ {{0.03f, 0.22f, 0.14f}, {0.95f, 0.78f, 0.15f}, {0.95f, 0.80f, 0.25f}, {0.02f, 0.08f, 0.05f}, nullptr},
    /* NES   */ {{0.07f, 0.07f, 0.08f}, {0.80f, 0.10f, 0.16f}, {0.95f, 0.95f, 0.95f}, {0.05f, 0.05f, 0.06f}, "NES"},
    /* SNES  */ {{0.76f, 0.75f, 0.80f}, {0.40f, 0.28f, 0.64f}, {0.18f, 0.14f, 0.28f}, {0.10f, 0.09f, 0.15f}, "SNES"},
    /* GB    */ {{0.34f, 0.40f, 0.20f}, {0.70f, 0.80f, 0.35f}, {0.95f, 0.97f, 0.88f}, {0.07f, 0.09f, 0.05f}, "GB"},
    /* GBC   */ {{0.29f, 0.17f, 0.50f}, {0.96f, 0.76f, 0.12f}, {1.00f, 1.00f, 1.00f}, {0.08f, 0.05f, 0.14f}, "GBC"},
    /* GBA   */ {{0.16f, 0.20f, 0.46f}, {0.56f, 0.66f, 1.00f}, {1.00f, 1.00f, 1.00f}, {0.05f, 0.06f, 0.14f}, "GBA"},
    /* N64   */ {{0.06f, 0.06f, 0.06f}, {0.15f, 0.60f, 0.28f}, {1.00f, 1.00f, 1.00f}, {0.04f, 0.05f, 0.04f}, "N64"},
    /* CD-i  */ {{0.12f, 0.24f, 0.46f}, {0.86f, 0.86f, 0.92f}, {1.00f, 1.00f, 1.00f}, {0.04f, 0.07f, 0.13f}, "CD-i"},
    /* PC    */ {{0.02f, 0.24f, 0.16f}, {0.95f, 0.78f, 0.15f}, {0.95f, 0.80f, 0.25f}, {0.02f, 0.08f, 0.05f}, "PC"},
};

bool has(const std::string &s, const char *needle) { return s.find(needle) != std::string::npos; }

Family family_of(const std::string &platform) {
    std::string p = platform;
    for (auto &c : p) c = (char)tolower((unsigned char)c);
    if (has(p, "cd-i") || has(p, "cdi"))                          return FAM_CDI;
    if (has(p, "n64") || has(p, "nintendo 64"))                   return FAM_N64;
    if (has(p, "gba") || has(p, "advance"))                       return FAM_GBA;
    if (has(p, "gbc") || has(p, "color"))                         return FAM_GBC;
    if (has(p, "snes") || has(p, "super") || has(p, "satellaview")) return FAM_SNES;
    if (has(p, "nes") || has(p, "famicom"))                       return FAM_NES;
    if (has(p, "gb") || has(p, "game boy"))                       return FAM_GB;
    if (has(p, "pc") || has(p, "switch") || has(p, "port"))       return FAM_PC;
    return FAM_DEFAULT;
}

const Palette &palette_for(const std::string &platform) { return PALETTES[family_of(platform)]; }

const float GOLD[3]     = {0.95f, 0.78f, 0.15f};
const float GOLD_LT[3]  = {1.00f, 0.90f, 0.52f};
const float INK[3]      = {0.20f, 0.12f, 0.03f};

/* ── Drawing helpers (into the current ui target) ───────────────────────── */
unsigned rng_next(unsigned &s) { s = s * 1664525u + 1013904223u; return s >> 8; }

unsigned hash_str(const std::string &s) {
    unsigned h = 2166136261u;
    for (unsigned char c : s) { h ^= c; h *= 16777619u; }
    return h;
}

/* Largest font size in [lo, hi] for which `s` fits in max_w. */
int fit_px(const char *s, float max_w, int hi, int lo) {
    for (int px = hi; px > lo; px -= 2)
        if ((float)ui_text_width(px, s) <= max_w) return px;
    return lo;
}

void text_centered(float cx, float y, int px, const char *s, const float c[3], float a) {
    ui_text_px(cx - (float)ui_text_width(px, s) * 0.5f, y, px, s, c[0], c[1], c[2], a);
}

/* Emerald-cut gem (the launcher's little "publisher seal"). */
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

/* Soft radial glow built from stacked translucent discs. */
void glow(float cx, float cy, float r, const float c[3], float a) {
    for (int i = 0; i < 8; i++) {
        float t = (float)(i + 1) / 8.0f;
        ui_circle(cx, cy, r * (1.0f - t * 0.85f), c[0], c[1], c[2], a / 8.0f);
    }
}

/* Diagonal diamond lattice, drawn straight into an RGBA buffer region. */
void lattice(unsigned char *buf, int stride, int x0, int y0, int w, int h,
             const float c[3], float a) {
    const int period = 26;
    unsigned ca = (unsigned)(a * 255.0f);
    for (int y = y0; y < y0 + h; y++)
        for (int x = x0; x < x0 + w; x++) {
            int d1 = (x + y) % period, d2 = (x - y + 4096 * period) % period;
            if (d1 != 0 && d2 != 0) continue;
            unsigned char *p = buf + ((size_t)y * (size_t)stride + (size_t)x) * 4;
            for (int k = 0; k < 3; k++) {
                unsigned v = (unsigned)(c[k] * 255.0f);
                p[k] = (unsigned char)((p[k] * (255u - ca) + v * ca) / 255u);
            }
        }
}

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

/* First usable screenshot for this version (falls back to its siblings'). */
const std::string *pick_shot(const GameGroup &g, int idx) {
    const GameEntry &e = g.entries[idx];
    for (const auto &s : e.screenshots)
        if (ui_image_size(s.c_str(), nullptr, nullptr)) return &s;
    for (const auto &o : g.entries)
        for (const auto &s : o.screenshots)
            if (ui_image_size(s.c_str(), nullptr, nullptr)) return &s;
    return nullptr;
}

const std::string &logo_for(const GameGroup &g, int idx) {
    const GameEntry &e = g.entries[idx];
    return e.logo_path.empty() ? g.logo_path : e.logo_path;
}

/* Title drawn as text (no logo image): gold with a dark outline. */
void title_text(const std::string &title, float x, float y, float w, float h) {
    int px = 48;
    for (; px > 24; px -= 4) {
        int n = ui_text_wrap_lines(w, px, title.c_str(), 0);
        if (n <= 3 && (float)n * (float)px * 1.1f <= h) break;
    }
    int lines = ui_text_wrap_lines(w, px, title.c_str(), 3);
    float lh = (float)px * 1.1f;
    float ty = y + (h - lh * (float)lines) * 0.5f;
    for (int dy = -3; dy <= 3; dy += 3)
        for (int dx = -3; dx <= 3; dx += 3)
            if (dx || dy)
                ui_text_wrap(x + (float)dx, ty + (float)dy, w, px, lh, title.c_str(), UI_ALIGN_CENTER, 3,
                             0.10f, 0.06f, 0.02f, 0.9f);
    ui_text_wrap(x, ty, w, px, lh, title.c_str(), UI_ALIGN_CENTER, 3, GOLD[0], GOLD[1], GOLD[2], 1.0f);
}

/* Gold ribbon with the version name. */
void ribbon(const char *label, float y, float W) {
    const float h = 40.0f;
    ui_gradient_v(0.0f, y, W, h, 1.0f, 0.88f, 0.45f, 1.0f, 0.78f, 0.56f, 0.12f, 1.0f);
    ui_rect(0.0f, y, W, 2.0f, 0.45f, 0.30f, 0.05f, 1.0f);
    ui_rect(0.0f, y + h - 2.0f, W, 2.0f, 0.45f, 0.30f, 0.05f, 1.0f);
    int px = fit_px(label, W - 40.0f, 24, 14);
    text_centered(W * 0.5f, y + (h - (float)ui_text_line_height(px)) * 0.5f + 1.0f, px, label, INK, 1.0f);
}

/* Darkened bevel around the edges so the faces read as a physical box. */
void edge_bevel(float W, float H, float strength) {
    for (int i = 0; i < 4; i++) {
        float a = strength * (1.0f - (float)i / 4.0f);
        ui_rect((float)i, (float)i, W - 2.0f * (float)i, 1.0f, 0, 0, 0, a);
        ui_rect((float)i, H - 1.0f - (float)i, W - 2.0f * (float)i, 1.0f, 0, 0, 0, a);
        ui_rect((float)i, (float)i + 1.0f, 1.0f, H - 2.0f * (float)i - 2.0f, 0, 0, 0, a);
        ui_rect(W - 1.0f - (float)i, (float)i + 1.0f, 1.0f, H - 2.0f * (float)i - 2.0f, 0, 0, 0, a);
    }
}

/* ── Front cover (BOXART_FRONT_W x BOXART_H at the atlas origin) ─────────── */
void draw_front(unsigned char *buf, const GameGroup &g, int idx) {
    const GameEntry &e = g.entries[idx];
    const Palette &pal = palette_for(e.platform);
    const float W = (float)BOXART_FRONT_W, H = (float)BOXART_H;
    const float BAN = 46.0f;
    const bool multi = g.entries.size() > 1;

    /* Real box art from db.json ("cover" on the version, or on the game). */
    const std::string &cover = !e.cover_path.empty() ? e.cover_path : g.cover_path;
    if (!cover.empty() && ui_image_size(cover.c_str(), nullptr, nullptr)) {
        ui_rect(0, 0, W, H, pal.dark[0], pal.dark[1], pal.dark[2], 1.0f);
        ui_image_ex(0, 0, W, H, cover.c_str(), UI_IMG_COVER | UI_IMG_SMOOTH, 1, 1, 1, 1);
        /* A cover shared by several versions still says which one this is. */
        if (e.cover_path.empty() && multi) ribbon(e.title.c_str(), H - 92.0f, W);
        edge_bevel(W, H, 0.45f);
        return;
    }

    /* Background art: the version's screenshot, dimmed, or a lattice. */
    ui_rect(0, 0, W, H, pal.dark[0], pal.dark[1], pal.dark[2], 1.0f);
    const std::string *shot = pick_shot(g, idx);
    if (shot) {
        ui_image_ex(0, BAN, W, H - BAN, shot->c_str(), UI_IMG_COVER, 0.80f, 0.80f, 0.80f, 1.0f);
        ui_rect(0, BAN, W, H - BAN, pal.dark[0], pal.dark[1], pal.dark[2], 0.22f);
        ui_gradient_v(0, BAN, W, 250.0f, 0, 0, 0, 0.82f, 0, 0, 0, 0.0f);
        ui_gradient_v(0, H - 220.0f, W, 220.0f, 0, 0, 0, 0.0f, 0, 0, 0, 0.88f);
    } else {
        ui_gradient_v(0, BAN, W, H - BAN,
                      pal.dark[0] * 2.6f + 0.02f, pal.dark[1] * 2.6f + 0.03f, pal.dark[2] * 2.6f + 0.02f, 1.0f,
                      pal.dark[0], pal.dark[1], pal.dark[2], 1.0f);
        lattice(buf, BOXART_ATLAS_W, 0, (int)BAN, (int)W, (int)(H - BAN), GOLD, 0.07f);
        glow(W * 0.5f, BAN + 150.0f, 190.0f, GOLD, 0.30f);
    }

    /* Platform banner. */
    ui_rect(0, 0, W, BAN, pal.banner[0], pal.banner[1], pal.banner[2], 1.0f);
    ui_gradient_v(0, 0, W, BAN, 1, 1, 1, 0.10f, 0, 0, 0, 0.12f);
    ui_rect(0, BAN - 5.0f, W, 5.0f, pal.accent[0], pal.accent[1], pal.accent[2], 1.0f);
    {
        const char *plat = e.platform.empty() ? "Emerald Collection" : e.platform.c_str();
        int px = fit_px(plat, W - 80.0f, 24, 14);
        ui_text_px(16.0f, (BAN - 5.0f - (float)ui_text_line_height(px)) * 0.5f + 1.0f, px, plat,
                   pal.text[0], pal.text[1], pal.text[2], 1.0f);
        gem(W - 26.0f, (BAN - 5.0f) * 0.5f, 11.0f);
    }

    /* Logo (or the title as text), with a drop shadow. */
    const float lx = 20.0f, ly = BAN + 22.0f, lw = W - 40.0f, lh = 178.0f;
    const std::string &logo = logo_for(g, idx);
    if (!logo.empty() && ui_image_size(logo.c_str(), nullptr, nullptr)) {
        ui_image_ex(lx + 4.0f, ly + 6.0f, lw, lh, logo.c_str(),
                    UI_IMG_FIT | UI_IMG_SMOOTH | UI_IMG_SILHOUETTE, 0, 0, 0, 0.60f);
        ui_image_ex(lx, ly, lw, lh, logo.c_str(), UI_IMG_FIT | UI_IMG_SMOOTH, 1, 1, 1, 1);
    } else {
        title_text(g.title, lx, ly, lw, lh);
    }

    /* Version ribbon + footer. */
    ribbon(e.title.empty() ? "Original" : e.title.c_str(), H - 104.0f, W);
    int year = e.year > 0 ? e.year : g.year;
    if (year > 0) {
        char ybuf[16];
        snprintf(ybuf, sizeof(ybuf), "%d", year);
        ui_text_px(18.0f, H - 50.0f, 22, ybuf, GOLD[0], GOLD[1], GOLD[2], 1.0f);
    }
    if (g.sequential) {
        char wbuf[32];
        snprintf(wbuf, sizeof(wbuf), "Part %d of %d", idx + 1, (int)g.entries.size());
        int w = ui_text_width(16, wbuf);
        ui_text_px(W - 18.0f - (float)w, H - 46.0f, 16, wbuf, 0.85f, 0.85f, 0.80f, 1.0f);
    } else {
        const char *brand = "EMERALD";
        int w = ui_text_width(14, brand);
        ui_text_px(W - 44.0f - (float)w, H - 44.0f, 14, brand, 0.80f, 0.86f, 0.80f, 0.85f);
        gem(W - 28.0f, H - 37.0f, 8.0f);
    }

    /* Thin gold inset frame + edge bevel. */
    ui_round_rect_outline(10.0f, BAN + 10.0f, W - 20.0f, H - BAN - 20.0f, 6.0f, 1.5f,
                          GOLD[0], GOLD[1], GOLD[2], 0.40f);
    edge_bevel(W, H, 0.50f);
}

/* ── Spine (drawn horizontally: BOXART_H long x BOXART_SPINE_W tall) ─────── */
void draw_spine(const GameGroup &g, int idx) {
    const GameEntry &e = g.entries[idx];
    const Palette &pal = palette_for(e.platform);
    const float L = (float)BOXART_H, T = (float)BOXART_SPINE_W;

    ui_rect(0, 0, L, T, pal.banner[0], pal.banner[1], pal.banner[2], 1.0f);
    ui_gradient_v(0, 0, L, T, 1, 1, 1, 0.12f, 0, 0, 0, 0.18f);
    ui_rect(0, 7.0f, L, 3.0f, pal.accent[0], pal.accent[1], pal.accent[2], 1.0f);
    ui_rect(0, T - 10.0f, L, 3.0f, pal.accent[0], pal.accent[1], pal.accent[2], 1.0f);
    /* Cap: the first rows of the rotated strip color the top/bottom/back. */
    ui_rect(0, 0, (float)BOXART_CAP_H, T, pal.banner[0] * 0.55f, pal.banner[1] * 0.55f,
            pal.banner[2] * 0.55f, 1.0f);

    /* Platform tag at the top end. */
    const char *tag = pal.tag ? pal.tag : (e.platform.empty() ? "EL" : e.platform.c_str());
    int tpx = fit_px(tag, 56.0f, 20, 12);
    float tag_w = (float)ui_text_width(tpx, tag);
    float tx = (float)BOXART_CAP_H + 12.0f;
    ui_text_px(tx, (T - (float)ui_text_line_height(tpx)) * 0.5f, tpx, tag,
               pal.text[0], pal.text[1], pal.text[2], 0.95f);

    /* Title, centered in the remaining length. */
    float x0 = tx + tag_w + 16.0f, x1 = L - 40.0f;
    const char *title = g.title.c_str();
    int px = fit_px(title, x1 - x0, 28, 14);
    float w = (float)ui_text_width(px, title);
    float ty = (T - (float)ui_text_line_height(px)) * 0.5f;
    ui_text_px(x0 + (x1 - x0 - w) * 0.5f + 1.5f, ty + 2.0f, px, title, 0, 0, 0, 0.45f);
    ui_text_px(x0 + (x1 - x0 - w) * 0.5f, ty, px, title, pal.text[0], pal.text[1], pal.text[2], 1.0f);

    gem(L - 22.0f, T * 0.5f, 10.0f);
}

/* ── Back cover (BACK_W x BACK_H) ───────────────────────────────────────── */
void draw_back(unsigned char *buf, const GameGroup &g, int idx) {
    const GameEntry &e = g.entries[idx];
    const Palette &pal = palette_for(e.platform);
    const float W = (float)BACK_W, H = (float)BACK_H;
    const float M = 28.0f;

    ui_gradient_v(0, 0, W, H,
                  pal.dark[0] * 2.4f + 0.02f, pal.dark[1] * 2.4f + 0.03f, pal.dark[2] * 2.4f + 0.02f, 1.0f,
                  pal.dark[0], pal.dark[1], pal.dark[2], 1.0f);
    lattice(buf, BACK_W, 0, 0, BACK_W, BACK_H - 96, GOLD, 0.05f);

    /* Title + meta. */
    float y = 26.0f;
    int tpx = fit_px(g.title.c_str(), W - 2.0f * M, 38, 26);
    y = ui_text_wrap(M, y, W - 2.0f * M, tpx, (float)tpx * 1.15f, g.title.c_str(), UI_ALIGN_CENTER, 2,
                     GOLD[0], GOLD[1], GOLD[2], 1.0f);
    {
        std::string meta;
        int year = e.year > 0 ? e.year : g.year;
        if (year > 0) meta += std::to_string(year);
        if (!e.platform.empty()) { if (!meta.empty()) meta += "  -  "; meta += e.platform; }
        if (!e.title.empty())    { if (!meta.empty()) meta += "  -  "; meta += e.title; }
        const float off[3] = {0.84f, 0.86f, 0.82f};
        int mpx = fit_px(meta.c_str(), W - 2.0f * M, 19, 13);
        text_centered(W * 0.5f, y + 4.0f, mpx, meta.c_str(), off, 1.0f);
        y += 34.0f;
    }
    ui_rect(M, y, W - 2.0f * M, 2.0f, GOLD[0], GOLD[1], GOLD[2], 0.55f);
    y += 18.0f;

    /* Screenshot strip. */
    std::vector<const std::string *> shots;
    for (const auto &s : e.screenshots)
        if (shots.size() < 3 && ui_image_size(s.c_str(), nullptr, nullptr)) shots.push_back(&s);
    if (shots.empty())
        for (const auto &o : g.entries)
            for (const auto &s : o.screenshots)
                if (shots.size() < 3 && ui_image_size(s.c_str(), nullptr, nullptr)) shots.push_back(&s);
    if (!shots.empty()) {
        const float gap = 12.0f;
        const float cw = (W - 2.0f * M - gap * 2.0f) / 3.0f, ch = cw * 0.75f;
        float total = cw * (float)shots.size() + gap * (float)(shots.size() - 1);
        float sx = (W - total) * 0.5f;
        for (size_t i = 0; i < shots.size(); i++) {
            float cx = sx + (float)i * (cw + gap);
            ui_rect(cx - 3.0f, y - 3.0f, cw + 6.0f, ch + 6.0f, GOLD[0], GOLD[1], GOLD[2], 0.85f);
            ui_rect(cx, y, cw, ch, 0.0f, 0.0f, 0.0f, 1.0f);
            ui_image_ex(cx, y, cw, ch, shots[i]->c_str(), UI_IMG_COVER, 1, 1, 1, 1);
        }
        y += ch + 26.0f;
    }

    /* Description, version notes. */
    const float body[3] = {0.93f, 0.93f, 0.90f};
    const float dim[3]  = {0.82f, 0.82f, 0.76f};
    const float bottom  = H - 110.0f;
    auto section = [&](const char *label, const std::string &text, const float c[3], int max_lines) {
        if (text.empty() || y > bottom - 40.0f) return;
        ui_text_px(M, y, 16, label, GOLD_LT[0], GOLD_LT[1], GOLD_LT[2], 0.95f);
        y += 24.0f;
        int avail = (int)((bottom - y) / 28.0f);
        if (avail < 1) return;
        if (max_lines > avail) max_lines = avail;
        y = ui_text_wrap(M, y, W - 2.0f * M, 21, 28.0f, text.c_str(), UI_ALIGN_LEFT, max_lines,
                         c[0], c[1], c[2], 1.0f);
        y += 14.0f;
    };
    section("THE ADVENTURE", g.description, body, 7);
    if (g.sequential) {
        char wk[64];
        snprintf(wk, sizeof(wk), "Week %d of %d of the original broadcast.", idx + 1, (int)g.entries.size());
        section("THIS WEEK", e.version_desc.empty() ? std::string(wk) : e.version_desc, dim, 5);
    } else {
        section("THIS VERSION", e.version_desc, dim, 6);
    }

    /* Spare room: the version's logo as a closing emblem. */
    const std::string &logo = logo_for(g, idx);
    if (bottom - y > 80.0f && !logo.empty() && ui_image_size(logo.c_str(), nullptr, nullptr)) {
        float lh = std::min(bottom - y - 16.0f, 130.0f);
        ui_image_ex(M + 40.0f, y + (bottom - y - lh) * 0.5f, W - 2.0f * M - 80.0f, lh, logo.c_str(),
                    UI_IMG_FIT | UI_IMG_SMOOTH, 1, 1, 1, 0.85f);
    }

    /* Bottom band: platform, barcode, seal. */
    const float by = H - 96.0f;
    ui_rect(0, by, W, 96.0f, pal.banner[0], pal.banner[1], pal.banner[2], 1.0f);
    ui_rect(0, by, W, 5.0f, pal.accent[0], pal.accent[1], pal.accent[2], 1.0f);
    {
        const char *plat = e.platform.empty() ? "Emerald Collection" : e.platform.c_str();
        int px = fit_px(plat, W - 240.0f, 24, 14);
        ui_text_px(M, by + 22.0f, px, plat, pal.text[0], pal.text[1], pal.text[2], 1.0f);
        ui_text_px(M, by + 56.0f, 14, "Emerald Launcher Collection", pal.text[0], pal.text[1], pal.text[2], 0.75f);
        barcode(W - M - 150.0f, by + 16.0f, 150.0f, 64.0f, hash_str(g.key + e.title));
    }
    edge_bevel(W, H, 0.50f);
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
std::deque<std::string> s_queue;        /* atlases, in request order */
std::deque<std::string> s_queue_back;   /* backs: served first        */
unsigned long s_frame = 1;

std::string make_key(const GameGroup &g, int idx, bool back) {
    return g.key + '\x1f' + std::to_string(idx) + (back ? "b" : "a");
}

unsigned request(const GameGroup &g, int idx, bool back) {
    if (idx < 0 || idx >= (int)g.entries.size()) return 0;
    std::string key = make_key(g, idx, back);
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

unsigned build_atlas(const GameGroup &g, int idx) {
    static std::vector<unsigned char> atlas((size_t)BOXART_ATLAS_W * BOXART_H * 4);
    static std::vector<unsigned char> strip((size_t)BOXART_H * BOXART_SPINE_W * 4);
    std::fill(atlas.begin(), atlas.end(), 0);
    std::fill(strip.begin(), strip.end(), 0);

    ui_target_begin(atlas.data(), BOXART_ATLAS_W, BOXART_H);
    ui_set_clip(0, 0, (float)BOXART_FRONT_W, (float)BOXART_H);
    draw_front(atlas.data(), g, idx);
    ui_target_end();

    ui_target_begin(strip.data(), BOXART_H, BOXART_SPINE_W);
    draw_spine(g, idx);
    ui_target_end();

    /* Rotate the strip 90° clockwise into the atlas: the text then runs top
       to bottom with the letters' tops toward the front of the box. */
    for (int s = 0; s < BOXART_H; s++)
        for (int t = 0; t < BOXART_SPINE_W; t++)
            memcpy(&atlas[((size_t)s * BOXART_ATLAS_W + BOXART_FRONT_W + (BOXART_SPINE_W - 1 - t)) * 4],
                   &strip[((size_t)t * BOXART_H + s) * 4], 4);

    return scene3d_texture_rgba(atlas.data(), BOXART_ATLAS_W, BOXART_H, true);
}

unsigned build_back(const GameGroup &g, int idx) {
    std::vector<unsigned char> buf((size_t)BACK_W * BACK_H * 4, 0);
    ui_target_begin(buf.data(), BACK_W, BACK_H);
    draw_back(buf.data(), g, idx);
    ui_target_end();
    return scene3d_texture_rgba(buf.data(), BACK_W, BACK_H, true);
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
        item.tex = item.back ? build_back(*item.g, item.idx) : build_atlas(*item.g, item.idx);
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
    s_queue.clear();
    s_queue_back.clear();
}

void boxart_platform_color(const std::string &platform, float rgb[3]) {
    const Palette &p = palette_for(platform);
    rgb[0] = p.banner[0]; rgb[1] = p.banner[1]; rgb[2] = p.banner[2];
}
