// D4: U15 (Flex 95, Step gate 94, Retrigger 93) END TO END on a real song: sounds/ELECTRO.thunder through the whole renderer (sequencer -> sampler voices ->
// Mixer -> FxBank -> FxHost -> clkcores), with Flex / Step gate / Retrigger placed on tracks and FLX / GAT / RTG / FXO tracker commands put into the phrases
// (tests/golden/u15song-patch.json: the same file is read by tools/u15-song-dump.mjs). The song's own master effects, a Retrigger and a Flex, stay.
//
// WHAT IS COMPARED WITH WHAT. The reference is the real JS: the real ThunderFx worklet (the three cores cut out of index.html, run in Node) per effects node and the
// JS model of the page's Web Audio strip (tools/mix-dump.mjs, the model test_mix uses) for the mixer, run by tools/u15-song-dump.mjs. What Node cannot make is the
// voices and the sequencer (they are other port units, gated elsewhere), so those come from this program: with --dump <dir> it writes what the C++ voices put into
// every track's effects node (after the mixer's TRIM, i.e. where the page's chain has its effects), the beat clock each node hears, and the events the sequencer
// hands to the mixer and to the effects hosts (taken from a second sequencer run in step with the renderer). Node renders from that; this program compares the
// whole render of SongRenderer (the product path: setEffects(true)) with Node's, sample by sample.
//   test_u15song                  no Node: the render must equal the COMMITTED golden (golden/u15song_golden.inc, made from Node's output on Linux x86-64): the gate on every platform
//   test_u15song --dump <dir>     writes <dir>/{meta.json,events.json,in.f32,bp.f32,cpp.f32} for tools/u15-song-dump.mjs
//   test_u15song <dir>            also compares with <dir>/ref.f32 (Node's render): THE GATE IS BIT-EXACT, the first differing sample is printed
// The input hash in the golden tells a platform whose voices differ from the effects' being wrong.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "fx/fxbank.h"
#include "mix/mix.h"
#include "render/render.h"
#include "rt/json.h"
#include "golden/u15song_golden.inc"

using namespace thunder;

static bool readFile(const std::string& path, std::string* out) { std::ifstream f(path, std::ios::binary); if (!f) return false; std::stringstream ss; ss << f.rdbuf(); *out = ss.str(); return true; }
static bool writeFile(const std::string& path, const void* p, size_t n) { std::ofstream f(path, std::ios::binary); if (!f) return false; f.write((const char*)p, (std::streamsize)n); return (bool)f; }
static uint64_t fnvAdd(uint64_t h, const void* p, size_t n) { const unsigned char* u = (const unsigned char*)p; for (size_t i = 0; i < n; i++) { h ^= u[i]; h *= 0x100000001b3ull; } return h; }
static const uint64_t kFnv0 = 0xcbf29ce484222325ull;
static int fails = 0;
static void check(bool ok, const char* what) { std::printf("%-100s %s\n", what, ok ? "ok" : "FAIL"); if (!ok) fails++; }

constexpr int SR = 48000, Q = kRenderQuantum, NT = kSongNT;

// ---- the song with the patch ------------------------------------------------------------------------------------------------------------
struct Setup {
  Song song; JsonDoc patch; std::string songText, patchText; int frames = 0; std::string err;
  bool build(const std::string& root) {
    if (!readFile(root + "/sounds/ELECTRO.thunder", &songText)) { err = "cannot read sounds/ELECTRO.thunder"; return false; }
    if (!readFile(root + "/native/tests/golden/u15song-patch.json", &patchText)) { err = "cannot read golden/u15song-patch.json"; return false; }
    JsonError je; if (!song.load(songText.data(), songText.size(), &je)) { err = "ELECTRO does not load"; return false; }
    if (!patch.parse(patchText)) { err = "u15song-patch.json is not JSON"; return false; }
    Json P = patch.root(); frames = (int)P.num("frames");
    // the song as the other whole-song references have it: no chains (no bus exists without a chains host), the song's own effects replaced by the patch's on the tracks
    for (int t = 0; t < NT; t++) song.chainOn[t] = false;
    song.tfx = P.arr("tfx");
    Json cm = P.arr("cmds");
    for (size_t i = 0; i < cm.size(); i++) {
      Json c = cm.at(i); const int t = (int)c.num("t"), sr = (int)c.num("sr"), row = (int)c.num("row"), cmd = (int)c.num("cmd"), v = (int)c.num("v");
      const int ch = song.cell(sr, t); const int16_t* cd = song.chain(ch); if (!cd) { err = "cmd: no chain at that song row"; return false; }
      const int ph = cd[0]; const int32_t off = ph >= 0 && ph < kNPH ? song.phraseOff[(size_t)ph] : -1; if (off < 0) { err = "cmd: no phrase"; return false; }
      int16_t* r = song.phraseData.data() + off + (size_t)row * kRW; int k = kFX0; while (k < kRW && r[k] >= 0) k += 2;
      if (k >= kRW) { err = "cmd: no free command column in the phrase row"; return false; }
      r[k] = (int16_t)cmd; r[k + 1] = (int16_t)v;
    }
    return true;
  }
};

