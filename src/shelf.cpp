#include "shelf.h"
#include "boxart.h"
#include "scene3d.h"
#include "ui.h"
#include "sfx.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

const float PI  = 3.14159265f;
const float DEG = PI / 180.0f;

/* Stage: boxes stand on the floor, centered at y = 0. */
const float FLOOR_Y = -BOX_H * 0.5f;

/* Shelf (cover-flow) layout */
const float Z0  = 0.55f;          /* focused game pulled forward        */
const float X1  = 1.30f;          /* first neighbor                     */
const float XS  = 0.50f;          /* further neighbors                  */
const float Z1  = -0.35f;
const float ZS  = 0.16f;
const float ANG = 62.0f * DEG;    /* neighbors turned toward the center */
const int   MAXVIS = 5;           /* offsets laid out on each side      */
const int   WRAP_MIN = 12;        /* shorter lists "rewind" instead of wrapping visually */

/* Version stacks: each extra version sits behind, a little to the right,
   tilted like a fanned hand of cards. */
const int   MAX_LAYERS = 4;
const float LDZ  = 0.25f;
const float LDX  = 0.12f;
const float ROLL = 3.6f * DEG;

/* Versions: up to FAN_STATIC versions are all laid out side by side (the
   focused one steps forward); longer lists scroll as a fan. */
const int   FAN_STATIC = 4;
const float VZ0 = 0.80f, VX1 = 1.48f, VXS = 1.02f, VZ1 = -0.05f, VZS = 0.28f, VANG = 34.0f * DEG;

/* Inspect: the box is lifted toward the camera */
const float IZ = 1.85f, IY = 0.10f;

/* Info panel */
const float INFO_Y = 500.0f;

const float OFF_R = 0.86f, OFF_G = 0.88f, OFF_B = 0.84f;   /* off-white body text */

inline float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
inline float ease(float t) { t = clamp01(t); return t * t * (3.0f - 2.0f * t); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline float approach(float cur, float target, float rate, float dt) {
    return target + (cur - target) * expf(-rate * dt);
}
inline int mod(int a, int n) { int r = a % n; return r < 0 ? r + n : r; }

float line_h(int px) { return (float)ui_text_line_height(px); }

/* Text with a soft drop shadow (readable over the 3D stage). */
void text_shadow(float x, float y, int px, const char *s, float r, float g, float b, float a) {
    ui_text_px(x + 2.0f, y + 2.0f, px, s, 0.0f, 0.0f, 0.0f, 0.65f * a);
    ui_text_px(x, y, px, s, r, g, b, a);
}

void text_shadow_center(float cx, float y, int px, const char *s, float r, float g, float b, float a) {
    text_shadow(cx - (float)ui_text_width(px, s) * 0.5f, y, px, s, r, g, b, a);
}

int fit_px(const char *s, float max_w, int hi, int lo) {
    for (int px = hi; px > lo; px -= 2)
        if ((float)ui_text_width(px, s) <= max_w) return px;
    return lo;
}

void diamond(float cx, float cy, float s, float r, float g, float b, float a) {
    ui_triangle(cx, cy - s, cx - s, cy, cx + s, cy, r, g, b, a);
    ui_triangle(cx - s, cy, cx, cy + s, cx + s, cy, r, g, b, a);
}

/* "1986  <>  NES / SNES": parts separated by small gold diamonds. */
void meta_line(float x, float y, int px, const std::vector<std::string> &parts, bool center,
               float r, float g, float b, float a) {
    const float gap = 26.0f;
    float total = 0.0f;
    for (size_t i = 0; i < parts.size(); i++)
        total += (float)ui_text_width(px, parts[i].c_str()) + (i + 1 < parts.size() ? gap : 0.0f);
    if (center) x -= total * 0.5f;
    for (size_t i = 0; i < parts.size(); i++) {
        text_shadow(x, y, px, parts[i].c_str(), r, g, b, a);
        x += (float)ui_text_width(px, parts[i].c_str());
        if (i + 1 < parts.size()) {
            diamond(x + gap * 0.5f, y + line_h(px) * 0.5f, 3.5f, GOLD_R, GOLD_G, GOLD_B, 0.85f * a);
            x += gap;
        }
    }
}

/* "NES / SNES (Satellaview)": the distinct platforms of a game's versions. */
std::string platforms_of(const GameGroup &g) {
    std::vector<std::string> seen;
    std::string out;
    for (const auto &e : g.entries) {
        if (e.platform.empty() || std::find(seen.begin(), seen.end(), e.platform) != seen.end()) continue;
        seen.push_back(e.platform);
        if (!out.empty()) out += " / ";
        out += e.platform;
    }
    return out;
}

std::vector<const std::string *> shots_of(const GameGroup &g, int j) {
    std::vector<const std::string *> out;
    for (const auto &s : g.entries[(size_t)j].screenshots)
        if (ui_image_size(s.c_str(), nullptr, nullptr)) out.push_back(&s);
    return out;
}

} // namespace

/* ── Setup ─────────────────────────────────────────────────────────────── */
bool Shelf::init(void) {
    m_ready = scene3d_init();
    return m_ready;
}

void Shelf::attach(const std::vector<GameGroup> *groups) {
    m_groups = groups;
    m_front.assign((size_t)count(), 0);
    m_sel = m_target = 0;
    m_scroll = 0.0f;
    m_view = V_SHELF;
    m_mode_t = m_insp_t = 0.0f;
    m_launching = false;
    m_action_sent = false;
    m_action = ShelfAction{};
    boxart_release();
}

void Shelf::select(int g) {
    int n = count();
    if (n == 0) return;
    g = std::max(0, std::min(g, n - 1));
    m_sel = m_target = g;
    m_scroll = (float)g;
    info_changed();
}

void Shelf::set_front(int g, int entry) {
    if (g < 0 || g >= (int)m_front.size()) return;
    m_front[(size_t)g] = entry;
}

int Shelf::front(int g) const {
    const GameGroup &grp = group(g);
    int E = (int)grp.entries.size();
    int f = grp.sequential ? db_progress_get(grp.key)
                           : (g < (int)m_front.size() ? m_front[(size_t)g] : 0);
    return std::max(0, std::min(f, E - 1));
}

