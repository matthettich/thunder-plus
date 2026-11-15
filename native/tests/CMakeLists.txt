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
add_test(NAME rt_core COMMAND rt_harness)
add_test(NAME rt_guard_selftest COMMAND rt_harness --selftest)

# U05: the core runtime (Auto, the seeded random stream and js_exp against the JS's own values; the callback contract).
add_executable(test_runtime test_runtime.cpp)
target_link_libraries(test_runtime PRIVATE thunder_core)
target_include_directories(test_runtime PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../core/src ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME runtime COMMAND test_runtime)
if(NODE_EXE)
  add_test(NAME runtime_golden_current COMMAND ${NODE_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/../tools/runtime-golden.mjs --check)
endif()
