// Host-only force-include for Cloud Seed: its headers call std::rand / std::sinf / std::sqrtf / std::expf, which wasi-sdk's libc++
// provides but libstdc++ does not (under -std=c++14). Not used for the wasm build, so the wasm objects are unchanged.
#pragma once
#include <cmath>
#include <cstdlib>
#include <math.h>
#include <stdlib.h>
namespace std { using ::rand; using ::sinf; using ::sqrtf; using ::expf; }
