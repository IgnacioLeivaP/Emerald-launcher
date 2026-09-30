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
    s_font_data = malloc((size_t)s_font_data_sz);
    if (!s_font_data) { fclose(f); return false; }
    fread(s_font_data, 1, (size_t)s_font_data_sz, f);
    fclose(f);

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
static TTF_Font *pick_font(float scale) {
    int target = (int)(scale * 16.0f + 0.5f);
    if (target < 10) target = 10;
    int best = 0, bestdiff = abs(target - FONT_SIZES[0]);
    for (int i = 1; i < NUM_FONT_SIZES; i++) {
        int diff = abs(target - FONT_SIZES[i]);
        if (diff < bestdiff) { bestdiff = diff; best = i; }
    }
    return s_fonts[best];
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

/* 8×8 bitmap fallback when no TTF font could be loaded. */
static void bitmap_text(float x, float y, float scale, const char *str, PmColor c) {
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

/* Render `str` with `font` and composite its coverage in color c. */
static void ttf_text(TTF_Font *font, float x, float y, const char *str, PmColor c) {
    SDL_Color white = { 255, 255, 255, 255 };
    SDL_Surface *surf = TTF_RenderUTF8_Blended(font, str, white);
    if (!surf) return;
    SDL_Surface *src = surf;
    if (surf->format->format != SDL_PIXELFORMAT_ARGB8888) {
        src = SDL_ConvertSurfaceFormat(surf, SDL_PIXELFORMAT_ARGB8888, 0);
        SDL_FreeSurface(surf);
        if (!src) return;
    }
    SDL_LockSurface(src);
    int ox = (int)floorf(x), oy = (int)floorf(y);
    for (int row = 0; row < src->h; row++) {
        int py = oy + row;
        if (py < s_clip_y0 || py >= s_clip_y1) continue;
        const Uint32 *line = (const Uint32 *)((const Uint8 *)src->pixels + row * src->pitch);
        for (int col = 0; col < src->w; col++) {
            unsigned cov = line[col] >> 24;
            if (!cov) continue;
            unsigned a = mul255(c.a, cov);
            put_pixel(ox + col, py, mul255(c.r, cov), mul255(c.g, cov), mul255(c.b, cov), a);
        }
    }
    SDL_UnlockSurface(src);
    SDL_FreeSurface(src);
}

void ui_text(float x, float y, float scale, const char *str,
             float r, float g, float b) {
    if (!str || !str[0]) return;
    PmColor c = pm_color(r, g, b, 1.0f);
    TTF_Font *font = pick_font(scale);
    if (!font) { bitmap_text(x, y, scale, str, c); return; }
    ttf_text(font, x, y, str, c);
}

void ui_text_px(float x, float y, int px, const char *str,
                float r, float g, float b, float a) {
    if (!str || !str[0] || a <= 0.0f) return;
    PmColor c = pm_color(r, g, b, a);
    TTF_Font *font = font_px(px);
    if (!font) { bitmap_text(x, y, (float)px / 8.0f, str, c); return; }
    ttf_text(font, x, y, str, c);
}

int ui_text_width(int px, const char *str) {
    if (!str || !str[0]) return 0;
    TTF_Font *font = font_px(px);
    if (!font) {
        int sc = (int)((float)px / 8.0f + 0.5f); if (sc < 1) sc = 1;
        return (int)strlen(str) * 8 * sc;
    }
    int w = 0, h = 0;
    if (TTF_SizeUTF8(font, str, &w, &h) != 0) return 0;
    return w;
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
            if (font) ttf_text(font, lx, y + (float)lines * line_h, line, c);
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
