#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define RETRO_API_VERSION 1

#define RETRO_MEMORY_SAVE_RAM           0
#define RETRO_MEMORY_RTC_RAM            1
#define RETRO_MEMORY_SYSTEM_RAM         2
#define RETRO_MEMORY_VIDEO_RAM          3

#define RETRO_ENVIRONMENT_SET_PIXEL_FORMAT  10
#define RETRO_ENVIRONMENT_SET_HW_RENDER     14
#define RETRO_ENVIRONMENT_GET_LOG_INTERFACE 27
#define RETRO_ENVIRONMENT_SET_HW_SHARED_CONTEXT   44
#define RETRO_ENVIRONMENT_GET_PREFERRED_HW_RENDER 71

/* Sentinel passed as `data` to video_refresh when the frame is in the
   hardware framebuffer (already rendered to the FBO) rather than in RAM. */
#define RETRO_HW_FRAME_BUFFER_VALID ((void*)-1)

enum retro_hw_context_type {
    RETRO_HW_CONTEXT_NONE             = 0,
    RETRO_HW_CONTEXT_OPENGL          = 1, /* OpenGL 2.x compat */
    RETRO_HW_CONTEXT_OPENGLES2       = 2,
    RETRO_HW_CONTEXT_OPENGL_CORE     = 3, /* OpenGL core profile */
    RETRO_HW_CONTEXT_OPENGLES3       = 4,
    RETRO_HW_CONTEXT_OPENGLES_VERSION = 5,
    RETRO_HW_CONTEXT_VULKAN          = 6,
    RETRO_HW_CONTEXT_D3D11           = 7,
    RETRO_HW_CONTEXT_D3D10           = 8,
    RETRO_HW_CONTEXT_D3D12           = 9,
    RETRO_HW_CONTEXT_D3D9            = 10,
    RETRO_HW_CONTEXT_DUMMY           = 0x7fffffff
};

typedef void (*retro_hw_context_reset_t)(void);
typedef uintptr_t (*retro_hw_get_current_framebuffer_t)(void);
typedef void (*retro_proc_address_t)(void);
typedef retro_proc_address_t (*retro_hw_get_proc_address_t)(const char *sym);

/* Field order/types MUST match upstream libretro.h — the core reads/writes
   this struct through the pointer we pass to SET_HW_RENDER. */
struct retro_hw_render_callback {
    enum retro_hw_context_type         context_type;
    retro_hw_context_reset_t           context_reset;
    retro_hw_get_current_framebuffer_t get_current_framebuffer;
    retro_hw_get_proc_address_t        get_proc_address;
    bool                               depth;
    bool                               stencil;
    bool                               bottom_left_origin;
    unsigned                           version_major;
    unsigned                           version_minor;
    bool                               cache_context;
    retro_hw_context_reset_t           context_destroy;
    bool                               debug_context;
};

#define RETRO_PIXEL_FORMAT_0RGB1555  0
#define RETRO_PIXEL_FORMAT_XRGB8888  1
#define RETRO_PIXEL_FORMAT_RGB565    2

#define RETRO_DEVICE_JOYPAD           1
#define RETRO_DEVICE_ID_JOYPAD_B      0
#define RETRO_DEVICE_ID_JOYPAD_Y      1
#define RETRO_DEVICE_ID_JOYPAD_SELECT 2
#define RETRO_DEVICE_ID_JOYPAD_START  3
#define RETRO_DEVICE_ID_JOYPAD_UP     4
#define RETRO_DEVICE_ID_JOYPAD_DOWN   5
#define RETRO_DEVICE_ID_JOYPAD_LEFT   6
#define RETRO_DEVICE_ID_JOYPAD_RIGHT  7
#define RETRO_DEVICE_ID_JOYPAD_A      8
#define RETRO_DEVICE_ID_JOYPAD_X      9
#define RETRO_DEVICE_ID_JOYPAD_L      10
#define RETRO_DEVICE_ID_JOYPAD_R      11
#define RETRO_DEVICE_ID_JOYPAD_L2     12
#define RETRO_DEVICE_ID_JOYPAD_R2     13

struct retro_game_info {
    const char *path;
    const void *data;
    size_t      size;
    const char *meta;
};

struct retro_system_av_info {
    struct {
        unsigned base_width;
        unsigned base_height;
        unsigned max_width;
        unsigned max_height;
        float    aspect_ratio;
    } geometry;
    struct {
        double fps;
        double sample_rate;
    } timing;
};

struct retro_variable {
    const char *key;
    const char *value;
};

/* Log interface (cmd 27). Cores log through this; a NULL log pointer that the
   core doesn't guard against will crash it. */
enum retro_log_level {
    RETRO_LOG_DEBUG = 0,
    RETRO_LOG_INFO,
    RETRO_LOG_WARN,
    RETRO_LOG_ERROR,
    RETRO_LOG_DUMMY = 0x7fffffff
};
typedef void (*retro_log_printf_t)(enum retro_log_level level, const char *fmt, ...);
struct retro_log_callback { retro_log_printf_t log; };

/* Performance interface (cmd 28). mupen64plus_next requires this — it calls
   get_cpu_features() during load to pick SIMD code paths; a NULL pointer here
   crashes the core. */
typedef uint64_t retro_perf_tick_t;
typedef int64_t  retro_time_t;
struct retro_perf_counter {
    const char       *ident;
    retro_perf_tick_t start;
    retro_perf_tick_t total;
    retro_perf_tick_t call_cnt;
    bool              registered;
};
typedef retro_time_t      (*retro_perf_get_time_usec_t)(void);
typedef retro_perf_tick_t (*retro_perf_get_counter_t)(void);
typedef uint64_t          (*retro_get_cpu_features_t)(void);
typedef void              (*retro_perf_register_t)(struct retro_perf_counter*);
typedef void              (*retro_perf_start_t)(struct retro_perf_counter*);
typedef void              (*retro_perf_stop_t)(struct retro_perf_counter*);
typedef void              (*retro_perf_log_t)(void);
struct retro_perf_callback {
    retro_perf_get_time_usec_t get_time_usec;
    retro_get_cpu_features_t   get_cpu_features;
    retro_perf_get_counter_t   get_perf_counter;
    retro_perf_register_t      perf_register;
    retro_perf_start_t         perf_start;
    retro_perf_stop_t          perf_stop;
    retro_perf_log_t           perf_log;
};

typedef bool    (*retro_environment_t)(unsigned cmd, void *data);
typedef void    (*retro_video_refresh_t)(const void *data, unsigned width, unsigned height, size_t pitch);
typedef void    (*retro_audio_sample_t)(int16_t left, int16_t right);
typedef size_t  (*retro_audio_sample_batch_t)(const int16_t *data, size_t frames);
typedef void    (*retro_input_poll_t)(void);
typedef int16_t (*retro_input_state_t)(unsigned port, unsigned device, unsigned index, unsigned id);
