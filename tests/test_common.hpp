#pragma once
// Tiny test helpers (no external test framework needed).
#include <cmath>
#include <iostream>

static int g_failures = 0;

#define CHECK(cond)                                                                      \
    do {                                                                                 \
        if (!(cond)) {                                                                   \
            std::cerr << "  FAIL  " << __FILE__ << ":" << __LINE__ << "   " << #cond << "\n"; \
            ++g_failures;                                                                \
        }                                                                                \
    } while (0)

#define CHECK_NEAR(a, b, tol)                                                            \
    do {                                                                                 \
        const double _a = (a), _b = (b);                                                 \
        if (!(std::fabs(_a - _b) <= (tol))) {                                            \
            std::cerr << "  FAIL  " << __FILE__ << ":" << __LINE__ << "   " << #a << " (" << _a \
                      << ") not near " << #b << " (" << _b << ")\n";                     \
            ++g_failures;                                                                \
        }                                                                                \
    } while (0)

inline int finishTest(const char* name)
{
    if (g_failures == 0) {
        std::cout << name << ": all checks passed\n";
        return 0;
    }
    std::cout << name << ": " << g_failures << " check(s) FAILED\n";
    return 1;
}
