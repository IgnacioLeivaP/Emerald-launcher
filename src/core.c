#include "core.h"
#include "gl.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

#if defined(_WIN32)
#  include <windows.h>
typedef HMODULE DylibHandle;
static DylibHandle dylib_load(const char *p) { return LoadLibraryA(p); }
static void *dylib_sym(DylibHandle h, const char *s) { return (void*)GetProcAddress(h,s); }
static void  dylib_close(DylibHandle h) { FreeLibrary(h); }
#elif defined(__SWITCH__)
/* libnx has no dlopen — in-launcher cores aren't loaded on Switch (heavy
   systems go via RetroArch chainload). Stub so core.c compiles; core_load
   returns false and the launcher falls back to external launch. */
typedef void *DylibHandle;
static DylibHandle dylib_load(const char *p) { (void)p; return NULL; }
static void *dylib_sym(DylibHandle h, const char *s) { (void)h; (void)s; return NULL; }
static void  dylib_close(DylibHandle h) { (void)h; }
#else
#  include <dlfcn.h>
typedef void *DylibHandle;
static DylibHandle dylib_load(const char *p) { return dlopen(p, RTLD_LAZY); }
static void *dylib_sym(DylibHandle h, const char *s) { return dlsym(h,s); }
static void  dylib_close(DylibHandle h) { dlclose(h); }
#endif

static DylibHandle s_lib;

static void   (*s_retro_init)(void);
static void   (*s_retro_deinit)(void);
static bool   (*s_retro_load_game)(const struct retro_game_info *);
static void   (*s_retro_unload_game)(void);
static void   (*s_retro_run)(void);
static void   (*s_retro_get_system_av_info)(struct retro_system_av_info *);
static void  *(*s_retro_get_memory_data)(unsigned);
static size_t (*s_retro_get_memory_size)(unsigned);
static void   (*s_retro_set_environment)(retro_environment_t);
static void   (*s_retro_set_video_refresh)(retro_video_refresh_t);
static void   (*s_retro_set_audio_sample_batch)(retro_audio_sample_batch_t);
static void   (*s_retro_set_input_poll)(retro_input_poll_t);
static void   (*s_retro_set_input_state)(retro_input_state_t);
/* Optional (a core may lack them): save states, reset, controller ports. */
static size_t (*s_retro_serialize_size)(void);
static bool   (*s_retro_serialize)(void *, size_t);
static bool   (*s_retro_unserialize)(const void *, size_t);
static void   (*s_retro_reset)(void);
static void   (*s_retro_set_controller_port_device)(unsigned, unsigned);

static retro_video_refresh_t      s_video_cb;
static retro_audio_sample_batch_t s_audio_cb;
static retro_input_poll_t         s_input_poll_cb;
static retro_input_state_t        s_input_state_cb;

static struct retro_system_av_info s_avinfo;
static char s_srm_path[512] = {0};
static int  s_pixel_format  = RETRO_PIXEL_FORMAT_0RGB1555;

/* ── Hardware render state ───────────────────────────────────────────── */
static struct retro_hw_render_callback s_hw;
static int      s_hw_enabled = 0;
static GLuint   s_hw_fbo = 0, s_hw_color = 0, s_hw_depth = 0;
static unsigned s_hw_mw = 0, s_hw_mh = 0;

static uintptr_t hw_get_framebuffer(void) { return (uintptr_t)s_hw_fbo; }
static retro_proc_address_t hw_get_proc(const char *sym) {
    return (retro_proc_address_t)gl_get_proc_address(sym);
}

/* Core-option overrides. The in-launcher cores (NES/SNES/GB/GBA/CD-i) run fine
   on their defaults; N64 is delegated to RetroArch, so no overrides are needed.
   Returns NULL for every key (core uses its own defaults). */
static const char *core_option_lookup(const char *key) {
    (void)key;
    return NULL;
}

