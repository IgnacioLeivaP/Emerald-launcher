#include "ui.h"
#include "gl.h"
#include "font8x8.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_image.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

/* ── Background shader (tiled image) ─────────────────────────────────── */
static const char *BG_VS =
    GLSL_VERSION
    "out vec2 v_uv;\n"
    "uniform vec2 u_tile;\n"
    "void main(){\n"
    "  const vec2 pos[4]=vec2[](vec2(-1,-1),vec2(-1,1),vec2(1,-1),vec2(1,1));\n"
    "  const vec2 uv[4] =vec2[](vec2(0,1),  vec2(0,0), vec2(1,1), vec2(1,0));\n"
    "  gl_Position=vec4(pos[gl_VertexID],0,1);\n"
    "  v_uv=uv[gl_VertexID]*u_tile;\n"
    "}\n";

static const char *BG_FS =
    GLSL_VERSION
    "in vec2 v_uv;\n"
    "out vec4 frag;\n"
    "uniform sampler2D u_tex;\n"
    "void main(){ frag=texture(u_tex,v_uv); }\n";

static GLuint s_bg_prog    = 0;
static GLuint s_bg_tex     = 0;
static float  s_bg_tile_x  = 1.0f;
static float  s_bg_tile_y  = 1.0f;
static int    s_draw_bg    = 1;

/* ── Canvas shader ────────────────────────────────────────────────────── */
static const char *VS =
    GLSL_VERSION
    "out vec2 v_uv;\n"
    "void main(){\n"
    "  const vec2 pos[4]=vec2[](vec2(-1,-1),vec2(-1,1),vec2(1,-1),vec2(1,1));\n"
    "  const vec2 uv[4] =vec2[](vec2(0,1),  vec2(0,0), vec2(1,1), vec2(1,0));\n"
    "  gl_Position=vec4(pos[gl_VertexID],0,1);\n"
    "  v_uv=uv[gl_VertexID];\n"
    "}\n";

static const char *FS =
    GLSL_VERSION
    "in vec2 v_uv;\n"
    "out vec4 frag;\n"
    "uniform sampler2D u_tex;\n"
    "void main(){ frag=texture(u_tex,v_uv); }\n";

static GLuint s_prog, s_vao, s_tex;
static int    s_sw, s_sh;

#define CANVAS_W 1280
#define CANVAS_H  720
/* Premultiplied RGBA (so partially transparent layers composite correctly). */
static unsigned char s_canvas[CANVAS_H * CANVAS_W * 4];

/* Current draw target: the screen canvas, or an offscreen buffer
   (ui_target_begin). All primitives write through s_px. */
static unsigned char *s_px = s_canvas;
static int s_pw = CANVAS_W, s_ph = CANVAS_H;

/* Clip rectangle — always inside the current target's bounds */
static int s_clip_x0 = 0, s_clip_y0 = 0;
static int s_clip_x1 = CANVAS_W, s_clip_y1 = CANVAS_H;

/* ── Pixel compositing ────────────────────────────────────────────────── */
static inline unsigned mul255(unsigned a, unsigned b) {
    unsigned t = a * b + 128u;
    return (t + (t >> 8)) >> 8;
}

/* "Source over" for a premultiplied source (r,g,b <= a) onto pixel p. */
static inline void blend_at(unsigned char *p, unsigned r, unsigned g,
                            unsigned b, unsigned a) {
    if (a == 0) return;
    if (a >= 255u || p[3] == 0) {
        p[0] = (unsigned char)r; p[1] = (unsigned char)g;
        p[2] = (unsigned char)b; p[3] = (unsigned char)a;
        return;
    }
    unsigned ia = 255u - a;
    p[0] = (unsigned char)(r + mul255(p[0], ia));
    p[1] = (unsigned char)(g + mul255(p[1], ia));
    p[2] = (unsigned char)(b + mul255(p[2], ia));
    p[3] = (unsigned char)(a + mul255(p[3], ia));
}

static inline void put_pixel(int x, int y, unsigned r, unsigned g,
                             unsigned b, unsigned a) {
    if (x < s_clip_x0 || x >= s_clip_x1 || y < s_clip_y0 || y >= s_clip_y1) return;
    blend_at(s_px + ((size_t)y * (size_t)s_pw + (size_t)x) * 4, r, g, b, a);
}

static inline float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

/* Straight color + alpha → premultiplied 0..255 channels. */
typedef struct { unsigned r, g, b, a; } PmColor;
static inline PmColor pm_color(float r, float g, float b, float a) {
    PmColor c;
    float fa = clamp01(a);
    c.a = (unsigned)(fa * 255.0f + 0.5f);
    c.r = (unsigned)(clamp01(r) * (float)c.a + 0.5f);
    c.g = (unsigned)(clamp01(g) * (float)c.a + 0.5f);
    c.b = (unsigned)(clamp01(b) * (float)c.a + 0.5f);
    return c;
}

/* Blend color c scaled by coverage (0..1) at (x,y). */
static inline void put_cov(int x, int y, PmColor c, float cov) {
    if (cov <= 0.0f) return;
    if (cov >= 1.0f) { put_pixel(x, y, c.r, c.g, c.b, c.a); return; }
    unsigned k = (unsigned)(cov * 256.0f);
    put_pixel(x, y, (c.r * k) >> 8, (c.g * k) >> 8, (c.b * k) >> 8, (c.a * k) >> 8);
}

/* Clamp a float rect to the clip region; returns false if empty. */
static bool clip_span(float x, float y, float w, float h,
                      int *x0, int *y0, int *x1, int *y1) {
    int ax = (int)floorf(x),     ay = (int)floorf(y);
    int bx = (int)floorf(x + w), by = (int)floorf(y + h);
    if (ax < s_clip_x0) ax = s_clip_x0;
    if (ay < s_clip_y0) ay = s_clip_y0;
    if (bx > s_clip_x1) bx = s_clip_x1;
    if (by > s_clip_y1) by = s_clip_y1;
    if (ax >= bx || ay >= by) return false;
    *x0 = ax; *y0 = ay; *x1 = bx; *y1 = by;
    return true;
}

/* ── Image cache (pixel-blitted into canvas) ──────────────────────────── */
typedef struct {
    char           path[512];
    unsigned char *pixels;      /* straight RGBA, as decoded */
    int            w, h;
    unsigned long  last_used;   /* LRU tick — 0 means empty slot */
} CachedImage;

/* db.json references ~116 logos+screenshots, far more than fit at once. The
   cache is bounded; when full it evicts the least-recently-used entry instead
   of refusing to load (which silently dropped images past the limit). */
#define MAX_CACHED_IMAGES 64
/* Also bounded in bytes: user logos can be big PNGs, and on the Switch the
   decoded pixels share memory with the running emulator. */
#ifdef __SWITCH__
#  define MAX_CACHED_BYTES (40u * 1024u * 1024u)
#else
#  define MAX_CACHED_BYTES (128u * 1024u * 1024u)
#endif
static CachedImage  s_img_cache[MAX_CACHED_IMAGES];
static int          s_img_count = 0;
static unsigned long s_img_tick  = 0;
static size_t       s_img_bytes = 0;

static void img_free(CachedImage *ci) {
    if (!ci->pixels) return;
    s_img_bytes -= (size_t)ci->w * (size_t)ci->h * 4u;
    free(ci->pixels);
    ci->pixels = NULL;
    ci->path[0] = '\0';
    ci->last_used = 0;
}

/* Least-recently-used loaded entry (NULL if none). */
static CachedImage *img_lru(void) {
    CachedImage *best = NULL;
    for (int i = 0; i < s_img_count; i++)
        if (s_img_cache[i].pixels && (!best || s_img_cache[i].last_used < best->last_used))
            best = &s_img_cache[i];
    return best;
}

