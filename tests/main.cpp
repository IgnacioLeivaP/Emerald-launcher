/* Unit tests: run with `ctest` (or ./el_tests [name-filter]) from the build
   directory. Each test works in its own scratch folder under the system's
   temporary directory, so nothing in the repository is touched. */
#include "check.h"
#include <cstring>

int main(int argc, char **argv) {
    const char *filter = argc > 1 ? argv[1] : nullptr;
    int run = 0;
    for (const auto &t : check::registry()) {
        if (filter && !std::strstr(t.name, filter)) continue;
        const int before = check::failures();
        t.fn();
        run++;
        std::printf("%s %s\n", check::failures() == before ? "ok  " : "FAIL", t.name);
    }
    std::printf("%d tests, %d failed checks\n", run, check::failures());
    return check::failures() == 0 && run > 0 ? 0 : 1;
}
