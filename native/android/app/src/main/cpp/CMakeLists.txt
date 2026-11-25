cmake_minimum_required(VERSION 3.22)
project(thunder_android LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# native/ is five folders up from here (cpp, main, src, app, android).
get_filename_component(NATIVE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../.." ABSOLUTE)
get_filename_component(REPO_ROOT "${NATIVE_ROOT}/.." ABSOLUTE)

add_subdirectory(${NATIVE_ROOT}/core ${CMAKE_BINARY_DIR}/thunder_core)
# N2 (lead's ruling 3, 24 Nov): the optional LXR drum module (GPL-3.0-only, native/lxr) is ON by default in this app, through the same option as the desktop build, so that it stays removable
# (-PthunderWithLxr=false in Gradle or -DTHUNDER_WITH_LXR=OFF here; docs/port/IF-SOLD.md). Nothing of it is configured, compiled or linked when OFF. The core never names it (lxr_seam.h);
# this app registers it at start (live_jni.cpp, #ifdef THUNDER_HAVE_LXR, which the module's target defines).
option(THUNDER_WITH_LXR "Build the optional LXR module (GPL-3.0-only; ON for the owner's own builds, OFF for a closed build)" ON)
if(THUNDER_WITH_LXR)
  add_subdirectory(${NATIVE_ROOT}/lxr ${CMAKE_BINARY_DIR}/thunder_lxr)
endif()

add_library(thunder_android SHARED jni.cpp midi_jni.cpp midi_out.cpp midi_in.cpp live_jni.cpp live_engine.cpp live_events.cpp pad_filler.cpp songfit.cpp miniaudio_impl.c)
target_include_directories(thunder_android PRIVATE ${REPO_ROOT}/io/vendor/miniaudio ${NATIVE_ROOT}/core/src)   # core/src: midi_out.cpp drives the scheduler (midi/midi_host.h) and a debug sequencer (seq/)
# AAudio only; no decoding / encoding / generation code in the build. The same definitions for both files.
target_compile_definitions(thunder_android PRIVATE
  MA_NO_DECODING MA_NO_ENCODING MA_NO_GENERATION
  MA_ENABLE_ONLY_SPECIFIC_BACKENDS MA_ENABLE_AAUDIO)
target_link_libraries(thunder_android PRIVATE thunder_core log android dl amidi)
if(TARGET thunder_lxr)
  target_link_libraries(thunder_android PRIVATE thunder_lxr)
endif()   # amidi: AMidiDevice / AMidiInputPort (API 29)