bool Shelf::wraps(void) const { return count() >= WRAP_MIN; }

int Shelf::layers(int g) const {
    return std::min((int)group(g).entries.size(), MAX_LAYERS);
}

/* ── Poses ─────────────────────────────────────────────────────────────── */
Shelf::Pose Shelf::mix(const Pose &a, const Pose &b, float t) {
    Pose p;
    p.x = lerpf(a.x, b.x, t);           p.y = lerpf(a.y, b.y, t);
    p.z = lerpf(a.z, b.z, t);           p.yaw = lerpf(a.yaw, b.yaw, t);
    p.pitch = lerpf(a.pitch, b.pitch, t); p.roll = lerpf(a.roll, b.roll, t);
    p.scale = lerpf(a.scale, b.scale, t); p.bright = lerpf(a.bright, b.bright, t);
    return p;
}

Mat4 Shelf::matrix(const Pose &p) {
    Mat4 m = m4_mul(m4_translate(p.x, p.y, p.z), m4_rot_y(p.yaw));
    if (p.pitch != 0.0f) m = m4_mul(m, m4_rot_x(p.pitch));
    if (p.roll != 0.0f)  m = m4_mul(m, m4_rot_z(p.roll));
    if (p.scale != 1.0f) m = m4_mul(m, m4_scale(p.scale, p.scale, p.scale));
    return m;
}

/* Slots for a shelf focused exactly on integer position F. Stacks take room:
   each group's extra layers push the groups beyond it further out. */
void Shelf::layout_at(int F, std::vector<Slot> &out) const {
    const int n = count();
    out.assign((size_t)n, Slot{false, 0, 0.0f, 0.0f, 0.0f, 0.0f});
    if (n == 0) return;
    const bool wrap = wraps();
    auto group_at = [&](int k) -> int {
        int idx = F + k;
        if (wrap) return mod(idx, n);
        return (idx >= 0 && idx < n) ? idx : -1;
    };
    const float stack_dx = LDZ * sinf(ANG) + LDX * cosf(ANG);

    int c = group_at(0);
    if (c < 0) return;
    out[(size_t)c] = Slot{true, 0, 0.0f, Z0, 0.0f, 1.0f};

    for (int side = -1; side <= 1; side += 2) {
        float x = X1 + (side > 0 ? (float)(layers(c) - 1) * 0.10f : 0.0f);
        int   lim = wrap ? (side > 0 ? n / 2 : (n - 1) / 2) : MAXVIS + 1;
        lim = std::min(lim, MAXVIS + 1);
        for (int k = 1; k <= lim; k++) {
            int g = group_at(side * k);
            if (g < 0) break;
            float bright = std::max(0.42f, 0.84f - 0.08f * (float)(k - 1));
            out[(size_t)g] = Slot{true, side * k, (float)side * x, Z1 - (float)(k - 1) * ZS,
                                  -(float)side * ANG, bright};
            x += XS + (float)(layers(g) - 1) * stack_dx;
        }
    }
}

/* Center pose of stack layer `layer` of a group standing in slot s, turned
   by `turn` (peek / idle sway) around the group's vertical axis. */
Shelf::Pose Shelf::stack_pose(const Slot &s, int layer, float turn) const {
    const float l = (float)layer;
    const float r = -l * ROLL;                             /* clockwise fan         */
    const float px = BOX_W * 0.5f, py = -BOX_H * 0.5f;     /* pivot: bottom-right   */
    const float cr = cosf(r), sr = sinf(r);
    const float lx = l * LDX + px - (cr * px - sr * py);
    const float ly = py - (sr * px + cr * py);
    const float lz = -l * LDZ;
    const float yaw = s.yaw + turn;
    const float cy = cosf(yaw), sy = sinf(yaw);
    Pose p;
    p.x = s.x + lx * cy + lz * sy;
    p.y = ly;
    p.z = s.z - lx * sy + lz * cy;
    p.yaw = yaw;
    p.pitch = 0.0f;
    p.roll = r;
    p.scale = 1.0f;
    p.bright = s.bright * (1.0f - 0.12f * l);
    return p;
}

Shelf::Pose Shelf::version_pose(int j) const {
    const int   E = (int)group(m_vgroup).entries.size();
    const float e = (float)j - m_vscroll;
    const float a = fabsf(e), s = e < 0.0f ? -1.0f : 1.0f;
    const float te = ease(std::min(a, 1.0f));
    const float far = std::max(a - 1.0f, 0.0f);
    const float focus = 1.0f - std::min(a, 1.0f);
    const float turn = (m_peek + 0.045f * sinf(m_time * 0.9f)) * focus;
    Pose p;
    if (E <= FAN_STATIC) {
        const float spacing = E <= 2 ? 1.60f : (E == 3 ? 1.50f : 1.30f);
        const float x = ((float)j - (float)(E - 1) * 0.5f) * spacing;
        const float f = ease(focus);
        p.x = x * (1.0f - 0.22f * f);                 /* stepping forward, toward the middle */
        p.y = 0.0f;
        p.z = -0.10f + 0.80f * f;
        p.yaw = -std::max(-1.0f, std::min(1.0f, e)) * 16.0f * DEG + turn;
        p.pitch = 0.0f;
        p.roll = 0.0f;
        p.scale = 1.0f;
        p.bright = 0.70f + 0.30f * f;
        return p;
    }
    p.x = s * (te * VX1 + far * VXS);
    p.y = 0.0f;
    p.z = VZ0 + (VZ1 - VZ0) * te - far * VZS;
    p.yaw = -s * VANG * te + turn;
    p.pitch = 0.0f;
    p.roll = 0.0f;
    p.scale = 1.0f;
    p.bright = 1.0f - 0.26f * te - 0.10f * far;
    return p;
}

