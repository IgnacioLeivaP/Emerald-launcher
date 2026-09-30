#pragma once
/* What the launcher knows about each version beyond db.json: whether it has
   a save, an automatic state to continue from, and how much it was played.
   Looked up on the disk once and cached; refreshed when a game ends. */
#include "db.h"
#include <string>

struct EntryStatus {
    bool        has_save = false;   /* battery save (.srm) or a save state  */
    long long   resume   = 0;       /* automatic state: when (0 = none)      */
    std::string resume_state;       /* its path, and its thumbnail           */
    std::string resume_thumb;
    double      seconds  = 0.0;     /* play time                             */
    long long   last     = 0;       /* last played (epoch seconds, 0 = never) */
};

const EntryStatus &entry_status(const GameGroup &g, int entry_idx);
/* Play time and last session of a whole game (all its versions). */
void group_play(const GameGroup &g, double &seconds, long long &last);
/* Forget what was cached (after a game ran, or saves were deleted). */
void entry_status_forget(const std::string &group_key, const std::string &stem);
void entry_status_forget_all(void);
