#include "prefs.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <unordered_map>

static const char *PREFS_PATH = "prefs.json";
static std::unordered_map<std::string, int> s_shaders;

void prefs_load(void) {
    std::ifstream f(PREFS_PATH);
    if (!f) return;
    try {
        auto j = nlohmann::json::parse(f);
        if (j.contains("shaders")) {
            for (auto &[k, v] : j["shaders"].items())
                s_shaders[k] = v.get<int>();
        }
    } catch (...) {}
}

void prefs_save(void) {
    nlohmann::json j;
    j["shaders"] = nlohmann::json::object();
    for (auto &[k, v] : s_shaders)
        j["shaders"][k] = v;
    std::ofstream f(PREFS_PATH);
    if (f) f << j.dump(2) << "\n";
}

int prefs_get_shader(const std::string &key) {
    auto it = s_shaders.find(key);
    return (it != s_shaders.end()) ? it->second : 0;
}

void prefs_set_shader(const std::string &key, int id) {
    s_shaders[key] = id;
}
