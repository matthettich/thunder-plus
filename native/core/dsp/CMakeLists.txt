# U02: the existing C++ DSP (dsp/w_*.cc, dsp/tfx, dsp/mods) as static libraries, built from the same sources, upstreams and
# per-file flags as dsp/build.sh, dsp/tfx/build.sh and dsp/mods/build.sh. The sources in dsp/ are used by path (never copied).
#   thunder_dsp_tfx   (tfx.wasm)          thunder_dsp_w   (thunder-dsp.wasm)          thunder_dsp_mods   (mods.wasm)
# Upstreams: third_party/fetch.sh at the commits in third_party/PINS.md, patched on a copy by third_party/prepare.sh.
# With the wasi-sdk toolchain (native/tools/build-wasm.sh) each library is also linked into its .wasm module.

if(MSVC)
  message(STATUS "thunder_dsp: skipped on MSVC (the upstream sources need clang or gcc)")
  return()
endif()

# Same flags in every configuration (build.sh passes exactly -O3/-O2 -ffast-math ... and no -DNDEBUG, no -fPIC on wasm).
foreach(cfg DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
  set(CMAKE_CXX_FLAGS_${cfg} "")
endforeach()
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_EXTENSIONS OFF)
set(IS_WASM FALSE)
if(CMAKE_SYSTEM_NAME STREQUAL "WASI" OR CMAKE_CXX_COMPILER_TARGET MATCHES "wasm32")
  set(IS_WASM TRUE)
  set(CMAKE_POSITION_INDEPENDENT_CODE OFF)
endif()

set(TP ${CMAKE_CURRENT_SOURCE_DIR}/../third_party)
set(UP ${CMAKE_BINARY_DIR}/upstream)
set(DEPS ${THUNDER_DEPS_DIR})
if(NOT DEPS)
  set(DEPS ${TP}/_deps)
endif()
# The prepared copy is remade when prepare.sh or fetch.sh change (their hashes are stamped beside it): an old build folder would otherwise
# keep compiling the previous patches (U14 diagnosis: three patches were added there).
file(SHA256 ${TP}/prepare.sh prep_h1)
file(SHA256 ${TP}/fetch.sh prep_h2)
set(prep_stamp "")
if(EXISTS ${UP}/.prepared)
  file(READ ${UP}/.prepared prep_stamp)
endif()
if(NOT EXISTS ${UP}/eurorack/plaits OR NOT prep_stamp STREQUAL "${prep_h1} ${prep_h2}")
  message(STATUS "thunder_dsp: fetching pinned upstreams into ${DEPS}")
  execute_process(COMMAND sh ${TP}/fetch.sh ${DEPS} RESULT_VARIABLE rc)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "third_party/fetch.sh failed (needs git and network access to github.com)")
  endif()
  execute_process(COMMAND sh ${TP}/prepare.sh ${DEPS} ${UP} RESULT_VARIABLE rc)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "third_party/prepare.sh failed")
  endif()
  file(WRITE ${UP}/.prepared "${prep_h1} ${prep_h2}")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${TP}/prepare.sh ${TP}/fetch.sh)
set(E ${UP}/eurorack)
set(C ${UP}/cs)
set(AW ${UP}/aw)
set(D ${DEPS}/DaisySP)
set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/../../../dsp)   # the repo's dsp/

if(IS_WASM)
  set(OPT_COMMON -msimd128 -fno-exceptions -fno-rtti)
else()
  set(OPT_COMMON -fno-exceptions -fno-rtti)
