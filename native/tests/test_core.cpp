#include <cstdio>
#include <vector>
#include "thunder/core.h"

int main() {
  thunder_core* c = thunder_create(48000, 512);
  if (!c) { std::puts("FAIL: create"); return 1; }
  std::vector<float> l(512, 1.0f), r(512, 1.0f);
  thunder_process(c, l.data(), r.data(), 512);
  for (int i = 0; i < 512; i++)
    if (l[i] != 0.0f || r[i] != 0.0f) { std::puts("FAIL: not silent"); return 1; }
  thunder_destroy(c);
  std::puts("PASS");
  return 0;
}
