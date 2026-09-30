/* prefs.json, stats.json, file helpers, translations and box shapes. */
#include "check.h"
#include "scratch.h"
#include "boxshape.h"
#include "fsutil.h"
#include "i18n.h"
#include "prefs.h"
#include "stats.h"
#include <algorithm>
#include <cstring>

TEST(prefs_round_trip) {
    Scratch sc("prefs");
    prefs_set_shader("zelda3", 3);
    prefs_set_view_3d(false);
    prefs_set_language("es");
    prefs_set_last_group("zelda3");
    prefs_set_last_entry("zelda3", 1);
    prefs_set_perf_hud(true);
    prefs_save();
    CHECK(Scratch::read("prefs.json").find("\"language\": \"es\"") != std::string::npos);

    /* Change everything in memory, then load the file back. */
    prefs_set_shader("zelda3", 0);
    prefs_set_view_3d(true);
    prefs_set_language("en");
    prefs_set_last_group("other");
    prefs_set_last_entry("zelda3", 0);
    prefs_set_perf_hud(false);
    prefs_load();
    CHECK_EQ(prefs_get_shader("zelda3"), 3);
    CHECK_EQ(prefs_get_shader("unknown"), 0);
    CHECK(!prefs_get_view_3d());
    CHECK_EQ(prefs_get_language(), std::string("es"));
    CHECK_EQ(prefs_get_last_group(), std::string("zelda3"));
    CHECK_EQ(prefs_get_last_entry("zelda3"), 1);
    CHECK_EQ(prefs_get_last_entry("unknown"), -1);
    CHECK(prefs_get_perf_hud());
    CHECK(!fs_exists("prefs.json.tmp"));
    /* Leave the defaults for the other tests. */
    prefs_set_view_3d(true);
    prefs_set_language("auto");
    prefs_set_perf_hud(false);
}

TEST(stats_sessions_and_time) {
    Scratch sc("stats");
    stats_load("stats.json");                          /* missing: starts empty */
    const std::string key = stats_key("zelda3", "alttp");
    CHECK_EQ(key, std::string("zelda3/alttp"));
    CHECK_EQ(stats_get(key).sessions, 0);
    stats_begin(key);
    for (int i = 0; i < 200; i++) stats_add(0.5);      /* 100 s */
    stats_add(5.0);                                    /* a stall: ignored */
    stats_end();
    stats_begin(key);
    stats_add(0.25);
    stats_end();
    stats_load("stats.json");
    PlayStats p = stats_get(key);
    CHECK_EQ(p.sessions, 2);
    CHECK_NEAR(p.seconds, 100.25, 1.0);
    CHECK(p.last > 0);
    CHECK_EQ(stats_get("other/x").sessions, 0);
}

TEST(stats_format_duration) {
    i18n_set(LANG_EN);
    CHECK_EQ(stats_format_duration(59.0), std::string("< 1 min"));
    CHECK_EQ(stats_format_duration(60.0), std::string("1 min"));
    CHECK_EQ(stats_format_duration(3600.0), std::string("1 h"));
    CHECK_EQ(stats_format_duration(3720.0), std::string("1 h 2 min"));
}

TEST(fsutil_basics) {
    Scratch sc("fsutil");
    CHECK(fs_mkdirs("a/b/c"));
    CHECK(fs_mkdirs("a/b/c"));                         /* already there: fine */
    CHECK(fs_write_atomic("a/b/c/f.txt", std::string("hello")));
    CHECK(fs_write_atomic("a/b/c/f.txt", std::string("hi")));   /* replaces */
    CHECK_EQ(Scratch::read("a/b/c/f.txt"), std::string("hi"));
    CHECK(!fs_exists("a/b/c/f.txt.tmp"));
    std::vector<unsigned char> data;
    CHECK(fs_read("a/b/c/f.txt", data));
    CHECK_EQ(data.size(), (size_t)2);
    CHECK(fs_copy("a/b/c/f.txt", "a/g.txt"));
    CHECK_EQ(Scratch::read("a/g.txt"), std::string("hi"));
    auto names = fs_list("a/b/c");
    CHECK_EQ(names.size(), (size_t)1);
    CHECK(fs_list("nope").empty());
    CHECK(fs_mtime("a/g.txt") > 0);
    CHECK_EQ(fs_mtime("nope"), 0LL);
    CHECK_EQ(fs_dirname("a/b/c.txt"), std::string("a/b"));
    CHECK_EQ(fs_dirname("c.txt"), std::string(""));
    CHECK_EQ(fs_dirname("sdmc:/emerald/db.json"), std::string("sdmc:/emerald"));
    CHECK_EQ(fs_stem("roms/Z3/zelda.sfc"), std::string("zelda"));
    CHECK_EQ(fs_stem("x/y.tar.gz"), std::string("y.tar"));
    CHECK_EQ(fs_stem(".hidden"), std::string(".hidden"));
}

