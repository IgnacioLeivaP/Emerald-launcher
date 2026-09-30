/* Dynamic rate control: with a device clock that runs a bit fast or slow,
   the queue must settle without ever running dry or piling up. */
#include "check.h"
#include "audio.h"
#include <cmath>
#include <vector>

namespace {

const double PI = 3.14159265358979323846;

struct Sim {
    double min_q = 1e9, max_q = 0.0, final_q = 0.0, adjust = 1.0;
    int    underruns = 0;
};

/* core_rate / fps: what the core produces per video frame; the device eats
   48000 * (1 + dev_err) frames a second, continuously. */
Sim simulate(double core_rate, double fps, double dev_err, double seconds) {
    DrcResampler r;
    const double target = 2400.0;
    r.reset(core_rate, 48000.0, target);
    Sim s;
    double queue = target;                 /* audio_open pre-fills with silence */
    double acc = 0.0;
    std::vector<int16_t> in, out;
    const double dt = 1.0 / fps;
    const double eat = 48000.0 * (1.0 + dev_err) * dt;
    for (double t = 0.0; t < seconds; t += dt) {
        acc += core_rate / fps;
        const size_t n = (size_t)acc;
        acc -= (double)n;
        in.assign(n * 2, 1000);
        out.clear();
        r.process(in.data(), n, queue, out);
        queue += (double)(out.size() / 2);
        queue -= eat;
        if (queue < 0.0) { s.underruns++; queue = 0.0; }
        if (t > 20.0) {
            if (queue < s.min_q) s.min_q = queue;
            if (queue > s.max_q) s.max_q = queue;
        }
    }
    s.final_q = queue;
    s.adjust = r.adjust;
    return s;
}

} // namespace

TEST(drc_snes_device_fast) {
    Sim s = simulate(32040.0, 60.0988, +0.003, 120.0);
    CHECK_EQ(s.underruns, 0);
    CHECK(s.min_q > 300.0);                /* never close to running dry */
    CHECK(s.max_q < 2400.0 * 1.5);
    CHECK_NEAR(s.adjust, 1.003, 0.0006);   /* it found the device's pace */
}

TEST(drc_snes_device_slow) {
    Sim s = simulate(32040.0, 60.0988, -0.003, 120.0);
    CHECK_EQ(s.underruns, 0);
    CHECK(s.max_q < 2400.0 * 2.0);         /* doesn't pile up either */
    CHECK_NEAR(s.adjust, 0.997, 0.0006);
}

TEST(drc_gb_matched_clocks) {
    Sim s = simulate(48000.0, 59.7275, 0.0, 60.0);
    CHECK_EQ(s.underruns, 0);
    CHECK_NEAR(s.final_q, 2400.0, 250.0);
    CHECK_NEAR(s.adjust, 1.0, 0.0006);
}

/* A sine split into odd-sized batches must come out smooth: no clicks at
   the joins. */
TEST(drc_continuous_across_batches) {
    DrcResampler r;
    r.reset(32040.0, 48000.0, 2400.0);
    const double amp = 12000.0, freq = 440.0;
    std::vector<int16_t> in, out;
    size_t done = 0;
    const size_t sizes[] = { 1, 7, 533, 2, 400, 64, 3, 534 };
    for (int round = 0; round < 40; round++)
        for (size_t n : sizes) {
            in.resize(n * 2);
            for (size_t i = 0; i < n; i++) {
                const double v = amp * std::sin(2.0 * PI * freq * (double)(done + i) / 32040.0);
                in[i * 2] = in[i * 2 + 1] = (int16_t)std::lrint(v);
            }
            done += n;
            r.process(in.data(), n, 2400.0, out);
        }
    /* Biggest step a 440 Hz sine can take between two 48 kHz samples. */
    const double max_step = amp * 2.0 * PI * freq / 48000.0 * 1.25 + 2.0;
    double worst = 0.0;
    for (size_t i = 2; i + 2 < out.size(); i += 2)
        worst = std::fmax(worst, std::fabs((double)out[i + 2] - (double)out[i]));
    CHECK(worst <= max_step);
    /* 32040 → 48000: 1.498 output frames per input frame. */
    CHECK_NEAR((double)(out.size() / 2) / (double)done, 48000.0 / 32040.0, 0.01);
}