/* ── Log interface (cmd 27) ──────────────────────────────────────────── */
static void core_log(enum retro_log_level level, const char *fmt, ...) {
    /* Drop DEBUG spam — some cores (e.g. mGBA) log every DMA every frame, and on
       Switch stderr is an unbuffered file on the SD card, so the flood of writes
       stalls the main thread and chops the audio. */
    if (level == RETRO_LOG_DEBUG) return;
    static const char *lvl[] = { "DBG", "INF", "WRN", "ERR" };
    fprintf(stderr, "[core %s] ", (level <= RETRO_LOG_ERROR) ? lvl[level] : "?");
    va_list ap; va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

/* ── Performance interface (cmd 28) ──────────────────────────────────── */
static retro_time_t perf_get_time_usec(void) {
#ifdef _WIN32
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (retro_time_t)(c.QuadPart * 1000000 / f.QuadPart);
#else
    return 0;
#endif
}
static uint64_t          perf_get_cpu_features(void) { return 0; } /* generic paths */
static retro_perf_tick_t perf_get_counter(void)      { return 0; }
static void perf_register(struct retro_perf_counter *c) { (void)c; }
static void perf_start(struct retro_perf_counter *c)    { (void)c; }
static void perf_stop(struct retro_perf_counter *c)     { (void)c; }
static void perf_log(void) {}

static bool env_cb(unsigned cmd, void *data) {
    switch (cmd) {
        case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
            if (data) s_pixel_format = *(int*)data;
            return true;
        case 9:    /* GET_SYSTEM_DIRECTORY (BIOS, e.g. CD-i for same_cdi) */
        case 31: { /* GET_SAVE_DIRECTORY */
            static const char *d = "system";
            if (data) *(const char **)data = d;
            return true;
        }
        case 14: { /* SET_HW_RENDER */
            struct retro_hw_render_callback *cb =
                (struct retro_hw_render_callback*)data;
            if (!cb) return false;
            if (cb->context_type != RETRO_HW_CONTEXT_OPENGL &&
                cb->context_type != RETRO_HW_CONTEXT_OPENGL_CORE) {
                fprintf(stderr, "core: hw render type %d unsupported (GL only)\n",
                        (int)cb->context_type);
                return false; /* core should fall back to a GL plugin */
            }
            s_hw = *cb;
            /* Hand the core our framebuffer + proc-address resolvers. */
            cb->get_current_framebuffer = hw_get_framebuffer;
            cb->get_proc_address        = hw_get_proc;
            s_hw.get_current_framebuffer = hw_get_framebuffer;
            s_hw.get_proc_address        = hw_get_proc;
            s_hw_enabled = 1;
            return true;
        }
        case 27: { /* GET_LOG_INTERFACE */
            struct retro_log_callback *cb = (struct retro_log_callback*)data;
            if (cb) cb->log = core_log;
            return cb != NULL;
        }
        case 28: { /* GET_PERF_INTERFACE */
            struct retro_perf_callback *p = (struct retro_perf_callback*)data;
            if (!p) return false;
            p->get_time_usec    = perf_get_time_usec;
            p->get_cpu_features = perf_get_cpu_features;
            p->get_perf_counter = perf_get_counter;
            p->perf_register    = perf_register;
            p->perf_start       = perf_start;
            p->perf_stop        = perf_stop;
            p->perf_log         = perf_log;
            return true;
        }
        case 44: /* SET_HW_SHARED_CONTEXT */
            return true;
        case 71: /* GET_PREFERRED_HW_RENDER */
            if (data) *(unsigned*)data = RETRO_HW_CONTEXT_OPENGL_CORE;
            return true;
        case 15: { /* GET_VARIABLE */
            struct retro_variable *v = (struct retro_variable*)data;
            if (!v) return false;
            v->value = core_option_lookup(v->key);
            return v->value != NULL;
        }
        case 16: /* SET_VARIABLES */
        case 17: /* GET_VARIABLE_UPDATE */
            if (cmd==17 && data) *(bool*)data = false;
            return true;
        case 18: /* SET_SUPPORT_NO_GAME */
        case 3:  /* GET_CAN_DUPE */
            if (cmd==3 && data) *(bool*)data = true;
            return true;
        case 37: /* SET_GEOMETRY — accept silently */
            return true;
        default:
            return false;
    }
}

int core_get_pixel_format(void) { return s_pixel_format; }

#define SYM(var, name) do { \
    void *_p = dylib_sym(s_lib, name); \
    if (!_p) { fprintf(stderr,"core: missing %s\n",name); return false; } \
    memcpy(&(var), &_p, sizeof(_p)); \
} while(0)
#define SYM_OPT(var, name) do { \
    void *_p = dylib_sym(s_lib, name); \
    memcpy(&(var), &_p, sizeof(_p)); \
} while(0)

