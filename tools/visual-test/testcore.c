/* A tiny libretro core for testing the launcher's in-game path without real
   emulators or ROMs: it draws a moving gradient (4:3), plays silence at
   48 kHz, supports save states (the frame counter), reset and controller
   ports, and after two seconds reports every Ancient Stone Tablets tablet
   as collected in its system RAM, so "week complete" can be tested too.

   Build (Linux):  cc -shared -fPIC -O2 -o testcore.so testcore.c
   make_sandbox.py copies it under every core name db.json uses. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

struct retro_game_info { const char *path; const void *data; size_t size; const char *meta; };
struct retro_game_geometry { unsigned base_width, base_height, max_width, max_height; float aspect_ratio; };
struct retro_system_timing { double fps, sample_rate; };
struct retro_system_av_info { struct retro_game_geometry geometry; struct retro_system_timing timing; };
typedef bool (*env_t)(unsigned, void *);
typedef void (*video_t)(const void *, unsigned, unsigned, size_t);
typedef size_t (*audio_batch_t)(const int16_t *, size_t);
typedef void (*poll_t)(void);
typedef int16_t (*state_t)(unsigned, unsigned, unsigned, unsigned);

static env_t env;
static video_t video;
static audio_batch_t audio;
static poll_t poll_cb;
static state_t state;
static uint32_t fb[256 * 224];
static int16_t snd[2 * 800];
static unsigned frame;
static unsigned char wram[0x20000];

void retro_set_environment(env_t e) { env = e; }
void retro_set_video_refresh(video_t v) { video = v; }
void retro_set_audio_sample_batch(audio_batch_t a) { audio = a; }
void retro_set_input_poll(poll_t p) { poll_cb = p; }
void retro_set_input_state(state_t s) { state = s; }
void retro_init(void) {}
void retro_deinit(void) {}
unsigned retro_api_version(void) { return 1; }

bool retro_load_game(const struct retro_game_info *i) {
    (void)i;
    int fmt = 1;                               /* RETRO_PIXEL_FORMAT_XRGB8888 */
    env(10, &fmt);                             /* SET_PIXEL_FORMAT */
    frame = 0;
    return true;
}
void retro_unload_game(void) {}

void retro_get_system_av_info(struct retro_system_av_info *av) {
    memset(av, 0, sizeof(*av));
    av->geometry.base_width = 256;
    av->geometry.base_height = 224;
    av->geometry.max_width = 256;
    av->geometry.max_height = 224;
    av->geometry.aspect_ratio = 4.0f / 3.0f;
    av->timing.fps = 60.0;
    av->timing.sample_rate = 48000.0;
}

void retro_run(void) {
    poll_cb();
    frame++;
    /* Holding A (id 8) on player 1 turns the picture blue: input reaches the core. */
    const uint32_t blue = state && state(0, 1, 0, 8) ? 0xE0 : 0x60;
    for (int y = 0; y < 224; y++)
        for (int x = 0; x < 256; x++)
            fb[y * 256 + x] = ((uint32_t)((x + frame) & 255) << 16) | ((uint32_t)(y & 255) << 8) | blue;
    video(fb, 256, 224, 256 * 4);
    audio(snd, 800);
}

/* System RAM: after 2 s the "tablets" byte (0xF37C) reads as all collected. */
void *retro_get_memory_data(unsigned id) {
    if (id != 2) return NULL;                  /* RETRO_MEMORY_SYSTEM_RAM */
    wram[0xF37C] = frame > 120 ? 0xFF : 0;
    return wram;
}
size_t retro_get_memory_size(unsigned id) { return id == 2 ? sizeof(wram) : 0; }

/* Save states: the frame counter is the whole state. */
size_t retro_serialize_size(void) { return sizeof(frame); }
bool retro_serialize(void *data, size_t size) {
    if (size < sizeof(frame)) return false;
    memcpy(data, &frame, sizeof(frame));
    return true;
}
bool retro_unserialize(const void *data, size_t size) {
    if (size < sizeof(frame)) return false;
    memcpy(&frame, data, sizeof(frame));
    return true;
}
void retro_reset(void) { frame = 0; }
void retro_set_controller_port_device(unsigned port, unsigned device) { (void)port; (void)device; }
