#include "prefs.h"
#include "fsutil.h"
#include "paths.h"
#include <nlohmann/json.hpp>
#include <cstdio>
#include <fstream>
#include <unordered_map>

/* Next to db.json (sdmc:/emerald/ on Switch). Older builds wrote it to the
   working directory, which on the Switch is wherever the NRO was started
   from: that file is picked up once and moved here. */
static const char *PREFS_PATH = DATA("prefs.json");
static const char *OLD_PREFS_PATH = "prefs.json";
static std::unordered_map<std::string, int> s_shaders;
static std::unordered_map<std::string, int> s_last_entry;
static bool        s_view_3d = true;
static std::string s_last_group;

void prefs_load(void) {
    const bool migrate = !fs_exists(PREFS_PATH) && std::string(PREFS_PATH) != OLD_PREFS_PATH &&
                         fs_exists(OLD_PREFS_PATH);
    std::ifstream f(migrate ? OLD_PREFS_PATH : PREFS_PATH);
    if (!f) return;
    try {
        auto j = nlohmann::json::parse(f);
        if (j.contains("shaders")) {
            for (auto &[k, v] : j["shaders"].items())
                s_shaders[k] = v.get<int>();
        }
        if (j.contains("view") && j["view"].is_string())
            s_view_3d = j["view"].get<std::string>() != "classic";
        if (j.contains("last_group") && j["last_group"].is_string())
            s_last_group = j["last_group"].get<std::string>();
        if (j.contains("last_entry") && j["last_entry"].is_object()) {
            for (auto &[k, v] : j["last_entry"].items())
                if (v.is_number_integer()) s_last_entry[k] = v.get<int>();
        }
    } catch (...) {}
    if (migrate) {
        f.close();
        prefs_save();
        remove(OLD_PREFS_PATH);
    }
}

void prefs_save(void) {
    nlohmann::json j;
    j["shaders"] = nlohmann::json::object();
    for (auto &[k, v] : s_shaders)
        j["shaders"][k] = v;
    j["view"] = s_view_3d ? "3d" : "classic";
    if (!s_last_group.empty()) j["last_group"] = s_last_group;
    j["last_entry"] = nlohmann::json::object();
    for (auto &[k, v] : s_last_entry)
        j["last_entry"][k] = v;
    if (!fs_write_atomic(PREFS_PATH, j.dump(2) + "\n"))
        fprintf(stderr, "prefs: couldn't write %s\n", PREFS_PATH);
}

int prefs_get_shader(const std::string &key) {
    auto it = s_shaders.find(key);
    return (it != s_shaders.end()) ? it->second : 0;
}

void prefs_set_shader(const std::string &key, int id) {
    s_shaders[key] = id;
}

bool prefs_get_view_3d(void)       { return s_view_3d; }
void prefs_set_view_3d(bool on)    { s_view_3d = on; }

std::string prefs_get_last_group(void)                 { return s_last_group; }
void        prefs_set_last_group(const std::string &k) { s_last_group = k; }

int prefs_get_last_entry(const std::string &key) {
    auto it = s_last_entry.find(key);
    return (it != s_last_entry.end()) ? it->second : -1;
}

void prefs_set_last_entry(const std::string &key, int idx) {
    s_last_entry[key] = idx;
}
