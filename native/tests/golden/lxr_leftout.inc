// Cases left out of the -100 dB gate BY NAME, each with the reason and the measured numbers; they get the structural check instead (finite, RMS level within 1 dB over the
// whole render, the same length within maxLenPct). Empty: no case needed it (see docs/port/U04-NOTES.md).
struct LxrLeftOutEntry { const char* name; double maxLenPct; const char* why; };
static const LxrLeftOutEntry kLxrLeftOut[] = {{nullptr, 0, nullptr}};
static const int kLxrLeftOutN = 0;
