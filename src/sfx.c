#include "sfx.h"
#include "paths.h"
#include <SDL2/SDL.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* stb_vorbis (public domain, include/stb_vorbis.c) decodes OGG menu music;
   only its decoder is needed here. */
#define STB_VORBIS_NO_PUSHDATA_API
#include "stb_vorbis.c"

/* Menu sounds and music, mixed in the audio callback: up to MAX_VOICES
   sound effects at once over a looping music track (WAV, or OGG streamed
   and resampled on the fly so a long track costs no memory or load time). */

/* 48 kHz like the Switch and the game audio (audio.cpp), so the same clips
   can also be mixed into a running game. A small buffer underruns on the
   Switch during boot / transitions. */
#define SFX_FREQ 48000
#ifdef __SWITCH__
#  define SFX_SAMPLES 2048
#else
#  define SFX_SAMPLES 512
#endif
#define MAX_VOICES 4

static SDL_AudioDeviceID s_dev = 0;
static int               s_rate = SFX_FREQ;

typedef struct { Sint16 *data; Uint32 frames; } Clip;       /* stereo S16 at s_rate */
typedef struct { const Clip *clip; Uint32 pos; } Voice;

static Clip  s_nav, s_confirm, s_back, s_boot, s_enter_game, s_open_menu, s_back_launcher, s_next_week;
static Voice s_voices[MAX_VOICES];

static struct {
    int            kind;              /* 0 none, 1 wav, 2 ogg                    */
    Clip           wav;
    Uint32         wav_pos;
    stb_vorbis    *ogg;
    unsigned char *file;              /* the OGG file, decoded as it plays       */
    int            src_rate;
    short          buf[2048 * 2];     /* decoded frames waiting to be resampled  */
    int            buf_frames, buf_pos;
    double         frac;              /* resampler position between prev and cur */
    float          prev[2], cur[2];
    float          volume;            /* 0..1                                    */
    float          gain;              /* fade in / out                           */
    int            playing;
} s_music;

/* ── Loading ─────────────────────────────────────────────────────────── */
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
    int need = SDL_BuildAudioCVT(&cvt, spec.format, spec.channels, spec.freq, AUDIO_S16SYS, 2, s_rate);
    if (need < 0) {
        fprintf(stderr, "sfx: BuildAudioCVT failed for %s\n", path);
        SDL_FreeWAV(buf);
        return c;
    }
    cvt.len = (int)len;
    cvt.buf = (Uint8 *)malloc((size_t)len * (size_t)(cvt.len_mult > 0 ? cvt.len_mult : 1));
    if (!cvt.buf) { SDL_FreeWAV(buf); return c; }
    memcpy(cvt.buf, buf, len);
    SDL_FreeWAV(buf);
    if (need > 0 && SDL_ConvertAudio(&cvt) != 0) { free(cvt.buf); return c; }
    c.data = (Sint16 *)cvt.buf;
    c.frames = (Uint32)((need > 0 ? cvt.len_cvt : cvt.len) / 4);
    return c;
}

static void clip_free(Clip *c) { free(c->data); c->data = NULL; c->frames = 0; }

/* ── Mixing (audio thread) ───────────────────────────────────────────── */
static int ogg_next_frame(float out[2]) {
    if (s_music.buf_pos >= s_music.buf_frames) {
        int n = stb_vorbis_get_samples_short_interleaved(s_music.ogg, 2, s_music.buf, 2048 * 2);
        if (n <= 0) {                                   /* end: loop */
            stb_vorbis_seek_start(s_music.ogg);
            n = stb_vorbis_get_samples_short_interleaved(s_music.ogg, 2, s_music.buf, 2048 * 2);
            if (n <= 0) return 0;
        }
        s_music.buf_frames = n;
        s_music.buf_pos = 0;
    }
    out[0] = (float)s_music.buf[s_music.buf_pos * 2];
    out[1] = (float)s_music.buf[s_music.buf_pos * 2 + 1];
    s_music.buf_pos++;
    return 1;
}