/* ── Camera ────────────────────────────────────────────────────────────── */
void Shelf::setup_camera(void) {
    m_eye[0] = 0.0f; m_eye[1] = 0.34f; m_eye[2] = 5.6f;
    m_view_m = m4_look_at(m_eye[0], m_eye[1], m_eye[2], 0.0f, 0.02f, 0.0f, 0.0f, 1.0f, 0.0f);
    /* Lens shift: the stage sits in the upper part of the screen, leaving
       room for the info panel below without tilting the camera. */
    m_proj_m = m4_mul(m4_translate(0.0f, 0.21f, 0.0f),
                      m4_perspective(30.0f * DEG, UI_W / UI_H, 0.1f, 60.0f));
    m_vp = m4_mul(m_proj_m, m_view_m);
}

bool Shelf::project(float x, float y, float z, float &sx, float &sy) const {
    float c[4];
    m4_apply(&m_vp, x, y, z, c);
    if (c[3] <= 0.05f) return false;
    sx = (c[0] / c[3] * 0.5f + 0.5f) * UI_W;
    sy = (0.5f - c[1] / c[3] * 0.5f) * UI_H;
    return true;
}

/* ── Scene assembly ────────────────────────────────────────────────────── */
void Shelf::build(void) {
    m_insts.clear();
    const int n = count();
    if (n == 0) return;
    setup_camera();

    const float mt = ease(m_mode_t), it = ease(m_insp_t);
    const bool ver_focus = (m_view == V_VERSIONS) || (m_view == V_INSPECT && m_insp_from == V_VERSIONS);

    int   F0 = (int)floorf(m_scroll);
    float t  = m_scroll - (float)F0;
    int   F1 = F0 + 1;
    if (!wraps()) {
        F0 = std::max(0, std::min(F0, n - 1));
        F1 = std::max(0, std::min(F1, n - 1));
    }
    layout_at(F0, m_slots0);
    layout_at(F1, m_slots1);

    const float sway  = 0.045f * sinf(m_time * 0.9f);
    const float bob   = 0.010f * sinf(m_time * 1.4f);
    const float shake = m_shake > 0.0f ? sinf(m_shake * 60.0f) * 0.06f * (m_shake / 0.35f) : 0.0f;

    for (int g = 0; g < n; g++) {
        const Slot &a = m_slots0[(size_t)g], &b = m_slots1[(size_t)g];
        Slot  s;
        float kc;
        if (a.valid && b.valid) {
            if (std::abs(a.k - b.k) > 1) continue;                 /* wrapped around the back */
            s = a;
            s.x = lerpf(a.x, b.x, t);       s.z = lerpf(a.z, b.z, t);
            s.yaw = lerpf(a.yaw, b.yaw, t); s.bright = lerpf(a.bright, b.bright, t);
            kc = lerpf((float)a.k, (float)b.k, t);
        } else if (a.valid) { s = a; kc = (float)a.k; }
        else if (b.valid)   { s = b; kc = (float)b.k; }
        else continue;

        const float fw   = std::max(0.0f, 1.0f - fabsf(kc));      /* 1 = at the center */
        const float turn = (m_peek + sway) * fw * (1.0f - mt);
        const GameGroup &grp = group(g);
        const int  E  = (int)grp.entries.size();
        const int  fr = front(g);
        const bool vg = (g == m_vgroup) && mt > 0.001f;

        for (int j = 0; j < E; j++) {
            const int l = (j - fr + E) % E;
            if (l >= MAX_LAYERS && !vg) continue;
            Pose p = stack_pose(s, std::min(l, MAX_LAYERS - 1), turn);
            p.y += bob * fw * (1.0f - mt);
            if (mt > 0.001f) {
                if (g == m_vgroup) {
                    p = mix(p, version_pose(j), mt);
                } else {
                    Pose q = p;
                    q.z -= 5.5f; q.x *= 1.3f; q.bright *= 0.10f;
                    p = mix(p, q, mt);
                }
            }
            const bool locked = grp.sequential && !db_entry_unlocked(grp, j);
            if (locked) p.bright *= 0.42f;
            const bool focus = ver_focus ? (g == m_vgroup && j == m_ver) : (g == m_sel && l == 0);
            if (it > 0.001f) {
                if (g == m_igroup && j == m_ientry) {
                    Pose q{0.0f, IY + bob, IZ, m_insp_yaw, m_insp_pitch, 0.0f, 1.0f, 1.0f};
                    p = mix(p, q, it);
                } else {
                    p.bright *= 1.0f - 0.72f * it;
                }
            }
            if (focus) p.x += shake;
            if (m_launching && g == m_lgroup && j == m_lentry) {
                float lt = m_launch_t * m_launch_t;                    /* ease-in */
                Pose q = p;
                q.x = 0.0f; q.y = 0.05f; q.z = 3.9f; q.pitch = 0.0f; q.roll = 0.0f;
                q.yaw = p.yaw * 0.25f; q.bright = 1.15f;
                p = mix(p, q, lt);
            }
            Inst in;
            in.g = g; in.j = j; in.layer = l; in.kc = kc; in.p = p;
            in.focus = focus; in.locked = locked;
            in.atlas = in.back = 0;
            m_insts.push_back(in);
        }
    }

    /* Matrices, view depth and projected screen rects. */
    for (auto &in : m_insts) {
        in.model = matrix(in.p);
        float c[4];
        m4_apply(&m_view_m, in.p.x, in.p.y, in.p.z, c);
        in.depth = c[2];
        float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f, miny = 1e9f;
        bool ok = true;
        for (int k = 0; k < 8; k++) {
            float w[4];
            m4_apply(&in.model, ((k & 1) ? 0.5f : -0.5f) * BOX_W, ((k & 2) ? 0.5f : -0.5f) * BOX_H,
                     ((k & 4) ? 0.5f : -0.5f) * BOX_D, w);
            miny = std::min(miny, w[1]);
            float sx, sy;
            if (!project(w[0], w[1], w[2], sx, sy)) { ok = false; continue; }
            x0 = std::min(x0, sx); x1 = std::max(x1, sx);
            y0 = std::min(y0, sy); y1 = std::max(y1, sy);
        }
        in.lift = std::max(0.0f, miny - FLOOR_Y);
        in.on_screen = ok && x1 > 0.0f && x0 < UI_W && y1 > 0.0f && y0 < UI_H;
        in.sx0 = x0; in.sy0 = y0; in.sx1 = x1; in.sy1 = y1;
    }

    /* Texture requests, focus first then outward, so art fills in center-out. */
    std::vector<size_t> order(m_insts.size());
    for (size_t i = 0; i < order.size(); i++) order[i] = i;
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        const Inst &A = m_insts[a], &B = m_insts[b];
        if (A.focus != B.focus) return A.focus;
        return fabsf(A.kc) * 4.0f + (float)A.layer < fabsf(B.kc) * 4.0f + (float)B.layer;
    });
    for (size_t i : order) {
        Inst &in = m_insts[i];
        const GameGroup &grp = group(in.g);
        in.atlas = boxart_atlas(grp, in.j);
        if (it > 0.15f && in.g == m_igroup && in.j == m_ientry) in.back = boxart_back(grp, in.j);
    }

    /* Far to near. */
    std::sort(m_insts.begin(), m_insts.end(),
              [](const Inst &a, const Inst &b) { return a.depth < b.depth; });
}