#ifdef __SWITCH__
/* Statically-linked cores. Each core's libretro API symbols are prefixed at
   build time (objcopy --redefine-syms, see CMakeLists) so several cores can
   coexist in one binary. core_load() picks one by matching the core's name. */
#define CORE_EXTERNS(p) \
    extern void   p##retro_init(void); \
    extern void   p##retro_deinit(void); \
    extern bool   p##retro_load_game(const struct retro_game_info *); \
    extern void   p##retro_unload_game(void); \
    extern void   p##retro_run(void); \
    extern void   p##retro_get_system_av_info(struct retro_system_av_info *); \
    extern void  *p##retro_get_memory_data(unsigned); \
    extern size_t p##retro_get_memory_size(unsigned); \
    extern void   p##retro_set_environment(retro_environment_t); \
    extern void   p##retro_set_video_refresh(retro_video_refresh_t); \
    extern void   p##retro_set_audio_sample_batch(retro_audio_sample_batch_t); \
    extern void   p##retro_set_input_poll(retro_input_poll_t); \
    extern void   p##retro_set_input_state(retro_input_state_t); \
    extern size_t p##retro_serialize_size(void); \
    extern bool   p##retro_serialize(void *, size_t); \
    extern bool   p##retro_unserialize(const void *, size_t); \
    extern void   p##retro_reset(void); \
    extern void   p##retro_set_controller_port_device(unsigned, unsigned);

CORE_EXTERNS(gambatte_)
CORE_EXTERNS(snes9x_)
CORE_EXTERNS(fceumm_)
CORE_EXTERNS(mgba_)

typedef struct {
    const char *match;                 /* substring matched against the core path */
    void   (*init)(void);
    void   (*deinit)(void);
    bool   (*load_game)(const struct retro_game_info *);
    void   (*unload_game)(void);
    void   (*run)(void);
    void   (*get_system_av_info)(struct retro_system_av_info *);
    void  *(*get_memory_data)(unsigned);
    size_t (*get_memory_size)(unsigned);
    void   (*set_environment)(retro_environment_t);
    void   (*set_video_refresh)(retro_video_refresh_t);
    void   (*set_audio_sample_batch)(retro_audio_sample_batch_t);
    void   (*set_input_poll)(retro_input_poll_t);
    void   (*set_input_state)(retro_input_state_t);
    size_t (*serialize_size)(void);
    bool   (*serialize)(void *, size_t);
    bool   (*unserialize)(const void *, size_t);
    void   (*reset)(void);
    void   (*set_controller_port_device)(unsigned, unsigned);
} BuiltinCore;

#define CORE_ENTRY(name, p) { name, \
    p##retro_init, p##retro_deinit, p##retro_load_game, p##retro_unload_game, \
    p##retro_run, p##retro_get_system_av_info, p##retro_get_memory_data, \
    p##retro_get_memory_size, p##retro_set_environment, p##retro_set_video_refresh, \
    p##retro_set_audio_sample_batch, p##retro_set_input_poll, p##retro_set_input_state, \
    p##retro_serialize_size, p##retro_serialize, p##retro_unserialize, p##retro_reset, \
    p##retro_set_controller_port_device }

static const BuiltinCore s_builtins[] = {
    CORE_ENTRY("gambatte", gambatte_),   /* GB / GBC */
    CORE_ENTRY("snes9x",   snes9x_),     /* SNES      */
    CORE_ENTRY("fceumm",   fceumm_),     /* NES       */
    CORE_ENTRY("mgba",     mgba_),       /* GBA       */
};
#endif