static void music_frame(float *l, float *r) {
    *l = *r = 0.0f;
    if (s_music.kind == 1) {
        const Sint16 *f = s_music.wav.data + (size_t)s_music.wav_pos * 2;
        *l = (float)f[0];
        *r = (float)f[1];
        if (++s_music.wav_pos >= s_music.wav.frames) s_music.wav_pos = 0;
    } else if (s_music.kind == 2) {
        const double step = (double)s_music.src_rate / (double)s_rate;
        while (s_music.frac >= 1.0) {
            s_music.prev[0] = s_music.cur[0];
            s_music.prev[1] = s_music.cur[1];
            if (!ogg_next_frame(s_music.cur)) return;
            s_music.frac -= 1.0;
        }
        const float t = (float)s_music.frac;
        *l = s_music.prev[0] + (s_music.cur[0] - s_music.prev[0]) * t;
        *r = s_music.prev[1] + (s_music.cur[1] - s_music.prev[1]) * t;
        s_music.frac += step;
    }
}

static void mix_cb(void *userdata, Uint8 *stream, int len) {
    (void)userdata;
    Sint16 *out = (Sint16 *)stream;
    const int frames = len / 4;
    const float fade_step = 1.0f / (0.8f * (float)s_rate);       /* 0.8 s fades */
    for (int i = 0; i < frames; i++) {
        float l = 0.0f, r = 0.0f;
        if (s_music.kind && (s_music.playing || s_music.gain > 0.0f)) {
            if (s_music.playing && s_music.gain < 1.0f) s_music.gain += fade_step;
            if (!s_music.playing) s_music.gain -= fade_step;
            if (s_music.gain > 1.0f) s_music.gain = 1.0f;
            if (s_music.gain < 0.0f) s_music.gain = 0.0f;
            float ml, mr;
            music_frame(&ml, &mr);
            const float g = s_music.gain * s_music.volume;
            l += ml * g;
            r += mr * g;
        }
        for (int v = 0; v < MAX_VOICES; v++) {
            Voice *vo = &s_voices[v];
            if (!vo->clip) continue;
            const Sint16 *f = vo->clip->data + (size_t)vo->pos * 2;
            l += (float)f[0];
            r += (float)f[1];
            if (++vo->pos >= vo->clip->frames) vo->clip = NULL;
        }
        out[i * 2]     = (Sint16)(l > 32767.0f ? 32767 : (l < -32768.0f ? -32768 : (int)l));
        out[i * 2 + 1] = (Sint16)(r > 32767.0f ? 32767 : (r < -32768.0f ? -32768 : (int)r));
    }
}

/* ── Device ──────────────────────────────────────────────────────────── */
static bool open_device(void) {
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq     = SFX_FREQ;
    want.format   = AUDIO_S16SYS;
    want.channels = 2;
    want.samples  = SFX_SAMPLES;
    want.callback = mix_cb;
    s_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);    /* SDL converts if needed */
    if (!s_dev) { fprintf(stderr, "sfx: %s\n", SDL_GetError()); return false; }
    s_rate = have.freq;
    SDL_PauseAudioDevice(s_dev, 0);
    return true;
}

bool sfx_init(void) {
    s_music.volume = 0.6f;
    if (!open_device()) return false;
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
   e.g. the Switch — allow only one output device at a time). Clips and the
   music position stay; the music fades back in on resume. */
void sfx_suspend(void) {
    if (s_dev) { SDL_CloseAudioDevice(s_dev); s_dev = 0; }
    s_music.gain = 0.0f;           /* sounds still playing carry on in the game's mix */
}

void sfx_resume(void) {
    if (!s_dev) open_device();
}

static void music_free(void) {
    if (s_music.ogg) stb_vorbis_close(s_music.ogg);
    s_music.ogg = NULL;
    free(s_music.file);
    s_music.file = NULL;
    clip_free(&s_music.wav);
    s_music.kind = 0;
}

void sfx_shutdown(void) {
    if (s_dev) { SDL_CloseAudioDevice(s_dev); s_dev = 0; }
    music_free();
    clip_free(&s_nav);
    clip_free(&s_confirm);
    clip_free(&s_back);
    clip_free(&s_boot);
    clip_free(&s_enter_game);
    clip_free(&s_open_menu);
    clip_free(&s_back_launcher);
    clip_free(&s_next_week);
}

/* ── Music ───────────────────────────────────────────────────────────── */
static int ends_with(const char *s, const char *suffix) {
    size_t a = strlen(s), b = strlen(suffix);
    if (a < b) return 0;
    for (size_t i = 0; i < b; i++)
        if (SDL_tolower((unsigned char)s[a - b + i]) != suffix[i]) return 0;
    return 1;
}

