add_executable(test_core test_core.cpp)
target_link_libraries(test_core PRIVATE thunder_core)
add_test(NAME core_silence COMMAND test_core)