// ---- the pads: deterministic, no libm (plain float arithmetic only; the build has -ffp-contract=off) -----------------------------------------------
struct Pads {
  RenderPads rp; std::vector<float> data;
  Pads() {
    const int PN = 64, NS = 24000; data.resize((size_t)PN * NS); uint32_t s = 2463534242u;
    for (int p = 0; p < PN; p++) { float* d = &data[(size_t)p * NS]; const int per = 36 + 11 * (p % 9);
      for (int i = 0; i < NS; i++) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; const float saw = (float)(i % per) / (float)per * 2.0f - 1.0f, nz = (float)(s >> 8) / 16777216.0f * 2.0f - 1.0f;
        d[i] = (saw * (p % 2 ? 0.6f : 0.3f) + nz * (p % 3 == 0 ? 0.35f : 0.05f)) * (1.0f - (float)i / (float)NS) * 0.6f; }
      rp.buf[p].data = d; rp.buf[p].len = NS; rp.buf[p].sr = 48000; rp.kit[p].syn = p >= 8; rp.kit[p].rel = p >= 8 ? 0.1 : 0; rp.kit[p].choke = p == 3 ? 1 : 0; }
  }
};

// ---- the taps: what each effects node is given (the page's node input), and the clock it hears ------------------------------------------------
struct Cap {
  int frames = 0; Mixer* mix = nullptr; FxBank* bank = nullptr;
  std::vector<float> in[kSongNT + 1][2];   // allocated when the first non-zero block arrives (zeros before)
  std::vector<float> bp[kSongNT + 1];      // the beat lane of every track that has slots (and the master): one float per frame
  bool seen[kSongNT + 1] = {false};
};
struct Tap { Cap* cap; int track; };
static void tapHook(void* user, int track, float* L, float* R, int N, double t0, const MixFeed* f, int nf) {
  Tap* T = (Tap*)user; Cap& C = *T->cap; const size_t at = (size_t)std::llround(t0 * SR);
  if (at + (size_t)N <= (size_t)C.frames) {
    bool any = false; for (int i = 0; i < N && !any; i++) any = L[i] != 0 || R[i] != 0 || L[i] != L[i] || R[i] != R[i];
    if (any && C.in[track][0].empty()) { C.in[track][0].assign((size_t)C.frames, 0.0f); C.in[track][1].assign((size_t)C.frames, 0.0f); }
    if (!C.in[track][0].empty()) { std::memcpy(&C.in[track][0][at], L, sizeof(float) * (size_t)N); std::memcpy(&C.in[track][1][at], R, sizeof(float) * (size_t)N); }
    if (C.bank->host(track).slotCount() > 0) {
      if (C.bp[track].empty()) C.bp[track].assign((size_t)C.frames, 0.0f);
      const float* b = C.mix->beat(track); if (b) std::memcpy(&C.bp[track][at], b, sizeof(float) * (size_t)N);
      C.seen[track] = true;
    }
  }
  if (C.bank->host(track).slotCount() > 0) {
    // the master: SongRenderer::masterHook (render.cpp) runs the master host WITHOUT the beat clock; this tap does the same so that the tapped render is the product's
    // (FxBank::hook, which every track gets, passes Mixer::beat(track); the page's master effects node hears the clock too: index.html setMfx -> clockTo)
    if (track == NT) C.bank->host(track).process(L, R, N, t0); else FxBank::hook(C.bank, track, L, R, N, t0, f, nf);
  }
}

struct Ev { int q; int type; int track; int p; double t, v; };

