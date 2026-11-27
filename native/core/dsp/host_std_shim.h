// Host-only force-include for Cloud Seed: its headers call std::rand / std::sinf / std::sqrtf / std::expf, which wasi-sdk's libc++
// provides but libstdc++ does not (under -std=c++14). Not used for the wasm build, so the wasm objects are unchanged.
#pragma once
#include <cmath>
#include <cstdlib>
#include <math.h>
#include <stdlib.h>
namespace std { using ::rand; using ::sinf; using ::sqrtf; using ::expf; }
#if defined(_MSC_VER)
// W-TFX (clang-cl): UCRT declares a real strcpy_s (it aborts on a too-long string); Cloud Seed's Parameters.h wants the truncating strncpy
// the Linux and wasm builds get from -D"strcpy_s(a,b,c)=strncpy(a,c,b)". Defined after the headers so the UCRT declaration is untouched.
#include <string.h>
#include <cstring>
#define strcpy_s(a, b, c) strncpy(a, c, b)
#endif
