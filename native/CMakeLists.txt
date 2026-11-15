cmake_minimum_required(VERSION 3.20)
project(thunder_native LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(THUNDER_BUILD_TESTS "Build and register the C++ tests" ON)
option(THUNDER_WITH_LXR "Build the optional LXR module (GPL-3.0-only; off by default, not part of N0)" OFF)

# Sanitizer run (CI's second Linux build): every target, tests included, under AddressSanitizer + UBSan, with UB fatal.
option(THUNDER_SANITIZE "Build with -fsanitize=address,undefined (GCC / clang only; off by default)" OFF)
if(THUNDER_SANITIZE)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined -fno-sanitize-recover=undefined)
  else()
    message(FATAL_ERROR "THUNDER_SANITIZE needs GCC or clang")
  endif()
endif()

add_subdirectory(core)
add_subdirectory(tools/thunder-render)   # U09: the offline song renderer

if(THUNDER_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
