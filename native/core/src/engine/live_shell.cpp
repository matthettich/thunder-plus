// U26: the live shell's four instantiations are compiled here so that the library (and every compiler of CI) checks them; the header is the code.
#include "engine/live_shell.h"

namespace thunder {
template class LiveShell<BassLive>;
template class LiveShell<VoxLive>;
template class LiveShell<AcidLive>;
template class LiveShell<MachLive>;
}  // namespace thunder
