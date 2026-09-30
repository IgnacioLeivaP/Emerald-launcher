#pragma once
/* Play statistics per version, kept in stats.json next to db.json:
     { "zelda3/zeldatriforce": { "seconds": 4520, "last": 1727700000, "sessions": 7 } }
   Only time actually spent playing counts (not the pause menu). */
#include <string>

struct PlayStats {
    double    seconds  = 0.0;
    long long last     = 0;     /* epoch seconds of the last session, 0 = never */
    int       sessions = 0;
};

void        stats_load(const std::string &path);
std::string stats_key(const std::string &group_key, const std::string &stem);
PlayStats   stats_get(const std::string &key);

/* A game session: begin when it starts, add the time played every frame,
   end when leaving the game (also saves). */
void stats_begin(const std::string &key);
void stats_add(double seconds);
void stats_end(void);
void stats_save(void);

/* "12 h 40 min", "35 min", "< 1 min" (translated). */
std::string stats_format_duration(double seconds);
