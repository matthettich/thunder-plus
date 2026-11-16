// See fx_alloc.h. Not force-included itself (it is a plain source of the library), so malloc here is the C library's.
#include <atomic>
#include <cstdlib>
#include <cstring>
namespace { std::atomic<int> g_poison{-1}; }
extern "C" void thunder_fx_poison(int byte) { g_poison.store(byte, std::memory_order_relaxed); }   // -1: off (the default); else every allocation is filled with this byte
extern "C" void* thunder_fx_malloc(size_t n) {
  void* p = std::malloc(n);
  const int b = g_poison.load(std::memory_order_relaxed);
  if (p && b >= 0) std::memset(p, b, n);
  return p;
}
