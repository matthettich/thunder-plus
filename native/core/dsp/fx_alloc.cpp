// See fx_alloc.h. Not force-included itself (it is a plain source of the library), so malloc here is the C library's.
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <new>
namespace { std::atomic<int> g_poison{-1}; std::atomic<long> g_failAt{-1}, g_count{0}; }
// Test hook (test_render_api_oom): the k-th thunder_fx_malloc from now on (1-based) fails, as a full address space would make it; k <= 0: off.
extern "C" void thunder_fx_fail_at(long k) { g_count.store(0, std::memory_order_relaxed); g_failAt.store(k, std::memory_order_relaxed); }
extern "C" void thunder_fx_poison(int byte) { g_poison.store(byte, std::memory_order_relaxed); }   // -1: off (the default); else every allocation is filled with this byte
extern "C" void* thunder_fx_malloc(size_t n) {
  const long fa = g_failAt.load(std::memory_order_relaxed);
  void* p = nullptr;
  if (fa > 0 && g_count.fetch_add(1, std::memory_order_relaxed) + 1 == fa) g_failAt.store(-1, std::memory_order_relaxed);   // the test hook: this one fails
  else p = std::malloc(n);
  if (!p) {   // Fx::operator new (the web's source) does not check: a failed malloc must not become a null write. The host catches bad_alloc (render_api.cpp: E_MEMORY).
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS)
    throw std::bad_alloc();
#else
    std::abort();
#endif
  }
  const int b = g_poison.load(std::memory_order_relaxed);
  if (b >= 0) std::memset(p, b, n);
  return p;
}
