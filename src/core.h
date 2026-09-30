#pragma once
#ifdef __cplusplus
extern "C" {
#endif
#include "el_libretro.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct { double fps; double sample_rate; unsigned width; unsigned height; float aspect; } CoreAVInfo;

bool   core_load(const char *dll_path);
bool   core_load_game(const char *rom_path, const char *srm_path);
void   core_run(void);
void   core_save_sram(void);
void   core_unload(void);

/* Save states (0 / false when the core doesn't support them). The size can
   change between calls for some cores: ask right before saving. */
size_t core_state_size(void);
bool   core_state_save(void *buf, size_t size);
bool   core_state_load(const void *buf, size_t size);
bool   core_can_reset(void);
void   core_reset(void);
/* retro_set_controller_port_device (e.g. RETRO_DEVICE_JOYPAD for player 2). */
void   core_set_port_device(unsigned port, unsigned device);

/* Access system RAM (WRAM for SNES) — returns NULL if unavailable */
const uint8_t *core_get_wram(void);

CoreAVInfo core_get_avinfo(void);
int        core_get_pixel_format(void);

/* ── Hardware rendering (OpenGL cores like mupen64plus_next) ──────────────
   When a core requests SET_HW_RENDER, it renders into an FBO we own instead
   of producing a CPU framebuffer. */
int      core_hw_enabled(void);     /* 1 if current core uses hw render */
void     core_hw_setup(void);       /* create FBO + call core's context_reset */
void     core_hw_teardown(void);    /* context_destroy + free FBO */
unsigned core_hw_texture(void);     /* GL color texture id of the FBO */
unsigned core_hw_fbo(void);         /* GL framebuffer object id */
unsigned core_hw_max_w(void);
unsigned core_hw_max_h(void);
int      core_hw_bottom_left(void); /* 1 if origin is bottom-left (GL) */

void core_set_video_cb(retro_video_refresh_t cb);
void core_set_audio_cb(retro_audio_sample_batch_t cb);
void core_set_input_poll_cb(retro_input_poll_t cb);
void core_set_input_state_cb(retro_input_state_t cb);

#ifdef __cplusplus
}
#endif