void Shelf::draw_scene(void) {
    if (!m_ready) return;
    build();

    scene3d_begin();
    ui_draw_bg();

    /* A soft pool of light follows the focused box. */
    float fx = 0.5f, fy = 0.6f;
    for (const auto &in : m_insts)
        if (in.focus && in.on_screen) {
            fx = (in.sx0 + in.sx1) * 0.5f / UI_W;
            fy = 1.0f - (in.sy0 + in.sy1) * 0.5f / UI_H;
            break;
        }
    const float spot[3] = {0.20f, 0.52f, 0.32f};
    scene3d_draw_backdrop(fx, fy, 0.30f, 0.46f, spot, 0.40f, 0.75f);
    const float floor_rgb[3] = {0.004f, 0.022f, 0.015f};
    scene3d_draw_floor(FLOOR_Y, floor_rgb, 0.80f);

    scene3d_set_camera(&m_view_m, &m_proj_m, m_eye);
    auto material = [&](const Inst &in) {
        BoxMaterial m;
        m.atlas = in.atlas;
        m.back  = in.back;
        boxart_platform_color(group(in.g).entries[(size_t)in.j].platform, m.color);
        m.brightness = in.p.bright;
        m.spec = 0.32f;
        return m;
    };
    const float it = ease(m_insp_t);
    auto held = [&](const Inst &in) { return in.g == m_igroup && in.j == m_ientry ? it : 0.0f; };
    for (const auto &in : m_insts) {
        float s = 0.30f * clamp01(1.0f - in.lift * 2.0f) * std::min(1.0f, in.p.bright) * (1.0f - held(in));
        if (s < 0.01f) continue;
        BoxMaterial m = material(in);
        scene3d_draw_box_reflection(&in.model, &m, FLOOR_Y, s, 0.60f);
    }
    for (const auto &in : m_insts) {
        float a = 0.60f * clamp01(1.0f - in.lift * 2.5f) * std::min(1.0f, in.p.bright * 1.5f)
                * (1.0f - held(in));
        scene3d_draw_shadow(in.p.x, in.p.z, in.p.yaw, FLOOR_Y, BOX_W * 0.62f, BOX_D * 1.7f, a);
    }
    for (const auto &in : m_insts) {
        BoxMaterial m = material(in);
        scene3d_draw_box(&in.model, &m);
    }
    if (!m_launching) {
        const float gold[3] = {0.95f, 0.70f, 0.18f};
        for (const auto &in : m_insts)
            if (in.focus)
                scene3d_draw_box_glow(&in.model, gold, (0.50f + 0.16f * sinf(m_time * 2.4f)) * (1.0f - it), 0.11f);
    }
    scene3d_end();

    boxart_pump(6.0);
}

/* ── 2D overlay ────────────────────────────────────────────────────────── */
void Shelf::draw_overlay(PadStyle style, const std::string &app_name, bool modal_open) {
    if (count() == 0) return;
    const float mt = ease(m_mode_t), it = ease(m_insp_t);
    const float a_shelf = (1.0f - mt) * (1.0f - it);
    const float a_ver   = mt * (1.0f - it);
    const float info    = ease(m_info_alpha);

    /* Readability band behind the info text. */
    ui_gradient_v(0.0f, 450.0f, UI_W, UI_H - HINT_BAR_H - 450.0f,
                  PANEL_R, PANEL_G, PANEL_B, 0.0f, PANEL_R, PANEL_G, PANEL_B, 0.90f * (1.0f - 0.8f * it));

    draw_box_labels(a_shelf, a_ver);
    if (a_shelf > 0.01f) draw_shelf_info(a_shelf * info);
    if (a_ver > 0.01f)   draw_versions_info(a_ver * info);
    if (it > 0.01f)      draw_inspect_info(it);
    draw_header(app_name, a_shelf, a_ver, it);
    if (!modal_open) draw_hints(style);
}

void Shelf::draw_fade(void) {
    if (m_fade > 0.003f) ui_rect(0.0f, 0.0f, UI_W, UI_H, 0.0f, 0.0f, 0.0f, std::min(1.0f, m_fade));
}