bool core_load(const char *dll_path) {
#ifdef __SWITCH__
    /* Pick a statically-linked core by matching the requested core's name.
       Systems without a built-in core report unavailable (the launcher then
       falls back to external launch, e.g. RetroArch chainload for N64). */
    const BuiltinCore *bc = NULL;
    if (dll_path) {
        for (size_t i = 0; i < sizeof(s_builtins)/sizeof(s_builtins[0]); i++)
            if (strstr(dll_path, s_builtins[i].match)) { bc = &s_builtins[i]; break; }
    }
    if (!bc) {
        fprintf(stderr, "core: '%s' not available on Switch yet\n",
                dll_path ? dll_path : "(null)");
        return false;
    }
    s_lib = (DylibHandle)1;   /* non-NULL sentinel so core_unload() proceeds */
    s_retro_init                   = bc->init;
    s_retro_deinit                 = bc->deinit;
    s_retro_load_game              = bc->load_game;
    s_retro_unload_game            = bc->unload_game;
    s_retro_run                    = bc->run;
    s_retro_get_system_av_info     = bc->get_system_av_info;
    s_retro_get_memory_data        = bc->get_memory_data;
    s_retro_get_memory_size        = bc->get_memory_size;
    s_retro_set_environment        = bc->set_environment;
    s_retro_set_video_refresh      = bc->set_video_refresh;
    s_retro_set_audio_sample_batch = bc->set_audio_sample_batch;
    s_retro_set_input_poll         = bc->set_input_poll;
    s_retro_set_input_state        = bc->set_input_state;
    s_retro_serialize_size         = bc->serialize_size;
    s_retro_serialize              = bc->serialize;
    s_retro_unserialize            = bc->unserialize;
    s_retro_reset                  = bc->reset;
    s_retro_set_controller_port_device = bc->set_controller_port_device;
    s_retro_set_environment(env_cb);
    fprintf(stderr, "core: using built-in '%s'\n", bc->match);
    return true;
#else
    s_lib = dylib_load(dll_path);
    if (!s_lib) { fprintf(stderr,"core: cannot load %s\n",dll_path); return false; }
    SYM(s_retro_init,                 "retro_init");
    SYM(s_retro_deinit,               "retro_deinit");
    SYM(s_retro_load_game,            "retro_load_game");
    SYM(s_retro_unload_game,          "retro_unload_game");
    SYM(s_retro_run,                  "retro_run");
    SYM(s_retro_get_system_av_info,   "retro_get_system_av_info");
    SYM(s_retro_get_memory_data,      "retro_get_memory_data");
    SYM(s_retro_get_memory_size,      "retro_get_memory_size");
    SYM(s_retro_set_environment,      "retro_set_environment");
    SYM(s_retro_set_video_refresh,    "retro_set_video_refresh");
    SYM(s_retro_set_audio_sample_batch,"retro_set_audio_sample_batch");
    SYM(s_retro_set_input_poll,       "retro_set_input_poll");
    SYM(s_retro_set_input_state,      "retro_set_input_state");
    SYM_OPT(s_retro_serialize_size,   "retro_serialize_size");
    SYM_OPT(s_retro_serialize,        "retro_serialize");
    SYM_OPT(s_retro_unserialize,      "retro_unserialize");
    SYM_OPT(s_retro_reset,            "retro_reset");
    SYM_OPT(s_retro_set_controller_port_device, "retro_set_controller_port_device");
    /* Only set the environment callback here. The rest must be set after
       the caller registers its video/audio/input callbacks via core_set_*,
       otherwise NULL function pointers get passed to the core and crash on
       the first retro_run / retro_load_game that triggers a video refresh. */
    s_retro_set_environment(env_cb);
    return true;
#endif
}

