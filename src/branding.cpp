#include "branding.h"
#include "uikit.h"
#include <nlohmann/json.hpp>
#include <cstdio>
#include <fstream>

/* Same content-root convention as db.cpp's content_path(): user-supplied
   asset paths in branding.json are relative to where db.json lives. */
#ifdef __SWITCH__
static const std::string CONTENT_BASE = "sdmc:/emerald/";
#else
static const std::string CONTENT_BASE = "";
#endif

static std::string content_path(const std::string &p) {
    return p.empty() ? p : CONTENT_BASE + p;
}

Branding branding_load(const char *path) {
    Branding b;
    std::ifstream f(path);
    if (!f) return b;
    try {
        auto j = nlohmann::json::parse(f);
        if (j.contains("app_name") && j["app_name"].is_string()) {
            auto name = j["app_name"].get<std::string>();
            if (!name.empty()) b.app_name = name;
        }
        if (j.contains("assets") && j["assets"].is_object()) {
            auto &a = j["assets"];
            b.icon_path       = content_path(a.value("icon", ""));
            b.splash_path     = content_path(a.value("splash", ""));
            b.background_path = content_path(a.value("background", ""));
            b.font_path       = content_path(a.value("font", ""));
            b.music_path      = content_path(a.value("music", ""));
        }
        if (j.contains("music_volume") && j["music_volume"].is_number())
            b.music_volume = j["music_volume"].get<float>();
    } catch (...) {
        /* malformed branding.json: keep whatever defaults were set before the throw */
    }
    return b;
}

/* "#RRGGBB" (or "RRGGBB") → 0..1 floats; false if it isn't one. */
static bool parse_color(const std::string &s, float out[3]) {
    const char *p = s.c_str();
    if (*p == '#') p++;
    unsigned v = 0;
    int n = 0;
    if (sscanf(p, "%6x%n", &v, &n) != 1 || n != 6 || p[6] != '\0') return false;
    out[0] = (float)((v >> 16) & 255) / 255.0f;
    out[1] = (float)((v >> 8) & 255) / 255.0f;
    out[2] = (float)(v & 255) / 255.0f;
    return true;
}

void branding_apply_theme(const char *path) {
    std::ifstream f(path);
    if (!f) return;
    try {
        auto j = nlohmann::json::parse(f);
        if (!j.contains("theme") || !j["theme"].is_object()) return;
        const auto &t = j["theme"];
        const struct { const char *key; float *rgb; } fields[] = {
            { "accent", g_theme.accent }, { "accent_dim", g_theme.accent_dim }, { "panel", g_theme.panel },
            { "spot", g_theme.spot }, { "floor", g_theme.floor }, { "glow", g_theme.glow },
        };
        for (const auto &fld : fields) {
            if (!t.contains(fld.key) || !t[fld.key].is_string()) continue;
            if (!parse_color(t[fld.key].get<std::string>(), fld.rgb))
                fprintf(stderr, "branding: theme.%s isn't a #RRGGBB color\n", fld.key);
        }
    } catch (...) {
        /* malformed branding.json: keep the default colors */
    }
}