// one render of the song through SongRenderer; cap != null: the taps (every track and the master) are put in front of the effects hooks
static void render(Setup& S, Pads& P, int frames, std::vector<float>* oL, std::vector<float>* oR, Cap* cap, std::vector<Tap>* taps) {
  SeqVars vars; SeqOpts o; o.once = true; o.row = S.song.renderFrom;
  SongRenderer R(S.song, vars, P.rp, SR, 512, o); R.setEffects(true);
  if (cap) {
    cap->frames = frames; cap->mix = &R.mixer(); cap->bank = R.fxBank(); taps->resize(kSongNT + 1);
    for (int t = 0; t <= NT; t++) { (*taps)[(size_t)t] = Tap{cap, t}; if (t < NT) R.mixer().setFx(t, tapHook, &(*taps)[(size_t)t]); else R.mixer().setMasterFx(tapHook, &(*taps)[(size_t)t]); }
  }
  oL->assign((size_t)frames, 0.0f); oR->assign((size_t)frames, 0.0f);
  for (int f = 0; f < frames; f += 512) R.block(&(*oL)[(size_t)f], &(*oR)[(size_t)f], std::min(512, frames - f));
}

// the events the renderer's quanta handed on, from a second sequencer run in step with it (render.cpp quantum(): while the next step is due within LEAD of the quantum's start, at most three steps)
static void events(Setup& S, int frames, std::vector<Ev>* out, int* fxp, int* dropped) {
  SeqVars vars; SeqOpts o; o.once = true; o.row = S.song.renderFrom; Seq seq(S.song, vars, SeqMode::Song, o);
  std::vector<SeqEvent> evs((size_t)kRenderEvents); std::unique_ptr<SeqStep> st(new SeqStep); const double lead = 0.25;
  for (int q = 0; q * Q < frames; q++) {
    const double t0 = (double)q * Q / SR; int n = 0;
    while (seq.alive() && seq.time() < t0 + lead && n++ < 3) {
      const int k = seq.step(evs.data(), kRenderEvents, st.get());
      for (int i = 0; i < k; i++) { const SeqEvent& e = evs[(size_t)i];
        if (e.type == SEV_FXC) out->push_back(Ev{q, e.type, e.track, e.p, e.t, e.v});
        else if (e.type == SEV_FXP) (*fxp)++;
        else if (e.type == SEV_TVL || e.type == SEV_TVA || e.type == SEV_TPN || e.type == SEV_MVOL || e.type == SEV_MOUT) out->push_back(Ev{q, e.type, e.track, 0, e.t, e.v}); }
    }
  }
  *dropped = (int)seq.dropped();
}

