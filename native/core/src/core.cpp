#include "thunder/core.h"

#include <cstring>
#include <new>

struct thunder_core {
  int sample_rate;
  int max_block;
};

extern "C" {

thunder_core* thunder_create(int sample_rate, int max_block) {
  thunder_core* c = new (std::nothrow) thunder_core();
  if (!c) return nullptr;
  c->sample_rate = sample_rate;
  c->max_block = max_block;
  return c;
}

void thunder_process(thunder_core* core, float* out_l, float* out_r, int frames) {
  (void)core;
  if (frames <= 0) return;
  std::memset(out_l, 0, sizeof(float) * (size_t)frames);
  std::memset(out_r, 0, sizeof(float) * (size_t)frames);
}

int thunder_post(thunder_core*, const char*, int) { return 0; }
int thunder_poll(thunder_core*, char*, int) { return 0; }
int thunder_put(thunder_core*, int, const char*, const float*, int) { return 0; }

void thunder_destroy(thunder_core* core) { delete core; }

}  // extern "C"