endif()
# U14: Fx::operator new zero-fills the object ("every effect starts in zeroed memory": members the effects never initialise rely on it, e.g.
# DaisySP's Flanger::lfo_freq_). GCC deletes a store before the constructor runs (-flifetime-dse, the default), so natively that memset was
# dropped and an effect started with whatever malloc returned; wasi-sdk's clang keeps it. Host builds only; the wasm objects are unchanged.
# The flag exists only in GCC (clang and MSVC reject or ignore it). For every compiler, tests/test_fxzero.cpp proves the zero-fill took place:
# fx_alloc.h routes the effect sources' malloc through thunder_fx_malloc, which the test makes fill memory with a pattern.
if(NOT IS_WASM AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  set(OPT_COMMON ${OPT_COMMON} -fno-lifetime-dse)
endif()
set(OPT_FX  -O3 -ffast-math ${OPT_COMMON} -w)                                            # dsp/build.sh and tfx/build.sh
set(OPT_MOD -O2 -ffast-math -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti -w)  # mods/build.sh

# dsp_group(<lib> <name> OPTS <flags> DEFS <defs> INCS <dirs> PRE <headers to -include> SRCS <files>)
# One OBJECT library per source file, so the wasm link can list every object in a canonical, path-independent order:
# sorted by the source's path relative to dsp/ ("dsp/..."), DaisySP ("ds/...") or the prepared upstream tree ("up/...").
set(THUNDER_DSP_DIR ${CMAKE_CURRENT_SOURCE_DIR})
function(dsp_group lib name)
  cmake_parse_arguments(G "" "" "OPTS;DEFS;INCS;PRE;SRCS" ${ARGN})
  set(i 0)
  foreach(src ${G_SRCS})
    set(obj ${lib}_${name}_${i})
    math(EXPR i "${i}+1")
    add_library(${obj} OBJECT ${src})
    target_compile_options(${obj} PRIVATE ${G_OPTS})
    foreach(h ${G_PRE})
      target_compile_options(${obj} PRIVATE "SHELL:-include ${h}")
    endforeach()
    if(NOT IS_WASM AND lib STREQUAL "thunder_dsp_tfx")   # fx_alloc.h: after the other force-includes (it needs <stdlib.h> before it)
      target_compile_options(${obj} PRIVATE "SHELL:-include ${CMAKE_CURRENT_SOURCE_DIR}/fx_alloc.h")
    endif()
    target_compile_definitions(${obj} PRIVATE ${G_DEFS})
    target_include_directories(${obj} PRIVATE ${G_INCS})
    if(NOT IS_WASM)   # host_fx_state.h: the prepared stmlib/utils/random.h and DaisySP pitchshifter.h include it on host builds (prepare.sh)
      target_include_directories(${obj} PRIVATE ${THUNDER_DSP_DIR})
    endif()
    target_sources(${lib} PRIVATE $<TARGET_OBJECTS:${obj}>)
    get_filename_component(absrc ${src} ABSOLUTE)
    foreach(root "dsp|${SRC}" "up|${UP}" "ds|${D}")
      string(REPLACE "|" ";" rp ${root})
      list(GET rp 0 rk)
      list(GET rp 1 rd)
      get_filename_component(rd ${rd} ABSOLUTE)
      string(FIND "${absrc}" "${rd}/" pos)
      if(pos EQUAL 0)
        file(RELATIVE_PATH rel ${rd} ${absrc})
        set_property(GLOBAL APPEND PROPERTY ${lib}_OBJS "${rk}/${rel}|${obj}")
        break()
      endif()
    endforeach()
  endforeach()
endfunction()
function(dsp_lib lib)
  # an empty static library needs one source: a tiny shared translation unit
  add_library(${lib} STATIC ${CMAKE_CURRENT_SOURCE_DIR}/empty.cpp)
  target_include_directories(${lib} PUBLIC ${SRC}/tfx ${SRC}/mods)
endfunction()

set(MI_DEFS TEST)
set(CS_DEFS BUFFER_SIZE=128 MAX_STR_SIZE=32)
set(CS_OPTS "-Dstrcpy_s(a,b,c)=strncpy(a,c,b)")   # a function-like macro: target_compile_definitions would drop it
set(DS_INCS ${D}/Source ${D}/Source/Effects ${D}/Source/Utility)

# ---------------------------------------------------------------------------------------------- tfx (tfx/build.sh)
dsp_lib(thunder_dsp_tfx)
set(T thunder_dsp_tfx)
set(RAND_PRE)
set(RAND_SRC)
if(NOT IS_WASM)   # U14: wasi-libc's rand() (wasi_rand.cpp); the wasm build is unchanged
  set(RAND_PRE ${CMAKE_CURRENT_SOURCE_DIR}/wasi_rand.h)
  set(RAND_SRC ${CMAKE_CURRENT_SOURCE_DIR}/wasi_rand.cpp)
endif()
if(NOT IS_WASM)
  target_sources(thunder_dsp_tfx PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/fx_alloc.cpp)   # thunder_fx_malloc / thunder_fx_poison (fx_alloc.h)
endif()
dsp_group(${T} core OPTS ${OPT_FX} INCS ${SRC}/tfx SRCS ${SRC}/tfx/tfx.cc ${SRC}/tfx/fx_extra.cc ${RAND_SRC})
# (U14 diagnosis: the sanitizer build instruments Clouds again. The read past lut_window and the shift by 32 it found are patched in third_party/prepare.sh.)
dsp_group(${T} mi OPTS ${OPT_FX} DEFS ${MI_DEFS} PRE cstdio INCS ${SRC}/tfx ${E} SRCS ${SRC}/tfx/fx_mi.cc
  ${E}/rings/dsp/part.cc ${E}/rings/dsp/resonator.cc ${E}/rings/dsp/string.cc ${E}/rings/dsp/fm_voice.cc ${E}/rings/resources.cc
  ${E}/clouds/dsp/granular_processor.cc ${E}/clouds/dsp/correlator.cc ${E}/clouds/dsp/mu_law.cc ${E}/clouds/resources.cc
  ${E}/clouds/dsp/pvoc/frame_transformation.cc ${E}/clouds/dsp/pvoc/phase_vocoder.cc ${E}/clouds/dsp/pvoc/stft.cc
  ${E}/stmlib/dsp/atan.cc ${E}/stmlib/dsp/units.cc ${E}/stmlib/utils/random.cc)
dsp_group(${T} beads OPTS ${OPT_FX} DEFS ${MI_DEFS} PRE cstdio INCS ${SRC}/tfx ${E} SRCS ${SRC}/tfx/fx_beads.cc
  ${E}/beads/dsp/delay_clock.cc ${E}/beads/dsp/delay_controller.cc ${E}/beads/dsp/mu_law.cc
  ${E}/beads/dsp/processor.cc ${E}/beads/dsp/trigger_generator.cc ${E}/beads/resources.cc)
dsp_group(${T} ds OPTS ${OPT_FX} INCS ${SRC}/tfx ${DS_INCS} SRCS ${SRC}/tfx/fx_ds.cc
  ${D}/Source/Effects/chorus.cpp ${D}/Source/Effects/decimator.cpp)
# U14 diagnosis: host builds take Effects/pitchshifter.h from the prepared copy (its myrand() seed is the effect host's, not one per process:
# prepare.sh, host_fx_state.h); the wasm build takes DaisySP's own file, as dsp/tfx/build.sh does.
set(DS2_PRE_INCS)
if(NOT IS_WASM)
  set(DS2_PRE_INCS ${UP}/ds)
endif()
dsp_group(${T} ds2 OPTS ${OPT_FX} INCS ${SRC}/tfx ${DS2_PRE_INCS} ${DS_INCS} SRCS ${SRC}/tfx/fx_ds2.cc
  ${D}/Source/Effects/flanger.cpp ${D}/Source/Effects/phaser.cpp ${D}/Source/Effects/tremolo.cpp ${D}/Source/Effects/overdrive.cpp
  ${D}/Source/Effects/wavefolder.cpp ${D}/Source/Effects/autowah.cpp ${D}/Source/Filters/ladder.cpp
  ${D}/Source/Synthesis/oscillator.cpp ${D}/Source/Control/phasor.cpp)
dsp_group(${T} warps OPTS ${OPT_FX} DEFS ${MI_DEFS} PRE cstdio INCS ${SRC}/tfx ${E} SRCS ${SRC}/tfx/fx_warps.cc
  ${E}/warps/dsp/modulator.cc ${E}/warps/dsp/oscillator.cc ${E}/warps/dsp/vocoder.cc ${E}/warps/dsp/filter_bank.cc ${E}/warps/resources.cc)
# host libstdc++ does not pull <cstdlib>/<cmath> into std:: transitively as wasi-sdk's libc++ does: force them in on the host only
set(CS_PRE)
if(NOT IS_WASM)
  set(CS_PRE ${CMAKE_CURRENT_SOURCE_DIR}/host_std_shim.h)
endif()
dsp_group(${T} cloudseed OPTS ${OPT_FX} ${CS_OPTS} DEFS ${CS_DEFS} PRE ${CS_PRE} ${RAND_PRE} INCS ${SRC}/tfx ${C} SRCS ${SRC}/tfx/fx_cloudseed.cc
  ${C}/DSP/RandomBuffer.cpp ${C}/DSP/Biquad.cpp)
foreach(p Density ToTape6 EQ Pressure4 Galactic TapeDelay2 DeRez2 PurestDrive Air3 Verbity2 ToVinyl4 Dubly Cabs Tube2)
  dsp_group(${T} aw_${p} OPTS ${OPT_FX} PRE stdlib.h ${RAND_PRE} INCS ${SRC}/tfx ${SRC}/stub ${AW}
    SRCS ${SRC}/tfx/aw_${p}.cc ${AW}/${p}/${p}.cpp ${AW}/${p}/${p}Proc.cpp)
endforeach()

# ------------------------------------------------------------------------------------------ mods (mods/build.sh)
dsp_lib(thunder_dsp_mods)
set(T thunder_dsp_mods)
dsp_group(${T} core OPTS ${OPT_MOD} DEFS ${MI_DEFS} PRE cstdio INCS ${SRC}/mods ${E} SRCS ${SRC}/mods/mods.cc ${E}/stmlib/dsp/units.cc)
dsp_group(${T} marbles OPTS ${OPT_MOD} DEFS ${MI_DEFS} PRE cstdio INCS ${SRC}/mods ${E} SRCS ${SRC}/mods/mod_marbles.cc
  ${E}/marbles/random/t_generator.cc ${E}/marbles/random/x_y_generator.cc ${E}/marbles/random/output_channel.cc
  ${E}/marbles/random/quantizer.cc ${E}/marbles/random/lag_processor.cc ${E}/marbles/ramp/ramp_extractor.cc ${E}/marbles/resources.cc)
dsp_group(${T} tides OPTS ${OPT_MOD} DEFS ${MI_DEFS} PRE cstdio INCS ${SRC}/mods ${E} SRCS ${SRC}/mods/mod_tides.cc
  ${E}/tides2/poly_slope_generator.cc ${E}/tides2/ramp/ramp_extractor.cc ${E}/tides2/resources.cc)

# ------------------------------------------------------------------------------------ thunder-dsp (dsp/build.sh)
dsp_lib(thunder_dsp_w)
set(T thunder_dsp_w)
set(PLAITS_NAMES algorithms additive_engine bass_drum_engine chiptune_engine chord_bank chord_engine dx_units fm_engine grain_engine
  hi_hat_engine lpc_speech_synth lpc_speech_synth_controller lpc_speech_synth_phonemes lpc_speech_synth_words modal_engine modal_voice
  naive_speech_synth noise_engine particle_engine phase_distortion_engine resonator sam_speech_synth six_op_engine snare_drum_engine
  speech_engine string string_engine string_machine_engine string_voice swarm_engine virtual_analog_engine virtual_analog_vcf_engine
  voice waveshaping_engine wavetable_engine wave_terrain_engine)
set(PLAITS_SRCS)
foreach(n ${PLAITS_NAMES})   # build.sh: find $E/plaits -name n.cc | head -1
  file(GLOB_RECURSE found ${E}/plaits/${n}.cc)
  list(SORT found)
  list(GET found 0 first)
  list(APPEND PLAITS_SRCS ${first})
endforeach()
dsp_group(${T} plaits OPTS ${OPT_FX} DEFS ${MI_DEFS} PRE cstdio INCS ${E} SRCS ${SRC}/w_plaits.cc ${PLAITS_SRCS}
  ${E}/stmlib/dsp/units.cc ${E}/stmlib/utils/random.cc ${E}/plaits/resources.cc)
dsp_group(${T} rings OPTS ${OPT_FX} DEFS ${MI_DEFS} PRE cstdio INCS ${E} SRCS ${SRC}/w_rings.cc
  ${E}/rings/dsp/part.cc ${E}/rings/dsp/resonator.cc ${E}/rings/dsp/string.cc ${E}/rings/dsp/fm_voice.cc ${E}/rings/resources.cc)
dsp_group(${T} clouds OPTS ${OPT_FX} DEFS ${MI_DEFS} PRE cstdio INCS ${E} SRCS ${SRC}/w_clouds.cc
  ${E}/clouds/dsp/granular_processor.cc ${E}/clouds/dsp/correlator.cc ${E}/clouds/dsp/mu_law.cc ${E}/clouds/resources.cc
  ${E}/clouds/dsp/pvoc/frame_transformation.cc ${E}/clouds/dsp/pvoc/phase_vocoder.cc ${E}/clouds/dsp/pvoc/stft.cc ${E}/stmlib/dsp/atan.cc)
dsp_group(${T} elements OPTS ${OPT_FX} DEFS ${MI_DEFS} PRE cstdio INCS ${E} SRCS ${SRC}/w_elements.cc
  ${E}/elements/dsp/exciter.cc ${E}/elements/dsp/multistage_envelope.cc ${E}/elements/dsp/ominous_voice.cc ${E}/elements/dsp/part.cc
  ${E}/elements/dsp/resonator.cc ${E}/elements/dsp/string.cc ${E}/elements/dsp/tube.cc ${E}/elements/dsp/voice.cc ${E}/elements/resources.cc)
dsp_group(${T} fx OPTS ${OPT_FX} PRE stdlib.h INCS ${SRC}/stub ${AW} ${DS_INCS} SRCS ${SRC}/w_fx.cc ${SRC}/w_density.cc ${SRC}/w_totape.cc
  ${AW}/Density/Density.cpp ${AW}/Density/DensityProc.cpp ${AW}/ToTape6/ToTape6.cpp ${AW}/ToTape6/ToTape6Proc.cpp
  ${D}/Source/Effects/chorus.cpp ${D}/Source/Effects/decimator.cpp)

# ----------------------------------------------------------------------------- wasm modules (wasi-sdk toolchain only)
if(IS_WASM)
  function(dsp_wasm exe lib out)
    cmake_parse_arguments(W "" "" "LINK" ${ARGN})
    add_executable(${exe} ${CMAKE_CURRENT_SOURCE_DIR}/empty.cpp)
    # link the objects directly (as build.sh does: an archive would drop everything the empty main does not reference), in the
    # canonical order: sorted by the keys above, so the bytes do not depend on the checkout path, the locale or the machine.
    get_property(objs GLOBAL PROPERTY ${lib}_OBJS)
    list(SORT objs COMPARE STRING)
    foreach(o ${objs})
      string(REGEX REPLACE "^.*[|]" "" g "${o}")
      target_sources(${exe} PRIVATE $<TARGET_OBJECTS:${g}>)
    endforeach()
    set_target_properties(${exe} PROPERTIES OUTPUT_NAME ${out} SUFFIX ".wasm")
    target_link_options(${exe} PRIVATE -mexec-model=reactor -Wl,--export=_initialize ${W_LINK} -Wl,--strip-all)
  endfunction()
  dsp_wasm(thunder_wasm_tfx thunder_dsp_tfx tfx LINK -Wl,--export=malloc -Wl,-z,stack-size=262144)
  dsp_wasm(thunder_wasm_w thunder_dsp_w thunder-dsp)
  dsp_wasm(thunder_wasm_mods thunder_dsp_mods mods LINK -Wl,--gc-sections -Wl,-z,stack-size=65536)
endif()
