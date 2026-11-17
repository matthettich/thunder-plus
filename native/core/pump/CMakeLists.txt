# U03: the pump DSP (the owner's own repo matthettich/pump, pinned in third_party/fetch.sh and PINS.md) as static libraries, built from the
# same sources and the numerics-relevant flags as pump/wasm/build.sh:
#   thunder_pump        what wasm/pump_wasm.cpp exposes (pump_*: 25 effect types; wasm/fx/<name>.cpp for the 16 creative ones)
#   thunder_pump_synth  what wasm/pump_synth.cpp exposes (psyn_*: 13 instruments)
#   thunder_pump_pffft  the vendored pffft both use (BSD-style licence)
# With the wasi-sdk toolchain (native/tools/build-wasm.sh) each is also linked into pump.wasm / pump-synth.wasm.
# LICENCE: nothing under pump's plugins/machine/ (GPL-3.0-only LXR firmware) is compiled here: the fetch leaves that folder out of the
# checkout, and wasm/build.sh does not use it (third_party/PINS.md, "pump"). The pump sources are not copied into this repo.

if(MSVC)
  message(STATUS "thunder_pump: skipped on MSVC (pump's wasm build needs clang or gcc; as for the other DSP, U02)")
  return()
endif()

foreach(cfg DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)   # same flags in every configuration (build.sh: -O3 -ffast-math ... -DNDEBUG)
  set(CMAKE_CXX_FLAGS_${cfg} "")
  set(CMAKE_C_FLAGS_${cfg} "")
endforeach()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(IS_WASM FALSE)
if(CMAKE_SYSTEM_NAME STREQUAL "WASI" OR CMAKE_CXX_COMPILER_TARGET MATCHES "wasm32")
  set(IS_WASM TRUE)
  set(CMAKE_POSITION_INDEPENDENT_CODE OFF)
endif()

set(TP ${CMAKE_CURRENT_SOURCE_DIR}/../third_party)
set(DEPS ${THUNDER_DEPS_DIR})
if(NOT DEPS)
  set(DEPS ${TP}/_deps)
endif()
set(P ${DEPS}/pump)
set(THUNDER_PUMP_ORIG ${P})
if(NOT EXISTS ${P}/wasm/pump_wasm.cpp)
  message(STATUS "thunder_pump: fetching the pinned pump checkout into ${DEPS}")
  execute_process(COMMAND sh ${TP}/fetch.sh ${DEPS} RESULT_VARIABLE rc)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "third_party/fetch.sh failed (needs git and network access to github.com)")
  endif()
endif()
if(EXISTS ${P}/plugins/machine)
  message(FATAL_ERROR "pump checkout contains plugins/machine (GPL-3.0-only): refusing to build; re-run third_party/fetch.sh into a clean folder")
endif()

# U33: HOST builds compile a patched COPY of the pin (third_party/prepare-pump.sh: reads outside a table found by AddressSanitizer in the synth fuzz; each one is commented there and reported to the owner).
# The wasm builds use the pin as it is (the shipped pump.wasm / pump-synth.wasm are byte-identical to it). The copy is remade when the script changes.
if(NOT IS_WASM)
  file(SHA256 ${TP}/prepare-pump.sh pump_prep_h)
  set(pump_prep_stamp "")
  set(PP ${CMAKE_BINARY_DIR}/pump_prepared)
  if(EXISTS ${PP}/.prepared)
    file(READ ${PP}/.prepared pump_prep_stamp)
  endif()
  if(NOT EXISTS ${PP}/wasm/pump_synth.cpp OR NOT pump_prep_stamp STREQUAL "${pump_prep_h} ${P}")
    execute_process(COMMAND sh ${TP}/prepare-pump.sh ${P} ${PP} RESULT_VARIABLE rc)
    if(NOT rc EQUAL 0)
      message(FATAL_ERROR "third_party/prepare-pump.sh failed")
    endif()
    file(WRITE ${PP}/.prepared "${pump_prep_h} ${P}")
  endif()
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${TP}/prepare-pump.sh)
  set(P ${PP})
endif()

# build.sh: FLAGS="... -O3 -ffast-math -fno-finite-math-only -msimd128 -DPUMP_SYNC_KERNELS=1 -DPFFFT_STATIC_DEFINE -DNDEBUG" (SIMD only on wasm)
set(OPT -O3 -ffast-math -fno-finite-math-only -w)
# U18 (the lead, 15 Nov): host builds compile pump WITHOUT -ffast-math, as the track effects (core/dsp/CMakeLists.txt, OPT_TFX; the owner agreed to this
# for effects whose samples depend on the compiler). With it VOX missed the gate against pump.wasm on every compiler (-71.8 / -75.1 dB, arithmetic spread:
# the same bits whatever ran before, valgrind clean). Strict: VOX is within -80 dB with GCC 13 and clang 20 and the two give the same failing list.
# The wasm keeps build.sh's flags.
if(NOT IS_WASM)
  set(OPT -O3 -fno-fast-math -ffp-contract=off -w)
endif()
if(IS_WASM)
  list(APPEND OPT -msimd128)
