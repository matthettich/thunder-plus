/* miniaudio (io/vendor/miniaudio: the single header the repo already vendors), compiled once for the desktop shells. The MA_* definitions (no decoding / encoding / generation; WASAPI on Windows,
   Core Audio on the Mac) come from CMake and are the same for this file and audio_miniaudio.cpp (the structs depend on them). */
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
