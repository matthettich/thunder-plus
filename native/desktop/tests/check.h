// A tiny test frame for the H6 host tests (the repo's other native tests do the same: a CHECK macro and a fail count).
#ifndef TPD_CHECK_H
#define TPD_CHECK_H
#include <cstdio>
static int g_fail = 0, g_checks = 0;
#define CHECK(c, ...) do { g_checks++; if (!(c)) { g_fail++; std::printf("FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)
#define DONE(name) do { std::printf("%s: %d checks, %d failed\n", name, g_checks, g_fail); return g_fail ? 1 : 0; } while (0)
#endif