/* Paths that failed to load: don't hit the disk (SD card on Switch) again
   every frame for an image that isn't there. */
#define MAX_FAILED_IMAGES 64
static unsigned long long s_failed[MAX_FAILED_IMAGES];
static int s_failed_n = 0, s_failed_next = 0;

static unsigned long long path_hash(const char *s) {
    unsigned long long h = 1469598103934665603ULL;
    while (*s) { h ^= (unsigned char)*s++; h *= 1099511628211ULL; }
    return h;
}

static CachedImage *img_load(const char *path) {
    for (int i = 0; i < s_img_count; i++)
        if (s_img_cache[i].pixels && strcmp(s_img_cache[i].path, path) == 0) {
            s_img_cache[i].last_used = ++s_img_tick;
            return &s_img_cache[i];
        }

    unsigned long long hash = path_hash(path);
    for (int i = 0; i < s_failed_n; i++)
        if (s_failed[i] == hash) return NULL;

    SDL_Surface *surf = IMG_Load(path);
    SDL_Surface *rgba = NULL;
    if (surf) {
        rgba = SDL_ConvertSurfaceFormat(surf, SDL_PIXELFORMAT_ABGR8888, 0);
        SDL_FreeSurface(surf);
    } else {
        fprintf(stderr, "ui_image: %s\n", IMG_GetError());
    }
    if (!rgba) {
        s_failed[s_failed_next] = hash;
        s_failed_next = (s_failed_next + 1) % MAX_FAILED_IMAGES;
        if (s_failed_n < MAX_FAILED_IMAGES) s_failed_n++;
        return NULL;
    }

    /* Make room within the byte budget, then pick a slot: a free one, a new
       one while there's room, else the LRU entry. */
    const size_t need = (size_t)rgba->w * (size_t)rgba->h * 4u;
    while (s_img_bytes + need > MAX_CACHED_BYTES) {
        CachedImage *old = img_lru();
        if (!old) break;
        img_free(old);
    }
    CachedImage *ci = NULL;
    for (int i = 0; i < s_img_count && !ci; i++)
        if (!s_img_cache[i].pixels) ci = &s_img_cache[i];
    if (!ci && s_img_count < MAX_CACHED_IMAGES) ci = &s_img_cache[s_img_count++];
    if (!ci) { ci = img_lru(); img_free(ci); }
    ci->last_used = ++s_img_tick;
    strncpy(ci->path, path, sizeof(ci->path)-1);
    ci->path[sizeof(ci->path)-1] = '\0';
    ci->w = rgba->w; ci->h = rgba->h;
    ci->pixels = (unsigned char*)malloc(need);
    if (!ci->pixels) {            /* invalidate slot (it gets reused first) */
        SDL_FreeSurface(rgba);
        ci->path[0] = '\0';
        ci->last_used = 0;
        return NULL;
    }
    s_img_bytes += need;
    for (int row = 0; row < rgba->h; row++)
        memcpy(ci->pixels + row*rgba->w*4,
               (Uint8*)rgba->pixels + row*rgba->pitch, (size_t)(rgba->w*4));
    SDL_FreeSurface(rgba);
    return ci;
}

/* Premultiplied texel fetch (clamped). */
static inline void texel_pm(const CachedImage *ci, int sx, int sy,
                            unsigned *r, unsigned *g, unsigned *b, unsigned *a) {
    if (sx < 0) sx = 0; else if (sx >= ci->w) sx = ci->w - 1;
    if (sy < 0) sy = 0; else if (sy >= ci->h) sy = ci->h - 1;
    const unsigned char *s = ci->pixels + ((size_t)sy * (size_t)ci->w + (size_t)sx) * 4;
    *a = s[3];
    if (s[3] == 255) { *r = s[0]; *g = s[1]; *b = s[2]; return; }   /* opaque: common case */
    *r = mul255(s[0], s[3]); *g = mul255(s[1], s[3]); *b = mul255(s[2], s[3]);
}

void ui_image_ex(float x, float y, float w, float h, const char *path, int flags,
                 float r, float g, float b, float a) {
    if (!path || !path[0] || w <= 0.0f || h <= 0.0f || a <= 0.0f) return;
    CachedImage *ci = img_load(path);
    if (!ci || ci->w <= 0 || ci->h <= 0) return;

    /* Destination rect (DX,DY,DW,DH) ↔ source rect (SX,SY,SW,SH). */
    float iw = (float)ci->w, ih = (float)ci->h;
    float DX = x, DY = y, DW = w, DH = h;
    float SX = 0.0f, SY = 0.0f, SW = iw, SH = ih;
    int fit = flags & 3;
    if (fit == UI_IMG_FIT) {
        float sc = fminf(w / iw, h / ih);
        DW = iw * sc; DH = ih * sc;
        DX = x + (w - DW) * 0.5f; DY = y + (h - DH) * 0.5f;
    } else if (fit == UI_IMG_COVER) {
        float sc = fmaxf(w / iw, h / ih);
        SW = w / sc; SH = h / sc;
        SX = (iw - SW) * 0.5f; SY = (ih - SH) * 0.5f;
    }

    int x0, y0, x1, y1;
    if (!clip_span(DX, DY, DW, DH, &x0, &y0, &x1, &y1)) return;

    const float kx = SW / DW, ky = SH / DH;      /* source px per dest px */
    const bool smooth = (flags & UI_IMG_SMOOTH) != 0;
    const bool down   = smooth && (kx > 1.0f || ky > 1.0f);
    const bool sil    = (flags & UI_IMG_SILHOUETTE) != 0;
    const unsigned op = (unsigned)(clamp01(a) * 256.0f);
    const unsigned tr = (unsigned)(clamp01(r) * 256.0f);
    const unsigned tg = (unsigned)(clamp01(g) * 256.0f);
    const unsigned tb = (unsigned)(clamp01(b) * 256.0f);
    const PmColor silc = pm_color(r, g, b, 1.0f);

    for (int py = y0; py < y1; py++) {
        float v   = SY + ((float)py + 0.5f - DY) * ky;
        int   sy0 = (int)floorf(SY + ((float)py - DY) * ky);
        int   sy1 = (int)floorf(SY + ((float)py + 1.0f - DY) * ky);
        if (sy1 <= sy0) sy1 = sy0 + 1;
        unsigned char *dst = s_px + ((size_t)py * (size_t)s_pw + (size_t)x0) * 4;
        for (int px = x0; px < x1; px++, dst += 4) {
            float u = SX + ((float)px + 0.5f - DX) * kx;
            unsigned cr, cg, cb, ca;
            if (!smooth) {
                texel_pm(ci, (int)u, (int)v, &cr, &cg, &cb, &ca);
            } else if (down) {
                /* Box filter over the source footprint of this pixel. */
                int sx0 = (int)floorf(SX + ((float)px - DX) * kx);
                int sx1 = (int)floorf(SX + ((float)px + 1.0f - DX) * kx);
                if (sx1 <= sx0) sx1 = sx0 + 1;
                unsigned ar = 0, ag = 0, ab = 0, aa = 0, n = 0;
                for (int yy = sy0; yy < sy1; yy++)
                    for (int xx = sx0; xx < sx1; xx++) {
                        unsigned tr2, tg2, tb2, ta2;
                        texel_pm(ci, xx, yy, &tr2, &tg2, &tb2, &ta2);
                        ar += tr2; ag += tg2; ab += tb2; aa += ta2; n++;
                    }
                cr = ar / n; cg = ag / n; cb = ab / n; ca = aa / n;
            } else {
                /* Bilinear (upscaling). */
                float fu = u - 0.5f, fv = v - 0.5f;
                int   ix = (int)floorf(fu), iy = (int)floorf(fv);
                unsigned wx = (unsigned)((fu - (float)ix) * 256.0f);
                unsigned wy = (unsigned)((fv - (float)iy) * 256.0f);
                unsigned c[4][4];
                texel_pm(ci, ix,     iy,     &c[0][0], &c[0][1], &c[0][2], &c[0][3]);
                texel_pm(ci, ix + 1, iy,     &c[1][0], &c[1][1], &c[1][2], &c[1][3]);
                texel_pm(ci, ix,     iy + 1, &c[2][0], &c[2][1], &c[2][2], &c[2][3]);
                texel_pm(ci, ix + 1, iy + 1, &c[3][0], &c[3][1], &c[3][2], &c[3][3]);
                unsigned out[4];
                for (int k = 0; k < 4; k++) {
                    unsigned top = (c[0][k] * (256u - wx) + c[1][k] * wx) >> 8;
                    unsigned bot = (c[2][k] * (256u - wx) + c[3][k] * wx) >> 8;
                    out[k] = (top * (256u - wy) + bot * wy) >> 8;
                }
                cr = out[0]; cg = out[1]; cb = out[2]; ca = out[3];
            }
            if (ca == 0) continue;
            if (sil) {
                ca = (ca * op) >> 8;
                blend_at(dst, mul255(silc.r, ca), mul255(silc.g, ca), mul255(silc.b, ca), ca);
            } else {
                blend_at(dst, (((cr * tr) >> 8) * op) >> 8, (((cg * tg) >> 8) * op) >> 8,
                         (((cb * tb) >> 8) * op) >> 8, (ca * op) >> 8);
            }
        }
    }
}

