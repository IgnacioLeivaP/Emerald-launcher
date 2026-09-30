#include "status.h"
#include "fsutil.h"
#include "savestate.h"
#include "stats.h"
#include <unordered_map>

namespace {

std::unordered_map<std::string, EntryStatus> s_cache;

EntryStatus compute(const GameGroup &g, const GameEntry &e) {
    EntryStatus st;
    const PlayStats ps = stats_get(stats_key(g.key, e.stem));
    st.seconds = ps.seconds;
    st.last    = ps.last;
    if (e.external || e.srm_path.empty() || e.stem.empty()) return st;
    const std::string saves = fs_dirname(e.srm_path);
    const std::string resume = state_path(saves, e.stem, true);
    st.resume = state_time(resume);
    if (st.resume) {
        st.resume_state = resume;
        st.resume_thumb = state_thumb(resume);
    }
    st.has_save = st.resume || fs_exists(e.srm_path) || fs_exists(state_path(saves, e.stem, false));
    return st;
}

} // namespace

const EntryStatus &entry_status(const GameGroup &g, int idx) {
    static const EntryStatus none;
    if (idx < 0 || idx >= (int)g.entries.size()) return none;
    const GameEntry &e = g.entries[(size_t)idx];
    const std::string key = stats_key(g.key, e.stem.empty() ? e.title : e.stem);
    auto it = s_cache.find(key);
    if (it == s_cache.end()) it = s_cache.emplace(key, compute(g, e)).first;
    return it->second;
}

void group_play(const GameGroup &g, double &seconds, long long &last) {
    seconds = 0.0;
    last = 0;
    for (int j = 0; j < (int)g.entries.size(); j++) {
        const EntryStatus &st = entry_status(g, j);
        seconds += st.seconds;
        if (st.last > last) last = st.last;
    }
}

void entry_status_forget(const std::string &group_key, const std::string &stem) {
    s_cache.erase(stats_key(group_key, stem));
}

void entry_status_forget_all(void) { s_cache.clear(); }