endif()
# x86 host builds: no MMX registers. GCC 13 at -O3 builds a two-quadword zero in pump::spring::SpringTank::process through %mm0 (movq (%rsp),%mm0;
# movq2dq) and, as compilers do, never runs EMMS, which leaves all eight x87 registers tagged in use. STUTTER's fmod is inlined as x87 fprem under
# -ffast-math, its fld then overflows the x87 stack and the result is NaN: "STUTTER puts out NaN at its first sample after a SPRING ran in the
# same process" (found in U03, the cause found 16 Nov with FE_INVALID trapped: gdb shows R0 to R7 tagged Zero at the faulting fld). GCC 15 and
# clang do not emit it; wasm and ARM have no x87. tests/dsp runs all 25 types in one process to keep it found.
if(NOT IS_WASM AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64|i[3-6]86)$")
  list(APPEND OPT -mno-mmx)
endif()
set(DEFS PUMP_SYNC_KERNELS=1 PFFFT_STATIC_DEFINE NDEBUG)
set(OPT_CXX ${OPT} -fno-exceptions -fno-rtti)

function(pump_obj name src)
  cmake_parse_arguments(O "C" "" "INCS" ${ARGN})
  add_library(${name} OBJECT ${src})
  if(O_C)
    target_compile_options(${name} PRIVATE ${OPT})
  else()
    target_compile_options(${name} PRIVATE ${OPT_CXX})
  endif()
  target_compile_definitions(${name} PRIVATE ${DEFS})
  target_include_directories(${name} PRIVATE ${O_INCS})
endfunction()

pump_obj(pump_o_pffft ${P}/common/pump_pffft.c C INCS ${P}/common)
pump_obj(pump_o_effects ${P}/wasm/pump_wasm.cpp INCS ${P}/common)
pump_obj(pump_o_synth ${P}/wasm/pump_synth.cpp INCS ${P}/common)
set(FX_NAMES tube snap gate echo spring verb grain vox tune crush swirl stutter chop twang master strip)   # build.sh's order
set(FX_OBJS)
foreach(p ${FX_NAMES})   # each plugin's own Plugin<Name>.hpp on the DPF stand-in: wasm/shim before common, the plugin's folder for DistrhoPluginInfo.h
  pump_obj(pump_o_fx_${p} ${P}/wasm/fx/${p}.cpp INCS ${P}/wasm/shim ${P}/plugins/${p} ${P}/common)
  list(APPEND FX_OBJS $<TARGET_OBJECTS:pump_o_fx_${p}>)
endforeach()

add_library(thunder_pump_pffft STATIC $<TARGET_OBJECTS:pump_o_pffft>)
add_library(thunder_pump STATIC $<TARGET_OBJECTS:pump_o_effects> ${FX_OBJS})
target_link_libraries(thunder_pump PUBLIC thunder_pump_pffft)
add_library(thunder_pump_synth STATIC $<TARGET_OBJECTS:pump_o_synth>)
target_link_libraries(thunder_pump_synth PUBLIC thunder_pump_pffft)
set_target_properties(thunder_pump_pffft thunder_pump thunder_pump_synth PROPERTIES LINKER_LANGUAGE CXX)

set(THUNDER_PUMP_DIR ${THUNDER_PUMP_ORIG} CACHE INTERNAL "the pinned pump checkout (the original, not the patched copy host builds compile)")

if(IS_WASM)
  # build.sh: link "$O/pump.o" $FXO "$O/pffft.o" in that order (objects, not archives: an archive would drop what nothing references)
  function(pump_wasm exe out exports)
    cmake_parse_arguments(W "" "" "OBJS" ${ARGN})
    add_executable(${exe} ${W_OBJS})
    set_target_properties(${exe} PROPERTIES OUTPUT_NAME ${out} SUFFIX ".wasm" LINKER_LANGUAGE CXX)
    set(opts -mexec-model=reactor -Wl,--export=_initialize)
    foreach(f ${exports})
      list(APPEND opts -Wl,--export=${f})
    endforeach()
    target_link_options(${exe} PRIVATE ${opts} -Wl,--strip-all -Wl,-z,stack-size=1048576 -Wl,--initial-memory=16777216)
  endfunction()
  set(PUMP_EXPORTS pump_types pump_nparams pump_stage pump_new pump_free pump_values pump_commit pump_io pump_process pump_latency
    pump_tel pump_tel_len pump_rings pump_ring_pos pump_ring_cap pump_ring_read pump_hold pump_midi pump_transport pump_text
    pump_state_get pump_state_set)
  set(PSYN_EXPORTS psyn_types psyn_nparams psyn_stage psyn_new psyn_free psyn_values psyn_commit psyn_midi psyn_out psyn_process
    psyn_latency psyn_tempo psyn_buf psyn_file)
  pump_wasm(thunder_wasm_pump pump "${PUMP_EXPORTS}" OBJS $<TARGET_OBJECTS:pump_o_effects> ${FX_OBJS} $<TARGET_OBJECTS:pump_o_pffft>)
  pump_wasm(thunder_wasm_pump_synth pump-synth "${PSYN_EXPORTS}" OBJS $<TARGET_OBJECTS:pump_o_synth> $<TARGET_OBJECTS:pump_o_pffft>)
endif()
