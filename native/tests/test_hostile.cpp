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

// ---- A5: a full mixer lane: the latest move wins ----
static void testA5() {
  Mixer m(48000, 128); std::vector<float> L(128), Rr(128); double t = 0;
  auto block = [&]() { m.setTime(t); for (int i = 0; i < 128; i++) { m.inputsL()[0][i] = 1; m.inputsR()[0][i] = 1; } m.render(L.data(), Rr.data(), 128, t); t += 128.0 / 48000; };
  for (int b = 0; b < 4; b++) block();
  m.setTime(t);
  for (int i = 0; i < 40; i++) { MixSet c; c.tr = 0; c.k = MixKey::Vol; c.v = 224 - i; m.set(c); }   // a fader pulled down in 40 moves inside one block
  { MixSet c; c.tr = 0; c.k = MixKey::Mute; c.v = 1; m.set(c); }
  for (int b = 0; b < 400; b++) block();
  CHECK("A5 muted track is silent 1 s after MUTE (was 0.86)", std::fabs(L[127]) < 1e-6f && std::fabs(Rr[127]) < 1e-6f);
  CHECK("A5 audible mask says muted", (m.audibleMask() & 1u) == 0);
  // and the reverse: UNMUTE after another flood comes back
  m.setTime(t);
  for (int i = 0; i < 40; i++) { MixSet c; c.tr = 0; c.k = MixKey::Vol; c.v = 100 + i; m.set(c); }
  { MixSet c; c.tr = 0; c.k = MixKey::Mute; c.v = 0; m.set(c); }
  for (int b = 0; b < 400; b++) block();
  CHECK("A5 unmuted track is audible again", L[127] > 0.01f);
}

// ---- B1: the same song, the same samples, whatever block size the host asks for ----
static std::vector<float> renderWith(const std::string& js, int total, const std::vector<int>& sizes, int maxBlock, double* overAt = nullptr) {
  Song song; loadSong(song, js);
  SeqVars vars; SeqOpts o; o.once = true; SongRenderer R(song, vars, *mkPads(), 48000, maxBlock, o);
  std::vector<float> out((size_t)total * 2); std::vector<float> L((size_t)maxBlock * 2 + 8192), Rr((size_t)maxBlock * 2 + 8192);
  int done = 0; size_t k = 0;
  while (done < total) {
    int n = sizes[k++ % sizes.size()]; if (n > total - done) n = total - done;
    R.block(L.data(), Rr.data(), n);
    for (int i = 0; i < n; i++) { out[(size_t)(done + i) * 2] = L[(size_t)i]; out[(size_t)(done + i) * 2 + 1] = Rr[(size_t)i]; }
    done += n;
    if (overAt && *overAt < 0 && R.songOver() && R.voices() == 0) *overAt = R.time();
  }
  return out;
}
static void testB1() {
  // 512 rows at 300 BPM, 16 lines a beat (12.5 ms a row): three steps fit in no 4096-frame block
  const std::string js = songJson(row(60, -1, -1), "", 300, 16, true, 16);
  const int total = 48000 * 3;
  const std::vector<float> ref = renderWith(js, total, {128}, 128);
  const std::vector<std::vector<int>> cases = {{480}, {4096}, {1}, {7, 333, 1000, 129, 127}, {1920}, {128, 4096, 3}};
  for (const auto& c : cases) {
    const std::vector<float> got = renderWith(js, total, c, 4096);
    CHECK("B1 samples equal to the 128-frame render whatever the block size", got == ref);
  }
  // the song is not slowed down by a large block: its second half is not silent / delayed (notes at 12.5 ms)
  double lastNote = 0; for (int i = total - 1; i >= 0; i--) if (std::fabs(ref[(size_t)i * 2]) > 1e-4f) { lastNote = i / 48000.0; break; }
  CHECK("B1 the song is still sounding at 3 s", lastNote > 2.9);
  Song song; loadSong(song, js); SeqVars vars; SeqOpts o; o.once = true; SongRenderer R(song, vars, *mkPads(), 48000, 4096, o);
  std::vector<float> L(4096), Rr(4096);
  while (!R.songOver() && R.frame() < 48000L * 60) R.block(L.data(), Rr.data(), 4096);
  CHECK("B1 512 rows of 12.5 ms are over by 6.6 s at block 4096 (was 14.6 s)", R.time() < 6.7);
  // a block larger than maxBlock is rendered whole, not cut
  { SongRenderer R2(song, vars, *mkPads(), 48000, 256, o); std::vector<float> a(1000, 7.0f), b(1000, 7.0f); R2.block(a.data(), b.data(), 1000);
    bool all = true; for (float v : a) if (v == 7.0f) all = false; CHECK("B1 N > maxBlock fills every sample", all); }
}

