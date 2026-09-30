#include "fsutil.h"
#include <cstdio>
#include <sys/stat.h>
#include <sys/types.h>
#ifdef _WIN32
#  include <direct.h>
#  include <windows.h>
#else
#  include <dirent.h>
#endif

bool fs_exists(const std::string &path) {
    struct stat st;
    return !path.empty() && stat(path.c_str(), &st) == 0;
}

static bool is_dir(const std::string &path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && (st.st_mode & S_IFMT) == S_IFDIR;
}

static bool make_dir(const std::string &path) {
#ifdef _WIN32
    return _mkdir(path.c_str()) == 0 || is_dir(path);
#else
    return mkdir(path.c_str(), 0777) == 0 || is_dir(path);
#endif
}

bool fs_mkdirs(const std::string &path) {
    if (path.empty() || is_dir(path)) return true;
    for (size_t i = 1; i <= path.size(); i++) {
        if (i < path.size() && path[i] != '/' && path[i] != '\\') continue;
        std::string part = path.substr(0, i);
        if (part.back() == ':') continue;               /* "C:" / "sdmc:" roots */
        if (!is_dir(part) && !make_dir(part)) return false;
    }
    return is_dir(path);
}

std::string fs_dirname(const std::string &path) {
    size_t p = path.find_last_of("/\\");
    return p == std::string::npos ? std::string() : path.substr(0, p);
}

std::string fs_stem(const std::string &path) {
    size_t s = path.find_last_of("/\\");
    std::string name = s == std::string::npos ? path : path.substr(s + 1);
    size_t dot = name.rfind('.');
    return dot == std::string::npos || dot == 0 ? name : name.substr(0, dot);
}

bool fs_read(const std::string &path, std::vector<unsigned char> &out) {
    out.clear();
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) return false;
    unsigned char buf[16384];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) out.insert(out.end(), buf, buf + n);
    bool ok = !ferror(f);
    fclose(f);
    return ok;
}

bool fs_copy(const std::string &src, const std::string &dst) {
    std::vector<unsigned char> data;
    if (!fs_read(src, data)) return false;
    return fs_write_atomic(dst, data.data(), data.size());
}

bool fs_write_atomic(const std::string &path, const void *data, size_t size) {
    const std::string tmp = path + ".tmp";
    FILE *f = fopen(tmp.c_str(), "wb");
    if (!f) return false;
    bool ok = size == 0 || fwrite(data, 1, size, f) == size;
    ok = fflush(f) == 0 && ok;
    ok = fclose(f) == 0 && ok;
    if (!ok) { remove(tmp.c_str()); return false; }
    return fs_replace(tmp, path);
}

bool fs_replace(const std::string &tmp, const std::string &path) {
#ifdef _WIN32
    if (!MoveFileExA(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        remove(tmp.c_str());
        return false;
    }
#else
    /* POSIX rename replaces atomically; the Switch's FS refuses to rename
       onto an existing file, so fall back to remove + rename there. */
    if (rename(tmp.c_str(), path.c_str()) != 0) {
        remove(path.c_str());
        if (rename(tmp.c_str(), path.c_str()) != 0) { remove(tmp.c_str()); return false; }
    }
#endif
    return true;
}

bool fs_write_atomic(const std::string &path, const std::string &text) {
    return fs_write_atomic(path, text.data(), text.size());
}

long long fs_mtime(const std::string &path) {
    struct stat st;
    if (path.empty() || stat(path.c_str(), &st) != 0) return 0;
    return (long long)st.st_mtime;
}

std::vector<std::string> fs_list(const std::string &dir) {
    std::vector<std::string> out;
#ifdef _WIN32
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((dir + "\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) out.push_back(fd.cFileName);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR *d = opendir(dir.c_str());
    if (!d) return out;
    while (struct dirent *e = readdir(d)) {
        if (e->d_name[0] == '.') continue;
        struct stat st;
        if (stat((dir + "/" + e->d_name).c_str(), &st) == 0 && (st.st_mode & S_IFMT) == S_IFREG)
            out.push_back(e->d_name);
    }
    closedir(d);
#endif
    return out;
}
