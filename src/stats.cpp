#include "stats.h"
#include "fsutil.h"
#include "i18n.h"
#include <nlohmann/json.hpp>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <map>

namespace {

std::string                      s_path;
std::map<std::string, PlayStats> s_stats;
std::string                      s_key;          /* running session ("" = none) */
double                           s_unsaved = 0.0;

} // namespace

void stats_load(const std::string &path) {
    s_path = path;
    s_stats.clear();
    std::ifstream f(path);
    if (!f) return;
    try {
        auto j = nlohmann::json::parse(f);
        for (auto &[k, v] : j.items()) {
            if (!v.is_object()) continue;
            PlayStats p;
            p.seconds  = v.value("seconds", 0.0);
            p.last     = v.value("last", 0LL);
            p.sessions = v.value("sessions", 0);
            s_stats[k] = p;
        }
    } catch (...) {
        fprintf(stderr, "stats: couldn't read %s\n", path.c_str());
    }
}

std::string stats_key(const std::string &group_key, const std::string &stem) {
    return group_key + "/" + stem;
}

PlayStats stats_get(const std::string &key) {
    auto it = s_stats.find(key);
    return it != s_stats.end() ? it->second : PlayStats{};
}

void stats_save(void) {
    if (s_path.empty()) return;
    nlohmann::json j = nlohmann::json::object();
    for (auto &[k, p] : s_stats)
        j[k] = { {"seconds", (long long)(p.seconds + 0.5)}, {"last", p.last}, {"sessions", p.sessions} };
    if (!fs_write_atomic(s_path, j.dump(2) + "\n"))
        fprintf(stderr, "stats: couldn't write %s\n", s_path.c_str());
    s_unsaved = 0.0;
}

void stats_begin(const std::string &key) {
    if (!s_key.empty()) stats_end();
    s_key = key;
    if (key.empty()) return;
    PlayStats &p = s_stats[key];
    p.sessions++;
    p.last = (long long)time(nullptr);
    stats_save();
}

void stats_add(double seconds) {
    if (s_key.empty() || seconds <= 0.0 || seconds > 1.0) return;   /* ignore stalls */
    PlayStats &p = s_stats[s_key];
    p.seconds += seconds;
    p.last = (long long)time(nullptr);
    s_unsaved += seconds;
    if (s_unsaved >= 60.0) stats_save();                             /* at most once a minute */
}

void stats_end(void) {
    if (s_key.empty()) return;
    s_key.clear();
    stats_save();
}

std::string stats_format_duration(double seconds) {
    const long long mins = (long long)(seconds / 60.0);
    char buf[64];
    if (mins < 1)        snprintf(buf, sizeof(buf), "%s", tr("< 1 min"));
    else if (mins < 60)  snprintf(buf, sizeof(buf), tr("%d min"), (int)mins);
    else if (mins % 60)  snprintf(buf, sizeof(buf), tr("%d h %d min"), (int)(mins / 60), (int)(mins % 60));
    else                 snprintf(buf, sizeof(buf), tr("%d h"), (int)(mins / 60));
    return buf;
}
