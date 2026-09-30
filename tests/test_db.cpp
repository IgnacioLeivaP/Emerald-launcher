/* db.json loading: groups, ROM scanning, platform filters, captures,
   external / RetroArch entries, standalone ROMs and week unlocking. */
#include "check.h"
#include "scratch.h"
#include "db.h"
#include <algorithm>

namespace {

const char *DB = R"({
  "extensions": {
    "sfc": { "core": "snes9x_libretro.dll" },
    "gb":  { "core": "gambatte_libretro.dll" },
    "z64": { "core": "mupen64plus_next_libretro.dll" }
  },
  "retroarch": {
    "exe_win": "C:/RA/retroarch.exe", "cores_win": "C:/RA/cores",
    "cores_switch": "sdmc:/retroarch/cores",
    "exe_linux": "/usr/bin/retroarch", "cores_linux": "/opt/cores"
  },
  "games": {
    "zelda3": {
      "title": "A Link to the Past", "year": 1991, "cover": "roms/box.png",
      "entries": [
        { "stem": "alttp", "title": "Original", "platform": "SNES",
          "screenshots": ["roms/a.png"] },
        { "stem": "alttp_redux", "title": "Redux", "platform": "SNES", "year": 2020 },
        { "stem": "missing", "title": "ROM not on disk" }
      ]
    },
    "ast": {
      "title": "Ancient Stone Tablets", "sequential": true,
      "entries": [
        { "stem": "w1", "title": "Week 1", "week_complete_mask": 3 },
        { "stem": "w2", "title": "Week 2", "carry_save_from": "w1" },
        { "stem": "w3", "title": "Week 3", "carry_save_from": "w2" }
      ]
    },
    "pconly":     { "title": "PC only", "platforms": ["pc"], "entries": [ { "stem": "pconly" } ] },
    "switchonly": { "title": "Switch only", "platforms": ["switch"], "entries": [ { "stem": "switchonly" } ] },
    "port": {
      "title": "Native port",
      "entries": [ { "title": "PC", "external": { "exe": "C:/p.exe", "linux": "/opt/p", "nro": "sdmc:/p.nro", "argv": "-x" } } ]
    },
    "n64": { "title": "N64", "entries": [ { "stem": "oot", "title": "OoT", "retroarch_core": "mupen64plus_next_libretro" } ] }
  }
})";

void make_library(void) {
    Scratch::write("db.json", DB);
    for (const char *f : { "roms/alttp.sfc", "roms/sub/alttp_redux.sfc", "roms/w1.sfc", "roms/w2.sfc",
                           "roms/w3.sfc", "roms/pconly.sfc", "roms/switchonly.sfc", "roms/loose.gb",
                           "roms/oot.z64", "roms/notes.txt" })
        Scratch::write(f, "rom");
    /* In-game captures sit next to each ROM: two of alttp, one of the redux
       (in its own folder), and files that aren't captures of alttp. */
    for (const char *f : { "roms/captures/alttp-20260101-120000.png", "roms/captures/alttp-20260102-120000.png",
                           "roms/captures/alttp_redux-20260104-000000.png", "roms/captures/alttp-notastamp.png",
                           "roms/sub/captures/alttp_redux-20260103-000000.png" })
        Scratch::write(f, "png");
}

const GameGroup *find(const std::vector<GameGroup> &gs, const std::string &key) {
    for (const auto &g : gs) if (g.key == key) return &g;
    return nullptr;
}

} // namespace

TEST(db_groups_and_filters) {
    Scratch sc("db_groups");
    make_library();
    auto gs = db_load("db.json", "roms", "saves", "cores");
    CHECK(find(gs, "zelda3") != nullptr);
    CHECK(find(gs, "ast") != nullptr);
    CHECK(find(gs, "port") != nullptr);
    CHECK(find(gs, "n64") != nullptr);
    CHECK(find(gs, "loose") != nullptr);               /* a ROM no game claims */
#ifdef __SWITCH__
    CHECK(find(gs, "pconly") == nullptr);
    CHECK(find(gs, "switchonly") != nullptr);
#else
    CHECK(find(gs, "pconly") != nullptr);
    CHECK(find(gs, "switchonly") == nullptr);           /* "platforms": ["switch"] */
#endif
    const GameGroup *z = find(gs, "zelda3");
    if (!z) return;
    CHECK_EQ(z->entries.size(), (size_t)2);             /* the missing ROM is skipped */
    CHECK_EQ(z->year, 1991);
    CHECK_EQ(z->cover_path, std::string("roms/box.png"));
    CHECK_EQ(z->entries[0].rom_path, std::string("roms/alttp.sfc"));
    CHECK_EQ(z->entries[0].core_dll, std::string("cores/snes9x_libretro.dll"));
    CHECK_EQ(z->entries[0].srm_path, std::string("saves/alttp.srm"));
    CHECK_EQ(z->entries[0].year, 1991);                 /* inherits the game's */
    CHECK_EQ(z->entries[1].year, 2020);
    CHECK(z->entries[1].rom_path.find("sub/alttp_redux.sfc") != std::string::npos);
}

