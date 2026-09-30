#include "db.h"
#include "fsutil.h"
#include "i18n.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <fstream>
#include <map>
#include <set>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#ifdef _WIN32
#  include <windows.h>
#else
#  include <dirent.h>
#  include <sys/stat.h>
#endif

using json = nlohmann::ordered_json;

/* ── Platform filtering ──────────────────────────────────────────────
   A group/entry with a "platforms" array is only shown on those platforms
   (e.g. "platforms": ["pc"] hides CD-i on Switch, where there's no core).
   Absent = available everywhere. */
#ifdef __SWITCH__
static const char *EL_PLATFORM = "switch";
#else
static const char *EL_PLATFORM = "pc";
#endif

#if !defined(__SWITCH__) && !defined(_WIN32)
/* RetroArch core on Linux: the configured folder, else the usual places
   (user-installed cores first, then distro packages). */
static std::string linux_core_path(const std::string &dir, const std::string &core) {
    const std::string file = core + ".so";
    if (!dir.empty()) return dir + "/" + file;
    std::vector<std::string> dirs;
    if (const char *home = getenv("HOME")) dirs.push_back(std::string(home) + "/.config/retroarch/cores");
    dirs.push_back("/usr/lib/x86_64-linux-gnu/libretro");
    dirs.push_back("/usr/lib/aarch64-linux-gnu/libretro");
    dirs.push_back("/usr/lib/libretro");
    dirs.push_back("/usr/local/lib/libretro");
    for (const auto &d : dirs)
        if (fs_exists(d + "/" + file)) return d + "/" + file;
    return dirs.front() + "/" + file;
}
#endif

static bool platform_allowed(const json &j) {
    if (!j.contains("platforms")) return true;
    for (const auto &p : j["platforms"])
        if (p.is_string() && p.get<std::string>() == EL_PLATFORM) return true;
    return false;
}

/* Image paths in db.json (logos, screenshots) are relative to the content root.
   On Switch that root is sdmc:/emerald/; on PC it's the working dir. */
#ifdef __SWITCH__
static const std::string CONTENT_BASE = "sdmc:/emerald/";
#else
static const std::string CONTENT_BASE = "";
#endif
static std::string content_path(const std::string &p) {
    return p.empty() ? p : CONTENT_BASE + p;
}

/* ── Progress file (saves progress.json next to db.json) ─────────────── */
static std::string s_progress_path;
static std::map<std::string,int> s_progress;

static void progress_load(void) {
    std::ifstream f(s_progress_path);
    if (!f.is_open()) return;
    try {
        json j = json::parse(f);
        for (auto &[k,v] : j.items())
            s_progress[k] = v.get<int>();
    } catch (...) {}
}

static void progress_save(void) {
    json j = s_progress;
    if (!fs_write_atomic(s_progress_path, j.dump(2)))
        fprintf(stderr, "db: couldn't write %s\n", s_progress_path.c_str());
}

int db_progress_get(const std::string &key) {
    auto it = s_progress.find(key);
    return it != s_progress.end() ? it->second : 0;
}

void db_progress_set(const std::string &key, int idx) {
    s_progress[key] = idx;
    progress_save();
}

