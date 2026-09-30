#pragma once
/* Performance overlay for testing on real hardware (the Switch above all):
   frame rate, time spent in the emulator core and in rendering, audio
   queue / rate control and memory in use. Toggled from the pause menu, the
   launcher settings or F3; the choice is saved in prefs.json. */

bool perf_enabled(void);
void perf_set_enabled(bool on);

/* Frame accounting: one frame_end per presented frame; the section timers
   wrap the work to measure ("core", "render", "ui"). */
void   perf_frame_end(void);
void   perf_section_begin(int which);
void   perf_section_end(int which);
enum { PERF_CORE, PERF_RENDER, PERF_UI, PERF_SECTIONS };

/* Memory used by the process, in MB (0 if unknown). */
double perf_memory_mb(void);

/* Draw the overlay (between ui_begin and ui_end). in_game adds the core and
   audio lines. */
void perf_draw(bool in_game);
