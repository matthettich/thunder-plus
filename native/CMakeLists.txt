cmake_minimum_required(VERSION 3.20)
project(thunder_native LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(THUNDER_BUILD_TESTS "Build and register the C++ tests" ON)
# H6: the desktop shell library's own tests (native/desktop/tests), so that a shell job can run them without building every core test. Follows THUNDER_BUILD_TESTS unless set.
option(THUNDER_BUILD_DESKTOP_TESTS "Build and register the desktop shell library's tests" ${THUNDER_BUILD_TESTS})
# The LXR drum-synth module (U04) is GPL-3.0-only: ON for the owner's own builds, OFF for a closed build (nothing of it is configured, compiled or linked; the core does not
# mention it: core/include/thunder/lxr_seam.h). See native/lxr/LICENSES.md. Not on MSVC (the firmware is GNU C): there the module is simply absent, as in a closed build.
option(THUNDER_WITH_LXR "Build the optional LXR module (GPL-3.0-only; ON for the owner's own builds, OFF for a closed build)" ON)

# Sanitizer run (CI's second Linux build): every target, tests included, under AddressSanitizer + UBSan, with UB fatal.
option(THUNDER_SANITIZE "Build with -fsanitize=address,undefined (GCC / clang only; off by default)" OFF)
if(THUNDER_SANITIZE)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    # shift-base is off: a left shift of a NEGATIVE value is undefined in C++17 only (defined in C++20, and arithmetic on GCC / clang); the
    # vendored DaisySP decimator does it. The shift-EXPONENT check (a shift by the type's width: review C1) stays on.
    add_compile_options(-fsanitize=address,undefined -fno-sanitize=shift-base -fno-sanitize-recover=undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined -fno-sanitize-recover=undefined)
  else()
    message(FATAL_ERROR "THUNDER_SANITIZE needs GCC or clang")
  endif()
endif()

# FX1 (render_api_oom on macOS arm64): clang's Darwin driver emits NO unwind tables for a file built -fno-exceptions (or for C) on arm64 (Darwin::IsUnwindTablesDefault: only x86_64 is
# exempt), so a std::bad_alloc thrown from operator new cannot pass through any frame of such a file (or a linker-picked copy of an inline function made there): the unwinder finds no
# entry and calls terminate. Linux / GCC / clang always emit .eh_frame. This only adds the metadata (no code change, no audio change); the -fno-exceptions / -fexceptions choices stay.
if(APPLE)
  add_compile_options(-funwind-tables)
endif()

add_subdirectory(core)
if(THUNDER_WITH_LXR AND NOT MSVC)
  add_subdirectory(lxr)
endif()
add_subdirectory(tools/thunder-render)   # U09: the offline song renderer

if(THUNDER_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
if(THUNDER_BUILD_DESKTOP_TESTS)
  enable_testing()
endif()
add_subdirectory(desktop)   # H6: the desktop shells (WebView2 / WKWebView): a platform-neutral library and its tests everywhere; the shell executables only with -DTHUNDER_DESKTOP_SHELL=ON
