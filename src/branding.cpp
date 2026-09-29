#include "branding.h"
#include <nlohmann/json.hpp>
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
        if (j.contains("app_name") && j["app_name"].is_string())
            b.app_name = j["app_name"].get<std::string>();
        if (j.contains("assets") && j["assets"].is_object()) {
            auto &a = j["assets"];
            b.icon_path       = content_path(a.value("icon", ""));
            b.splash_path     = content_path(a.value("splash", ""));
            b.background_path = content_path(a.value("background", ""));
            b.font_path       = content_path(a.value("font", ""));
        }
    } catch (...) {
        /* malformed branding.json: keep whatever defaults were set before the throw */
    }
    return b;
}
