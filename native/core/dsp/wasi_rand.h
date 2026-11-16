// Host builds only, force-included after <stdlib.h>: every later rand() / std::rand() of the effect sources is wasi-libc's (wasi_rand.cpp).
#pragma once
#include <cstdlib>
#include <stdlib.h>
extern "C" int thunder_wasi_rand(void);
namespace std { using ::thunder_wasi_rand; }
#define rand thunder_wasi_rand
