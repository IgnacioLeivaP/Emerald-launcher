#pragma once
/* Minimal 3D renderer for the shelf view: textured game boxes with lighting,
   floor reflections, soft contact shadows and glows. Renders into an
   offscreen (multisampled when available) target sized to the current GL
   viewport, then composites it back — so it works the same on desktop GL 3.3
   and on the Switch's GLES 3.0. */
#include "mat4.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Box proportions in world units (a retro cardboard game box: 5 x 7 x 1). */
#define BOX_W 1.0f
#define BOX_H 1.4f
#define BOX_D 0.2f

/* Box-art atlas layout (pixels): front cover on the left, the spine as an
   upright strip on the right. The spine's top BOXART_CAP_H rows are a solid
   color used for the top, bottom and (by default) back faces. */
#define BOXART_FRONT_W 360
#define BOXART_SPINE_W 72
#define BOXART_H       504
#define BOXART_ATLAS_W (BOXART_FRONT_W + BOXART_SPINE_W)
#define BOXART_CAP_H   14

bool scene3d_init(void);
void scene3d_shutdown(void);
/* Free the offscreen render targets (recreated lazily by scene3d_begin). */
void scene3d_release_targets(void);

/* Frame: begin captures the current GL viewport (the letterboxed 16:9 area),
   binds the offscreen target and clears it. end resolves it and draws it back
   into that viewport of the default framebuffer, restoring 2D GL state. */
void scene3d_begin(void);
void scene3d_end(void);

void scene3d_set_camera(const Mat4 *view, const Mat4 *proj, const float eye[3]);

/* Textures (RGBA8 from tightly packed rows). */
unsigned scene3d_texture_rgba(const unsigned char *rgba, int w, int h, bool mipmaps);
void     scene3d_texture_free(unsigned tex);

typedef struct {
    unsigned atlas;        /* front+spine atlas (0 = flat `color` box)        */
    unsigned back;         /* separate back cover (0 = atlas cap color)       */
    float    color[3];     /* flat color for untextured boxes                 */
    float    brightness;   /* lighting multiplier (dims far/unfocused boxes)  */
    float    spec;         /* glossy highlight strength                       */
} BoxMaterial;

/* model: rotation + translation (+ uniform scale) of a BOX_W x BOX_H x BOX_D
   box centered on the origin. */
void scene3d_draw_box(const Mat4 *model, const BoxMaterial *mat);
/* Mirror image under the floor plane y = floor_y, fading out over `fade`. */
void scene3d_draw_box_reflection(const Mat4 *model, const BoxMaterial *mat,
                                 float floor_y, float strength, float fade);
/* Soft luminous rim around the box's front outline (additive). */
void scene3d_draw_box_glow(const Mat4 *model, const float rgb[3], float strength, float margin);

/* Screen-space backdrop: a soft spotlight (NDC center/radius) and vignette. */
void scene3d_draw_backdrop(float cx, float cy, float rx, float ry,
                           const float spot_rgb[3], float spot_a, float vignette);
/* Dark glossy floor plane at y = floor_y, fading out with distance. */
void scene3d_draw_floor(float floor_y, const float rgb[3], float alpha);
/* Blurry elliptical contact shadow on the floor. */
void scene3d_draw_shadow(float x, float z, float yaw, float floor_y,
                         float half_w, float half_d, float alpha);

#ifdef __cplusplus
}
#endif
