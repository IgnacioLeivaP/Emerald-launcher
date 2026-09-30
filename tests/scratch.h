#pragma once
/* A fresh scratch folder under the system temp directory; the test runs
   inside it (the working directory is restored afterwards). */
#include <filesystem>
#include <fstream>
#include <string>

struct Scratch {
    std::filesystem::path dir, prev;

    explicit Scratch(const char *name) {
        prev = std::filesystem::current_path();
        dir = std::filesystem::temp_directory_path() / (std::string("el_tests_") + name);
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
        std::filesystem::create_directories(dir);
        std::filesystem::current_path(dir);
    }
    ~Scratch() {
        std::error_code ec;
        std::filesystem::current_path(prev, ec);
        std::filesystem::remove_all(dir, ec);
    }

    /* Create a file (and its folders) relative to the scratch folder. */
    static void write(const std::string &path, const std::string &content = "") {
        std::filesystem::path p(path);
        if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path());
        std::ofstream(p, std::ios::binary) << content;
    }
    static std::string read(const std::string &path) {
        std::ifstream f(path, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    }
};