void Shelf::draw_shelf_info(float a) {
    const GameGroup &g = group(m_sel);
    const int E = (int)g.entries.size();
    float y = INFO_Y + 6.0f;

    int tpx = fit_px(g.title.c_str(), 1120.0f, 40, 26);
    text_shadow_center(UI_W * 0.5f, y, tpx, g.title.c_str(), GOLD_R, GOLD_G, GOLD_B, a);
    y += line_h(tpx) + 6.0f;

    std::vector<std::string> meta;
    if (g.year > 0) meta.push_back(std::to_string(g.year));
    std::string plats = platforms_of(g);
    if (!plats.empty()) meta.push_back(plats);
    if (!meta.empty()) meta_line(UI_W * 0.5f, y, 18, meta, true, 0.80f, 0.82f, 0.78f, a);
    y += 30.0f;

    /* The answer to "does this game have other versions?", right here. */
    char label[32];
    if (g.sequential)  snprintf(label, sizeof(label), "%d WEEKS", E);
    else if (E > 1)    snprintf(label, sizeof(label), "%d VERSIONS", E);
    else               snprintf(label, sizeof(label), "ONE VERSION");
    const int cpx = 16;
    const float label_w = (float)ui_text_width(15, label) + 14.0f;
    float total = label_w;
    int shown = 0;
    const float more_w = uikit_chip_width("+99", cpx) + 8.0f;
    for (int j = 0; j < E; j++) {
        float w = uikit_chip_width(g.entries[(size_t)j].title.c_str(), cpx) + 8.0f;
        if (total + w + (j + 1 < E ? more_w : 0.0f) > 1180.0f) break;
        total += w;
        shown++;
    }
    char more[16] = "";
    if (shown < E) {
        snprintf(more, sizeof(more), "+%d", E - shown);
        total += uikit_chip_width(more, cpx) + 8.0f;
    }
    float x = (UI_W - total) * 0.5f;
    const float chip_h = (float)cpx + 12.0f;
    const bool multi = E > 1 || g.sequential;
    ui_text_px(x, y + (chip_h - line_h(15)) * 0.5f, 15, label,
               multi ? GOLD_R : 0.62f, multi ? GOLD_G : 0.66f, multi ? GOLD_B : 0.60f, a);
    x += label_w;
    const int fr = front(m_sel);
    for (int j = 0; j < shown; j++) {
        const char *t = g.entries[(size_t)j].title.c_str();
        if (g.sequential) {
            bool locked = !db_entry_unlocked(g, j);
            bool done   = j < db_progress_get(g.key);
            float w = uikit_chip(x, y, t, cpx, done, locked ? 0.45f * a : a);
            if (locked) uikit_padlock(x + w - 12.0f, y + chip_h * 0.5f, 16.0f, 0.9f * a);
            x += w + 8.0f;
        } else {
            x += uikit_chip(x, y, t, cpx, multi && j == fr, a) + 8.0f;
        }
    }
    if (more[0]) uikit_chip(x, y, more, cpx, false, 0.8f * a);
    y += chip_h + 12.0f;

    ui_text_wrap(150.0f, y, UI_W - 300.0f, 18, 23.0f, g.description.c_str(), UI_ALIGN_CENTER, 2,
                 OFF_R, OFF_G, OFF_B, a);
}

void Shelf::draw_shots(const GameGroup &g, int j, float x, float y, float w, float h, float a) {
    std::vector<const std::string *> shots = shots_of(g, j);
    if (shots.empty()) {
        ui_round_rect_outline(x, y, w, h, 8.0f, 1.5f, GOLD_R, GOLD_G, GOLD_B, 0.25f * a);
        const char *t = "No screenshots for this version";
        ui_text_px(x + (w - (float)ui_text_width(16, t)) * 0.5f, y + (h - line_h(16)) * 0.5f, 16, t,
                   0.55f, 0.58f, 0.54f, a);
        return;
    }
    const float gap = 12.0f;
    float widths[32];
    float total = 0.0f;
    int n = std::min((int)shots.size(), 32);
    for (int i = 0; i < n; i++) {
        int iw = 4, ih = 3;
        ui_image_size(shots[(size_t)i]->c_str(), &iw, &ih);
        widths[i] = h * (float)iw / (float)std::max(ih, 1);
        total += widths[i] + gap;
    }
    ui_set_clip(x, y - 3.0f, w, h + 6.0f);
    float start;
    int reps;
    if (total - gap <= w) {                 /* all fit: centered, static */
        start = x + (w - (total - gap)) * 0.5f;
        reps = 1;
    } else {                                /* slow marquee */
        start = x - floorf(fmodf(m_shot_scroll, total));
        reps = (int)(w / total) + 2;
    }
    for (int r = 0; r < reps; r++) {
        float cx = start + (float)r * total;
        for (int i = 0; i < n; i++) {
            if (cx + widths[i] >= x && cx < x + w) {
                ui_rect(cx - 2.0f, y - 2.0f, widths[i] + 4.0f, h + 4.0f, GOLD_R, GOLD_G, GOLD_B, 0.70f * a);
                ui_image_ex(cx, y, widths[i], h, shots[(size_t)i]->c_str(), UI_IMG_STRETCH, 1, 1, 1, a);
            }
            cx += widths[i] + gap;
        }
    }
    ui_clear_clip();
}

void Shelf::draw_versions_info(float a) {
    const GameGroup &g = group(m_vgroup);
    const int E = (int)g.entries.size();
    const int j = std::max(0, std::min(m_ver, E - 1));
    const GameEntry &e = g.entries[(size_t)j];
    const float lx = 70.0f, lw = 590.0f;
    float y = INFO_Y - 4.0f;

    text_shadow(lx, y, 18, g.title.c_str(), GOLD_DIM_R + 0.1f, GOLD_DIM_G + 0.1f, GOLD_DIM_B + 0.05f, a);
    y += 26.0f;
    int tpx = fit_px(e.title.c_str(), lw, 36, 22);
    text_shadow(lx, y, tpx, e.title.c_str(), GOLD_R, GOLD_G, GOLD_B, a);
    y += line_h(tpx) + 4.0f;

    std::vector<std::string> meta;
    if (!e.platform.empty()) meta.push_back(e.platform);
    int year = e.year > 0 ? e.year : g.year;
    if (year > 0) meta.push_back(std::to_string(year));
    if (!meta.empty()) meta_line(lx, y, 18, meta, false, 0.80f, 0.82f, 0.78f, a);
    y += 28.0f;

    if (g.sequential) {
        int prog = db_progress_get(g.key);
        char buf[96];
        if (!db_entry_unlocked(g, j)) {
            snprintf(buf, sizeof(buf), "Locked - finish %s first", g.entries[(size_t)(j - 1)].title.c_str());
            uikit_padlock(lx + 8.0f, y + 9.0f, 18.0f, a);
            text_shadow(lx + 24.0f, y, 16, buf, 0.95f, 0.50f, 0.38f, a);
        } else if (j < prog) {
            text_shadow(lx, y, 16, "Completed - you can replay it any time", GOLD_R, GOLD_G, GOLD_B, 0.9f * a);
        } else {
            text_shadow(lx, y, 16, "Current week - your save carries over", 0.62f, 0.95f, 0.66f, a);
        }
        y += 24.0f;
    }

    const std::string &desc = e.version_desc.empty() ? g.description : e.version_desc;
    int lines = (int)((UI_H - HINT_BAR_H - 10.0f - y) / 23.0f);
    if (lines > 0)
        ui_text_wrap(lx, y, lw, 18, 23.0f, desc.c_str(), UI_ALIGN_LEFT, lines, OFF_R, OFF_G, OFF_B, a);

    draw_shots(g, j, 690.0f, INFO_Y + 14.0f, 540.0f, 140.0f, a);
}

