#pragma once
/* Small portable file helpers (Windows, Linux, Switch/newlib). */
#include <cstddef>
#include <string>
#include <vector>

bool fs_exists(const std::string &path);
/* mkdir -p: creates every missing directory of `path`. */
bool fs_mkdirs(const std::string &path);
/* Directory part of a path ("a/b/c.txt" → "a/b", "c.txt" → ""). */
std::string fs_dirname(const std::string &path);
/* "a/b/c.txt" → "c", "rom.sfc" → "rom". */
std::string fs_stem(const std::string &path);
bool fs_copy(const std::string &src, const std::string &dst);
/* Whole file into memory; false if it can't be read. */
bool fs_read(const std::string &path, std::vector<unsigned char> &out);
/* Write through a temporary file and rename it into place, so a crash or a
   power cut mid-write never leaves a truncated prefs / stats / save state. */
bool fs_write_atomic(const std::string &path, const void *data, size_t size);
bool fs_write_atomic(const std::string &path, const std::string &text);
/* Seconds since the epoch of the file's last modification (0 = missing). */
long long fs_mtime(const std::string &path);
