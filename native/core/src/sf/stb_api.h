// U32: stb_vorbis's declarations (the pinned file's own header section), with the same macros the library is compiled with (core/sf/CMakeLists.txt).
#ifndef THUNDER_SF_STB_API_H
#define THUNDER_SF_STB_API_H
#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#define STB_VORBIS_NO_INTEGER_CONVERSION
#define STB_VORBIS_HEADER_ONLY
extern "C" {
#include "stb_vorbis.c"
}
#undef STB_VORBIS_HEADER_ONLY
#endif
