#include "boxshape.h"
#include <ctype.h>
#include <string.h>

/* Natural size of each family's box (w x h x d, world units = in / 5). */
static const struct {
    float w, h, d, bevel;
    BoxStyle style;
} BASE[FAM_COUNT] = {
    /* DEFAULT */ { 1.00f, 1.40f, 0.25f, 0.018f, BOXSTYLE_CARDBOARD },
    /* NES     */ { 1.00f, 1.40f, 0.25f, 0.018f, BOXSTYLE_CARDBOARD },
    /* SNES    */ { 1.42f, 1.02f, 0.28f, 0.018f, BOXSTYLE_CARDBOARD },
    /* N64     */ { 1.42f, 1.02f, 0.28f, 0.018f, BOXSTYLE_CARDBOARD },
    /* GB      */ { 0.82f, 1.06f, 0.20f, 0.014f, BOXSTYLE_CARDBOARD },
    /* GBC     */ { 0.82f, 1.06f, 0.20f, 0.014f, BOXSTYLE_CARDBOARD },
    /* GBA     */ { 0.80f, 1.02f, 0.20f, 0.014f, BOXSTYLE_CARDBOARD },
    /* CD-i    */ { 1.06f, 1.46f, 0.22f, 0.045f, BOXSTYLE_CASE },
    /* PC      */ { 1.12f, 1.46f, 0.32f, 0.020f, BOXSTYLE_CARDBOARD },
};

static BoxShape s_shapes[BOXSHAPE_COUNT];
static int      s_built = 0;

static int px(float units) { return (int)(units * BOXART_DENSITY + 0.5f); }

static void build(void) {
    for (int f = 0; f < FAM_COUNT; f++)
        for (int o = 0; o < 2; o++) {
            BoxShape *s = &s_shapes[f * 2 + o];
            const bool nat_land = BASE[f].w > BASE[f].h;
            const bool land = o == 1;
            s->family = (BoxFamily)f;
            s->landscape = land;
            s->style = BASE[f].style;
            /* The other orientation swaps width and height. */
            s->w = (land == nat_land) ? BASE[f].w : BASE[f].h;
            s->h = (land == nat_land) ? BASE[f].h : BASE[f].w;
            s->d = BASE[f].d;
            s->bevel = BASE[f].bevel;
            s->fw = px(s->w);
            s->fh = px(s->h);
            s->sd = px(s->d);
            s->atlas_w = s->fw + s->sd;
            s->atlas_h = s->fh + s->sd;
            s->back_w = (s->fw * 3) / 2;
            s->back_h = (s->fh * 3) / 2;
        }
    s_built = 1;
}

static int has(const char *s, const char *needle) { return strstr(s, needle) != NULL; }

BoxFamily boxfamily_of(const char *platform) {
    char p[128];
    size_t n = 0;
    if (platform)
        for (; platform[n] && n < sizeof(p) - 1; n++) p[n] = (char)tolower((unsigned char)platform[n]);
    p[n] = '\0';
    if (has(p, "cd-i") || has(p, "cdi"))                              return FAM_CDI;
    if (has(p, "n64") || has(p, "nintendo 64"))                       return FAM_N64;
    if (has(p, "gba") || has(p, "advance"))                           return FAM_GBA;
    if (has(p, "gbc") || has(p, "color"))                             return FAM_GBC;
    if (has(p, "snes") || has(p, "super") || has(p, "satellaview"))   return FAM_SNES;
    if (has(p, "nes") || has(p, "famicom"))                           return FAM_NES;
    if (has(p, "gb") || has(p, "game boy"))                           return FAM_GB;
    if (has(p, "pc") || has(p, "switch") || has(p, "port"))           return FAM_PC;
    return FAM_DEFAULT;
}

bool boxfamily_landscape(BoxFamily fam) {
    return fam >= 0 && fam < FAM_COUNT && BASE[fam].w > BASE[fam].h;
}

int boxshape_id(BoxFamily fam, bool landscape) {
    if (fam < 0 || fam >= FAM_COUNT) fam = FAM_DEFAULT;
    return (int)fam * 2 + (landscape ? 1 : 0);
}

const BoxShape *boxshape_get(int id) {
    if (!s_built) build();
    if (id < 0 || id >= BOXSHAPE_COUNT) id = boxshape_id(FAM_DEFAULT, false);
    return &s_shapes[id];
}
