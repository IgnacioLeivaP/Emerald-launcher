#pragma once
/* A very small test harness: TEST(name) { CHECK(...); CHECK_EQ(a, b); }.
   Every TEST registers itself; tests/main.cpp runs them all (or the ones
   whose name contains the first argument) and fails on any failed check. */
#include <cmath>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

namespace check {

struct Test {
    const char *name;
    void (*fn)(void);
};

inline std::vector<Test> &registry() {
    static std::vector<Test> tests;
    return tests;
}

inline int &failures() {
    static int n = 0;
    return n;
}

struct Registrar {
    Registrar(const char *name, void (*fn)(void)) { registry().push_back(Test{name, fn}); }
};

template <typename T> std::string show(const T &v) {
    std::ostringstream os;
    os << v;
    return os.str();
}

inline void fail(const char *file, int line, const std::string &what) {
    std::fprintf(stderr, "  FAILED %s:%d: %s\n", file, line, what.c_str());
    failures()++;
}

} // namespace check

#define TEST(name)                                                        \
    static void test_##name(void);                                        \
    static check::Registrar reg_##name(#name, test_##name);               \
    static void test_##name(void)

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) check::fail(__FILE__, __LINE__, #cond);              \
    } while (0)

#define CHECK_EQ(a, b)                                                    \
    do {                                                                  \
        auto _a = (a);                                                    \
        auto _b = (b);                                                    \
        if (!(_a == _b))                                                  \
            check::fail(__FILE__, __LINE__, std::string(#a " == " #b "  (") + \
                        check::show(_a) + " vs " + check::show(_b) + ")"); \
    } while (0)

#define CHECK_NEAR(a, b, tol)                                             \
    do {                                                                  \
        double _a = (double)(a), _b = (double)(b);                        \
        if (std::fabs(_a - _b) > (tol))                                   \
            check::fail(__FILE__, __LINE__, std::string(#a " ~= " #b "  (") + \
                        check::show(_a) + " vs " + check::show(_b) + ")"); \
    } while (0)