// ---- B2: song text is UTF-8 bytes; 4-byte characters and surrogate-pair escapes load ----
static void testB2() {
  const char raw[] = "{\"v\":6,\"name\":\"a\xF0\x9F\x98\x80z\"}";                 // U+1F600 as real UTF-8 (what toByteArray(UTF_8) gives)
  const char esc[] = "{\"v\":6,\"name\":\"a\\ud83d\\ude00z\"}";                  // the same as a JSON surrogate-pair escape
  const char mod[] = "{\"v\":6,\"name\":\"a\xED\xA0\xBD\xED\xB8\x80z\"}";      // modified UTF-8 (GetStringUTFChars): still refused, so the glue must not use it
  Song a, b, c; JsonError je;
  CHECK("B2 raw 4-byte UTF-8 loads", a.load(raw, sizeof raw - 1, &je));
  CHECK("B2 surrogate-pair escape loads", b.load(esc, sizeof esc - 1, &je));
  CHECK("B2 both decode to the same name", a.root.get("name").asStr() == b.root.get("name").asStr() && a.root.get("name").asStr() == "a\xF0\x9F\x98\x80z");
  CHECK("B2 modified UTF-8 is refused (why the glue passes bytes)", !c.load(mod, sizeof mod - 1, &je));
  thunder_core* core = thunder_create(48000, 256);
  const std::string msg = std::string("{\"t\":\"song.load\",\"rev\":3,\"S\":") + raw + "}";
  CHECK("B2 song.load with an emoji through thunder_post", thunder_post(core, msg.data(), (int)msg.size()) == 1);
  thunder_destroy(core);
}

// ---- B3: the song acknowledgement survives a full event ring; drops and late commands are reported ----
static void testB3() {
  thunder_core* c = thunder_create(48000, 64); float l[64], r[64];
  int queued = 0; std::vector<float> big(12000);
  for (int k = 0; k < 20; k++) { for (int i = 0; i < 200; i++) queued += thunder_post(c, "{\"t\":\"echo\",\"n\":1}", 20); thunder_process(c, l, r, 64); }   // ~4000 echoes, never polled
  const char* sj = "{\"t\":\"song.load\",\"rev\":7,\"S\":{\"v\":6}}";
  CHECK("B3 song.load accepted", thunder_post(c, sj, (int)std::strlen(sj)) == 1);
  thunder_process(c, l, r, 64);
  char buf[512]; int song = 0, got = 0; long long ackRev = -1; bool clockSeen = false; unsigned dropped = 0;
  for (int round = 0; round < 40 && !song; round++) {   // the host polls now: room appears, the retried ack lands in a following block
    while (thunder_poll(c, buf, sizeof buf)) { got++; if (std::strstr(buf, "\"t\":\"song\"")) { song++; std::sscanf(std::strstr(buf, "\"rev\":"), "\"rev\":%lld", &ackRev); }
      if (const char* d = std::strstr(buf, "\"evtDropped\":")) { clockSeen = true; std::sscanf(d, "\"evtDropped\":%u", &dropped); } }
    thunder_process(c, l, r, 64);
  }
  CHECK("B3 the song ack arrives after the ring was full", song == 1 && ackRev == 7);
  for (int round = 0; round < 200; round++) { thunder_process(c, l, r, 64); while (thunder_poll(c, buf, sizeof buf)) if (const char* d = std::strstr(buf, "\"evtDropped\":")) { clockSeen = true; std::sscanf(d, "\"evtDropped\":%u", &dropped); } }
  CHECK("B3 the clock event reports evtDropped > 0", clockSeen && dropped > 0);
  CHECK("B3 the clock event carries lateCmds", [&] { thunder_process(c, big.data(), big.data() + 6000, 6000); while (thunder_poll(c, buf, sizeof buf)) if (std::strstr(buf, "\"lateCmds\":")) return true; return false; }());
  (void)got; thunder_destroy(c);
}

// ---- B4: the step buffer holds the worst case: sixteen poly tracks (the ticks clamp at 20 a hit each: about 80 a track), nothing dropped ----
static void testB4() {
  // four notes on a row with RET 01 (every tick) and TIC 20 (32 ticks a line): 4 x up to 64 hits a track
  char r[160]; std::snprintf(r, sizeof r, "[60,64,67,71,-1,0,11,1,27,32,-1,-1]");
  std::string js = "{\"v\":6,\"bpm\":120,\"lpb\":4,\"ntr\":16,\"song\":[[0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0]],\"chains\":[[[0,0]]],\"plen\":[16],\"phrases\":[[" + std::string(r);
  for (int i = 1; i < 32; i++) js += ",[-1,-1,-1,-1,-1,0,-1,-1,-1,-1,-1,-1]";
  js += "]]}";
  Song song; CHECK("B4 song loads", loadSong(song, js)); SeqVars vars; SeqOpts o; o.once = true;
  SongRenderer R(song, vars, *mkPads(), 48000, 512, o); std::vector<float> L(512), Rr(512);
  for (int b = 0; b < 200; b++) R.block(L.data(), Rr.data(), 512);
  const RenderStats st = R.stats();
  std::printf("  B4: largest step %d events, seqDropped %llu\n", st.peakStepEvents, (unsigned long long)st.seqDropped);
  CHECK("B4 one step wrote more than the old 1024-event buffer", st.peakStepEvents > 1024);
  CHECK("B4 and nothing was dropped", st.seqDropped == 0);
}

// @@TESTS_END

int main() {
  testA1();
  testA2A3();
  testA5();
  testB1();
  testB2();
  testB3();
  testB4();
  // @@CALLS_END
  if (g_fail) { std::printf("test_hostile: %d FAILED\n", g_fail); return 1; }
  std::printf("test_hostile: all passed\n");
  return 0;
}
