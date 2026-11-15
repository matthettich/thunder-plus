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
