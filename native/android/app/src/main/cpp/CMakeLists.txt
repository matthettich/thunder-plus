cmake_minimum_required(VERSION 3.22)
project(thunder_android LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# native/ is five folders up from here (cpp, main, src, app, android).
get_filename_component(NATIVE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../.." ABSOLUTE)
get_filename_component(REPO_ROOT "${NATIVE_ROOT}/.." ABSOLUTE)

add_subdirectory(${NATIVE_ROOT}/core ${CMAKE_BINARY_DIR}/thunder_core)

add_library(thunder_android SHARED jni.cpp midi_jni.cpp midi_out.cpp miniaudio_impl.c)
target_include_directories(thunder_android PRIVATE ${REPO_ROOT}/io/vendor/miniaudio ${NATIVE_ROOT}/core/src)   # core/src: midi_out.cpp drives the scheduler (midi/midi_host.h) and a debug sequencer (seq/)
# AAudio only; no decoding / encoding / generation code in the build. The same definitions for both files.
target_compile_definitions(thunder_android PRIVATE
  MA_NO_DECODING MA_NO_ENCODING MA_NO_GENERATION
  MA_ENABLE_ONLY_SPECIFIC_BACKENDS MA_ENABLE_AAUDIO)
target_link_libraries(thunder_android PRIVATE thunder_core log android dl amidi)   # amidi: AMidiDevice / AMidiInputPort (API 29)
