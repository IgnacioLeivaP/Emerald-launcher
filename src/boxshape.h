#pragma once
/* Physical game-box shapes for the 3D shelf.

   Each platform family gets the proportions of its real retail box, scaled
   so 1 world unit ≈ 5 inches:
     NES          5.0 x 7.0 x 1.25 in   cardboard, portrait
     SNES / N64   7.1 x 5.1 x 1.4  in   cardboard, landscape
     Game Boy     4.1 x 5.3 x 1.0  in   cardboard, small portrait
     GBA          4.0 x 5.1 x 1.0  in   cardboard, small portrait
     CD-i         5.3 x 7.3 x 1.1  in   black plastic clamshell (Mega Drive / VHS-like)
     PC           big cardboard box, portrait
   Every family exists in both orientations: a real box scan given as
   "cover" in db.json picks the orientation that matches the image.

   The same table drives the mesh (scene3d.c), the texture atlas layout
   (boxart.cpp) and the shelf layout (shelf.cpp). Atlas layout, in pixels:

       +-------------------+-------+
       |                   |   S   |
       |   FRONT (fw x fh) |   P   |   spine: sd x fh, text runs top→bottom
       |                   |   I   |
       +-------------------+-------+
       |   TOP  (fw x sd)  | patch |   patch: edge / bevel color (sd x sd)
       +-------------------+-------+                                      */
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FAM_DEFAULT, FAM_NES, FAM_SNES, FAM_N64, FAM_GB, FAM_GBC, FAM_GBA, FAM_CDI, FAM_PC,
    FAM_COUNT
} BoxFamily;

typedef enum { BOXSTYLE_CARDBOARD, BOXSTYLE_CASE } BoxStyle;

typedef struct {
    BoxFamily family;
    bool      landscape;
    BoxStyle  style;
    float     w, h, d;          /* world units                          */
    float     bevel;            /* rounded-edge size (world units)      */
    int       fw, fh, sd;       /* front size and depth, in atlas px    */
    int       atlas_w, atlas_h;
    int       back_w, back_h;   /* separate back-cover texture          */
} BoxShape;

#define BOXSHAPE_COUNT (FAM_COUNT * 2)

/* Pixels per world unit on the box faces. */
#define BOXART_DENSITY 360.0f

BoxFamily       boxfamily_of(const char *platform);
bool            boxfamily_landscape(BoxFamily fam);   /* natural orientation */
int             boxshape_id(BoxFamily fam, bool landscape);
const BoxShape *boxshape_get(int id);

#ifdef __cplusplus
}
#endif
