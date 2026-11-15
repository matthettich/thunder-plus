// N1 review fixes: regression tests for the independent review's findings (progress/review-n1.md, 15 Nov 2026). Each test is
// named for its finding (A1..C9) and fails with that id. Built with the sanitizers in CI (THUNDER_SANITIZE), where a read out of
// range, a shift by the type's width or a cast of NaN is itself a failure.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "mix/mix.h"
#include "render/render.h"
#include "seq/seq.h"
#include "song/song.h"
#include "thunder/core.h"
#include "voice/voice.h"

using namespace thunder;

static int g_fail = 0;
#define CHECK(id, cond) do { if (!(cond)) { std::printf("FAIL %s: %s (line %d)\n", id, #cond, __LINE__); g_fail++; } } while (0)

static std::string row(int note, int cmd, int val) { char b[128]; std::snprintf(b, sizeof b, "[%d,-1,-1,-1,-1,0,%d,%d,-1,-1,-1,-1]", note, cmd, val); return b; }
// A one-phrase song: `firstRow` on row 0, then 31 rows of `fill` (every row a note C-5) or empty rows.
static std::string songJson(const std::string& firstRow, const std::string& extra, int bpm = 120, int lpb = 4, bool fill = false, int chainRows = 1) {
  std::string s = "{\"v\":6,\"bpm\":" + std::to_string(bpm) + ",\"lpb\":" + std::to_string(lpb) + ",\"ntr\":8,\"song\":[[0]],\"chains\":[[";
  for (int i = 0; i < chainRows; i++) s += std::string(i ? "," : "") + "[0,0]";
  s += "]],\"plen\":[" + std::string(fill ? "32" : "16") + "],\"phrases\":[[" + firstRow;
  for (int i = 1; i < 32; i++) s += "," + (fill ? row(60, -1, -1) : row(-1, -1, -1));
  s += "]]" + extra + "}";
  return s;
}
static std::vector<float> g_pad(24000);
static RenderPads* mkPads(double sr = 48000) {
  static RenderPads pads;
  for (int i = 0; i < 24000; i++) g_pad[(size_t)i] = (float)std::sin(0.05 * i) * 0.3f;
  for (int p = 0; p < 64; p++) { pads.buf[p].data = g_pad.data(); pads.buf[p].len = 24000; pads.buf[p].sr = sr; }
  return &pads;
}
static bool loadSong(Song& s, const std::string& js) { JsonError je; return s.load(js.data(), js.size(), &je); }

// ---- A1: a NaN rate / position / sample rate must not index a pad buffer ----
static long renderNanFrames(const std::string& js, double padSr, int blocks, RenderStats* st, int* voicesAfter) {
  Song song; if (!loadSong(song, js)) return -1;
  SeqVars vars; SeqOpts o; o.once = true; SongRenderer R(song, vars, *mkPads(padSr), 48000, 256, o);
  std::vector<float> L(256), Rr(256); long bad = 0;
  for (int b = 0; b < blocks; b++) { R.block(L.data(), Rr.data(), 256); for (int i = 0; i < 256; i++) if (!std::isfinite(L[(size_t)i]) || !std::isfinite(Rr[(size_t)i])) bad++; }
  *st = R.stats(); *voicesAfter = R.voices(); return bad;
}
static void testA1() {
  RenderStats st; int va = 0;
  // a part whose instrument has no `tsp` (and one with no `pad`), then PRT 01 on a row with a note
  for (const char* parts : {",\"parts\":[{\"mix\":[],\"fx\":{},\"insts\":[{\"pad\":0}]}]", ",\"parts\":[{\"mix\":[],\"fx\":{},\"insts\":[{\"tsp\":0}]}]"}) {
    const long bad = renderNanFrames(songJson(row(60, 72, 1), parts), 48000, 1500, &st, &va);
    CHECK("A1 part inst without tsp/pad: no NaN in the output", bad == 0);
    CHECK("A1 part inst without tsp/pad: no voice left sounding", va == 0);
  }
  // a pad buffer with sample rate 0
  const long bad0 = renderNanFrames(songJson(row(60, -1, -1), ""), 0, 400, &st, &va);
  CHECK("A1 pad sr 0: no NaN", bad0 == 0);
  CHECK("A1 pad sr 0: nothing played", st.notesPlayed == 0);
  // the pool directly
  VoicePool pool(48000, 8); VoiceBuf b; b.data = g_pad.data(); b.len = 24000; b.sr = 48000; VoiceKit k;
  SeqEvent e; e.type = SEV_NOTE; e.track = 0; e.t = 0; e.rate = 1; e.gain = 1; e.pan = 128;
  const double bad[] = {NAN, INFINITY, -1.0, 0.0};
  for (double r : bad) { SeqEvent x = e; x.rate = r; CHECK("A1 note refuses a bad rate", !pool.note(x, nullptr, b, k, 120)); }
  { SeqEvent x = e; x.t = NAN; CHECK("A1 note refuses a NaN time", !pool.note(x, nullptr, b, k, 120)); }
  { VoiceBuf s0 = b; s0.sr = NAN; CHECK("A1 note refuses a NaN pad rate", !pool.note(e, nullptr, s0, k, 120)); }
  { VoiceBuf s1 = b; s1.len = 1; CHECK("A1 note refuses a 1-sample pad", !pool.note(e, nullptr, s1, k, 120)); }
  CHECK("A1 refused() counts them", pool.refused() == 7);
  CHECK("A1 a good note still starts", pool.note(e, nullptr, b, k, 120) && pool.active() == 1);
}

