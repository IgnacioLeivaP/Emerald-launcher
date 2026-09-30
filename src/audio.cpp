#include "audio.h"
#include <SDL2/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

const double TARGET_MS = 50.0;        /* how much audio to keep queued */

SDL_AudioDeviceID    s_dev = 0;
DrcResampler         s_drc;
std::vector<int16_t> s_out;

} // namespace

void DrcResampler::reset(double core, double dev, double target) {
    core_rate = core > 1000.0 ? core : 44100.0;
    dev_rate = dev > 1000.0 ? dev : 48000.0;
    target_frames = target > 1.0 ? target : 1.0;
    pos = 0.0;
    prev[0] = prev[1] = 0;
    adjust = 1.0;
}

void DrcResampler::process(const int16_t *in, size_t n, double queued, std::vector<int16_t> &out) {
    if (!in || n == 0) return;
    /* Below target → stretch a little (more output), above → squeeze. */
    double err = (target_frames - queued) / target_frames;
    if (err > 1.0) err = 1.0;
    if (err < -1.0) err = -1.0;
    adjust = 1.0 + max_deviation * err;
    const double step = core_rate / (dev_rate * adjust);    /* input frames per output frame */

    /* Linear interpolation over [prev, in[0], in[1], ...]: index 0 is the
       last frame of the previous batch, so batches join seamlessly. */
    out.reserve(out.size() + (size_t)((double)n / step) * 2 + 8);
    double p = pos;
    while (p < (double)n) {
        const size_t i = (size_t)p;
        const double f = p - (double)i;
        const int16_t *a = i == 0 ? prev : in + (i - 1) * 2;
        const int16_t *b = in + i * 2;
        for (int c = 0; c < 2; c++)
            out.push_back((int16_t)lrint((double)a[c] + ((double)b[c] - (double)a[c]) * f));
        p += step;
    }
    pos = p - (double)n;
    prev[0] = in[(n - 1) * 2];
    prev[1] = in[(n - 1) * 2 + 1];
}

bool audio_open(double core_rate) {
    audio_close();
    SDL_AudioSpec want{}, have{};
    want.freq     = 48000;
    want.format   = AUDIO_S16SYS;
    want.channels = 2;
#ifdef __SWITCH__
    want.samples  = 1024;   /* 512 underran on Switch → crackle */
#else
    want.samples  = 512;
#endif
    s_dev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    if (!s_dev) {
        fprintf(stderr, "audio: couldn't open the game device: %s\n", SDL_GetError());
        return false;
    }
    const double dev_rate = have.freq > 0 ? (double)have.freq : 48000.0;
    s_drc.reset(core_rate, dev_rate, std::max(2.0 * (double)have.samples, dev_rate * TARGET_MS / 1000.0));
    /* Start at the target level (silence) so the first frames don't underrun. */
    std::vector<int16_t> silence((size_t)s_drc.target_frames * 2, 0);
    SDL_QueueAudio(s_dev, silence.data(), (Uint32)(silence.size() * sizeof(int16_t)));
    SDL_PauseAudioDevice(s_dev, 0);
    fprintf(stderr, "audio: core %.1f Hz -> device %d Hz (%d-frame buffer), target %.0f ms\n",
            s_drc.core_rate, have.freq, (int)have.samples, TARGET_MS);
    return true;
}

void audio_close(void) {
    if (s_dev) SDL_CloseAudioDevice(s_dev);
    s_dev = 0;
}

void audio_push(const int16_t *in, size_t frames) {
    if (!s_dev || !in || frames == 0) return;
    const double queued = (double)SDL_GetQueuedAudioSize(s_dev) / 4.0;
    /* Way behind (a stall, a loading screen): drop rather than lag. */
    if (queued > s_drc.target_frames * 4.0) return;
    s_out.clear();
    s_drc.process(in, frames, queued, s_out);
    if (!s_out.empty())
        SDL_QueueAudio(s_dev, s_out.data(), (Uint32)(s_out.size() * sizeof(int16_t)));
}

double audio_queued_ms(void) {
    if (!s_dev) return 0.0;
    return (double)SDL_GetQueuedAudioSize(s_dev) / 4.0 * 1000.0 / s_drc.dev_rate;
}

double audio_rate_adjust(void) { return s_drc.adjust; }
