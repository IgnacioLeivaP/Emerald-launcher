#include "savestate.h"
#include "core.h"
#include "fsutil.h"
#include "i18n.h"
#include "renderer.h"
#include "ui.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <vector>

std::string state_path(const std::string &saves_dir, const std::string &stem, bool autosave) {
    return saves_dir + "/states/" + stem + (autosave ? ".auto.state" : ".state");
}

std::string state_thumb(const std::string &path) { return path + ".png"; }

bool png_save(const std::string &path, const unsigned char *rgba, int w, int h) {
    if (!rgba || w <= 0 || h <= 0) return false;
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormatFrom((void *)rgba, w, h, 32, w * 4,
                                                        SDL_PIXELFORMAT_RGBA32);
    if (!s) return false;
    /* Write next to the target and rename, like every other save. */
    const std::string tmp = path + ".tmp";
    bool ok = IMG_SavePNG(s, tmp.c_str()) == 0;
    SDL_FreeSurface(s);
    ok = ok ? fs_replace(tmp, path) : (remove(tmp.c_str()), false);
    if (ok) ui_image_forget(path.c_str());
    else fprintf(stderr, "png: couldn't write %s: %s\n", path.c_str(), IMG_GetError());
    return ok;
}

/* The game frame as the player sees it: pixels stretched to the display
   aspect (SNES 256x224 is shown at 4:3) and doubled for small frames, with
   nearest-neighbour sampling so pixel art stays crisp. */
static bool save_frame(const std::string &path) {
    int w = 0, h = 0;
    unsigned char *rgba = renderer_capture(&w, &h);
    if (!rgba) return false;
    float aspect = core_get_avinfo().aspect;
    if (aspect <= 0.0f) aspect = (float)w / (float)h;
    const int oh = h <= 400 ? h * 2 : h;
    const int ow = (int)((float)oh * aspect + 0.5f);
    bool ok;
    if (ow == w && oh == h) {
        ok = png_save(path, rgba, w, h);
    } else {
        std::vector<unsigned char> out((size_t)ow * (size_t)oh * 4);
        for (int y = 0; y < oh; y++) {
            const unsigned char *src = rgba + (size_t)(y * h / oh) * (size_t)w * 4;
            unsigned char *dst = &out[(size_t)y * (size_t)ow * 4];
            for (int x = 0; x < ow; x++)
                memcpy(dst + (size_t)x * 4, src + (size_t)(x * w / ow) * 4, 4);
        }
        ok = png_save(path, out.data(), ow, oh);
    }
    free(rgba);
    return ok;
}

bool state_save(const std::string &path) {
    const size_t size = core_state_size();
    if (!size) return false;
    std::vector<unsigned char> buf(size);
    if (!core_state_save(buf.data(), size)) {
        fprintf(stderr, "state: the core couldn't serialize\n");
        return false;
    }
    fs_mkdirs(fs_dirname(path));
    if (!fs_write_atomic(path, buf.data(), buf.size())) {
        fprintf(stderr, "state: couldn't write %s\n", path.c_str());
        return false;
    }
    save_frame(state_thumb(path));
    fprintf(stderr, "state: saved %s (%zu bytes)\n", path.c_str(), size);
    return true;
}

bool state_load(const std::string &path) {
    std::vector<unsigned char> buf;
    if (!fs_read(path, buf) || buf.empty()) return false;
    /* Some cores want exactly their own size; a state from an older core
       version may be larger: hand over what we have and let it decide. */
    if (!core_state_load(buf.data(), buf.size())) {
        fprintf(stderr, "state: the core rejected %s\n", path.c_str());
        return false;
    }
    fprintf(stderr, "state: loaded %s\n", path.c_str());
    return true;
}

long long state_time(const std::string &path) { return fs_mtime(path); }

void state_delete(const std::string &path) {
    remove(path.c_str());
    remove(state_thumb(path).c_str());
    ui_image_forget(state_thumb(path).c_str());
}

std::string captures_dir_for(const std::string &rom_path) {
    std::string dir = fs_dirname(rom_path);
    return (dir.empty() ? std::string(".") : dir) + "/captures";
}

std::string screenshot_save(const std::string &captures_dir, const std::string &stem) {
    if (!fs_mkdirs(captures_dir)) return "";
    time_t now = time(nullptr);
    struct tm tmv;
#ifdef _WIN32
    localtime_s(&tmv, &now);
#else
    localtime_r(&now, &tmv);
#endif
    char stamp[32];
    strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &tmv);
    std::string path = captures_dir + "/" + stem + "-" + stamp + ".png";
    for (int n = 2; fs_exists(path) && n < 100; n++)           /* two in the same second */
        path = captures_dir + "/" + stem + "-" + stamp + "-" + std::to_string(n) + ".png";
    return save_frame(path) ? path : std::string();
}

std::string time_ago(long long when) {
    if (when <= 0) return "";
    long long d = (long long)time(nullptr) - when;
    char buf[64];
    if (d < 60)                snprintf(buf, sizeof(buf), "%s", tr("just now"));
    else if (d < 3600)         snprintf(buf, sizeof(buf), tr("%d min ago"), (int)(d / 60));
    else if (d < 86400)        snprintf(buf, sizeof(buf), tr("%d h ago"), (int)(d / 3600));
    else if (d < 2 * 86400)    snprintf(buf, sizeof(buf), "%s", tr("yesterday"));
    else                       snprintf(buf, sizeof(buf), tr("%d days ago"), (int)(d / 86400));
    return buf;
}