/* ── Recursive ROM scanner — returns stem → full_path ────────────────── */
#ifdef _WIN32
static void scan_recursive(const char *dir,
                            const std::set<std::string> &valid_exts,
                            std::map<std::string,std::string> &out) {
    char pattern[1024];
    snprintf(pattern, sizeof(pattern), "%s\\*", dir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (strcmp(fd.cFileName,".")==0 || strcmp(fd.cFileName,"..")==0) continue;
        char full[1024];
        snprintf(full, sizeof(full), "%s/%s", dir, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            scan_recursive(full, valid_exts, out);
        } else {
            const char *dot = strrchr(fd.cFileName, '.');
            if (!dot) continue;
            std::string ext(dot+1);
            for (auto &c : ext) c = (char)tolower(c);
            if (valid_exts.count(ext)) {
                std::string stem(fd.cFileName, dot - fd.cFileName);
                out[stem] = std::string(full);
            }
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}
#else
static void scan_recursive(const char *dir,
                            const std::set<std::string> &valid_exts,
                            std::map<std::string,std::string> &out) {
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (strcmp(e->d_name,".")==0 || strcmp(e->d_name,"..")==0) continue;
        char full[1024];
        snprintf(full, sizeof(full), "%s/%s", dir, e->d_name);
        struct stat st;
        if (stat(full, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            scan_recursive(full, valid_exts, out);
        } else {
            const char *dot = strrchr(e->d_name, '.');
            if (!dot) continue;
            std::string ext(dot+1);
            for (auto &c : ext) c = (char)tolower(c);
            if (valid_exts.count(ext)) {
                std::string stem(e->d_name, dot - e->d_name);
                out[stem] = std::string(full);
            }
        }
    }
    closedir(d);
}
#endif

/* Screenshots taken in-game (savestate.cpp) sit in a "captures" folder next
   to the ROM as <stem>-YYYYMMDD-HHMMSS.png: they follow the version's own
   screenshots, newest first (and become its box art if db.json has none). */
static void add_captures(GameEntry &e, std::map<std::string, std::vector<std::string>> &listed) {
    e.shots_explicit = (int)e.screenshots.size();
    if (e.rom_path.empty() || e.stem.empty()) return;
    std::string dir = fs_dirname(e.rom_path);
    dir = (dir.empty() ? std::string(".") : dir) + "/captures";
    auto it = listed.find(dir);
    if (it == listed.end()) it = listed.emplace(dir, fs_list(dir)).first;
    const std::string prefix = e.stem + "-";
    std::vector<std::string> mine;
    for (const auto &name : it->second) {
        if (name.size() < prefix.size() + 12 || name.compare(0, prefix.size(), prefix) != 0) continue;
        if (name.compare(name.size() - 4, 4, ".png") != 0) continue;
        bool stamp = name[prefix.size() + 8] == '-';          /* "20260930-" after the stem */
        for (size_t k = prefix.size(); k < prefix.size() + 8 && stamp; k++)
            stamp = name[k] >= '0' && name[k] <= '9';
        if (stamp) mine.push_back(dir + "/" + name);
    }
    std::sort(mine.rbegin(), mine.rend());
    e.screenshots.insert(e.screenshots.end(), mine.begin(), mine.end());
}

/* ── Main loader ──────────────────────────────────────────────────────── */
std::vector<GameGroup> db_load(const char *db_path, const char *roms_dir,
                               const char *saves_dir, const char *cores_dir) {
    /* Derive progress file path */
    {
        std::string dp(db_path);
        size_t slash = dp.find_last_of("/\\");
        std::string dir = (slash!=std::string::npos) ? dp.substr(0,slash+1) : "./";
        s_progress_path = dir + "progress.json";
    }
    progress_load();

    /* Parse db.json */
    json jdb;
    {
        std::ifstream f(db_path);
        if (!f.is_open()) {
            fprintf(stderr,"db: cannot open %s\n",db_path);
            return {};
        }
        try { jdb = json::parse(f); }
        catch (const std::exception &e) {
            fprintf(stderr,"db: JSON error: %s\n",e.what());
            return {};
        }
    }

    /* RetroArch chainload config (for heavy systems like N64) */
    std::string ra_exe_win, ra_cores_win, ra_cores_switch, ra_exe_linux, ra_cores_linux;
    if (jdb.contains("retroarch")) {
        const auto &ra = jdb["retroarch"];
        ra_exe_win      = ra.value("exe_win","");
        ra_cores_win    = ra.value("cores_win","");
        ra_cores_switch = ra.value("cores_switch","");
        ra_exe_linux    = ra.value("exe_linux","");
        ra_cores_linux  = ra.value("cores_linux","");
    }

    /* Build extension → core mapping */
    std::map<std::string,std::string> ext_core;
    std::set<std::string> valid_exts;
    if (jdb.contains("extensions")) {
        for (auto &[ext, info] : jdb["extensions"].items()) {
            std::string lext = ext;
            for (auto &c : lext) c = (char)tolower(c);
            valid_exts.insert(lext);
            std::string core_file = info.value("core","");
            std::string core_path = std::string(cores_dir) + "/" + core_file;
            ext_core[lext] = core_path;
        }
    }

    /* Scan roms directory recursively: stem → full path */
    std::map<std::string,std::string> rom_map;
    scan_recursive(roms_dir, valid_exts, rom_map);

    /* Collect stems claimed by groups (won't appear standalone) */
    std::set<std::string> claimed_stems;
    if (jdb.contains("games")) {
        for (auto &[key, ginfo] : jdb["games"].items()) {
            if (ginfo.contains("entries")) {
                for (auto &e : ginfo["entries"])
                    claimed_stems.insert(e.value("stem",""));
            }
        }
    }

    /* Helper: find ROM path for a stem */
    auto find_rom = [&](const std::string &stem) -> std::string {
        auto it = rom_map.find(stem);
        return it != rom_map.end() ? it->second : "";
    };

    /* Helper: find core dll for a stem (derived from file extension) */
    auto find_core = [&](const std::string &stem) -> std::string {
        auto it = rom_map.find(stem);
        if (it == rom_map.end()) return "";
        size_t dot = it->second.rfind('.');
        if (dot == std::string::npos) return "";
        std::string ext = it->second.substr(dot+1);
        for (auto &c : ext) c = (char)tolower(c);
        auto cit = ext_core.find(ext);
        return cit != ext_core.end() ? cit->second : "";
    };

    std::vector<GameGroup> groups;
    std::map<std::string, std::vector<std::string>> capture_dirs;

    /* ── Build groups from db.json["games"] ── */
    if (jdb.contains("games")) {
        for (auto &[key, ginfo] : jdb["games"].items()) {
            if (!platform_allowed(ginfo)) continue;   /* hidden on this platform */
            GameGroup g;
            g.key         = key;
            g.title          = ginfo.value("title", key);
            g.title_es       = ginfo.value("title_es","");
            g.description    = ginfo.value("description","");
            g.description_es = ginfo.value("description_es","");
            g.year        = ginfo.value("year",0);
            g.sequential  = ginfo.value("sequential",false);
            g.logo_path   = content_path(ginfo.value("logo",""));
            g.cover_path  = content_path(ginfo.value("cover",""));

            /* Core override at group level */
            std::string group_core = ginfo.value("core","");
            if (!group_core.empty())
                group_core = std::string(cores_dir)+"/"+group_core;

            if (ginfo.contains("entries")) {
                for (auto &einfo : ginfo["entries"]) {
                    if (!platform_allowed(einfo)) continue;  /* hidden on this platform */
                    bool has_external = einfo.contains("external");
                    std::string stem = einfo.value("stem","");
                    std::string rom;
                    if (!has_external) {
                        if (stem.empty()) continue;
                        rom = find_rom(stem);
                        if (rom.empty()) continue; /* ROM not on disk — skip */
                    }

                    GameEntry e;
                    e.stem         = stem;
                    e.title        = einfo.value("title", stem);
                    e.title_es     = einfo.value("title_es","");
                    e.platform     = einfo.value("platform","");
                    e.version_desc = einfo.value("version_desc","");
                    e.version_desc_es = einfo.value("version_desc_es","");
                    e.logo_path    = content_path(einfo.value("logo",""));
                    e.cover_path   = content_path(einfo.value("cover",""));
                    if (einfo.contains("screenshots"))
                        for (auto &s : einfo["screenshots"])
                            e.screenshots.push_back(content_path(s.get<std::string>()));
                    e.shots_explicit = (int)e.screenshots.size();
                    e.year     = einfo.value("year", g.year);

                    if (has_external) {
                        /* External launch entry — resolve exec path per platform.
                           Always included (intentionally authored), no ROM/core/srm. */
                        const auto &ext = einfo["external"];
                        e.external  = true;
#ifdef __SWITCH__
                        e.exec_path = ext.value("nro","");
#elif defined(_WIN32)
                        e.exec_path = ext.value("exe","");
#else
                        e.exec_path = ext.value("linux", ext.value("exe",""));
#endif
                        e.exec_argv = ext.value("argv","");
                        g.entries.push_back(e);
                        continue;
                    }

                    /* RetroArch chainload (heavy systems like N64): emulate via
                       RetroArch instead of in-launcher. ROM resolved on disk above. */
                    std::string ra_core = einfo.value("retroarch_core","");
                    if (!ra_core.empty()) {
                        e.rom_path  = rom;
                        e.external  = true;
                        e.keep_open = true;   /* PC: keep the launcher running */
#ifdef __SWITCH__
                        e.exec_path = ra_cores_switch + "/" + ra_core + "_libnx.nro";
                        /* RetroArch expects argv[0]=program, argv[1]=content. */
                        e.exec_argv = "\"" + e.exec_path + "\" \"" + rom + "\"";
#elif defined(_WIN32)
                        e.exec_path = ra_exe_win;
                        e.exec_argv = "-L \"" + ra_cores_win + "/" + ra_core +
                                      ".dll\" \"" + rom + "\"";
#else
                        e.exec_path = ra_exe_linux.empty() ? "retroarch" : ra_exe_linux;
                        e.exec_argv = "-L \"" + linux_core_path(ra_cores_linux, ra_core) +
                                      "\" \"" + rom + "\"";
#endif
                        g.entries.push_back(e);
                        continue;
                    }

                    e.rom_path = rom;
                    e.week_complete_mask = einfo.value("week_complete_mask",0);
                    std::string entry_core = einfo.value("core","");
                    if (!entry_core.empty())
                        e.core_dll = std::string(cores_dir)+"/"+entry_core;
                    else if (!group_core.empty())
                        e.core_dll = group_core;
                    else
                        e.core_dll = find_core(stem);

                    /* srm path in saves_dir */
                    e.srm_path = std::string(saves_dir)+"/"+stem+".srm";

                    /* carry_save_from → source srm */
                    std::string csf = einfo.value("carry_save_from","");
                    if (!csf.empty())
                        e.carry_srm_from = std::string(saves_dir)+"/"+csf+".srm";

                    add_captures(e, capture_dirs);
                    g.entries.push_back(e);
                }
            }

            if (!g.entries.empty()) groups.push_back(g);
        }
    }

    /* ── Standalone ROMs (not claimed by any group) ── */
    for (auto &[stem, rom] : rom_map) {
        if (claimed_stems.count(stem)) continue;
        std::string core = find_core(stem);

        GameGroup g;
        g.key = g.title = stem;
        g.sequential    = false;

        GameEntry e;
        e.stem     = stem;
        e.title    = stem;
        e.rom_path = rom;
        e.core_dll = core;
        e.srm_path = std::string(saves_dir)+"/"+stem+".srm";
        add_captures(e, capture_dirs);
        g.entries.push_back(e);
        groups.push_back(g);
    }

    return groups;
}

static const std::string &pick(const std::string &en, const std::string &es) {
    return i18n_lang() == LANG_ES && !es.empty() ? es : en;
}
const std::string &db_title(const GameGroup &g)        { return pick(g.title, g.title_es); }
const std::string &db_description(const GameGroup &g)  { return pick(g.description, g.description_es); }
const std::string &db_entry_title(const GameEntry &e)  { return pick(e.title, e.title_es); }
const std::string &db_version_desc(const GameEntry &e) { return pick(e.version_desc, e.version_desc_es); }

bool db_entry_unlocked(const GameGroup &g, int idx) {
    if (!g.sequential || idx==0) return true;
    /* A week unlocks once the previous one was completed, i.e. progress was
       advanced past it with the in-game "next week" prompt. (Just having
       started the previous week — its .srm existing — isn't enough.) */
    return idx <= db_progress_get(g.key);
}
