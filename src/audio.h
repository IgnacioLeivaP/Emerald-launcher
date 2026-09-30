#pragma once
/* Game audio with dynamic rate control.

   The core produces samples at its own rate (32040 Hz SNES, 48000 Hz GB...)
   while the frame limiter paces video by the clock: the two never match
   exactly, so a plain queue slowly fills up (and has to drop audio: pops)
   or runs dry (gaps: crackles). Here the samples are resampled to the
   device's rate with a ratio that bends by up to ±0.5% to keep the queue
   near its target level, which no one can hear, and nothing is dropped. */
#include <cstddef>
#include <cstdint>
#include <vector>

/* The resampler on its own (no SDL), so it can be tested with a simulated
   device clock. process() turns one batch of core frames into device frames,
   bending the ratio by how far the device queue is from its target. */
struct DrcResampler {
    double  core_rate = 48000.0, dev_rate = 48000.0;
    double  target_frames = 2400.0;           /* queue level to keep            */
    double  max_deviation = 0.005;            /* ±0.5%                          */
    double  pos = 0.0;                        /* between prev and the next frame */
    int16_t prev[2] = {0, 0};
    double  adjust = 1.0;                     /* last ratio correction          */

    void reset(double core, double dev, double target);
    /* Appends interleaved stereo output to `out`. */
    void process(const int16_t *in, size_t frames, double queued_frames, std::vector<int16_t> &out);
};

bool   audio_open(double core_rate);
void   audio_close(void);
/* Interleaved stereo frames from the core (retro_audio_sample_batch). */
void   audio_push(const int16_t *data, size_t frames);
/* Queued audio, in milliseconds (performance HUD). */
double audio_queued_ms(void);
/* Current resampling adjustment (1.0 = none), for the performance HUD. */
double audio_rate_adjust(void);
