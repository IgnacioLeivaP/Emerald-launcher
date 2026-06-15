#include "sfx.h"
#include "paths.h"
#include <SDL2/SDL.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static SDL_AudioDeviceID s_dev = 0;
static SDL_AudioSpec     s_fmt;

typedef struct { Uint8 *data; Uint32 len; } Clip;

static Clip s_nav            = {NULL, 0};
static Clip s_confirm        = {NULL, 0};
static Clip s_back           = {NULL, 0};
static Clip s_boot           = {NULL, 0};
static Clip s_enter_game     = {NULL, 0};
static Clip s_open_menu      = {NULL, 0};
static Clip s_back_launcher  = {NULL, 0};
static Clip s_next_week      = {NULL, 0};

static Clip load_wav(const char *path) {
    Clip c = {NULL, 0};
    SDL_AudioSpec spec;
    Uint8 *buf = NULL;
    Uint32 len = 0;

    if (!SDL_LoadWAV(path, &spec, &buf, &len)) {
        fprintf(stderr, "sfx: cannot load %s: %s\n", path, SDL_GetError());
        return c;
    }

    SDL_AudioCVT cvt;
    int need = SDL_BuildAudioCVT(&cvt,
        spec.format, spec.channels, spec.freq,
        s_fmt.format, s_fmt.channels, s_fmt.freq);

    if (need < 0) {
        fprintf(stderr, "sfx: BuildAudioCVT failed for %s\n", path);
        SDL_FreeWAV(buf);
        return c;
    }

    if (need > 0) {
        cvt.len = (int)len;
        cvt.buf = (Uint8 *)malloc((size_t)(cvt.len * cvt.len_mult));
        if (!cvt.buf) { SDL_FreeWAV(buf); return c; }
        memcpy(cvt.buf, buf, len);
        SDL_ConvertAudio(&cvt);
        c.data = cvt.buf;
        c.len  = (Uint32)cvt.len_cvt;
    } else {
        c.data = (Uint8 *)malloc(len);
        if (!c.data) { SDL_FreeWAV(buf); return c; }
        memcpy(c.data, buf, len);
        c.len = len;
    }

    SDL_FreeWAV(buf);
    return c;
}

static void clip_free(Clip *c) { free(c->data); c->data = NULL; c->len = 0; }

/* Switch audio is natively 48 kHz; a small buffer underruns on boot/transitions. */
#ifdef __SWITCH__
#  define SFX_FREQ    48000
#  define SFX_SAMPLES 2048
#else
#  define SFX_FREQ    44100
#  define SFX_SAMPLES 512
#endif

bool sfx_init(void) {
    SDL_AudioSpec want = {0};
    want.freq     = SFX_FREQ;
    want.format   = AUDIO_S16SYS;
    want.channels = 2;
    want.samples  = SFX_SAMPLES;
    s_dev = SDL_OpenAudioDevice(NULL, 0, &want, &s_fmt, 0);
    if (!s_dev) { fprintf(stderr, "sfx: %s\n", SDL_GetError()); return false; }
    SDL_PauseAudioDevice(s_dev, 0);

    s_nav           = load_wav(ASSET("sounds/updown.wav"));
    s_confirm       = load_wav(ASSET("sounds/enter.wav"));
    s_back          = load_wav(ASSET("sounds/backinmenu.wav"));
    s_boot          = load_wav(ASSET("sounds/boot.wav"));
    s_enter_game    = load_wav(ASSET("sounds/entergame.wav"));
    s_open_menu     = load_wav(ASSET("sounds/openmenuingame.wav"));
    s_back_launcher = load_wav(ASSET("sounds/backtolauncher.wav"));
    s_next_week     = load_wav(ASSET("sounds/youcangotonextweek.wav"));

    return true;
}

/* Release the SFX audio device so a game can open its own (some backends —
   e.g. the Switch — allow only one output device at a time). Clips stay loaded. */
void sfx_suspend(void) {
    if (s_dev) { SDL_CloseAudioDevice(s_dev); s_dev = 0; }
}
void sfx_resume(void) {
    if (s_dev) return;
    SDL_AudioSpec want = {0};
    want.freq = SFX_FREQ; want.format = AUDIO_S16SYS; want.channels = 2; want.samples = SFX_SAMPLES;
    s_dev = SDL_OpenAudioDevice(NULL, 0, &want, &s_fmt, 0);
    if (s_dev) SDL_PauseAudioDevice(s_dev, 0);
}

void sfx_shutdown(void) {
    if (s_dev) { SDL_CloseAudioDevice(s_dev); s_dev = 0; }
    clip_free(&s_nav);
    clip_free(&s_confirm);
    clip_free(&s_back);
    clip_free(&s_boot);
    clip_free(&s_enter_game);
    clip_free(&s_open_menu);
    clip_free(&s_back_launcher);
    clip_free(&s_next_week);
}

static void play(Clip *c) {
    if (!s_dev || !c->data || !c->len) return;
    SDL_ClearQueuedAudio(s_dev);
    SDL_QueueAudio(s_dev, c->data, c->len);
}

void sfx_play_nav(void)              { play(&s_nav); }
void sfx_play_confirm(void)          { play(&s_confirm); }
void sfx_play_back(void)             { play(&s_back); }
void sfx_play_boot(void)             { play(&s_boot); }
void sfx_play_enter_game(void)       { play(&s_enter_game); }
void sfx_play_open_menu(void)        { play(&s_open_menu); }
void sfx_play_back_to_launcher(void) { play(&s_back_launcher); }
void sfx_play_next_week(void)        { play(&s_next_week); }