void Shelf::draw_inspect_info(float a) {
    const GameGroup &g = group(m_igroup);
    const GameEntry &e = g.entries[(size_t)m_ientry];
    std::vector<std::string> parts;
    parts.push_back(g.title);
    if (g.entries.size() > 1) parts.push_back(e.title);
    float c = cosf(m_insp_yaw);
    parts.push_back(c > 0.35f ? "Front" : (c < -0.35f ? "Back" : "Side"));
    meta_line(UI_W * 0.5f, 646.0f, 20, parts, true, GOLD_R, GOLD_G, GOLD_B, a);
}

void Shelf::draw_box_labels(float a_shelf, float a_ver) {
    for (const auto &in : m_insts) {
        if (!in.on_screen) continue;
        const GameGroup &g = group(in.g);
        const int E = (int)g.entries.size();
        const float cx = (in.sx0 + in.sx1) * 0.5f;

        if (a_shelf > 0.01f && in.layer == 0 && (E > 1 || g.sequential)) {
            float center = clamp01(1.0f - fabsf(in.kc) * 2.0f);
            if (center > 0.01f && in.g == m_sel) {
                /* Sticker on the focused stack: how many versions are inside. */
                char b[24];
                snprintf(b, sizeof(b), g.sequential ? "%d WEEKS" : "%d VERSIONS", E);
                float w = uikit_chip_width(b, 15);
                uikit_chip(in.sx1 - w + 26.0f, in.sy0 - 12.0f, b, 15, true, a_shelf * center);
            }
            if (center < 0.99f) {
                /* Pips under the neighbors: one diamond per version. */
                int np = std::min(E, 6);
                float span = (float)(np - 1) * 11.0f;
                float al = 0.9f * a_shelf * (1.0f - center) * std::min(1.0f, in.p.bright * 1.3f);
                for (int p = 0; p < np; p++)
                    diamond(cx - span * 0.5f + (float)p * 11.0f, in.sy1 + 12.0f, 3.5f,
                            GOLD_R, GOLD_G, GOLD_B, al);
            }
        }

        if (a_ver > 0.01f && in.g == m_vgroup) {
            const GameEntry &e = g.entries[(size_t)in.j];
            float al = a_ver * clamp01(in.p.bright * 1.25f);
            int px = fit_px(e.title.c_str(), std::max(90.0f, in.sx1 - in.sx0 + 40.0f), 16, 12);
            if (!in.focus)
                text_shadow_center(cx, in.sy1 + 8.0f, px, e.title.c_str(), 0.85f, 0.86f, 0.82f, al);
            if (in.locked) uikit_padlock(cx, (in.sy0 + in.sy1) * 0.5f, 46.0f, al);
        }
    }
}

void Shelf::draw_header(const std::string &app_name, float a_shelf, float a_ver, float a_insp) {
    const float top = 1.0f - 0.6f * a_insp;
    diamond(30.0f, 30.0f, 7.0f, GOLD_R, GOLD_G, GOLD_B, top);
    diamond(30.0f, 30.0f, 3.0f, 0.10f, 0.55f, 0.30f, top);
    text_shadow(46.0f, 30.0f - line_h(20) * 0.5f, 20, app_name.c_str(), GOLD_R, GOLD_G, GOLD_B, top);

    char buf[48];
    if (a_shelf > 0.01f) {
        snprintf(buf, sizeof(buf), "%d / %d", m_sel + 1, count());
        float w = (float)ui_text_width(18, buf);
        text_shadow(UI_W - 26.0f - w, 30.0f - line_h(18) * 0.5f, 18, buf, 0.80f, 0.82f, 0.78f, a_shelf);
    }
    if (a_ver > 0.01f) {
        const GameGroup &g = group(m_vgroup);
        snprintf(buf, sizeof(buf), "%s %d / %d", g.sequential ? "Week" : "Version", m_ver + 1,
                 (int)g.entries.size());
        float w = (float)ui_text_width(18, buf);
        text_shadow(UI_W - 26.0f - w, 30.0f - line_h(18) * 0.5f, 18, buf, 0.80f, 0.82f, 0.78f, a_ver);
    }
}

void Shelf::draw_hints(PadStyle style) {
    Hint h[6];
    int n = 0;
    char confirm_lbl[40];
    const char *right;
#ifdef __SWITCH__
    right = "In-game: L3 = launcher";
#else
    right = style == STYLE_KEYBOARD ? "In-game: Esc = launcher" : "In-game: L3 = launcher";
#endif
    if (m_view == V_SHELF) {
        const GameGroup &g = group(m_sel);
        const int E = (int)g.entries.size();
        if (g.sequential)  snprintf(confirm_lbl, sizeof(confirm_lbl), "Weeks (%d)", E);
        else if (E > 1)    snprintf(confirm_lbl, sizeof(confirm_lbl), "Versions (%d)", E);
        else               snprintf(confirm_lbl, sizeof(confirm_lbl), "Play");
        h[n++] = {HB_DPAD_H, "Browse"};
        h[n++] = {HB_CONFIRM, confirm_lbl};
        h[n++] = {HB_INSPECT, "Look at box"};
        h[n++] = {HB_SETTINGS, "Settings"};
#ifndef __SWITCH__
        if (style == STYLE_KEYBOARD) h[n++] = {HB_FULLSCREEN, "Maximize"};
#endif
    } else if (m_view == V_VERSIONS) {
        const GameGroup &g = group(m_vgroup);
        bool locked = g.sequential && !db_entry_unlocked(g, m_ver);
        h[n++] = {HB_DPAD_H, g.sequential ? "Week" : "Version"};
        h[n++] = {HB_CONFIRM, locked ? "Locked" : "Play"};
        h[n++] = {HB_BACK, "Back"};
        h[n++] = {HB_INSPECT, "Look at box"};
        h[n++] = {HB_SETTINGS, "Settings"};
    } else {
        const GameGroup &g = group(m_igroup);
        bool locked = g.sequential && !db_entry_unlocked(g, m_ientry);
        h[n++] = {HB_DPAD_H, "Turn over"};
        h[n++] = {style == STYLE_KEYBOARD ? HB_STICK_R : HB_STICK_R, style == STYLE_KEYBOARD ? "Drag to spin" : "Spin"};
        h[n++] = {HB_CONFIRM, locked ? "Locked" : "Play"};
        h[n++] = {HB_BACK, "Put back"};
    }
    uikit_hint_bar(h, n, style, right);
}