// ---- A2: thunder_put offsets and counts; A3: thunder_create arguments ----
static void testA2A3() {
  thunder_core* c = thunder_create(48000, 256); CHECK("A3 sane create", c != nullptr); if (!c) return;
  float d[8] = {1, 2, 3, 4, 5, 6, 7, 8};
  CHECK("A2 first chunk", thunder_put(c, 0, "{\"o\":0,\"n\":16}", d, 8) == 1);
  CHECK("A2 second chunk at 8", thunder_put(c, 0, "{\"o\":8,\"n\":16}", d, 8) == 1);
  CHECK("A2 negative offset refused (it wrote before the buffer)", thunder_put(c, 0, "{\"o\":-4,\"n\":16}", d, 8) == 0);
  CHECK("A2 fractional offset refused", thunder_put(c, 0, "{\"o\":1.5,\"n\":16}", d, 8) == 0);
  CHECK("A2 past the end refused", thunder_put(c, 0, "{\"o\":12,\"n\":16}", d, 8) == 0);
  CHECK("A2 offset beyond size refused", thunder_put(c, 0, "{\"o\":1000000,\"n\":16}", d, 8) == 0);
  CHECK("A2 huge o refused", thunder_put(c, 0, "{\"o\":1e300,\"n\":16}", d, 8) == 0);
  CHECK("A2 huge n refused (it was bad_alloc)", thunder_put(c, 1, "{\"o\":0,\"n\":1e18}", d, 8) == 0);
  CHECK("A2 negative n refused", thunder_put(c, 1, "{\"o\":0,\"n\":-5}", d, 8) == 0);
  CHECK("A2 n smaller than count refused", thunder_put(c, 1, "{\"o\":0,\"n\":4}", d, 8) == 0);
  CHECK("A2 no meta: whole buffer", thunder_put(c, 2, nullptr, d, 8) == 1);
  CHECK("A2 zero count", thunder_put(c, 3, "{\"o\":0,\"n\":4}", d, 0) == 1);
  int64_t cnt = 0; double sum = 0; CHECK("A2 info", thunder_put_info(c, 0, &cnt, &sum) == 1 && cnt == 16 && sum == 72);
  thunder_destroy(c);
  CHECK("A3 rate 40", thunder_create(40, 64) == nullptr);
  CHECK("A3 rate 0", thunder_create(0, 64) == nullptr);
  CHECK("A3 rate negative", thunder_create(-48000, 64) == nullptr);
  CHECK("A3 rate huge", thunder_create(2000000000, 64) == nullptr);
  CHECK("A3 block 0", thunder_create(48000, 0) == nullptr);
  CHECK("A3 block negative", thunder_create(48000, -1) == nullptr);
  thunder_core* e = thunder_create(8000, 1); CHECK("A3 smallest accepted", e != nullptr);
  if (e) { float l[4], r[4]; thunder_post(e, "{\"t\":\"play\"}", 12); thunder_process(e, l, r, 4); thunder_destroy(e); }
}

// @@TESTS_END

int main() {
  testA1();
  testA2A3();
  // @@CALLS_END
  if (g_fail) { std::printf("test_hostile: %d FAILED\n", g_fail); return 1; }
  std::printf("test_hostile: all passed\n");
  return 0;
}
