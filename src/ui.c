#include "ui.h"
#include "gl.h"
#include "font8x8.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_image.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

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
static unsigned char s_canvas[CANVAS_H * CANVAS_W * 4];

/* Clip rectangle — put_pixel ignores pixels outside this region */
static int s_clip_x0 = 0, s_clip_y0 = 0;
static int s_clip_x1 = CANVAS_W, s_clip_y1 = CANVAS_H;

/* Forward declaration — defined after the canvas is set up */
static inline void put_pixel(int x, int y,
                              unsigned char r, unsigned char g,
                              unsigned char b, unsigned char a);

/* ── Image cache (pixel-blitted into canvas) ──────────────────────────── */
typedef struct {
    char           path[512];
    unsigned char *pixels;
    int            w, h;
    unsigned long  last_used;   /* LRU tick — 0 means empty slot */
} CachedImage;

/* db.json references ~116 logos+screenshots, far more than fit at once. The
   cache is bounded; when full it evicts the least-recently-used entry instead
   of refusing to load (which silently dropped images past the limit). */
#define MAX_CACHED_IMAGES 64
static CachedImage  s_img_cache[MAX_CACHED_IMAGES];
static int          s_img_count = 0;
static unsigned long s_img_tick  = 0;

static CachedImage *img_load(const char *path) {
    for (int i = 0; i < s_img_count; i++)
        if (strcmp(s_img_cache[i].path, path) == 0) {
            s_img_cache[i].last_used = ++s_img_tick;
            return &s_img_cache[i];
        }

    SDL_Surface *surf = IMG_Load(path);
    if (!surf) { fprintf(stderr, "ui_image: %s\n", IMG_GetError()); return NULL; }
    SDL_Surface *rgba = SDL_ConvertSurfaceFormat(surf, SDL_PIXELFORMAT_ABGR8888, 0);
    SDL_FreeSurface(surf);
    if (!rgba) return NULL;

    /* Pick a slot: grow while there's room, else evict the LRU entry. */
    CachedImage *ci;
    if (s_img_count < MAX_CACHED_IMAGES) {
        ci = &s_img_cache[s_img_count++];
    } else {
        ci = &s_img_cache[0];
        for (int i = 1; i < s_img_count; i++)
            if (s_img_cache[i].last_used < ci->last_used) ci = &s_img_cache[i];
        free(ci->pixels);
        ci->pixels = NULL;
    }
    ci->last_used = ++s_img_tick;
    strncpy(ci->path, path, sizeof(ci->path)-1);
    ci->path[sizeof(ci->path)-1] = '\0';
    ci->w = rgba->w; ci->h = rgba->h;
    ci->pixels = (unsigned char*)malloc((size_t)(rgba->w * rgba->h * 4));
    if (!ci->pixels) {            /* invalidate slot (LRU will reuse it first) */
        SDL_FreeSurface(rgba);
        ci->path[0] = '\0';
        ci->last_used = 0;
        return NULL;
    }
    for (int row = 0; row < rgba->h; row++)
        memcpy(ci->pixels + row*rgba->w*4,
               (Uint8*)rgba->pixels + row*rgba->pitch, (size_t)(rgba->w*4));
    SDL_FreeSurface(rgba);
    return ci;
}

void ui_image(float fx, float fy, float max_w, float max_h, const char *path, float brightness) {
    if (!path || !path[0]) return;
    CachedImage *ci = img_load(path);
    if (!ci) return;

    float sx = max_w / (float)ci->w;
    float sy = max_h / (float)ci->h;
    float sc = (sx < sy) ? sx : sy;
    int dw = (int)(ci->w * sc + 0.5f);
    int dh = (int)(ci->h * sc + 0.5f);
    int ox = (int)fx + ((int)max_w - dw) / 2;
    int oy = (int)fy + ((int)max_h - dh) / 2;

    if (brightness < 0.0f) brightness = 0.0f;
    if (brightness > 1.0f) brightness = 1.0f;

    for (int py = 0; py < dh; py++) {
        int sy2 = py * ci->h / dh;
        for (int px = 0; px < dw; px++) {
            int sx2 = px * ci->w / dw;
            const unsigned char *src = ci->pixels + (sy2 * ci->w + sx2) * 4;
            if (src[3] > 0) {
                unsigned char r = (unsigned char)(src[0] * brightness);
                unsigned char g = (unsigned char)(src[1] * brightness);
                unsigned char b = (unsigned char)(src[2] * brightness);
                put_pixel(ox+px, oy+py, r, g, b, src[3]);
            }
        }
    }
}

/* ── TTF font cache ────────────────────────────────────────────────────── */
#define NUM_FONT_SIZES 5
static const int FONT_SIZES[NUM_FONT_SIZES] = {16, 20, 24, 32, 40};
static TTF_Font *s_fonts[NUM_FONT_SIZES];
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
    if (s_ttf_inited) { TTF_Quit(); s_ttf_inited = 0; }
    free(s_font_data); s_font_data = NULL;

    for (int i = 0; i < s_img_count; i++) { free(s_img_cache[i].pixels); s_img_cache[i].pixels = NULL; }
    s_img_count = 0;
    if (s_bg_prog) { gl_DeleteProgram(s_bg_prog); s_bg_prog = 0; }
    if (s_bg_tex)  { glDeleteTextures(1, &s_bg_tex); s_bg_tex = 0; }
    if (s_prog) { gl_DeleteProgram(s_prog); s_prog = 0; }
    if (s_vao)  { gl_DeleteVertexArrays(1, &s_vao); s_vao = 0; }
    if (s_tex)  { glDeleteTextures(1, &s_tex); s_tex = 0; }
}

