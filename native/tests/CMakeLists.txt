add_executable(test_core test_core.cpp)
target_link_libraries(test_core PRIVATE thunder_core)
add_test(NAME core_pattern COMMAND test_core)

# A7: the census tool's unit test (needs Node; skipped when Node is not installed).
find_program(NODE_EXE node)
if(NODE_EXE)
  add_test(NAME census COMMAND ${NODE_EXE} --test ${CMAKE_CURRENT_SOURCE_DIR}/census.test.mjs)
endif()

add_executable(bench_put bench_put.cpp)
target_link_libraries(bench_put PRIVATE thunder_core)
add_test(NAME bench_put COMMAND bench_put)

# A5: the real-time harness. rt_core: the core's callback under an allocator that aborts on allocation, plus the clock-drift
# logger's checks. rt_guard_selftest: proves the allocator really kills a callback that allocates.
add_executable(rt_harness rt_harness.cpp)
target_link_libraries(rt_harness PRIVATE thunder_core)
target_include_directories(rt_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../core/src)   # U07: the sequencer's step() runs under the guard too
target_compile_definitions(rt_harness PRIVATE THUNDER_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../..")
add_test(NAME rt_core COMMAND rt_harness)
add_test(NAME rt_guard_selftest COMMAND rt_harness --selftest)

# U02: smoke tests for the existing C++ DSP libraries (native/core/dsp)
add_subdirectory(dsp)
# U05: the core runtime (Auto, the seeded random stream and js_exp against the JS's own values; the callback contract).
add_executable(test_runtime test_runtime.cpp)
target_link_libraries(test_runtime PRIVATE thunder_core)
target_include_directories(test_runtime PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../core/src ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME runtime COMMAND test_runtime)
# The golden is made by Node on Linux x86-64 (the reference); the same script gave a different file on the macOS and Windows
# CI runners (15 Nov; not yet looked at which lines: line endings on Windows, and V8's own exp may differ in the last bit where
# V8 is built with fused multiply-add). The C++ equals the committed golden on every platform (ctest `runtime`), which is the
# point; whether the committed golden is current is checked where it is made.
if(NODE_EXE AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_test(NAME runtime_golden_current COMMAND ${NODE_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/../tools/runtime-golden.mjs --check)
endif()

# U06: the JSON reader / writer and the song reader, against sounds/ELECTRO.thunder and the JS's own JSON round trip.
add_executable(test_song test_song.cpp)
target_link_libraries(test_song PRIVATE thunder_core)
target_include_directories(test_song PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../core/src ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(test_song PRIVATE THUNDER_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../..")
add_test(NAME song COMMAND test_song)
if(NODE_EXE AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_test(NAME song_golden_current COMMAND ${NODE_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/../tools/song-golden.mjs --check)
endif()

# U07: the sequencer against the JS's four goldens (tests/golden/seq-electro*.json): every row hash and the overall sha256.
add_executable(test_seq test_seq.cpp)
target_link_libraries(test_seq PRIVATE thunder_core)
target_include_directories(test_seq PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../core/src ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(test_seq PRIVATE THUNDER_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../..")
if(NOT MSVC)
  target_compile_options(test_seq PRIVATE -Wall -Wextra)
endif()
add_test(NAME seq COMMAND test_seq)
# The JS run fresh beside the C++ (Linux, where Node is installed): seq_dump_current proves the browser-less tool still gives the
# committed goldens; seq_extra plays 19 cases the goldens do not reach (every command, PERFORM scenes / macros / locks / LFOs /
# parts, sect and phrase modes, loops, JMP / END, the wrap, a hand-over) in both and compares every row, nested members included.
if(NODE_EXE AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_test(NAME seq_dump_current COMMAND ${NODE_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/../tools/seq-dump.mjs ${CMAKE_CURRENT_BINARY_DIR}/seq-dump)
  add_test(NAME seq_extra COMMAND ${NODE_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/../tools/seq-extra.mjs ${CMAKE_CURRENT_BINARY_DIR}/seq-extra $<TARGET_FILE:test_seq>)
endif()

# U08: the sampler voice player against the JS (tools/voice-dump.mjs runs ThunderCore in a Node vm). With Node: the JS output is made
# fresh (fixture voice_ref) and compared, every case at -80 dB or better (the figure is printed; most are exact). Without Node the
# test only lists the cases (the committed hashes, tests/golden/voice_golden.inc, are of the JS output and are checked by voice_golden_current).
add_executable(test_voice test_voice.cpp)
target_link_libraries(test_voice PRIVATE thunder_core)
target_include_directories(test_voice PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../core/src ${CMAKE_CURRENT_SOURCE_DIR})
if(NOT MSVC)
  target_compile_options(test_voice PRIVATE -Wall -Wextra)
endif()
if(NODE_EXE)
  add_test(NAME voice_ref COMMAND ${NODE_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/../tools/voice-dump.mjs ${CMAKE_CURRENT_BINARY_DIR}/voice-ref)
  set_tests_properties(voice_ref PROPERTIES FIXTURES_SETUP voice_ref)
  add_test(NAME voice COMMAND test_voice ${CMAKE_CURRENT_BINARY_DIR}/voice-ref)
  set_tests_properties(voice PROPERTIES FIXTURES_REQUIRED voice_ref)
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_test(NAME voice_golden_current COMMAND ${NODE_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/../tools/voice-dump.mjs --check)
  endif()
else()
  add_test(NAME voice COMMAND test_voice)
endif()

# U09: the mixer against reference A (tools/mix-dump.mjs: a JS model of the page's Web Audio strip, itself checked against real Web
# Audio in Chrome by tools/mix-webaudio-check.mjs) and busPlan / chainsFeed / audible against the JS on hand-made patches and sounds/.
add_executable(test_mix test_mix.cpp)
target_link_libraries(test_mix PRIVATE thunder_core)
target_include_directories(test_mix PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../core/src ${CMAKE_CURRENT_SOURCE_DIR})
add_executable(test_busplan test_busplan.cpp)
target_link_libraries(test_busplan PRIVATE thunder_core)
target_include_directories(test_busplan PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../core/src ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(test_busplan PRIVATE THUNDER_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../..")
if(NOT MSVC)
  target_compile_options(test_mix PRIVATE -Wall -Wextra)
  target_compile_options(test_busplan PRIVATE -Wall -Wextra)
endif()
if(NODE_EXE)
  add_test(NAME mix_ref COMMAND ${NODE_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/../tools/mix-dump.mjs ${CMAKE_CURRENT_BINARY_DIR}/mix-ref)
  set_tests_properties(mix_ref PROPERTIES FIXTURES_SETUP mix_ref)
  add_test(NAME mix COMMAND test_mix ${CMAKE_CURRENT_BINARY_DIR}/mix-ref)
  set_tests_properties(mix PROPERTIES FIXTURES_REQUIRED mix_ref)
  add_test(NAME busplan_ref COMMAND ${NODE_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/../tools/mix-dump.mjs --busplan ${CMAKE_CURRENT_BINARY_DIR}/busplan-ref)
  set_tests_properties(busplan_ref PROPERTIES FIXTURES_SETUP busplan_ref)
  add_test(NAME busplan COMMAND test_busplan ${CMAKE_CURRENT_BINARY_DIR}/busplan-ref/busplan.json)
  set_tests_properties(busplan PROPERTIES FIXTURES_REQUIRED busplan_ref)
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_test(NAME mix_golden_current COMMAND ${NODE_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/../tools/mix-dump.mjs --check)
  endif()
else()
  add_test(NAME mix COMMAND test_mix)
  add_test(NAME busplan COMMAND test_busplan)
endif()

# U10: latency compensation. pdc_plan against the web app's own pdcPlan (tools/pdc-dump.mjs, a Node vm, hand-made songs and sounds/).
add_executable(test_pdc test_pdc.cpp)
target_link_libraries(test_pdc PRIVATE thunder_core)
target_include_directories(test_pdc PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../core/src ${CMAKE_CURRENT_SOURCE_DIR})
if(NOT MSVC)
  target_compile_options(test_pdc PRIVATE -Wall -Wextra)
endif()
if(NODE_EXE)
  add_test(NAME pdc_ref COMMAND ${NODE_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/../tools/pdc-dump.mjs ${CMAKE_CURRENT_BINARY_DIR}/pdc-ref)
  set_tests_properties(pdc_ref PROPERTIES FIXTURES_SETUP pdc_ref)
  add_test(NAME pdc COMMAND test_pdc ${CMAKE_CURRENT_BINARY_DIR}/pdc-ref/pdc.json)
  set_tests_properties(pdc PROPERTIES FIXTURES_REQUIRED pdc_ref)
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_test(NAME pdc_golden_current COMMAND ${NODE_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/../tools/pdc-dump.mjs --check)
  endif()
else()
  add_test(NAME pdc COMMAND test_pdc)
endif()