void ui_image(float fx, float fy, float max_w, float max_h, const char *path, float brightness) {
    if (brightness < 0.0f) brightness = 0.0f;
    if (brightness > 1.0f) brightness = 1.0f;
    ui_image_ex(fx, fy, max_w, max_h, path, UI_IMG_FIT, brightness, brightness, brightness, 1.0f);
}

/* ── TTF font cache ────────────────────────────────────────────────────── */
#define NUM_FONT_SIZES 5
static const int FONT_SIZES[NUM_FONT_SIZES] = {16, 20, 24, 32, 40};
static TTF_Font *s_fonts[NUM_FONT_SIZES];
/* Extra sizes opened on demand by ui_text_px (same font data). */
#define MAX_PX_FONTS 16
static struct { int px; TTF_Font *font; } s_pxfonts[MAX_PX_FONTS];
static int       s_pxfont_n     = 0;
static void     *s_font_data    = NULL;
static long      s_font_data_sz = 0;
static int       s_ttf_inited   = 0;

/* Rendered-text cache (see "Text cache" below). */
static void tc_trim(unsigned age);
static void tc_tick(void);

/* ─────────────────────────────────────────────────────────────────────── */

static GLuint compile_shader(GLenum type, const char *src) {
    GLuint s = gl_CreateShader(type);
    gl_ShaderSource(s, 1, &src, NULL);
    gl_CompileShader(s);
    GLint ok; gl_GetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512]; gl_GetShaderInfoLog(s, sizeof(log), NULL, log);
        fprintf(stderr, "ui shader: %s\n", log);
    }
    return s;
}

bool ui_init(int sw, int sh) {
    s_sw = sw; s_sh = sh;
    GLuint vs = compile_shader(GL_VERTEX_SHADER, VS);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, FS);
    s_prog = gl_CreateProgram();
    gl_AttachShader(s_prog, vs);
    gl_AttachShader(s_prog, fs);
    gl_LinkProgram(s_prog);
    gl_DeleteShader(vs); gl_DeleteShader(fs);
    GLint ok = 0;
    gl_GetProgramiv(s_prog, GL_LINK_STATUS, &ok);
    if (!ok) return false;
    gl_UseProgram(s_prog);
    GLint loc = gl_GetUniformLocation(s_prog, "u_tex");
    if (loc >= 0) gl_Uniform1i(loc, 0);
    gl_GenVertexArrays(1, &s_vao);
    glGenTextures(1, &s_tex);
    glBindTexture(GL_TEXTURE_2D, s_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, sw, sh, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, s_canvas);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

void ui_set_draw_bg(bool enable) { s_draw_bg = enable ? 1 : 0; }

