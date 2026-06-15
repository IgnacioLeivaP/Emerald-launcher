#pragma once
#ifdef __cplusplus
extern "C" {
#endif
#include <stddef.h>

int  renderer_init(void);
/* Where on the actual window the 16:9 content is drawn (letterboxed). Set by
   the host on startup / window resize / fullscreen toggle. */
void renderer_set_screen_viewport(int x, int y, int w, int h);
void renderer_set_frame(const void *data, unsigned w, unsigned h, size_t pitch, int pixel_fmt);
/* Hardware-rendered frame: the core already drew into `tex` (a w×h sub-rect of
   a max_w×max_h texture). bottom_left=1 for GL origin (needs no Y flip). */
void renderer_set_hw_frame(unsigned tex, unsigned w, unsigned h,
                           unsigned max_w, unsigned max_h, int bottom_left);
void renderer_draw(void);
void renderer_shutdown(void);
/* Shader IDs: 0=None(sharp), 1=Smooth(bilinear), 2=Scanlines, 3=CRT, 4=LCD */
void renderer_set_shader(int shader_id);

#ifdef __cplusplus
}
#endif