TEST(i18n_lookup) {
    i18n_set(LANG_ES);
    CHECK_EQ(std::string(tr("Resume")), std::string("Continuar"));
    CHECK_EQ(std::string(tr("%d VERSIONS")), std::string("%d VERSIONES"));
    CHECK_EQ(std::string(tr("no translation for this")), std::string("no translation for this"));
    i18n_set(LANG_EN);
    CHECK_EQ(std::string(tr("Resume")), std::string("Resume"));
    i18n_init("es");
    CHECK_EQ(i18n_lang(), LANG_ES);
    i18n_init("en");
    CHECK_EQ(i18n_lang(), LANG_EN);
    CHECK_EQ(std::string(i18n_code(LANG_ES)), std::string("es"));
}

TEST(box_families) {
    CHECK_EQ(boxfamily_of("NES"), FAM_NES);
    CHECK_EQ(boxfamily_of("Famicom"), FAM_NES);
    CHECK_EQ(boxfamily_of("SNES"), FAM_SNES);
    CHECK_EQ(boxfamily_of("SNES (Satellaview)"), FAM_SNES);
    CHECK_EQ(boxfamily_of("Super Famicom"), FAM_SNES);
    CHECK_EQ(boxfamily_of("N64"), FAM_N64);
    CHECK_EQ(boxfamily_of("Nintendo 64"), FAM_N64);
    CHECK_EQ(boxfamily_of("Game Boy"), FAM_GB);
    CHECK_EQ(boxfamily_of("GBC (Fan Remake)"), FAM_GBC);
    CHECK_EQ(boxfamily_of("Game Boy Color"), FAM_GBC);
    CHECK_EQ(boxfamily_of("Game Boy Advance"), FAM_GBA);
    CHECK_EQ(boxfamily_of("CD-i"), FAM_CDI);
    CHECK_EQ(boxfamily_of("PC / Switch Port"), FAM_PC);
    CHECK_EQ(boxfamily_of(""), FAM_DEFAULT);
    CHECK_EQ(boxfamily_of(nullptr), FAM_DEFAULT);
}

TEST(box_shapes) {
    CHECK(boxfamily_landscape(FAM_SNES));
    CHECK(boxfamily_landscape(FAM_N64));
    CHECK(!boxfamily_landscape(FAM_NES));
    CHECK(!boxfamily_landscape(FAM_CDI));
    for (int f = 0; f < FAM_COUNT; f++)
        for (int land = 0; land < 2; land++) {
            const BoxShape *s = boxshape_get(boxshape_id((BoxFamily)f, land != 0));
            CHECK_EQ(s->landscape, land != 0);
            CHECK_EQ(s->w > s->h, land != 0);           /* orientation holds */
            CHECK_EQ(s->fw, (int)(s->w * BOXART_DENSITY + 0.5f));
            CHECK_EQ(s->atlas_w, s->fw + s->sd);
            CHECK_EQ(s->atlas_h, s->fh + s->sd);
            CHECK(s->bevel > 0.0f && s->bevel < s->d * 0.5f);
        }
    CHECK_EQ(boxshape_get(boxshape_id(FAM_CDI, false))->style, BOXSTYLE_CASE);
    CHECK_EQ(boxshape_get(boxshape_id(FAM_SNES, true))->style, BOXSTYLE_CARDBOARD);
    CHECK(boxshape_get(-5) != nullptr);                /* out of range: a safe default */
}