TEST(db_captures_follow_screenshots) {
    Scratch sc("db_captures");
    make_library();
    auto gs = db_load("db.json", "roms", "saves", "cores");
    const GameGroup *z = find(gs, "zelda3");
    if (!z) { CHECK(z != nullptr); return; }
    const auto &shots = z->entries[0].screenshots;
    CHECK_EQ(z->entries[0].shots_explicit, 1);
    CHECK_EQ(shots.size(), (size_t)3);
    if (shots.size() == 3) {
        CHECK_EQ(shots[0], std::string("roms/a.png"));
        CHECK_EQ(shots[1], std::string("roms/captures/alttp-20260102-120000.png"));   /* newest first */
        CHECK_EQ(shots[2], std::string("roms/captures/alttp-20260101-120000.png"));
    }
    /* "alttp_redux-..." belongs to the redux only, even though it starts
       with "alttp", and only the one next to its ROM counts. */
    const auto &redux = z->entries[1].screenshots;
    CHECK_EQ(redux.size(), (size_t)1);
    if (redux.size() == 1) CHECK_EQ(redux[0], std::string("roms/sub/captures/alttp_redux-20260103-000000.png"));
    CHECK_EQ(z->entries[1].shots_explicit, 0);
}

TEST(db_external_and_retroarch) {
    Scratch sc("db_external");
    make_library();
    auto gs = db_load("db.json", "roms", "saves", "cores");
    const GameGroup *p = find(gs, "port");
    const GameGroup *n = find(gs, "n64");
    if (!p || !n) { CHECK(p && n); return; }
    const GameEntry &e = p->entries[0];
    CHECK(e.external);
    CHECK_EQ(e.exec_argv, std::string("-x"));
#if defined(_WIN32)
    CHECK_EQ(e.exec_path, std::string("C:/p.exe"));
#else
    CHECK_EQ(e.exec_path, std::string("/opt/p"));
#endif
    const GameEntry &ra = n->entries[0];
    CHECK(ra.external);
    CHECK(ra.keep_open);
    CHECK(ra.exec_argv.find("roms/oot.z64") != std::string::npos);
#if defined(_WIN32)
    CHECK_EQ(ra.exec_path, std::string("C:/RA/retroarch.exe"));
    CHECK(ra.exec_argv.find("C:/RA/cores/mupen64plus_next_libretro.dll") != std::string::npos);
#else
    CHECK_EQ(ra.exec_path, std::string("/usr/bin/retroarch"));
    CHECK(ra.exec_argv.find("/opt/cores/mupen64plus_next_libretro.so") != std::string::npos);
#endif
}

TEST(db_week_unlocking) {
    Scratch sc("db_weeks");
    make_library();
    auto gs = db_load("db.json", "roms", "saves", "cores");
    const GameGroup *a = find(gs, "ast");
    if (!a) { CHECK(a != nullptr); return; }
    CHECK_EQ(a->entries.size(), (size_t)3);
    CHECK_EQ(a->entries[0].week_complete_mask, 3);
    CHECK_EQ(a->entries[1].carry_srm_from, std::string("saves/w1.srm"));
    CHECK_EQ(db_progress_get("ast"), 0);
    CHECK(db_entry_unlocked(*a, 0));
    CHECK(!db_entry_unlocked(*a, 1));
    CHECK(!db_entry_unlocked(*a, 2));
    db_progress_set("ast", 1);                          /* week 1 completed */
    CHECK(db_entry_unlocked(*a, 1));
    CHECK(!db_entry_unlocked(*a, 2));
    /* Saved next to db.json and read back by the next load. */
    CHECK(Scratch::read("progress.json").find("\"ast\": 1") != std::string::npos);
    db_progress_set("ast", 0);
    auto again = db_load("db.json", "roms", "saves", "cores");
    CHECK_EQ(db_progress_get("ast"), 0);
    /* Non-sequential games are always open. */
    const GameGroup *z = find(again, "zelda3");
    if (z) CHECK(db_entry_unlocked(*z, 1));
}

TEST(db_missing_or_broken_file) {
    Scratch sc("db_broken");
    CHECK(db_load("nope.json", "roms", "saves", "cores").empty());
    Scratch::write("db.json", "{ this is not json");
    CHECK(db_load("db.json", "roms", "saves", "cores").empty());
}