/* srm_path: explicit save path, or NULL to auto-derive from rom_path */
bool core_load_game(const char *rom_path, const char *srm_path) {
    /* Register callbacks and init the core now — after the host has called
       core_set_video_cb / core_set_audio_cb / core_set_input_*_cb. */
    s_retro_set_video_refresh(s_video_cb);
    s_retro_set_audio_sample_batch(s_audio_cb);
    s_retro_set_input_poll(s_input_poll_cb);
    s_retro_set_input_state(s_input_state_cb);
    s_retro_init();
    FILE *f = fopen(rom_path, "rb");
    if (!f) { fprintf(stderr,"core: cannot open ROM: %s\n",rom_path); return false; }
    fseek(f, 0, SEEK_END); long size = ftell(f); fseek(f, 0, SEEK_SET);
    if (size < 0) size = 0;
    /* Empty files (e.g. a stub that the core opens by path) load as no data. */
    void *data = size > 0 ? malloc((size_t)size) : NULL;
    if (size > 0 && !data) { fclose(f); return false; }
    size_t got = size > 0 ? fread(data, 1, (size_t)size, f) : 0;
    fclose(f);
    if (got != (size_t)size) {
        fprintf(stderr,"core: short read on ROM: %s\n",rom_path);
        free(data);
        return false;
    }

    if (srm_path) {
        strncpy(s_srm_path, srm_path, sizeof(s_srm_path)-1);
    } else {
        strncpy(s_srm_path, rom_path, sizeof(s_srm_path)-1);
        char *dot = strrchr(s_srm_path, '.');
        if (dot) strcpy(dot, ".srm");
        else strncat(s_srm_path, ".srm", sizeof(s_srm_path)-strlen(s_srm_path)-1);
    }

    struct retro_game_info info = { rom_path, data, (size_t)size, NULL };
    bool ok = s_retro_load_game(&info);
    free(data);
    if (!ok) { fprintf(stderr,"core: failed to load ROM\n"); return false; }
    s_retro_get_system_av_info(&s_avinfo);

    void  *sram = s_retro_get_memory_data(RETRO_MEMORY_SAVE_RAM);
    size_t slen = s_retro_get_memory_size(RETRO_MEMORY_SAVE_RAM);
    if (sram && slen > 0) {
        FILE *sf = fopen(s_srm_path, "rb");
        if (sf) {
            size_t got = fread(sram, 1, slen, sf);
            fclose(sf);
            fprintf(stderr,"core: loaded SRAM from %s (%zu bytes)\n",s_srm_path,got);
        }
    }
    return true;
}

void core_run(void)  { s_retro_run(); }

/* ── Save states ─────────────────────────────────────────────────────── */
size_t core_state_size(void) {
    if (!s_lib || !s_retro_serialize_size || !s_retro_serialize) return 0;
    return s_retro_serialize_size();
}

bool core_state_save(void *buf, size_t size) {
    if (!s_lib || !s_retro_serialize || !buf || !size) return false;
    return s_retro_serialize(buf, size);
}

bool core_state_load(const void *buf, size_t size) {
    if (!s_lib || !s_retro_unserialize || !buf || !size) return false;
    return s_retro_unserialize(buf, size);
}

bool core_can_reset(void) { return s_lib && s_retro_reset; }
void core_reset(void)     { if (s_lib && s_retro_reset) s_retro_reset(); }

void core_set_port_device(unsigned port, unsigned device) {
    if (s_lib && s_retro_set_controller_port_device)
        s_retro_set_controller_port_device(port, device);
}

void core_save_sram(void) {
    if (!s_retro_get_memory_data || !s_srm_path[0]) return;
    void  *sram = s_retro_get_memory_data(RETRO_MEMORY_SAVE_RAM);
    size_t slen = s_retro_get_memory_size(RETRO_MEMORY_SAVE_RAM);
    if (!sram || slen==0) return;
    FILE *f = fopen(s_srm_path, "wb");
    if (f) { fwrite(sram, 1, slen, f); fclose(f);
             fprintf(stderr,"core: saved SRAM to %s\n",s_srm_path); }
}

void core_unload(void) {
    if (!s_lib) return;
    core_save_sram();
    core_hw_teardown();   /* context_destroy + free FBO while core still loaded */
    s_retro_unload_game();
    s_retro_deinit();
    dylib_close(s_lib);
    s_lib = NULL;
    s_srm_path[0] = '\0';
    s_retro_serialize_size = NULL;
    s_retro_serialize = NULL;
    s_retro_unserialize = NULL;
    s_retro_reset = NULL;
    s_retro_set_controller_port_device = NULL;
}

void core_set_video_cb(retro_video_refresh_t cb)           { s_video_cb = cb; }
void core_set_audio_cb(retro_audio_sample_batch_t cb)      { s_audio_cb = cb; }
void core_set_input_poll_cb(retro_input_poll_t cb)         { s_input_poll_cb = cb; }
void core_set_input_state_cb(retro_input_state_t cb)       { s_input_state_cb = cb; }

