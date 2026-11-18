// U04: the registry of the LXR seam (include/thunder/lxr_seam.h). No LXR code, symbol or header: one pointer.
#include <atomic>

#include "thunder/lxr_seam.h"

namespace {
std::atomic<const ThunderLxrApi*> g_api{nullptr};
}

extern "C" {
int thunder_lxr_register(const ThunderLxrApi* api) {
  if (!api || api->abi != THUNDER_LXR_ABI || api->size != sizeof(ThunderLxrApi) || !api->describe || !api->render_bound || !api->render_voice) return -1;
  g_api.store(api, std::memory_order_release);
  return 0;
}
void thunder_lxr_unregister(void) { g_api.store(nullptr, std::memory_order_release); }
const ThunderLxrApi* thunder_lxr_api(void) { return g_api.load(std::memory_order_acquire); }
}