/* ── Input ─────────────────────────────────────────────────────────────── */
void Shelf::nav_group(int d) {
    const int n = count();
    if (n <= 1) { m_shake = 0.3f; return; }
    const int prev = m_sel;
    if (wraps()) {
        m_target += d;
    } else {
        int t = m_target + d;
        if (d == 1 || d == -1) t = mod(t, n);             /* past the end: rewind */
        else t = std::max(0, std::min(t, n - 1));         /* page jumps stop at the ends */
        m_target = t;
    }
    m_sel = mod(m_target, n);
    if (m_sel != prev) { info_changed(); sfx_play_nav(); }
}

void Shelf::nav_version(int d) {
    const int E = (int)group(m_vgroup).entries.size();
    int v = std::max(0, std::min(m_ver + d, E - 1));
    if (v == m_ver) { m_shake = 0.25f; return; }
    m_ver = v;
    info_changed();
    sfx_play_nav();
}

void Shelf::open_versions(void) {
    m_vgroup = m_sel;
    m_ver = front(m_sel);
    m_vscroll = (float)m_ver;
    m_view = V_VERSIONS;
    info_changed();
    sfx_play_confirm();
}

void Shelf::open_inspect(void) {
    m_insp_from = m_view;
    if (m_view == V_VERSIONS) { m_igroup = m_vgroup; m_ientry = m_ver; }
    else                      { m_igroup = m_sel;    m_ientry = front(m_sel); }
    m_view = V_INSPECT;
    m_insp_yaw = 0.0f;
    m_insp_yaw_target = PI;       /* turn it over: the back has the details */
    m_insp_pitch = 0.0f;
    sfx_play_confirm();
}

void Shelf::close_inspect(void) {
    /* Unwind whole turns so the box doesn't spin on its way back. */
    float turns = roundf(m_insp_yaw / (2.0f * PI));
    m_insp_yaw -= turns * 2.0f * PI;
    m_insp_yaw_target = 0.0f;
    m_view = m_insp_from;
    sfx_play_back();
}

void Shelf::start_launch(int g, int j) {
    m_launching = true;
    m_launch_t = 0.0f;
    m_lgroup = g;
    m_lentry = j;
    m_action_sent = false;
    sfx_play_enter_game();
}

void Shelf::confirm(void) {
    if (m_view == V_SHELF) {
        const GameGroup &g = group(m_sel);
        if (g.entries.size() > 1 || g.sequential) open_versions();
        else start_launch(m_sel, 0);
        return;
    }
    int g = m_view == V_VERSIONS ? m_vgroup : m_igroup;
    int j = m_view == V_VERSIONS ? m_ver : m_ientry;
    if (!db_entry_unlocked(group(g), j)) { m_shake = 0.35f; sfx_play_back(); return; }
    start_launch(g, j);
}

void Shelf::back(void) {
    if (m_view == V_INSPECT) close_inspect();
    else if (m_view == V_VERSIONS) { m_view = V_SHELF; info_changed(); sfx_play_back(); }
}

void Shelf::input(UiInput in) {
    if (count() == 0 || m_launching) return;
    switch (m_view) {
    case V_SHELF:
        switch (in) {
        case IN_LEFT:  case IN_UP:   nav_group(-1); break;
        case IN_RIGHT: case IN_DOWN: nav_group(+1); break;
        case IN_PAGE_PREV: nav_group(-5); break;
        case IN_PAGE_NEXT: nav_group(+5); break;
        case IN_CONFIRM:  confirm(); break;
        case IN_INSPECT:  open_inspect(); break;
        case IN_SETTINGS: m_action = ShelfAction{ShelfAction::SETTINGS, m_sel, -1}; break;
        default: break;
        }
        break;
    case V_VERSIONS:
        switch (in) {
        case IN_LEFT:  case IN_UP:   case IN_PAGE_PREV: nav_version(-1); break;
        case IN_RIGHT: case IN_DOWN: case IN_PAGE_NEXT: nav_version(+1); break;
        case IN_CONFIRM:  confirm(); break;
        case IN_BACK:     back(); break;
        case IN_INSPECT:  open_inspect(); break;
        case IN_SETTINGS: m_action = ShelfAction{ShelfAction::SETTINGS, m_vgroup, -1}; break;
        }
        break;
    case V_INSPECT:
        switch (in) {
        case IN_LEFT:  m_insp_yaw_target -= PI; sfx_play_nav(); break;
        case IN_RIGHT: m_insp_yaw_target += PI; sfx_play_nav(); break;
        case IN_CONFIRM: confirm(); break;
        case IN_BACK: case IN_INSPECT: close_inspect(); break;
        case IN_SETTINGS: m_action = ShelfAction{ShelfAction::SETTINGS, m_igroup, -1}; break;
        default: break;
        }
        break;
    }
}

void Shelf::set_right_stick(float x, float y) {
    m_stick_x = x;
    m_stick_y = y;
}

const Shelf::Inst *Shelf::pick(float x, float y) const {
    for (size_t i = m_insts.size(); i-- > 0;) {        /* nearest first */
        const Inst &in = m_insts[i];
        if (!in.on_screen || in.p.bright < 0.2f) continue;
        if (x >= in.sx0 && x <= in.sx1 && y >= in.sy0 && y <= in.sy1) return &in;
    }
    return nullptr;
}

