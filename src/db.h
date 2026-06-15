#pragma once
#include <string>
#include <vector>

struct GameEntry {
    std::string stem;
    std::string title;
    std::string platform;
    std::string              version_desc;
    std::string              logo_path;
    std::vector<std::string> screenshots;
    std::string rom_path;
    std::string core_dll;
    std::string srm_path;          /* explicit srm path (in saves dir) */
    std::string carry_srm_from;    /* copy this srm into srm_path before launch */
    int         year             = 0;
    int         week_complete_mask = 0; /* bits in WRAM[0xF37C] → week done */

    /* External launch (e.g. Ship of Harkinian NRO): instead of loading a ROM
       into a libretro core, chainload/spawn an external executable and exit
       Emerald Launcher. exec_path is resolved per-platform at load time. */
    bool        external         = false;
    std::string exec_path;         /* nro on Switch / exe on PC */
    std::string exec_argv;         /* optional argv string */
    bool        keep_open        = false; /* PC: don't close launcher (e.g. RetroArch) */
};

struct GameGroup {
    std::string              key;
    std::string              title;
    std::string              description;
    int                      year       = 0;
    bool                     sequential = false;
    std::vector<GameEntry>   entries;
    std::string              logo_path;
};

/* Load db.json + scan roms/ directory. Returns populated groups (only those
   with at least one ROM present on disk). */
std::vector<GameGroup> db_load(const char *db_path, const char *roms_dir,
                               const char *saves_dir, const char *cores_dir);

/* Progress: which entry index to auto-launch for a sequential group */
int  db_progress_get(const std::string &group_key);
void db_progress_set(const std::string &group_key, int entry_idx);

/* Check if entry_idx in a sequential group is unlocked.
   For non-sequential groups all entries are always unlocked. */
bool db_entry_unlocked(const GameGroup &g, int entry_idx);
