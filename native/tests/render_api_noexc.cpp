// U34a verification D3: render_api.cpp must compile with C++ exceptions OFF (the wasm build has them off; render.h, MEMORY: there an allocation failure aborts instead
// of being E_MEMORY). This file is that compile, with -fno-exceptions -fno-rtti (tests/CMakeLists.txt, target render_api_noexc_obj, run by the ctest render_api_noexc).
// It is a separate file so that the per-file -fexceptions of core/CMakeLists.txt does not apply to it.
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS)
#error "render_api_noexc must be compiled with exceptions off"
#endif
#include "render/render_api.cpp"
