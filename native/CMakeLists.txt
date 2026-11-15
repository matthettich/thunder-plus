cmake_minimum_required(VERSION 3.20)
project(thunder_native LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(THUNDER_BUILD_TESTS "Build and register the C++ tests" ON)
option(THUNDER_WITH_LXR "Build the optional LXR module (GPL-3.0-only; off by default, not part of N0)" OFF)

add_subdirectory(core)

if(THUNDER_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
