/* Thunder host patch (native/lxr/prepare-lxr.sh): saturating float -> integer conversions (the wasm's and the STM32's behaviour; C leaves out-of-range undefined). */
#ifndef LXR_HOST_CVT_H
#define LXR_HOST_CVT_H
#include <stdint.h>
static inline int32_t lxr_f2i32(double x){ if(x!=x) return 0; if(x>=2147483648.0) return INT32_MAX; if(x<=-2147483648.0) return INT32_MIN; return (int32_t)x; }
static inline uint32_t lxr_f2u32(double x){ if(x!=x) return 0; if(x<=0.0) return 0; if(x>=4294967296.0) return UINT32_MAX; return (uint32_t)x; }
static inline int16_t lxr_f2i16(double x){ return (int16_t)(uint16_t)((uint32_t)lxr_f2i32(x)&0xffffu); }
/* A wavetable row is 1024 long and the firmware reads [1024] (one past a row): the next row's first sample; past the last row of a table: that row's own first sample. */
static inline int16_t lxr_tab(const int16_t (*t)[1024], unsigned o, unsigned i){ unsigned f=o*1024u+i; if(i>1023u && o>=10u) f=o*1024u+(i&1023u); return ((const int16_t*)t)[f]; }
#endif