int main(int argc, char** argv) {
  std::string dumpDir, refDir;
  for (int i = 1; i < argc; i++) { if (!std::strcmp(argv[i], "--dump") && i + 1 < argc) dumpDir = argv[++i]; else refDir = argv[i]; }
  Setup S; if (!S.build(THUNDER_ROOT)) { std::printf("u15song: %s  FAIL\n", S.err.c_str()); return 1; }
  static Pads P; const int N = S.frames;
  // 1. the product path: SongRenderer with its effects (no taps)
  std::vector<float> aL, aR; render(S, P, N, &aL, &aR, nullptr, nullptr);
  // 2. the same with the taps in front of every effects hook (must not change one sample), and the events
  static Cap cap; std::vector<Tap> taps; std::vector<float> bL, bR; render(S, P, N, &bL, &bR, &cap, &taps);
  std::vector<Ev> evs; int fxp = 0, seqDropped = 0; events(S, N, &evs, &fxp, &seqDropped);
  check(!std::memcmp(aL.data(), bL.data(), (size_t)N * 4) && !std::memcmp(aR.data(), bR.data(), (size_t)N * 4), "the taps do not change the render (a pass-through hook on every track)");
  check(seqDropped == 0, "the second sequencer run dropped no event");
  // what is in the render
  int nSlots = 0, tracksIn = 0, nFxc = 0, nSw = 0; for (int t = 0; t <= NT; t++) { if (cap.seen[t]) nSlots++; if (!cap.in[t][0].empty()) tracksIn++; }
  for (const Ev& e : evs) if (e.type == SEV_FXC) { if (e.p >= 8) nFxc++; else nSw++; }
  std::printf("u15song: %d frames, %d tracks with sound, %d effects nodes (tracks and the master), %d FLX/GAT/RTG lane events, %d FXO/wake switch events, %d FXV events\n", N, tracksIn, nSlots, nFxc, nSw, fxp);
  check(nSlots >= 5 && nFxc >= 18, "the song has Flex, Step gate and Retrigger on several nodes and their commands reach them");
  // the bytes Node is given, and their hash
  std::string eventsJson = "[";
  for (size_t i = 0; i < evs.size(); i++) { char b[200]; std::snprintf(b, sizeof b, "%s{\"q\":%d,\"ty\":%d,\"tr\":%d,\"p\":%d,\"t\":%.17g,\"v\":%.17g}", i ? "," : "", evs[i].q, evs[i].type, evs[i].track, evs[i].p, evs[i].t, evs[i].v); eventsJson += b; }
  eventsJson += "]";
  std::string meta = "{\"sr\":48000,\"frames\":" + std::to_string(N) + ",\"q\":" + std::to_string(Q) + ",\"tracks\":[";
  std::vector<float> inAll, bpAll; bool first = true, firstB = true; std::string bpTracks = "[";
  for (int t = 0; t <= NT; t++) if (!cap.in[t][0].empty()) { meta += (first ? "" : ",") + std::to_string(t); first = false; inAll.insert(inAll.end(), cap.in[t][0].begin(), cap.in[t][0].end()); inAll.insert(inAll.end(), cap.in[t][1].begin(), cap.in[t][1].end()); }
  for (int t = 0; t <= NT; t++) if (!cap.bp[t].empty()) { bpTracks += (firstB ? "" : ",") + std::to_string(t); firstB = false; bpAll.insert(bpAll.end(), cap.bp[t].begin(), cap.bp[t].end()); }
  meta += "],\"bpTracks\":" + bpTracks + "]}";
  uint64_t inHash = kFnv0; inHash = fnvAdd(inHash, inAll.data(), inAll.size() * 4); inHash = fnvAdd(inHash, bpAll.data(), bpAll.size() * 4); inHash = fnvAdd(inHash, eventsJson.data(), eventsJson.size());
  std::vector<float> out((size_t)N * 2); std::memcpy(out.data(), aL.data(), (size_t)N * 4); std::memcpy(out.data() + N, aR.data(), (size_t)N * 4);
  const uint64_t outHash = fnvAdd(kFnv0, out.data(), out.size() * 4);
  std::printf("u15song: input hash 0x%016llxull, output hash 0x%016llxull\n", (unsigned long long)inHash, (unsigned long long)outHash);
  if (!dumpDir.empty()) {
    bool ok = writeFile(dumpDir + "/meta.json", meta.data(), meta.size()) && writeFile(dumpDir + "/events.json", eventsJson.data(), eventsJson.size()) && writeFile(dumpDir + "/in.f32", inAll.data(), inAll.size() * 4)
      && writeFile(dumpDir + "/bp.f32", bpAll.data(), bpAll.size() * 4) && writeFile(dumpDir + "/cpp.f32", out.data(), out.size() * 4);
    check(ok, "dump files written");
    return fails ? 1 : 0;   // a dump run only makes the fixture; the golden and Node checks are the other ctest entries
  }
  // 3. the committed golden (Node's render on Linux x86-64): the gate on every platform
  check(inHash == kU15SongInHash, "input hash (voices, sequencer events, clock) equals the golden's: this platform feeds the effects what Linux does");
  check(outHash == kU15SongOutHash && N == kU15SongFrames, "the render equals the committed golden (made by the real JS in Node), bit for bit");
  // 4. the local Node run, where there is one
  if (!refDir.empty()) {
    std::string ref; if (!readFile(refDir + "/ref.f32", &ref) || ref.size() != (size_t)N * 8) { check(false, "Node's reference render is missing or the wrong size"); }
    else {
      const float* J = (const float*)ref.data(); long nd = 0, first = -1; double pk = 0, md = 0; float gf = 0, wf = 0;
      for (long i = 0; i < (long)N * 2; i++) { const float a = out[(size_t)i], r = J[i]; pk = std::max(pk, (double)std::fabs(r));
        if (std::memcmp(&a, &r, 4) == 0 || (a != a && r != r)) continue;
        nd++; md = std::max(md, (double)std::fabs(a - r)); if (first < 0) { first = i; gf = a; wf = r; } }
      char w[200]; std::snprintf(w, sizeof w, "the render equals Node's, bit for bit (%ld of %ld samples differ)", nd, (long)N * 2); check(nd == 0, w);
      if (nd) std::printf("  first difference: channel %c sample %ld (%.4f s): got %.9g want %.9g; largest %.3g = %.1f dB of the reference peak\n", first >= N ? 'R' : 'L', first % N, (double)(first % N) / SR, gf, wf, md, 20 * std::log10(md / (pk > 0 ? pk : 1)));
    }
  }
  std::printf("u15song: %d failed\n", fails);
  return fails ? 1 : 0;
}
