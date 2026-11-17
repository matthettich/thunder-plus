# The optional LXR module (U04). GPL-3.0-only code: see LICENSES.md in this folder. NOTHING in native/core refers to this folder; the core reaches the module through
# core/include/thunder/lxr_seam.h (a table of function pointers the host application registers at start-up). With -DTHUNDER_WITH_LXR=OFF none of this is configured, compiled or linked.
#
# Sources (never copied into native/): the repo's lxr/src (the firmware files, the glue lxr_wasm.c the web app's lxr.wasm is built from, the stubs), prepared into the build
# folder by prepare-lxr.sh (host-only patches, listed there and in docs/port/U04-NOTES.md), plus this folder's host stubs and the C++ module.
find_program(LXR_SH sh)
find_package(Python3 COMPONENTS Interpreter)
if(NOT LXR_SH OR NOT Python3_EXECUTABLE)
  message(FATAL_ERROR "THUNDER_WITH_LXR needs sh and python3 (prepare-lxr.sh); build with -DTHUNDER_WITH_LXR=OFF to leave the LXR module out")
endif()
set(LXR_REPO_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../lxr)
set(LXR_PREP ${CMAKE_CURRENT_BINARY_DIR}/lxr-src)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/prepare-lxr.sh ${LXR_REPO_DIR}/src/lxr_wasm.c)
execute_process(COMMAND ${LXR_SH} ${CMAKE_CURRENT_SOURCE_DIR}/prepare-lxr.sh ${LXR_REPO_DIR} ${LXR_PREP}
                RESULT_VARIABLE LXR_PREP_RC OUTPUT_VARIABLE LXR_PREP_OUT ERROR_VARIABLE LXR_PREP_ERR)
if(NOT LXR_PREP_RC EQUAL 0)
  message(FATAL_ERROR "prepare-lxr.sh failed (${LXR_PREP_RC}): ${LXR_PREP_ERR}")
endif()

set(LXR_FW ${LXR_PREP}/Core/Src)
file(GLOB LXR_FW_SOURCES ${LXR_FW}/Soundengine/DSPAudio/*.c ${LXR_FW}/Soundengine/DSPAudio/FX/*.c ${LXR_FW}/Soundengine/DSPAudio/FX/VerbTools/*.c)
# exactly lxr/build.sh's list
list(APPEND LXR_FW_SOURCES ${LXR_FW}/Soundengine/MIDI/MidiParser.c ${LXR_FW}/Soundengine/MIDI/ParameterArray.c ${LXR_FW}/Soundengine/FxBus.c
     ${LXR_FW}/Parameters.c ${LXR_FW}/menu/Cc2Text.c ${LXR_FW}/Preset/initKit.c ${LXR_PREP}/lxr_wasm.c)
add_library(thunder_lxr STATIC ${LXR_FW_SOURCES} src/lxr_host_stubs.c src/lxr_module.cpp)
target_include_directories(thunder_lxr PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(thunder_lxr PRIVATE src ${CMAKE_CURRENT_SOURCE_DIR}/../core/src)   # rt/jsmath.h (the core's JS maths; the module may use the core, never the other way round)
target_include_directories(thunder_lxr PRIVATE ${LXR_PREP}/stub ${LXR_PREP}/Core/Inc)
target_link_libraries(thunder_lxr PUBLIC thunder_core)
target_compile_definitions(thunder_lxr PUBLIC THUNDER_HAVE_LXR=1)
target_compile_features(thunder_lxr PUBLIC cxx_std_17)
# The firmware as the web build compiles it (lxr/build.sh: -O3 -ffast-math -funsigned-char -std=gnu11), STRICT here: no fast-math, no fused multiply-add (the core's rule), the
# unsigned char of the ARM it was written for, no strict-aliasing assumptions (it reads parameters through casts). Its warnings are the firmware's own: not ours to fix.
target_compile_options(thunder_lxr PRIVATE $<$<COMPILE_LANGUAGE:C>:-std=gnu11 -funsigned-char -fno-strict-aliasing -ffp-contract=off -w>
                                           $<$<COMPILE_LANGUAGE:CXX>:-Wall -Wextra -fno-exceptions -fno-rtti -ffp-contract=off>)
# GCC has no __fp16 on x86 (clang does everywhere): the FX buffers' half-float storage type (not used by a voice's render) is _Float16 there.
if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
  target_compile_definitions(thunder_lxr PRIVATE $<$<COMPILE_LANGUAGE:C>:__fp16=_Float16>)
endif()
target_link_libraries(thunder_lxr PUBLIC m)
