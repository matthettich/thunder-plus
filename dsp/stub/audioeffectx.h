// Minimal stand-in for the VST 2 SDK so Airwindows' plugin code compiles to WebAssembly.
#ifndef __audioeffect__
#define __audioeffect__
#include <stdint.h>
#include <string.h>
#include <stddef.h>
typedef int32_t VstInt32;
typedef intptr_t (*audioMasterCallback)(void*, int32_t, int32_t, intptr_t, void*, float);
enum VstPlugCategory { kPlugCategUnknown = 0, kPlugCategEffect = 1 };
enum { kVstMaxProgNameLen = 24, kVstMaxParamStrLen = 8, kVstMaxVendorStrLen = 64,
       kVstMaxProductStrLen = 64, kVstMaxEffectNameLen = 32 };
class AudioEffect { public: virtual ~AudioEffect() {} };
class AudioEffectX : public AudioEffect {
 public:
  AudioEffectX(audioMasterCallback, VstInt32, VstInt32) : sample_rate_(48000.0f) {}
  virtual ~AudioEffectX() {}
  float getSampleRate() { return sample_rate_; }
  void setSampleRate(float sr) { sample_rate_ = sr; }
  void setNumInputs(VstInt32) {} void setNumOutputs(VstInt32) {} void setUniqueID(VstInt32) {}
  void canProcessReplacing(bool = true) {} void canDoubleReplacing(bool = true) {} void programsAreChunks(bool = true) {}
 protected:
  float sample_rate_;
};
inline char* vst_strncpy(char* d, const char* s, size_t n) { strncpy(d, s, n); d[n] = 0; return d; }
inline void float2string(float, char* t, VstInt32) { if (t) t[0] = 0; }
inline void dB2string(float, char* t, VstInt32) { if (t) t[0] = 0; }
inline void int2string(VstInt32, char* t, VstInt32) { if (t) t[0] = 0; }
#endif
