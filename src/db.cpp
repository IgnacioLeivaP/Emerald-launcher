#include "db.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <map>
#include <set>
#include <cstdio>
#include <cstring>

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
    std::ofstream f(s_progress_path);
    if (f.is_open()) f << j.dump(2);
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

static bool file_exists(const std::string &path) {
    FILE *f = fopen(path.c_str(), "rb");
    if (f) { fclose(f); return true; }
    return false;
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
    std::string ra_exe_win, ra_cores_win, ra_cores_switch;
    if (jdb.contains("retroarch")) {
        const auto &ra = jdb["retroarch"];
        ra_exe_win      = ra.value("exe_win","");
        ra_cores_win    = ra.value("cores_win","");
        ra_cores_switch = ra.value("cores_switch","");
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

    /* ── Build groups from db.json["games"] ── */
    if (jdb.contains("games")) {
        for (auto &[key, ginfo] : jdb["games"].items()) {
            if (!platform_allowed(ginfo)) continue;   /* hidden on this platform */
            GameGroup g;
            g.key         = key;
            g.title       = ginfo.value("title", key);
            g.description = ginfo.value("description","");
            g.year        = ginfo.value("year",0);
            g.sequential  = ginfo.value("sequential",false);
            g.logo_path   = content_path(ginfo.value("logo",""));

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
                    e.platform     = einfo.value("platform","");
                    e.version_desc = einfo.value("version_desc","");
                    e.logo_path    = content_path(einfo.value("logo",""));
                    if (einfo.contains("screenshots"))
                        for (auto &s : einfo["screenshots"])
                            e.screenshots.push_back(content_path(s.get<std::string>()));
                    e.year     = einfo.value("year", g.year);

                    if (has_external) {
                        /* External launch entry — resolve exec path per platform.
                           Always included (intentionally authored), no ROM/core/srm. */
                        const auto &ext = einfo["external"];
                        e.external  = true;
#ifdef __SWITCH__
                        e.exec_path = ext.value("nro","");
#else
                        e.exec_path = ext.value("exe","");
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
#else
                        e.exec_path = ra_exe_win;
                        e.exec_argv = "-L \"" + ra_cores_win + "/" + ra_core +
                                      ".dll\" \"" + rom + "\"";
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
        g.entries.push_back(e);
        groups.push_back(g);
    }

    return groups;
}

bool db_entry_unlocked(const GameGroup &g, int idx) {
    if (!g.sequential || idx==0) return true;
    /* Unlocked if previous entry's srm file exists */
    const GameEntry &prev = g.entries[idx-1];
    return file_exists(prev.srm_path);
}
