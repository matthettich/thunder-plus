# thunder_core: the one C++ engine library (N0: a test core only; it is thrown away in N1).
add_library(thunder_core STATIC src/core.cpp src/rt/auto.cpp src/rt/jsmath.cpp src/rt/fdlibm.cpp src/rt/json.cpp src/song/song.cpp src/seq/seq.cpp src/seq/session.cpp src/voice/voice.cpp src/mix/mix.cpp src/mix/pdc.cpp src/mix/delay.cpp src/mix/beat.cpp src/master/master.cpp src/master/eq.cpp src/master/chain.cpp src/meter/meter.cpp src/fx/fxhost.cpp src/fx/clkcores.cpp src/fx/idmcores.cpp src/modfx/modfx.cpp src/modfx/mod_stereo.cpp src/modfx/mod_reverbs.cpp src/modfx/mod_cv.cpp src/modfx/mod_filters.cpp src/modfx/mod_delays.cpp src/modfx/mod_misc.cpp src/modfx/mod_clk.cpp src/modfx/mod_basic.cpp src/modfx/mod_voice.cpp src/modfx/mod_gen.cpp src/waves/wmods.cpp src/waves/engine.cpp src/waves/live.cpp src/chains/cmods.cpp src/chains/cmods2.cpp src/chains/cengine.cpp src/chains/clive.cpp src/modfx/bbexpr.cpp src/fx/fxmap.cpp src/fx/fxbank.cpp src/fx/fxpump.cpp src/render/render.cpp src/render/render_api.cpp src/fx/u17cores.cpp src/engine/engine.cpp src/engine/engine_json.cpp src/engine/pumpsynth.cpp)
target_include_directories(thunder_core PUBLIC include)
# U34a (verification D3, D5): a failed allocation is a std::bad_alloc that render_api.cpp catches (E_MEMORY). So that what a failed thunder_render_create has already built
# (the Song, the sequencer, the voices, the mixer's delay lines, the effects, the master chain) is freed on the way out, the files that make those objects are built WITH
# exceptions (their constructors get the cleanup an exception needs); nothing throws on the audio path and the audio code is the same. The rest of the core, and the wasm
# build (which has its own script and keeps -fno-exceptions), are not changed. MSVC has exceptions on already. RENDER-SF: the SoundFont pool (its constructor) and the font parser / decoder (add_font, set_pad_sf) too.
if(NOT MSVC)
  set_source_files_properties(
    src/render/render_api.cpp src/render/render.cpp src/rt/json.cpp src/song/song.cpp src/seq/seq.cpp src/seq/session.cpp src/voice/voice.cpp src/mix/mix.cpp src/mix/pdc.cpp src/mix/delay.cpp src/mix/beat.cpp
    src/master/master.cpp src/master/eq.cpp src/master/chain.cpp src/fx/fxhost.cpp src/fx/clkcores.cpp src/fx/idmcores.cpp src/fx/u17cores.cpp src/fx/fxmap.cpp src/fx/fxbank.cpp src/fx/fxpump.cpp
    src/sf/sf_live.cpp src/sf/sf_parse.cpp src/sf/sf_decode.cpp
    PROPERTIES COMPILE_OPTIONS "-fexceptions")
endif()
target_sources(thunder_core PRIVATE src/modfx/fftr.cpp src/modfx/mod_spectral.cpp src/modfx/mod_convolve.cpp)   # U20
target_sources(thunder_core PRIVATE src/engine/layer_u23.cpp src/engine/layer_click.cpp src/engine/layer_wavetable.cpp src/engine/layer_pd.cpp src/engine/layer_chip.cpp src/engine/layer_supersaw.cpp src/engine/layer_pwm.cpp src/engine/layer_sample.cpp src/engine/layer_vint.cpp src/engine/layer_grains.cpp)   # U23
target_sources(thunder_core PRIVATE src/engine/xeng_util.cpp src/engine/xeng_voice.cpp src/engine/xeng_tone.cpp src/engine/xeng_modal.cpp src/engine/xeng_swarm.cpp src/engine/xeng_drift.cpp src/engine/xeng_piano.cpp src/engine/xeng_ep.cpp src/engine/xeng_organ.cpp src/engine/xeng_breath.cpp src/engine/xeng_strings.cpp src/engine/xeng_fmdrum.cpp src/engine/xeng_wind.cpp src/engine/xeng_phase.cpp src/engine/xeng_partials.cpp src/engine/xeng_skin.cpp src/engine/xeng_chip.cpp)   # U28
target_sources(thunder_core PRIVATE src/engine/xeng_table.cpp src/engine/xeng_vector.cpp src/engine/xeng_scanned.cpp src/engine/xeng_resynth.cpp src/engine/xeng_bytebeat.cpp)   # U29
target_sources(thunder_core PRIVATE src/engine/xeng_live.cpp src/engine/xeng_blip.cpp)   # U27
target_sources(thunder_core PRIVATE src/engine/bass_core.cpp src/engine/vox_core.cpp src/engine/live_shell.cpp)   # U26
target_sources(thunder_core PRIVATE src/engine/layer_u24.cpp src/engine/layer_fm.cpp src/engine/layer_add.cpp src/engine/layer_karplus.cpp src/engine/layer_formant.cpp)   # U24: FM / additive / karplus / vosim / formant layers
# U32: the SoundFont / GM player (SF2 / SF3 parse, Vorbis decode through the pinned stb_vorbis, zones and modulators, the pad render and the live note)
add_subdirectory(sf)
target_sources(thunder_core PRIVATE src/sf/sf_math.cpp src/sf/sf_parse.cpp src/sf/sf_decode.cpp src/sf/sf_voice.cpp src/sf/sf_render.cpp src/sf/sf_live.cpp)
target_sources(thunder_core PRIVATE src/midi/midisched.cpp)  # U35: MIDI scheduling
target_link_libraries(thunder_core PUBLIC thunder_stb_vorbis)
target_sources(thunder_core PRIVATE src/lxr/lxr_seam.cpp)   # U04: the LXR seam (a registry of one pointer; no LXR code: the optional module is native/lxr)
target_include_directories(thunder_core PRIVATE src)
target_include_directories(thunder_core SYSTEM PRIVATE third_party)
target_compile_features(thunder_core PUBLIC cxx_std_17)
# No exceptions or RTTI on the audio path (WORKPLAN section 3, rule 2).
if(MSVC)
  target_compile_options(thunder_core PRIVATE /W4 /GR-)