void Shelf::mouse_button(float x, float y, int button, bool down) {
    if (m_launching || count() == 0) return;
    if (button == 3) { if (down) back(); return; }          /* right click */
    if (button != 1) return;
    if (down) {
        m_drag = true;
        m_drag_x0 = m_drag_last_x = x;
        m_drag_moved = 0.0f;
        return;
    }
    const bool click = m_drag && m_drag_moved < 6.0f;
    m_drag = false;
    m_drag_peek = 0.0f;
    if (!click) return;
    const Inst *hit = pick(x, y);
    if (!hit) return;
    if (m_view == V_SHELF) {
        if (hit->g == m_sel) { confirm(); return; }
        /* Jump straight to the clicked game (relative to where the shelf is
           heading, not where it happens to be mid-scroll). */
        const int n = count();
        if (wraps()) {
            int d = mod(hit->g - m_sel, n);
            if (d > n / 2) d -= n;
            m_target += d;
        } else {
            m_target = hit->g;
        }
        m_sel = mod(m_target, n);
        info_changed();
        sfx_play_nav();
    } else if (m_view == V_VERSIONS && hit->g == m_vgroup) {
        if (hit->j == m_ver) confirm();
        else { m_ver = hit->j; info_changed(); sfx_play_nav(); }
    }
}

void Shelf::mouse_motion(float x, float y) {
    (void)y;
    if (!m_drag) return;
    float dx = x - m_drag_last_x;
    m_drag_last_x = x;
    m_drag_moved += fabsf(dx);
    if (m_view == V_INSPECT) m_insp_yaw_target += dx * 0.012f;
    else m_drag_peek = std::max(-1.3f, std::min(1.3f, (x - m_drag_x0) * 0.008f));
}

void Shelf::mouse_wheel(int dy) {
    if (m_launching || dy == 0 || count() == 0) return;
    if (m_view == V_INSPECT) { m_insp_yaw_target += dy > 0 ? -PI * 0.25f : PI * 0.25f; return; }
    input(dy > 0 ? IN_LEFT : IN_RIGHT);
}

/* ── Frame update / lifecycle ──────────────────────────────────────────── */
void Shelf::update(float dt) {
    if (dt > 0.1f) dt = 0.1f;
    m_time += dt;
    const int n = count();
    if (n == 0) return;

    m_scroll = approach(m_scroll, (float)m_target, 11.0f, dt);
    if (fabsf(m_scroll - (float)m_target) < 0.0005f) m_scroll = (float)m_target;
    if (wraps() && (m_target > 100 * n || m_target < -100 * n)) {   /* keep numbers small */
        int shift = m_target - mod(m_target, n);
        m_target -= shift;
        m_scroll -= (float)shift;
    }
    m_vscroll = approach(m_vscroll, (float)m_ver, 12.0f, dt);
    if (fabsf(m_vscroll - (float)m_ver) < 0.0005f) m_vscroll = (float)m_ver;

    const bool in_versions = m_view == V_VERSIONS || (m_view == V_INSPECT && m_insp_from == V_VERSIONS);
    m_mode_t = approach(m_mode_t, in_versions ? 1.0f : 0.0f, 9.0f, dt);
    m_insp_t = approach(m_insp_t, m_view == V_INSPECT ? 1.0f : 0.0f, 8.0f, dt);
    if (m_view == V_INSPECT) m_insp_yaw_target += m_stick_x * 3.0f * dt;
    m_insp_yaw   = approach(m_insp_yaw, m_insp_yaw_target, 7.0f, dt);
    m_insp_pitch = approach(m_insp_pitch, m_view == V_INSPECT ? -m_stick_y * 0.5f : 0.0f, 7.0f, dt);
    m_peek = approach(m_peek, m_view == V_INSPECT ? 0.0f : m_stick_x * 1.15f + m_drag_peek, 7.0f, dt);

    m_info_alpha = std::min(1.0f, m_info_alpha + dt * 4.5f);
    if (m_shake > 0.0f) m_shake = std::max(0.0f, m_shake - dt);
    m_shot_scroll += dt * 40.0f;

    if (m_launching) {
        m_launch_t = std::min(1.0f, m_launch_t + dt / 0.42f);
        m_fade = std::max(m_fade, ease(m_launch_t));
        if (m_launch_t >= 1.0f && !m_action_sent) {
            m_action = ShelfAction{ShelfAction::LAUNCH, m_lgroup, m_lentry};
            m_action_sent = true;
        }
    } else {
        m_fade = std::max(0.0f, m_fade - dt * 2.6f);
    }
}

bool Shelf::poll_action(ShelfAction &out) {
    if (m_action.kind == ShelfAction::NONE) return false;
    out = m_action;
    m_action = ShelfAction{};
    return true;
}

void Shelf::on_launched(void) {
    m_launching = false;
    m_launch_t = 0.0f;
    m_action_sent = false;
    if (m_lgroup >= 0 && m_lgroup < count() && !group(m_lgroup).sequential) set_front(m_lgroup, m_lentry);
    if (m_view == V_INSPECT) {
        m_view = m_insp_from;
        m_insp_t = 0.0f;
        m_insp_yaw = m_insp_yaw_target = 0.0f;
    }
    m_fade = 1.0f;     /* fade back in if the launcher stays visible (RetroArch on PC) */
}

void Shelf::on_resume(void) {
    m_fade = 1.0f;
    m_scroll = (float)m_target;
    m_vscroll = (float)m_ver;
    m_peek = m_stick_x = m_stick_y = 0.0f;
    m_drag = false;
    m_drag_peek = 0.0f;
}

void Shelf::prewarm(double budget_ms) {
    const int n = count();
    if (!m_ready || n == 0) return;
    for (int d = 0; d <= n / 2; d++)
        for (int s = -1; s <= 1; s += 2) {
            if (d == 0 && s > 0) continue;
            int g = mod(m_sel + s * d, n);
            for (size_t j = 0; j < group(g).entries.size(); j++) boxart_atlas(group(g), (int)j);
        }
    boxart_pump(budget_ms);
}

void Shelf::release_gpu(void) {
    scene3d_release_targets();
}