bool ui_load_bg(const char *img_path) {
    SDL_Surface *surf = IMG_Load(img_path);
    if (!surf) {
        fprintf(stderr, "ui_load_bg: %s\n", IMG_GetError());
        return false;
    }
    SDL_Surface *rgba = SDL_ConvertSurfaceFormat(surf, SDL_PIXELFORMAT_ABGR8888, 0);
    SDL_FreeSurface(surf);
    if (!rgba) return false;

    /* Calculate tiling so the image fills 1280x720 exactly */
    s_bg_tile_x = (float)CANVAS_W / (float)rgba->w;
    s_bg_tile_y = (float)CANVAS_H / (float)rgba->h;

    glGenTextures(1, &s_bg_tex);
    glBindTexture(GL_TEXTURE_2D, s_bg_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, rgba->w, rgba->h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, rgba->pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glBindTexture(GL_TEXTURE_2D, 0);
    SDL_FreeSurface(rgba);

    GLuint bvs = compile_shader(GL_VERTEX_SHADER, BG_VS);
    GLuint bfs = compile_shader(GL_FRAGMENT_SHADER, BG_FS);
    s_bg_prog = gl_CreateProgram();
    gl_AttachShader(s_bg_prog, bvs);
    gl_AttachShader(s_bg_prog, bfs);
    gl_LinkProgram(s_bg_prog);
    gl_DeleteShader(bvs); gl_DeleteShader(bfs);
    gl_UseProgram(s_bg_prog);
    GLint tloc = gl_GetUniformLocation(s_bg_prog, "u_tex");
    if (tloc >= 0) gl_Uniform1i(tloc, 0);

    return true;
}

bool ui_load_font(const char *ttf_path) {
    if (!s_ttf_inited) {
        if (TTF_Init() < 0) {
            fprintf(stderr, "TTF_Init: %s\n", TTF_GetError());
            return false;
        }
        s_ttf_inited = 1;
    }

    FILE *f = fopen(ttf_path, "rb");
    if (!f) {
        fprintf(stderr, "ui: cannot open font '%s'\n", ttf_path);
        return false;
    }
    fseek(f, 0, SEEK_END);
    s_font_data_sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    s_font_data = s_font_data_sz > 0 ? malloc((size_t)s_font_data_sz) : NULL;
    if (!s_font_data) { fclose(f); return false; }
    size_t got = fread(s_font_data, 1, (size_t)s_font_data_sz, f);
    fclose(f);
    if (got != (size_t)s_font_data_sz) {
        fprintf(stderr, "ui: short read on font '%s'\n", ttf_path);
        free(s_font_data); s_font_data = NULL;
        return false;
    }
    tc_trim(0);

    for (int i = 0; i < NUM_FONT_SIZES; i++) {
        /* Each call creates a new RWops wrapper around the same buffer.
           freesrc=1: SDL_ttf owns and closes the RWops; it copies the font
           data internally so the RWops closure doesn't affect FreeType. */
        SDL_RWops *rw = SDL_RWFromMem(s_font_data, (int)s_font_data_sz);
        s_fonts[i] = TTF_OpenFontRW(rw, 1, FONT_SIZES[i]);
        if (!s_fonts[i])
            fprintf(stderr, "ui: font size %d: %s\n", FONT_SIZES[i], TTF_GetError());
    }

    return s_fonts[0] != NULL;
}

void ui_shutdown(void) {
    for (int i = 0; i < NUM_FONT_SIZES; i++) {
        if (s_fonts[i]) { TTF_CloseFont(s_fonts[i]); s_fonts[i] = NULL; }
    }
    for (int i = 0; i < s_pxfont_n; i++) {
        if (s_pxfonts[i].font) TTF_CloseFont(s_pxfonts[i].font);
        s_pxfonts[i].font = NULL;
    }
    s_pxfont_n = 0;
    tc_trim(0);
    if (s_ttf_inited) { TTF_Quit(); s_ttf_inited = 0; }
    free(s_font_data); s_font_data = NULL;

    for (int i = 0; i < s_img_count; i++) img_free(&s_img_cache[i]);
    s_img_count = 0;
    s_img_bytes = 0;
    if (s_bg_prog) { gl_DeleteProgram(s_bg_prog); s_bg_prog = 0; }
    if (s_bg_tex)  { glDeleteTextures(1, &s_bg_tex); s_bg_tex = 0; }
    if (s_prog) { gl_DeleteProgram(s_prog); s_prog = 0; }
    if (s_vao)  { gl_DeleteVertexArrays(1, &s_vao); s_vao = 0; }
    if (s_tex)  { glDeleteTextures(1, &s_tex); s_tex = 0; }
}

void ui_begin(void) {
    memset(s_canvas, 0, (size_t)(s_sw * s_sh * 4));
    tc_tick();
}

void ui_draw_bg(void) {
    if (!s_bg_tex || !s_bg_prog) return;
    glDisable(GL_BLEND);
    gl_UseProgram(s_bg_prog);
    GLint tloc = gl_GetUniformLocation(s_bg_prog, "u_tile");
    if (tloc >= 0) gl_Uniform2f(tloc, s_bg_tile_x, s_bg_tile_y);
    gl_ActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, s_bg_tex);
    gl_BindVertexArray(s_vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    gl_BindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void ui_end(void) {
    /* 1. Render tiled background (no blend — replaces whatever is behind) */
    if (s_draw_bg) ui_draw_bg();

    /* 2. Upload canvas and blend UI on top (premultiplied alpha) */
    glBindTexture(GL_TEXTURE_2D, s_tex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, s_sw, s_sh,
                    GL_RGBA, GL_UNSIGNED_BYTE, s_canvas);
    glBindTexture(GL_TEXTURE_2D, 0);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    gl_UseProgram(s_prog);
    gl_ActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, s_tex);
    gl_BindVertexArray(s_vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    gl_BindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_BLEND);
}

/* ── Targets and clipping ─────────────────────────────────────────────── */
void ui_target_begin(unsigned char *rgba, int w, int h) {
    s_px = rgba; s_pw = w; s_ph = h;
    ui_clear_clip();
}

void ui_target_end(void) {
    s_px = s_canvas; s_pw = CANVAS_W; s_ph = CANVAS_H;
    ui_clear_clip();
}

void ui_set_clip(float x, float y, float w, float h) {
    s_clip_x0 = (int)x;
    s_clip_y0 = (int)y;
    s_clip_x1 = (int)(x + w);
    s_clip_y1 = (int)(y + h);
    if (s_clip_x0 < 0) s_clip_x0 = 0;
    if (s_clip_y0 < 0) s_clip_y0 = 0;
    if (s_clip_x1 > s_pw) s_clip_x1 = s_pw;
    if (s_clip_y1 > s_ph) s_clip_y1 = s_ph;
}
void ui_clear_clip(void) {
    s_clip_x0 = 0; s_clip_y0 = 0;
    s_clip_x1 = s_pw; s_clip_y1 = s_ph;
}
void ui_image_forget(const char *path) {
    if (!path || !path[0]) return;
    for (int i = 0; i < s_img_count; i++)
        if (s_img_cache[i].pixels && strcmp(s_img_cache[i].path, path) == 0) img_free(&s_img_cache[i]);
    /* It may exist now even if an earlier load failed. */
    unsigned long long hash = path_hash(path);
    for (int i = 0; i < s_failed_n; i++)
        if (s_failed[i] == hash) s_failed[i] = 0;
}

bool ui_image_size(const char *path, int *out_w, int *out_h) {
    if (!path || !path[0]) return false;
    CachedImage *ci = img_load(path);
    if (!ci) return false;
    if (out_w) *out_w = ci->w;
    if (out_h) *out_h = ci->h;
    return true;
}

/* ── Shapes ───────────────────────────────────────────────────────────── */
void ui_rect(float x, float y, float w, float h,
             float r, float g, float b, float a) {
    PmColor c = pm_color(r, g, b, a);
    if (c.a == 0) return;
    int x0, y0, x1, y1;
    if (!clip_span(x, y, w, h, &x0, &y0, &x1, &y1)) return;
    for (int py = y0; py < y1; py++) {
        unsigned char *p = s_px + ((size_t)py * (size_t)s_pw + (size_t)x0) * 4;
        if (c.a == 255) {
            for (int px = x0; px < x1; px++, p += 4) {
                p[0] = (unsigned char)c.r; p[1] = (unsigned char)c.g;
                p[2] = (unsigned char)c.b; p[3] = 255;
            }
        } else {
            for (int px = x0; px < x1; px++, p += 4) blend_at(p, c.r, c.g, c.b, c.a);
        }
    }
}

void ui_gradient_v(float x, float y, float w, float h,
                   float r0, float g0, float b0, float a0,
                   float r1, float g1, float b1, float a1) {
    int x0, y0, x1, y1;
    if (h <= 0.0f || !clip_span(x, y, w, h, &x0, &y0, &x1, &y1)) return;
    for (int py = y0; py < y1; py++) {
        float t = clamp01(((float)py + 0.5f - y) / h);
        PmColor c = pm_color(r0 + (r1 - r0) * t, g0 + (g1 - g0) * t,
                             b0 + (b1 - b0) * t, a0 + (a1 - a0) * t);
        if (c.a == 0) continue;
        unsigned char *p = s_px + ((size_t)py * (size_t)s_pw + (size_t)x0) * 4;
        for (int px = x0; px < x1; px++, p += 4) blend_at(p, c.r, c.g, c.b, c.a);
    }
}

/* Signed distance from a pixel center to a rounded rect (negative inside). */
static inline float rrect_dist(float fx, float fy, float cx, float cy,
                               float hw, float hh, float rad) {
    float dx = fabsf(fx - cx) - (hw - rad);
    float dy = fabsf(fy - cy) - (hh - rad);
    if (dx > 0.0f && dy > 0.0f) return sqrtf(dx * dx + dy * dy) - rad;
    return fmaxf(dx, dy) - rad;
}

static void rrect_raster(float x, float y, float w, float h, float rad, float thick,
                         float r, float g, float b, float a) {
    if (w <= 0.0f || h <= 0.0f) return;
    PmColor c = pm_color(r, g, b, a);
    if (c.a == 0) return;
    float maxr = fminf(w, h) * 0.5f;
    if (rad > maxr) rad = maxr;
    if (rad < 0.0f) rad = 0.0f;
    int x0, y0, x1, y1;
    if (!clip_span(x - 1.0f, y - 1.0f, w + 2.0f, h + 2.0f, &x0, &y0, &x1, &y1)) return;
    float cx = x + w * 0.5f, cy = y + h * 0.5f, hw = w * 0.5f, hh = h * 0.5f;
    for (int py = y0; py < y1; py++) {
        float fy = (float)py + 0.5f;
        for (int px = x0; px < x1; px++) {
            float d = rrect_dist((float)px + 0.5f, fy, cx, cy, hw, hh, rad);
            float cov = clamp01(0.5f - d);
            if (thick > 0.0f) cov = fminf(cov, clamp01(d + thick + 0.5f));
            put_cov(px, py, c, cov);
        }
    }
}

void ui_round_rect(float x, float y, float w, float h, float radius,
                   float r, float g, float b, float a) {
    rrect_raster(x, y, w, h, radius, 0.0f, r, g, b, a);
}

void ui_round_rect_outline(float x, float y, float w, float h, float radius, float thickness,
                           float r, float g, float b, float a) {
    rrect_raster(x, y, w, h, radius, thickness, r, g, b, a);
}

void ui_circle(float cx, float cy, float radius, float r, float g, float b, float a) {
    PmColor c = pm_color(r, g, b, a);
    if (c.a == 0 || radius <= 0.0f) return;
    int x0, y0, x1, y1;
    if (!clip_span(cx - radius - 1.0f, cy - radius - 1.0f, radius * 2.0f + 2.0f,
                   radius * 2.0f + 2.0f, &x0, &y0, &x1, &y1)) return;
    for (int py = y0; py < y1; py++) {
        float dy = (float)py + 0.5f - cy;
        for (int px = x0; px < x1; px++) {
            float dx = (float)px + 0.5f - cx;
            put_cov(px, py, c, clamp01(radius + 0.5f - sqrtf(dx * dx + dy * dy)));
        }
    }
}

void ui_ring(float cx, float cy, float radius, float thickness,
             float r, float g, float b, float a) {
    PmColor c = pm_color(r, g, b, a);
    if (c.a == 0 || radius <= 0.0f) return;
    float rin = radius - thickness;
    int x0, y0, x1, y1;
    if (!clip_span(cx - radius - 1.0f, cy - radius - 1.0f, radius * 2.0f + 2.0f,
                   radius * 2.0f + 2.0f, &x0, &y0, &x1, &y1)) return;
    for (int py = y0; py < y1; py++) {
        float dy = (float)py + 0.5f - cy;
        for (int px = x0; px < x1; px++) {
            float dx = (float)px + 0.5f - cx;
            float d  = sqrtf(dx * dx + dy * dy);
            put_cov(px, py, c, fminf(clamp01(radius + 0.5f - d), clamp01(d - rin + 0.5f)));
        }
    }
}

void ui_triangle(float x0f, float y0f, float x1f, float y1f, float x2f, float y2f,
                 float r, float g, float b, float a) {
    PmColor c = pm_color(r, g, b, a);
    if (c.a == 0) return;
    /* Orient counter-clockwise (in screen space, y down) so "inside" > 0. */
    float area = (x1f - x0f) * (y2f - y0f) - (x2f - x0f) * (y1f - y0f);
    if (fabsf(area) < 1e-3f) return;
    if (area < 0.0f) { float tx = x1f, ty = y1f; x1f = x2f; y1f = y2f; x2f = tx; y2f = ty; }
    float ex[3] = { x0f, x1f, x2f }, ey[3] = { y0f, y1f, y2f };
    float nx[3], ny[3], nd[3];
    for (int i = 0; i < 3; i++) {
        int j = (i + 1) % 3;
        float dx = ex[j] - ex[i], dy = ey[j] - ey[i];
        float len = sqrtf(dx * dx + dy * dy);
        nx[i] = -dy / len; ny[i] = dx / len;           /* inward normal */
        nd[i] = nx[i] * ex[i] + ny[i] * ey[i];
    }
    float minx = fminf(x0f, fminf(x1f, x2f)), maxx = fmaxf(x0f, fmaxf(x1f, x2f));
    float miny = fminf(y0f, fminf(y1f, y2f)), maxy = fmaxf(y0f, fmaxf(y1f, y2f));
    int bx0, by0, bx1, by1;
    if (!clip_span(minx - 1.0f, miny - 1.0f, maxx - minx + 2.0f, maxy - miny + 2.0f,
                   &bx0, &by0, &bx1, &by1)) return;
    for (int py = by0; py < by1; py++) {
        float fy = (float)py + 0.5f;
        for (int px = bx0; px < bx1; px++) {
            float fx = (float)px + 0.5f;
            float d = 1e9f;
            for (int i = 0; i < 3; i++) d = fminf(d, nx[i] * fx + ny[i] * fy - nd[i]);
            put_cov(px, py, c, clamp01(d + 0.5f));
        }
    }
}

/* ── Text ─────────────────────────────────────────────────────────────── */
/* Map scale factor to the closest pre-loaded font size. */
static int pick_size(float scale) {
    int target = (int)(scale * 16.0f + 0.5f);
    if (target < 10) target = 10;
    int best = 0, bestdiff = abs(target - FONT_SIZES[0]);
    for (int i = 1; i < NUM_FONT_SIZES; i++) {
        int diff = abs(target - FONT_SIZES[i]);
        if (diff < bestdiff) { bestdiff = diff; best = i; }
    }
    return FONT_SIZES[best];
}

static TTF_Font *pick_font(float scale) {
    int px = pick_size(scale);
    for (int i = 0; i < NUM_FONT_SIZES; i++)
        if (FONT_SIZES[i] == px) return s_fonts[i];
    return s_fonts[0];
}

/* Exact pixel size (opened on demand, cached). NULL → bitmap fallback. */
static TTF_Font *font_px(int px) {
    if (px < 6) px = 6;
    for (int i = 0; i < NUM_FONT_SIZES; i++)
        if (FONT_SIZES[i] == px && s_fonts[i]) return s_fonts[i];
    for (int i = 0; i < s_pxfont_n; i++)
        if (s_pxfonts[i].px == px)
            return s_pxfonts[i].font ? s_pxfonts[i].font : pick_font((float)px / 16.0f);
    if (!s_font_data || !s_fonts[0]) return NULL;
    if (s_pxfont_n >= MAX_PX_FONTS) return pick_font((float)px / 16.0f);
    SDL_RWops *rw = SDL_RWFromMem(s_font_data, (int)s_font_data_sz);
    TTF_Font *f = TTF_OpenFontRW(rw, 1, px);
    s_pxfonts[s_pxfont_n].px   = px;
    s_pxfonts[s_pxfont_n].font = f;
    s_pxfont_n++;
    return f ? f : pick_font((float)px / 16.0f);
}

/* ── UTF-8 and synthesized accents ──────────────────────────────────────
   The bundled pixel font only has ASCII. Letters it can't draw (á é í ó ú
   ñ ü ¿ ¡ …) are rendered as their base letter with the mark drawn on top
   in the same pixel style, so Spanish text works with any font; a font that
   has the real glyphs uses them. */
static unsigned utf8_next(const char **ps) {
    const unsigned char *s = (const unsigned char *)*ps;
    unsigned c = s[0];
    int n;
    if (c < 0x80)                { *ps += 1; return c; }
    else if ((c & 0xE0) == 0xC0) { n = 1; c &= 0x1F; }
    else if ((c & 0xF0) == 0xE0) { n = 2; c &= 0x0F; }
    else if ((c & 0xF8) == 0xF0) { n = 3; c &= 0x07; }
    else                         { *ps += 1; return 0xFFFD; }
    for (int i = 1; i <= n; i++) {
        if ((s[i] & 0xC0) != 0x80) { *ps += i; return 0xFFFD; }
        c = (c << 6) | (s[i] & 0x3Fu);
    }
    *ps += n + 1;
    return c;
}

static bool is_ascii(const char *s) {
    for (; *s; s++) if ((unsigned char)*s >= 0x80) return false;
    return true;
}

enum { MK_NONE, MK_ACUTE, MK_GRAVE, MK_CIRC, MK_DIAER, MK_TILDE, MK_RING, MK_CEDIL, MK_INVERT };
typedef struct { unsigned cp; const char *base; unsigned char mark; } Accent;
static const Accent ACCENTS[] = {
    {0xE1,"a",MK_ACUTE},{0xE9,"e",MK_ACUTE},{0xED,"i",MK_ACUTE},{0xF3,"o",MK_ACUTE},{0xFA,"u",MK_ACUTE},
    {0xC1,"A",MK_ACUTE},{0xC9,"E",MK_ACUTE},{0xCD,"I",MK_ACUTE},{0xD3,"O",MK_ACUTE},{0xDA,"U",MK_ACUTE},
    {0xFD,"y",MK_ACUTE},{0xDD,"Y",MK_ACUTE},
    {0xE0,"a",MK_GRAVE},{0xE8,"e",MK_GRAVE},{0xEC,"i",MK_GRAVE},{0xF2,"o",MK_GRAVE},{0xF9,"u",MK_GRAVE},
    {0xC0,"A",MK_GRAVE},{0xC8,"E",MK_GRAVE},{0xCC,"I",MK_GRAVE},{0xD2,"O",MK_GRAVE},{0xD9,"U",MK_GRAVE},
    {0xE2,"a",MK_CIRC},{0xEA,"e",MK_CIRC},{0xEE,"i",MK_CIRC},{0xF4,"o",MK_CIRC},{0xFB,"u",MK_CIRC},
    {0xC2,"A",MK_CIRC},{0xCA,"E",MK_CIRC},{0xCE,"I",MK_CIRC},{0xD4,"O",MK_CIRC},{0xDB,"U",MK_CIRC},
    {0xE4,"a",MK_DIAER},{0xEB,"e",MK_DIAER},{0xEF,"i",MK_DIAER},{0xF6,"o",MK_DIAER},{0xFC,"u",MK_DIAER},
    {0xC4,"A",MK_DIAER},{0xCB,"E",MK_DIAER},{0xCF,"I",MK_DIAER},{0xD6,"O",MK_DIAER},{0xDC,"U",MK_DIAER},
    {0xFF,"y",MK_DIAER},
    {0xF1,"n",MK_TILDE},{0xD1,"N",MK_TILDE},{0xE3,"a",MK_TILDE},{0xF5,"o",MK_TILDE},
    {0xC3,"A",MK_TILDE},{0xD5,"O",MK_TILDE},
    {0xE5,"a",MK_RING},{0xC5,"A",MK_RING},
    {0xE7,"c",MK_CEDIL},{0xC7,"C",MK_CEDIL},
    {0xBF,"?",MK_INVERT},{0xA1,"!",MK_INVERT},
    /* Typographic punctuation → plain ASCII. */
    {0x2018,"'",MK_NONE},{0x2019,"'",MK_NONE},{0x201C,"\"",MK_NONE},{0x201D,"\"",MK_NONE},
    {0x2013,"-",MK_NONE},{0x2014,"-",MK_NONE},{0x2026,"...",MK_NONE},{0xAB,"<<",MK_NONE},
    {0xBB,">>",MK_NONE},{0xBA,"o",MK_NONE},{0xAA,"a",MK_NONE},{0xB7,".",MK_NONE},
    {0xD7,"x",MK_NONE},{0xA0," ",MK_NONE},
};

static const Accent *accent_of(unsigned cp) {
    for (size_t i = 0; i < sizeof(ACCENTS) / sizeof(ACCENTS[0]); i++)
        if (ACCENTS[i].cp == cp) return &ACCENTS[i];
    return NULL;
}

#define MAX_MARKS 48
typedef struct {
    char plain[1024];                       /* what SDL_ttf actually renders */
    struct { int pos; unsigned char mark; } marks[MAX_MARKS];
    int  nmarks;
} Prepared;

/* Replace characters the font can't draw by their base letters (bitmap
   font: font == NULL) and note where the marks go. */
static void prepare_text(TTF_Font *font, const char *str, Prepared *out) {
    size_t n = 0;
    out->nmarks = 0;
    const char *p = str;
    while (*p && n < sizeof(out->plain) - 8) {
        const char *start = p;
        unsigned cp = utf8_next(&p);
        const Accent *ac = cp >= 0x80 ? accent_of(cp) : NULL;
        bool native = cp < 0x80 || (font && cp <= 0xFFFF && TTF_GlyphIsProvided(font, (Uint16)cp));
        if (native || !ac) {
            if (!native && !font) { out->plain[n++] = '?'; continue; }
            size_t len = (size_t)(p - start);
            memcpy(out->plain + n, start, len);
            n += len;
            continue;
        }
        if (ac->mark != MK_NONE && out->nmarks < MAX_MARKS) {
            out->marks[out->nmarks].pos = (int)n;
            out->marks[out->nmarks].mark = ac->mark;
            out->nmarks++;
        }
        size_t bl = strlen(ac->base);
        memcpy(out->plain + n, ac->base, bl);
        n += bl;
    }
    out->plain[n] = '\0';
}

/* Pixel-art marks on a grid of u x u cells: {cells}, width, height. */
typedef struct { unsigned char cells[8][2]; int n, w, h; } MarkShape;
static const MarkShape MARK_SHAPES[] = {
    /* NONE   */ {{{0,0}}, 0, 0, 0},
    /* ACUTE  */ {{{0,1},{1,0}}, 2, 2, 2},
    /* GRAVE  */ {{{0,0},{1,1}}, 2, 2, 2},
    /* CIRC   */ {{{0,1},{1,0},{2,1}}, 3, 3, 2},
    /* DIAER  */ {{{0,0},{2,0}}, 2, 3, 1},
    /* TILDE  */ {{{0,1},{1,0},{2,1},{3,0}}, 4, 4, 2},
    /* RING   */ {{{1,0},{0,1},{2,1},{1,2}}, 4, 3, 3},
    /* CEDIL  */ {{{1,0},{2,1},{1,2},{0,2}}, 4, 3, 3},
};

static void mark_fill(unsigned char *m, int mw, int mh, int x, int y, int w, int h) {
    for (int yy = y; yy < y + h; yy++) {
        if (yy < 0 || yy >= mh) continue;
        for (int xx = x; xx < x + w; xx++)
            if (xx >= 0 && xx < mw) m[(size_t)yy * (size_t)mw + (size_t)xx] = 255;
    }
}

/* Coverage mask of a string: SDL_ttf rendering plus synthesized marks.
   *oy is the row offset of the mask relative to the text's top (accents on
   capitals may rise above the line box). Returns NULL for empty output. */
static unsigned char *render_mask(TTF_Font *font, int px, const char *str, int *mw, int *mh, int *oy) {
    static Prepared P;
    const char *plain = str;
    int nmarks = 0;
    if (!is_ascii(str)) {
        prepare_text(font, str, &P);
        plain = P.plain;
        nmarks = P.nmarks;
    }
    *mw = *mh = *oy = 0;
    if (!plain[0]) return NULL;
    SDL_Color white = { 255, 255, 255, 255 };
    SDL_Surface *surf = TTF_RenderUTF8_Blended(font, plain, white);
    if (!surf) return NULL;
    SDL_Surface *src = surf;
    if (surf->format->format != SDL_PIXELFORMAT_ARGB8888) {
        src = SDL_ConvertSurfaceFormat(surf, SDL_PIXELFORMAT_ARGB8888, 0);
        SDL_FreeSurface(surf);
        if (!src) return NULL;
    }

    /* Where each mark goes, and how much room it needs outside the box. */
    const int u = px >= 12 ? (px + 6) / 12 : 1;
    const int ascent = TTF_FontAscent(font);
    int top_extra = 0, bottom_extra = 0;
    struct { int x, y, mark, gx0, gy0, gx1, gy1, dotless; } place[MAX_MARKS];
    for (int i = 0; i < nmarks; i++) {
        const char base = P.plain[P.marks[i].pos];
        int x0 = 0, hh = 0;
        P.plain[P.marks[i].pos] = '\0';                 /* measure the text before it */
        if (P.marks[i].pos > 0) TTF_SizeUTF8(font, P.plain, &x0, &hh);
        P.plain[P.marks[i].pos] = base;
        int minx = 0, maxx = 0, miny = 0, maxy = 0, adv = 0;
        TTF_GlyphMetrics(font, (Uint16)(unsigned char)base, &minx, &maxx, &miny, &maxy, &adv);
        int mk = P.marks[i].mark;
        place[i].mark = mk;
        place[i].gx0 = x0 + minx; place[i].gx1 = x0 + maxx;
        place[i].gy0 = ascent - maxy; place[i].gy1 = ascent - miny;
        place[i].dotless = 0;
        if (mk == MK_INVERT) continue;
        if (base == 'i') {
            /* í, ï…: the mark replaces the dot, which goes from the x-height up. */
            int xminx, xmaxx, xminy, xmaxy, xadv;
            if (TTF_GlyphMetrics(font, 'x', &xminx, &xmaxx, &xminy, &xmaxy, &xadv) == 0 &&
                ascent - xmaxy > place[i].gy0) {
                place[i].dotless = ascent - xmaxy;
                place[i].gy0 = ascent - xmaxy;
            }
        }
        const MarkShape *ms = &MARK_SHAPES[mk];
        int cx = x0 + (minx + maxx) / 2;
        place[i].x = cx - (ms->w * u) / 2;
        if (mk == MK_CEDIL) {
            place[i].y = ascent;                       /* hangs under the baseline */
            int need = place[i].y + ms->h * u - src->h;
            if (need > bottom_extra) bottom_extra = need;
        } else {
            place[i].y = place[i].gy0 - u - ms->h * u; /* one cell above the letter */
            if (-place[i].y > top_extra) top_extra = -place[i].y;
        }
    }

    const int w = src->w, h = src->h + top_extra + bottom_extra;
    unsigned char *m = (unsigned char *)calloc((size_t)w * (size_t)h, 1);
    if (!m) { SDL_FreeSurface(src); return NULL; }
    SDL_LockSurface(src);
    for (int row = 0; row < src->h; row++) {
        const Uint32 *line = (const Uint32 *)((const Uint8 *)src->pixels + row * src->pitch);
        unsigned char *dst = m + (size_t)(row + top_extra) * (size_t)w;
        for (int col = 0; col < w; col++) dst[col] = (unsigned char)(line[col] >> 24);
    }
    SDL_UnlockSurface(src);
    SDL_FreeSurface(src);

    for (int i = 0; i < nmarks; i++) {
        if (place[i].mark == MK_INVERT) {
            /* ¿ ¡: the glyph turned upside down within its own box. */
            int x0 = place[i].gx0 < 0 ? 0 : place[i].gx0, x1 = place[i].gx1 > w ? w : place[i].gx1;
            int y0 = place[i].gy0 + top_extra, y1 = place[i].gy1 + top_extra;
            if (y0 < 0) y0 = 0;
            if (y1 > h) y1 = h;
            const int bw = x1 - x0, bh = y1 - y0;
            for (int k = 0; bw > 0 && k < bw * bh / 2; k++) {
                unsigned char *a = &m[(size_t)(y0 + k / bw) * (size_t)w + (size_t)(x0 + k % bw)];
                unsigned char *b = &m[(size_t)(y1 - 1 - k / bw) * (size_t)w + (size_t)(x1 - 1 - k % bw)];
                unsigned char t = *a; *a = *b; *b = t;
            }
            continue;
        }
        if (place[i].dotless > 0)       /* erase the dot of the i */
            for (int y = 0; y < place[i].dotless - 1 + top_extra && y < h; y++)
                for (int x = place[i].gx0; x < place[i].gx1; x++)
                    if (x >= 0 && x < w) m[(size_t)y * (size_t)w + (size_t)x] = 0;
        const MarkShape *ms = &MARK_SHAPES[place[i].mark];
        for (int k = 0; k < ms->n; k++)
            mark_fill(m, w, h, place[i].x + ms->cells[k][0] * u,
                      place[i].y + top_extra + ms->cells[k][1] * u, u, u);
    }
    *mw = w; *mh = h; *oy = -top_extra;
    return m;
}

/* ── Text cache ──────────────────────────────────────────────────────────
   SDL_ttf rasterizes a whole string into a new surface on every call, and
   the launcher draws the same few dozen strings each frame (plus many width
   measurements while word-wrapping). Coverage masks and widths are cached
   per (pixel size, string); once the cache outgrows its budget, entries
   that haven't been used for a while are dropped (checked in ui_begin). */
typedef struct TextEntry {
    struct TextEntry *next;
    unsigned       hash;
    int            px;
    int            width;        /* advance width, -1 = not measured yet  */
    int            mw, mh, oy;   /* coverage mask size and row offset     */
    bool           rendered;
    unsigned char *mask;
    unsigned       last_used;
    char           str[];
} TextEntry;

#define TC_BUCKETS   1024
#define TC_BUDGET    ((size_t)6 << 20)   /* bytes of masks */
#define TC_MAX_ITEMS 4096
static TextEntry *s_tc[TC_BUCKETS];
static size_t     s_tc_bytes = 0;
static int        s_tc_items = 0;
static unsigned   s_tc_frame = 1;

static unsigned tc_hash(int px, const char *s) {
    unsigned h = (2166136261u ^ (unsigned)px) * 16777619u;
    for (; *s; s++) h = (h ^ (unsigned char)*s) * 16777619u;
    return h;
}

static TextEntry *tc_find(int px, const char *s, bool create) {
    const unsigned h = tc_hash(px, s);
    TextEntry **bucket = &s_tc[h & (TC_BUCKETS - 1)];
    for (TextEntry *e = *bucket; e; e = e->next)
        if (e->hash == h && e->px == px && strcmp(e->str, s) == 0) {
            e->last_used = s_tc_frame;
            return e;
        }
    if (!create) return NULL;
    size_t n = strlen(s);
    TextEntry *e = (TextEntry *)malloc(sizeof(TextEntry) + n + 1);
    if (!e) return NULL;
    memcpy(e->str, s, n + 1);
    e->hash = h; e->px = px; e->width = -1;
    e->mw = e->mh = e->oy = 0; e->rendered = false; e->mask = NULL;
    e->last_used = s_tc_frame;
    e->next = *bucket;
    *bucket = e;
    s_tc_items++;
    return e;
}

/* Drop entries not used during the last `age` frames (0 = everything). */
static void tc_trim(unsigned age) {
    for (int i = 0; i < TC_BUCKETS; i++) {
        TextEntry **pp = &s_tc[i];
        while (*pp) {
            TextEntry *e = *pp;
            if (age == 0 || e->last_used + age < s_tc_frame) {
                *pp = e->next;
                s_tc_bytes -= (size_t)e->mw * (size_t)e->mh;
                free(e->mask);
                free(e);
                s_tc_items--;
            } else {
                pp = &e->next;
            }
        }
    }
}

static void tc_tick(void) {
    s_tc_frame++;
    if (s_tc_bytes > TC_BUDGET || s_tc_items > TC_MAX_ITEMS) {
        tc_trim(120);
        if (s_tc_bytes > TC_BUDGET || s_tc_items > TC_MAX_ITEMS) tc_trim(3);
    }
}

static void blit_mask(const unsigned char *m, int mw, int mh, int ox, int oy, PmColor c) {
    int c0 = s_clip_x0 - ox, c1 = s_clip_x1 - ox;
    if (c0 < 0) c0 = 0;
    if (c1 > mw) c1 = mw;
    if (c0 >= c1) return;
    for (int row = 0; row < mh; row++) {
        int py = oy + row;
        if (py < s_clip_y0 || py >= s_clip_y1) continue;
        const unsigned char *src = m + (size_t)row * (size_t)mw;
        unsigned char *p = s_px + ((size_t)py * (size_t)s_pw + (size_t)(ox + c0)) * 4;
        for (int col = c0; col < c1; col++, p += 4) {
            unsigned cov = src[col];
            if (!cov) continue;
            if (cov == 255u) blend_at(p, c.r, c.g, c.b, c.a);
            else blend_at(p, mul255(c.r, cov), mul255(c.g, cov), mul255(c.b, cov), mul255(c.a, cov));
        }
    }
}

/* 8×8 bitmap fallback when no TTF font could be loaded. */
static void bitmap_text(float x, float y, float scale, const char *str, PmColor c) {
    static Prepared P;
    if (!is_ascii(str)) { prepare_text(NULL, str, &P); str = P.plain; }
    int sc = (int)(scale + .5f); if (sc < 1) sc = 1;
    float cx = x;
    while (*str) {
        unsigned char ch = (unsigned char)*str++;
        if (ch >= 128) ch = '?';
        if (ch == '\n') { cx = x; y += 8.0f * scale; continue; }
        for (int fy = 0; fy < 8; fy++) {
            unsigned char byte = font8x8[ch][fy];
            for (int fx = 0; fx < 8; fx++) {
                if (byte & (1 << fx))
                    for (int sy = 0; sy < sc; sy++)
                        for (int sx = 0; sx < sc; sx++)
                            put_pixel((int)cx + fx * sc + sx, (int)y + fy * sc + sy,
                                      c.r, c.g, c.b, c.a);
            }
        }
        cx += 8.0f * scale;
    }
}

static int bitmap_width(int px, const char *str) {
    int sc = (int)((float)px / 8.0f + 0.5f); if (sc < 1) sc = 1;
    int n = 0;
    for (const char *p = str; *p;) { utf8_next(&p); n++; }
    return n * 8 * sc;
}

/* Draw `str` at pixel size px (font already resolved). */
static void text_draw(TTF_Font *font, int px, float x, float y, const char *str, PmColor c) {
    const int ox = (int)floorf(x), oy = (int)floorf(y);
    const bool offscreen = s_px != s_canvas;
    /* Box art and other offscreen compositions draw one-off strings: don't
       let them flush the on-screen text out of the cache. */
    TextEntry *e = tc_find(px, str, !offscreen);
    if (e) {
        if (!e->rendered) {
            e->mask = render_mask(font, px, str, &e->mw, &e->mh, &e->oy);
            e->rendered = true;
            s_tc_bytes += (size_t)e->mw * (size_t)e->mh;
        }
        if (e->mask) blit_mask(e->mask, e->mw, e->mh, ox, oy + e->oy, c);
        return;
    }
    int mw, mh, moy;
    unsigned char *m = render_mask(font, px, str, &mw, &mh, &moy);
    if (m) blit_mask(m, mw, mh, ox, oy + moy, c);
    free(m);
}

static int text_measure(TTF_Font *font, const char *str) {
    static Prepared P;
    const char *plain = str;
    if (!is_ascii(str)) { prepare_text(font, str, &P); plain = P.plain; }
    int w = 0, h = 0;
    if (!plain[0] || TTF_SizeUTF8(font, plain, &w, &h) != 0) return 0;
    return w;
}

void ui_text(float x, float y, float scale, const char *str,
             float r, float g, float b) {
    if (!str || !str[0]) return;
    PmColor c = pm_color(r, g, b, 1.0f);
    TTF_Font *font = pick_font(scale);
    if (!font) { bitmap_text(x, y, scale, str, c); return; }
    text_draw(font, pick_size(scale), x, y, str, c);
}

void ui_text_px(float x, float y, int px, const char *str,
                float r, float g, float b, float a) {
    if (!str || !str[0] || a <= 0.0f) return;
    PmColor c = pm_color(r, g, b, a);
    TTF_Font *font = font_px(px);
    if (!font) { bitmap_text(x, y, (float)px / 8.0f, str, c); return; }
    text_draw(font, px, x, y, str, c);
}

int ui_text_width(int px, const char *str) {
    if (!str || !str[0]) return 0;
    TTF_Font *font = font_px(px);
    if (!font) return bitmap_width(px, str);
    TextEntry *e = tc_find(px, str, true);
    if (!e) return text_measure(font, str);
    if (e->width < 0) e->width = text_measure(font, str);
    return e->width;
}

int ui_text_line_height(int px) {
    TTF_Font *font = font_px(px);
    if (!font) {
        int sc = (int)((float)px / 8.0f + 0.5f); if (sc < 1) sc = 1;
        return 8 * sc;
    }
    return TTF_FontHeight(font);
}

/* Greedy word wrap shared by ui_text_wrap / ui_text_wrap_lines. When draw is
   false only the line count is computed. */
static int wrap_layout(float x, float y, float max_w, int px, float line_h, const char *str,
                       int align, int max_lines, PmColor c, bool draw) {
    if (!str) return 0;
    char line[512];
    int  lines = 0;
    const char *p = str;
    while (*p) {
        while (*p == ' ') p++;
        if (!*p) break;
        /* Collect words while they fit. */
        size_t len = 0;
        const char *q = p;
        bool forced = false;
        while (*q && *q != '\n') {
            const char *we = q;
            while (*we && *we != ' ' && *we != '\n') we++;
            size_t wl = (size_t)(we - q);
            size_t need = len + (len ? 1 : 0) + wl;
            if (need >= sizeof(line) - 4) break;
            char tmp[512];
            memcpy(tmp, line, len);
            size_t tl = len;
            if (tl) tmp[tl++] = ' ';
            memcpy(tmp + tl, q, wl);
            tmp[tl + wl] = '\0';
            if (len && (float)ui_text_width(px, tmp) > max_w) break;
            memcpy(line, tmp, tl + wl + 1);
            len = tl + wl;
            q = we;
            while (*q == ' ') q++;
        }
        if (*q == '\n') { forced = true; }
        line[len] = '\0';
        bool last = (max_lines > 0 && lines + 1 >= max_lines);
        if (last && *q && !(forced && !q[1])) {
            /* More text remains: end this line with an ellipsis. */
            while (len > 0) {
                char tmp[520];
                snprintf(tmp, sizeof(tmp), "%s...", line);
                if ((float)ui_text_width(px, tmp) <= max_w) break;
                while (len > 0 && line[len - 1] != ' ') len--;   /* drop last word */
                while (len > 0 && line[len - 1] == ' ') len--;
                line[len] = '\0';
            }
            strncat(line, "...", sizeof(line) - strlen(line) - 1);
            q = p + strlen(p);   /* stop */
        }
        if (draw) {
            float lw = (float)ui_text_width(px, line);
            float lx = x;
            if (align == UI_ALIGN_CENTER) lx = x + (max_w - lw) * 0.5f;
            else if (align == UI_ALIGN_RIGHT) lx = x + max_w - lw;
            TTF_Font *font = font_px(px);
            if (font) text_draw(font, px, lx, y + (float)lines * line_h, line, c);
            else      bitmap_text(lx, y + (float)lines * line_h, (float)px / 8.0f, line, c);
        }
        lines++;
        if (q == p) q++;          /* safety: always make progress */
        p = q;
        if (*p == '\n') p++;
        if (max_lines > 0 && lines >= max_lines) break;
    }
    return lines;
}

float ui_text_wrap(float x, float y, float max_w, int px, float line_h, const char *str,
                   int align, int max_lines, float r, float g, float b, float a) {
    if (!str || !str[0]) return y;
    PmColor c = pm_color(r, g, b, a);
    int n = wrap_layout(x, y, max_w, px, line_h, str, align, max_lines, c, a > 0.0f);
    return y + (float)n * line_h;
}

int ui_text_wrap_lines(float max_w, int px, const char *str, int max_lines) {
    PmColor c = { 0, 0, 0, 0 };
    return wrap_layout(0.0f, 0.0f, max_w, px, 0.0f, str, UI_ALIGN_LEFT, max_lines, c, false);
}