void ui_begin(void) {
    memset(s_canvas, 0, (size_t)(s_sw * s_sh * 4));
}

void ui_end(void) {
    /* 1. Render tiled background (no blend — replaces whatever is behind) */
    if (s_draw_bg && s_bg_tex && s_bg_prog) {
        glDisable(GL_BLEND);
        gl_UseProgram(s_bg_prog);
        GLint tloc = gl_GetUniformLocation(s_bg_prog, "u_tile");
        if (tloc >= 0) gl_Uniform2f(tloc, s_bg_tile_x, s_bg_tile_y);
        gl_ActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, s_bg_tex);
        gl_BindVertexArray(s_vao);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }

    /* 2. Upload canvas and blend UI on top */
    glBindTexture(GL_TEXTURE_2D, s_tex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, s_sw, s_sh,
                    GL_RGBA, GL_UNSIGNED_BYTE, s_canvas);
    glBindTexture(GL_TEXTURE_2D, 0);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    gl_UseProgram(s_prog);
    gl_ActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, s_tex);
    gl_BindVertexArray(s_vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    gl_BindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_BLEND);
}

static inline void put_pixel(int x, int y,
                              unsigned char r, unsigned char g,
                              unsigned char b, unsigned char a) {
    if (x < s_clip_x0 || x >= s_clip_x1 || y < s_clip_y0 || y >= s_clip_y1) return;
    if ((unsigned)x >= (unsigned)s_sw || (unsigned)y >= (unsigned)s_sh) return;
    unsigned char *p = s_canvas + (y * s_sw + x) * 4;
    p[0]=r; p[1]=g; p[2]=b; p[3]=a;
}

void ui_set_clip(float x, float y, float w, float h) {
    s_clip_x0 = (int)x;
    s_clip_y0 = (int)y;
    s_clip_x1 = (int)(x + w);
    s_clip_y1 = (int)(y + h);
}
void ui_clear_clip(void) {
    s_clip_x0 = 0; s_clip_y0 = 0;
    s_clip_x1 = CANVAS_W; s_clip_y1 = CANVAS_H;
}
bool ui_image_size(const char *path, int *out_w, int *out_h) {
    if (!path || !path[0]) return false;
    CachedImage *ci = img_load(path);
    if (!ci) return false;
    if (out_w) *out_w = ci->w;
    if (out_h) *out_h = ci->h;
    return true;
}

void ui_rect(float x, float y, float w, float h,
             float r, float g, float b, float a) {
    unsigned char cr=(unsigned char)(r*255+.5f), cg=(unsigned char)(g*255+.5f),
                  cb=(unsigned char)(b*255+.5f), ca=(unsigned char)(a*255+.5f);
    int x0=(int)x, y0=(int)y, x1=(int)(x+w), y1=(int)(y+h);
    for (int py=y0; py<y1; py++)
        for (int px=x0; px<x1; px++)
            put_pixel(px, py, cr, cg, cb, ca);
}

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

void ui_text(float x, float y, float scale, const char *str,
             float r, float g, float b) {
    TTF_Font *font = pick_font(scale);

    if (!font) {
        /* Fallback: 8×8 bitmap font */
        unsigned char cr=(unsigned char)(r*255+.5f),
                      cg=(unsigned char)(g*255+.5f),
                      cb=(unsigned char)(b*255+.5f);
        int sc = (int)(scale+.5f); if (sc<1) sc=1;
        float cx=x;
        while (*str) {
            unsigned char c=(unsigned char)*str++;
            if (c>=128) c='?';
            if (c=='\n') { cx=x; y+=8.0f*scale; continue; }
            for (int fy=0; fy<8; fy++) {
                unsigned char byte=font8x8[c][fy];
                for (int fx=0; fx<8; fx++) {
                    if (byte&(1<<fx))
                        for (int sy=0; sy<sc; sy++)
                            for (int sx=0; sx<sc; sx++)
                                put_pixel((int)cx+fx*sc+sx,(int)y+fy*sc+sy,cr,cg,cb,255);
                }
            }
            cx+=8.0f*scale;
        }
        return;
    }

    SDL_Color color = {
        (Uint8)(r*255.0f+0.5f),
        (Uint8)(g*255.0f+0.5f),
        (Uint8)(b*255.0f+0.5f),
        255
    };
    SDL_Surface *surf = TTF_RenderUTF8_Blended(font, str, color);
    if (!surf) return;

    /* ABGR8888: in little-endian memory, bytes are [R, G, B, A] — matches canvas. */
    SDL_Surface *rgba = SDL_ConvertSurfaceFormat(surf, SDL_PIXELFORMAT_ABGR8888, 0);
    SDL_FreeSurface(surf);
    if (!rgba) return;

    SDL_LockSurface(rgba);
    const Uint8 *src = (const Uint8*)rgba->pixels;
    for (int row = 0; row < rgba->h; row++) {
        const Uint8 *line = src + row * rgba->pitch;
        for (int col = 0; col < rgba->w; col++) {
            const Uint8 *px = line + col * 4;
            if (px[3] > 0)
                put_pixel((int)x + col, (int)y + row, px[0], px[1], px[2], px[3]);
        }
    }
    SDL_UnlockSurface(rgba);
    SDL_FreeSurface(rgba);
}