bool sfx_music_load(const char *path) {
    if (!path || !path[0]) return false;
    if (s_dev) SDL_LockAudioDevice(s_dev);
    music_free();
    bool ok = false;
    if (ends_with(path, ".ogg")) {
        SDL_RWops *rw = SDL_RWFromFile(path, "rb");
        Sint64 size = rw ? SDL_RWsize(rw) : -1;
        if (rw && size > 0) {
            s_music.file = (unsigned char *)malloc((size_t)size);
            if (s_music.file && SDL_RWread(rw, s_music.file, 1, (size_t)size) == (size_t)size) {
                int err = 0;
                s_music.ogg = stb_vorbis_open_memory(s_music.file, (int)size, &err, NULL);
                if (s_music.ogg) {
                    stb_vorbis_info info = stb_vorbis_get_info(s_music.ogg);
                    s_music.src_rate = (int)info.sample_rate;
                    s_music.kind = 2;
                    ok = true;
                } else {
                    fprintf(stderr, "music: %s isn't a readable OGG (error %d)\n", path, err);
                }
            }
        }
        if (rw) SDL_RWclose(rw);
    } else {
        s_music.wav = load_wav(path);
        if (s_music.wav.data) { s_music.kind = 1; ok = true; }
    }
    if (!ok) music_free();
    s_music.wav_pos = 0;
    s_music.buf_frames = s_music.buf_pos = 0;
    s_music.frac = 1.0;
    s_music.prev[0] = s_music.prev[1] = s_music.cur[0] = s_music.cur[1] = 0.0f;
    s_music.gain = 0.0f;
    if (s_dev) SDL_UnlockAudioDevice(s_dev);
    if (!ok) fprintf(stderr, "music: couldn't load %s\n", path);
    return ok;
}

bool sfx_music_loaded(void) { return s_music.kind != 0; }

void sfx_music_play(bool on) {
    if (s_dev) SDL_LockAudioDevice(s_dev);
    s_music.playing = on && s_music.kind != 0;
    if (s_dev) SDL_UnlockAudioDevice(s_dev);
}

void sfx_music_set_volume(float v) {
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    if (s_dev) SDL_LockAudioDevice(s_dev);
    s_music.volume = v;
    if (s_dev) SDL_UnlockAudioDevice(s_dev);
}

/* ── Sound effects ───────────────────────────────────────────────────── */
void sfx_mix_game(short *stereo, size_t frames) {
    if (s_dev || !stereo) return;              /* the menu device is playing them */
    for (int v = 0; v < MAX_VOICES; v++) {
        Voice *vo = &s_voices[v];
        for (size_t i = 0; vo->clip && i < frames; i++) {
            const Sint16 *f = vo->clip->data + (size_t)vo->pos * 2;
            for (int c = 0; c < 2; c++) {
                int m = stereo[i * 2 + c] + f[c];
                stereo[i * 2 + c] = (short)(m > 32767 ? 32767 : (m < -32768 ? -32768 : m));
            }
            if (++vo->pos >= vo->clip->frames) vo->clip = NULL;
        }
    }
}

static void play(const Clip *c) {
    if (!c->data || !c->frames) return;
    /* In a game the menu device is closed: the voice is mixed into the
       game's audio instead (sfx_mix_game, same thread, no lock needed). */
    if (s_dev) SDL_LockAudioDevice(s_dev);
    /* The same sound restarts instead of stacking (holding the d-pad);
       otherwise take a free voice, or the one that has played longest. */
    int slot = -1;
    for (int v = 0; v < MAX_VOICES && slot < 0; v++) if (s_voices[v].clip == c) slot = v;
    for (int v = 0; v < MAX_VOICES && slot < 0; v++) if (!s_voices[v].clip) slot = v;
    if (slot < 0) {
        slot = 0;
        for (int v = 1; v < MAX_VOICES; v++) if (s_voices[v].pos > s_voices[slot].pos) slot = v;
    }
    s_voices[slot].clip = c;
    s_voices[slot].pos = 0;
    if (s_dev) SDL_UnlockAudioDevice(s_dev);
}

void sfx_play_nav(void)              { play(&s_nav); }
void sfx_play_confirm(void)          { play(&s_confirm); }
void sfx_play_back(void)             { play(&s_back); }
void sfx_play_boot(void)             { play(&s_boot); }
void sfx_play_enter_game(void)       { play(&s_enter_game); }
void sfx_play_open_menu(void)        { play(&s_open_menu); }
void sfx_play_back_to_launcher(void) { play(&s_back_launcher); }
void sfx_play_next_week(void)        { play(&s_next_week); }
