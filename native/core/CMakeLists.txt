# thunder_core: the one C++ engine library (N0: a test core only; it is thrown away in N1).
add_library(thunder_core STATIC src/core.cpp src/rt/auto.cpp src/rt/jsmath.cpp src/rt/json.cpp src/song/song.cpp src/seq/seq.cpp src/voice/voice.cpp src/mix/mix.cpp src/mix/pdc.cpp src/mix/delay.cpp src/mix/beat.cpp src/master/master.cpp src/master/eq.cpp src/master/chain.cpp src/meter/meter.cpp src/render/render.cpp)
target_include_directories(thunder_core PUBLIC include)
target_include_directories(thunder_core PRIVATE src)
target_include_directories(thunder_core SYSTEM PRIVATE third_party)
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
# U03: the pump DSP (owner's repo matthettich/pump, pinned; plugins/machine (GPL) is never fetched or built; see pump/CMakeLists.txt)
option(THUNDER_WITH_PUMP "Build the pump effects and synths (thunder_pump, thunder_pump_synth) from the pinned matthettich/pump checkout (needs git + network at configure)" ON)
if(THUNDER_WITH_PUMP)
  add_subdirectory(pump)
endif()
# SAME BITS AS THE WEB COPY (U05): no fused multiply-add. JavaScript never fuses a * b + c; GCC and clang do by default
# where the CPU has the instruction (every arm64 phone and Mac), which changes the last bit of sums like the automation
# ramps and the sequencer's times, and the goldens are compared exactly. PUBLIC so the tests' own reference code is built
# the same way. MSVC: /fp:precise does not contract.
if(MSVC)
  target_compile_options(thunder_core PUBLIC /fp:precise)
else()
  target_compile_options(thunder_core PUBLIC -ffp-contract=off)
endif()
