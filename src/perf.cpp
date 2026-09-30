#include "perf.h"
#include "audio.h"
#include "ui.h"
#include <SDL2/SDL.h>
#include <cstdio>

#ifdef __SWITCH__
#  include <switch.h>
#elif defined(_WIN32)
#  include <windows.h>
#  include <psapi.h>
#else
#  include <unistd.h>
#endif

namespace {

bool   s_on = false;
Uint64 s_freq = 0;
Uint64 s_sec_start[PERF_SECTIONS] = {};
double s_sec_acc[PERF_SECTIONS] = {};     /* ms accumulated this second        */
double s_sec_avg[PERF_SECTIONS] = {};     /* ms per frame over the last second */
double s_worst = 0.0, s_worst_shown = 0.0; /* longest frame (ms)               */
Uint64 s_window_start = 0, s_last_frame = 0;
int    s_frames = 0;
double s_fps = 0.0;
double s_mem = 0.0;

double ms_between(Uint64 a, Uint64 b) { return (double)(b - a) * 1000.0 / (double)s_freq; }

} // namespace

bool perf_enabled(void)        { return s_on; }
void perf_set_enabled(bool on) { s_on = on; }

void perf_section_begin(int w) {
    if (!s_on || w < 0 || w >= PERF_SECTIONS) return;
    if (!s_freq) s_freq = SDL_GetPerformanceFrequency();
    s_sec_start[w] = SDL_GetPerformanceCounter();
}

void perf_section_end(int w) {
    if (!s_on || w < 0 || w >= PERF_SECTIONS || !s_sec_start[w]) return;
    s_sec_acc[w] += ms_between(s_sec_start[w], SDL_GetPerformanceCounter());
    s_sec_start[w] = 0;
}

void perf_frame_end(void) {
    if (!s_on) return;
    if (!s_freq) s_freq = SDL_GetPerformanceFrequency();
    const Uint64 now = SDL_GetPerformanceCounter();
    if (s_last_frame) {
        const double dt = ms_between(s_last_frame, now);
        if (dt > s_worst) s_worst = dt;
    }
    s_last_frame = now;
    if (!s_window_start) s_window_start = now;
    s_frames++;
    const double span = ms_between(s_window_start, now);
    if (span >= 1000.0) {                      /* publish once a second */
        s_fps = (double)s_frames * 1000.0 / span;
        for (int i = 0; i < PERF_SECTIONS; i++) {
            s_sec_avg[i] = s_sec_acc[i] / (double)s_frames;
            s_sec_acc[i] = 0.0;
        }
        s_worst_shown = s_worst;
        s_worst = 0.0;
        s_frames = 0;
        s_window_start = now;
        s_mem = perf_memory_mb();
    }
}

double perf_memory_mb(void) {
#ifdef __SWITCH__
    u64 used = 0;
    if (R_SUCCEEDED(svcGetInfo(&used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0)))
        return (double)used / (1024.0 * 1024.0);
    return 0.0;
#elif defined(_WIN32)
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
        return (double)pmc.WorkingSetSize / (1024.0 * 1024.0);
    return 0.0;
#else
    long pages = 0, rss = 0;
    FILE *f = fopen("/proc/self/statm", "r");
    if (!f) return 0.0;
    int n = fscanf(f, "%ld %ld", &pages, &rss);
    fclose(f);
    if (n != 2) return 0.0;
    return (double)rss * (double)sysconf(_SC_PAGESIZE) / (1024.0 * 1024.0);
#endif
}

void perf_draw(bool in_game) {
    if (!s_on) return;
    char lines[6][96];
    int n = 0;
    snprintf(lines[n++], sizeof(lines[0]), "%.1f fps   worst %.1f ms", s_fps, s_worst_shown);
    if (in_game)
        snprintf(lines[n++], sizeof(lines[0]), "core %.2f ms   render %.2f ms", s_sec_avg[PERF_CORE],
                 s_sec_avg[PERF_RENDER]);
    else
        snprintf(lines[n++], sizeof(lines[0]), "3D %.2f ms   2D %.2f ms", s_sec_avg[PERF_RENDER],
                 s_sec_avg[PERF_UI]);
    if (in_game)
        snprintf(lines[n++], sizeof(lines[0]), "audio %.0f ms   rate %+.3f%%", audio_queued_ms(),
                 (audio_rate_adjust() - 1.0) * 100.0);
    if (s_mem > 0.0) snprintf(lines[n++], sizeof(lines[0]), "memory %.0f MB", s_mem);

    const int px = 14;
    const float lh = (float)ui_text_line_height(px) + 2.0f;
    float w = 0.0f;
    for (int i = 0; i < n; i++) {
        float lw = (float)ui_text_width(px, lines[i]);
        if (lw > w) w = lw;
    }
    /* Out of the way: top right in-game, above the hint bar in the launcher. */
    const float bw = w + 16.0f, bh = lh * (float)n + 10.0f;
    const float x = 1280.0f - bw - 8.0f, y = in_game ? 8.0f : 720.0f - 28.0f - bh - 8.0f;
    ui_round_rect(x, y, bw, bh, 6.0f, 0.0f, 0.0f, 0.0f, 0.70f);
    for (int i = 0; i < n; i++) {
        const bool bad = i == 0 && s_fps > 0.0 && s_fps < 57.0;
        ui_text_px(x + 8.0f, y + 5.0f + lh * (float)i, px, lines[i], bad ? 1.0f : 0.70f,
                   bad ? 0.45f : 1.0f, bad ? 0.40f : 0.72f, 1.0f);
    }
}
