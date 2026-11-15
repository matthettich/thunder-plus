# thunder_core: the one C++ engine library (N0: a test core only; it is thrown away in N1).
add_library(thunder_core STATIC src/core.cpp)
target_include_directories(thunder_core PUBLIC include)
target_compile_features(thunder_core PUBLIC cxx_std_17)
# No exceptions or RTTI on the audio path (WORKPLAN section 3, rule 2).
if(MSVC)
  target_compile_options(thunder_core PRIVATE /W4 /GR-)
else()
  target_compile_options(thunder_core PRIVATE -Wall -Wextra -fno-exceptions -fno-rtti)
endif()

# U02: the existing C++ DSP as static libraries (see dsp/CMakeLists.txt, third_party/PINS.md)
option(THUNDER_WITH_DSP "Build the existing C++ DSP (tfx, thunder-dsp, mods) from pinned upstreams (needs git + network at configure)" ON)
if(THUNDER_WITH_DSP)
  add_subdirectory(dsp)
endif()
