// Host builds only, force-included after <stdlib.h>: every later rand() / std::rand() of the effect sources is wasi-libc's (wasi_rand.cpp).
#pragma once
#include <cstdlib>
#include <stdlib.h>
extern "C" int thunder_wasi_rand(void);
namespace std { using ::thunder_wasi_rand; }
#define rand thunder_wasi_rand
#if defined(_MSC_VER) && defined(RAND_MAX) && RAND_MAX != 0x7fffffff
// W-TFX (clang-cl): UCRT's RAND_MAX is 32767; wasi-libc's (and glibc's) is 0x7fffffff, and thunder_wasi_rand returns 31-bit values. Cloud Seed's modulation phases are
// 0.01 + 0.98 * rand() / (float)RAND_MAX: with 32767 they were far above 1 (the "space" effect measured -13 dB against the web's).
#undef RAND_MAX
#define RAND_MAX 0x7fffffff
#endif
