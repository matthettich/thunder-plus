// Host builds only, force-included into every translation unit of the tfx library (after <stdlib.h>): malloc of the effect sources is
// thunder_fx_malloc (fx_alloc.cpp), which is the plain malloc unless a test asks it to fill what it returns (thunder_fx_poison).
// Why: Fx::operator new (dsp/tfx/tfx.h, the web app's source, not edited) zero-fills the object right after malloc, before the constructor
// runs. A compiler may treat a store before the constructor as dead (GCC's -flifetime-dse), and then an effect starts with whatever malloc
// returned. test_fxzero fills every allocation with a pattern and checks the effects still start from zeros, with every compiler in CI.
#pragma once
#include <cstdlib>
#include <stdlib.h>
extern "C" void* thunder_fx_malloc(size_t n);
namespace std { using ::thunder_fx_malloc; }
#define malloc thunder_fx_malloc