else()
  target_compile_options(thunder_core PRIVATE -Wall -Wextra -fno-exceptions -fno-rtti)
  # U31: the patch-preparation boundary of the …chains engine catches std::bad_alloc (a patch that cannot be built is refused, the process goes on): this one file has exceptions. The throw comes from operator new
  # in the other (-fno-exceptions) code, which has unwind tables; a half-made patch is simply dropped (its memory leaks on that path, which is already the failure path).
  set_source_files_properties(src/chains/cengine.cpp PROPERTIES COMPILE_OPTIONS "-fexceptions")
endif()

# U02: the existing C++ DSP as static libraries (see dsp/CMakeLists.txt, third_party/PINS.md)
option(THUNDER_WITH_DSP "Build the existing C++ DSP (tfx, thunder-dsp, mods) from pinned upstreams (needs git + network at configure)" ON)
if(THUNDER_WITH_DSP)
  add_subdirectory(dsp)
endif()
# U14: the track-effect host links the tfx effects directly (make_* of dsp/tfx/tfx.h), no wasm
if(TARGET thunder_dsp_tfx)
  target_link_libraries(thunder_core PUBLIC thunder_dsp_tfx)
  target_include_directories(thunder_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../dsp/tfx)
  target_compile_definitions(thunder_core PUBLIC THUNDER_HAVE_TFX=1)
endif()
# U25: the Plaits / Elements layer types host the strict upstream build (thunder_dsp_pe, dsp/pe_host.h) directly. layer_pe.cpp is always built (without the library the two types are simply not supported).
target_sources(thunder_core PRIVATE src/engine/layer_pe.cpp)
if(TARGET thunder_dsp_pe)
  target_link_libraries(thunder_core PUBLIC thunder_dsp_pe)
  target_include_directories(thunder_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/dsp)
  target_compile_definitions(thunder_core PUBLIC THUNDER_ENGINE_PE=1)
endif()
# U21: the Random / Function generators (Marbles, Tides) host the mods C++ directly (make_marbles / make_tides of dsp/mods/mods.h), no wasm.
# OFF by default: not bit-exact with the wasm-derived golden (see docs/port/U21-NOTES.md), so they are not in the registry and not gated.
option(THUNDER_MODFX_MODS "Register the Marbles / Tides MODFX modules (hosted mods C++, built strict; gated at -100 dB against the JS wasm and by committed native hashes)" ON)
if(THUNDER_MODFX_MODS AND TARGET thunder_dsp_mods)
  target_sources(thunder_core PRIVATE src/modfx/mod_mods.cpp)
  target_link_libraries(thunder_core PUBLIC thunder_dsp_mods)
  target_include_directories(thunder_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../dsp/mods)
  target_compile_definitions(thunder_core PUBLIC THUNDER_MODFX_MODS=1)
endif()
# U03: the pump DSP (owner's repo matthettich/pump, pinned; plugins/machine (GPL) is never fetched or built; see pump/CMakeLists.txt)
option(THUNDER_WITH_PUMP "Build the pump effects and synths (thunder_pump, thunder_pump_synth) from the pinned matthettich/pump checkout (needs git + network at configure)" ON)
if(THUNDER_WITH_PUMP)
  add_subdirectory(pump)
endif()
# U18: the track-effect host calls pump's pump_* entry points directly (fx/fxpump.cpp), no wasm. (MSVC: pump is not built, so a pump slot is left out of the chain.)
if(TARGET thunder_pump)
  target_link_libraries(thunder_core PUBLIC thunder_pump thunder_pump_synth)   # U33: the synths' psyn_* (engine/pumpsynth.cpp)
  target_compile_definitions(thunder_core PUBLIC THUNDER_HAVE_PUMP=1)
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