const uint8_t *core_get_wram(void) {
    if (!s_retro_get_memory_data) return NULL;
    const uint8_t *sys = (const uint8_t*)s_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
    if (sys) return sys;
    return (const uint8_t*)s_retro_get_memory_data(RETRO_MEMORY_SAVE_RAM);
}

CoreAVInfo core_get_avinfo(void) {
    CoreAVInfo av;
    av.fps         = s_avinfo.timing.fps         > 0 ? s_avinfo.timing.fps         : 60.0;
    av.sample_rate = s_avinfo.timing.sample_rate > 0 ? s_avinfo.timing.sample_rate : 44100.0;
    av.width       = s_avinfo.geometry.base_width  > 0 ? s_avinfo.geometry.base_width  : 256;
    av.height      = s_avinfo.geometry.base_height > 0 ? s_avinfo.geometry.base_height : 224;
    /* Display aspect: prefer the core's reported ratio (e.g. GBA 3:2); fall back
       to 4:3 when the core doesn't set one (keeps NES/SNES looking right). */
    av.aspect      = s_avinfo.geometry.aspect_ratio > 0.0f
                     ? s_avinfo.geometry.aspect_ratio : (4.0f/3.0f);
    return av;
}

/* ── Hardware rendering ──────────────────────────────────────────────── */
int core_hw_enabled(void)   { return s_hw_enabled; }
unsigned core_hw_texture(void) { return s_hw_color; }
unsigned core_hw_fbo(void)     { return s_hw_fbo; }
unsigned core_hw_max_w(void)   { return s_hw_mw; }
unsigned core_hw_max_h(void)   { return s_hw_mh; }
int core_hw_bottom_left(void)  { return s_hw.bottom_left_origin ? 1 : 0; }

void core_hw_setup(void) {
    if (!s_hw_enabled) return;
    s_hw_mw = s_avinfo.geometry.max_width;
    s_hw_mh = s_avinfo.geometry.max_height;
    if (s_hw_mw == 0) s_hw_mw = 640;
    if (s_hw_mh == 0) s_hw_mh = 480;

    /* Color attachment */
    glGenTextures(1, &s_hw_color);
    glBindTexture(GL_TEXTURE_2D, s_hw_color);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei)s_hw_mw, (GLsizei)s_hw_mh,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    gl_GenFramebuffers(1, &s_hw_fbo);
    gl_BindFramebuffer(GL_FRAMEBUFFER, s_hw_fbo);
    gl_FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                            GL_TEXTURE_2D, s_hw_color, 0);

    /* Depth / stencil renderbuffer if the core requested it */
    if (s_hw.depth) {
        gl_GenRenderbuffers(1, &s_hw_depth);
        gl_BindRenderbuffer(GL_RENDERBUFFER, s_hw_depth);
        if (s_hw.stencil) {
            gl_RenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8,
                                   (GLsizei)s_hw_mw, (GLsizei)s_hw_mh);
            gl_FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                                       GL_RENDERBUFFER, s_hw_depth);
        } else {
            gl_RenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24,
                                   (GLsizei)s_hw_mw, (GLsizei)s_hw_mh);
            gl_FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                       GL_RENDERBUFFER, s_hw_depth);
        }
        gl_BindRenderbuffer(GL_RENDERBUFFER, 0);
    }
    if (gl_CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        fprintf(stderr, "core: warning — hw FBO incomplete\n");
    gl_BindFramebuffer(GL_FRAMEBUFFER, 0);

    if (s_hw.context_reset) s_hw.context_reset();
}

void core_hw_teardown(void) {
    if (!s_hw_enabled) return;
    if (s_hw.context_destroy) s_hw.context_destroy();
    if (s_hw_fbo)   { gl_DeleteFramebuffers(1, &s_hw_fbo);   s_hw_fbo = 0; }
    if (s_hw_color) { glDeleteTextures(1, &s_hw_color);      s_hw_color = 0; }
    if (s_hw_depth) { gl_DeleteRenderbuffers(1, &s_hw_depth); s_hw_depth = 0; }
    s_hw_mw = s_hw_mh = 0;
    s_hw_enabled = 0;   /* reset for the next core (may be CPU-only) */
    memset(&s_hw, 0, sizeof(s_hw));
}
