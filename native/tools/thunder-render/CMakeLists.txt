# U09: the offline renderer's command line (sequencer -> voices -> mixer -> a float32 WAV). See main.cpp.
add_executable(thunder-render main.cpp)
target_link_libraries(thunder-render PRIVATE thunder_core)
target_include_directories(thunder-render PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../core/src)
if(NOT MSVC)
  target_compile_options(thunder-render PRIVATE -Wall -Wextra)
endif()
