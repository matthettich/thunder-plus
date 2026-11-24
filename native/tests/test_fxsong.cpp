// FX1: the pump and MODFX effect slots of a SONG, through the song reader (core/src/fx/fxsong.cpp), FxHost and FxBank.
//  1. THE READER against the page's own code (golden/fxsong_cases.json, made by tools/fxsong-dump.mjs from index.html's MODFX track-effect loop, normTfx, pumpClamp, pumpNorm1 and
//     tfxChain's `pv`, over the pinned pump's tables): every MODFX type's bytes and module values bit-exact for four settings, every pump type's `pv` for four settings.
//  2. A MODFX SLOT THROUGH THE HOST: FxHost::planFromSong + process, equal bit for bit to the module (modfx::Module, itself golden against the JS in test_modfx) driven directly with the
//     values the golden gives, as the worklet's modRun drives it (inputs, the anti-denormal dither, the +-8 clip).
//  3. A PUMP SLOT THROUGH THE HOST: the song reader's entry equals the hand-made entry of the pump tests (test_pump's entriesOf: pt, pv, bi, cr, clk) bit for bit through process();
//     pump itself is golden against the real pump.wasm in test_pump.
//  4. Slots the host does not play are COUNTED, never silent (a Source / SC FROM, NOTES FROM, an unknown type); an empty slot is not; the master's pump latency as pass() adds it.
//  5. FXV on a MODFX slot, and every slot of ELECTRO played (25 of 25).
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "fx/fxbank.h"
#include "fx/fxhost.h"
#include "fx/pumpfx.h"
#include "modfx/modfx.h"
#include "rt/json.h"
#include "seq/seq.h"
#include "song/song.h"

using namespace thunder;

static int fails = 0;
static void check(bool ok, const std::string& what) { if (!ok) { std::printf("FAIL: %s\n", what.c_str()); fails++; } }
static bool readFile(const std::string& path, std::string* out) { std::ifstream f(path, std::ios::binary); if (!f) return false; std::stringstream ss; ss << f.rdbuf(); *out = ss.str(); return true; }
static bool sameBits(double a, double b) { return std::memcmp(&a, &b, 8) == 0; }
static bool sameBitsF(float a, float b) { return std::memcmp(&a, &b, 4) == 0; }

struct Noise { uint32_t s = 12345; double next() { s = s * 1664525u + 1013904223u; return (s >> 8) / 16777216.0 - 0.5; } };
// one slot object as a one-element slot array (what planFromSong reads)
static bool slotArray(Json slot, JsonDoc* doc) { return doc->parse("[" + json_write(slot) + "]"); }

int main() {
  const std::string root = THUNDER_ROOT;
  std::string gtext; if (!readFile(root + "/native/tests/golden/fxsong_cases.json", &gtext)) { std::printf("FAIL: golden/fxsong_cases.json is missing\n"); return 1; }
  JsonDoc gdoc; if (!gdoc.parse(gtext)) { std::printf("FAIL: fxsong_cases.json is not JSON\n"); return 1; }
  Json G = gdoc.root(), MOD = G.arr("mod"), PUMP = G.arr("pump");
  const bool havePump = pump::available();

  // ---- 1. the reader: MODFX
  int nModOk = 0, nModRefused = 0;
  for (size_t i = 0; i < MOD.size(); i++) {
    Json c = MOD.at(i); const std::string type(c.str("type")); const std::string tag = type + " set " + std::to_string((int)c.num("set"));
    FxEntry e; int why = -1; const bool ok = FxHost::entryFromSlot(c.obj("slot"), 3, 48000, 120, 0, &e, nullptr, &why);
    Json bytes = c.arr("bytes"), mp = c.arr("mp"), names = c.arr("names"); bool sck = false;
    for (size_t k = 0; k < names.size(); k++) if (names.at(k).asStr() == "sck" && bytes.at(k).asNum() > 0) sck = true;
    if (sck) { check(!ok && why == kRefSidechain, tag + ": a Source other than Self is refused as a sidechain"); nModRefused++; continue; }
    check(ok, tag + ": entry made"); if (!ok) continue;
    check(e.t == fx_mod_index(type) && e.slot == 3 && e.on == c.boolean("on", true) && e.nmp == (int)mp.size(), tag + ": kind / slot / on / count");
    for (size_t k = 0; k < mp.size() && (int)k < e.nmp; k++) { check(sameBits(e.mb[k], bytes.at(k).asNum()), tag + ": byte " + std::to_string(k)); check(sameBits(e.mp[k], mp.at(k).asNum()), tag + ": value " + names.at(k).asStr().data()); }
    nModOk++;
  }
  // ---- 1. the reader: pump
  int nPumpOk = 0;
  for (size_t i = 0; i < PUMP.size(); i++) {
    Json c = PUMP.at(i); const std::string type(c.str("type")); const std::string tag = "p" + type + " set " + std::to_string((int)c.num("set"));
    FxEntry e; int why = -1; const bool ok = FxHost::entryFromSlot(c.obj("slot"), 2, 48000, 120, 0, &e, nullptr, &why);
    check(ok, tag + ": entry made"); if (!ok) continue;
    Json pv = c.arr("pv"); int pt = -1; for (int k = 0; k < kPumpTypes; k++) if (type == fx_pump_key(k)) pt = k;
    check(pt >= 0 && e.t == kPumpT0 + pt && e.slot == 2 && e.npv == (int)pv.size() && e.on == c.obj("slot").boolean("on", true), tag + ": kind / slot / count / on");
    check(e.bi == fx_pump_bypass(pt) && e.cr == fx_pump_creative(pt) && e.clk == fx_pump_clocked(pt) && e.sck == 0, tag + ": bi / cr / clk / sck");
    for (size_t k = 0; k < pv.size() && (int)k < e.npv; k++) if (!sameBitsF(e.pv[k], (float)pv.at(k).asNum())) { check(false, tag + ": pv " + std::to_string(k) + " (" + fx_pump_param_name(pt, (int)k) + ")"); break; }
    nPumpOk++;
  }
  std::printf("reader: %d MODFX entries equal the page's (%d sidechain ones refused), %d pump entries\n", nModOk, nModRefused, nPumpOk);
  check(nModOk > 100 && nPumpOk == 100, "all cases read");

  // ---- 2. a MODFX slot through the host, against the module driven directly
  int nHostMod = 0; const int NB = 80;
  for (size_t i = 0; i < MOD.size(); i++) {
    Json c = MOD.at(i); const std::string type(c.str("type")); const int set = (int)c.num("set"); if (set != 0 && set != 1 && set != 2) continue;
    Json bytes = c.arr("bytes"), mp = c.arr("mp"), names = c.arr("names"); bool sck = false;
    for (size_t k = 0; k < names.size(); k++) if (names.at(k).asStr() == "sck" && bytes.at(k).asNum() > 0) sck = true;
    if (sck) continue;
    const std::string tag = type + " set " + std::to_string(set);
    JsonDoc sd; check(slotArray(c.obj("slot"), &sd), tag + ": slot text"); FxHost H(48000);
    modfx::SeedCounter seeds1; std::unique_ptr<FxPlan> pl(H.planFromSong(sd.root(), 120, 0, &seeds1)); check(pl && pl->size() == 1, tag + ": planned"); if (!pl || pl->size() != 1) continue;
    H.commit(pl.get());
    // the reference: the module itself
    const modfx::Def* def = modfx::find(type.c_str()); check(def != nullptr, tag + ": module"); if (!def) continue;
    modfx::SeedCounter seeds2; std::unique_ptr<modfx::Module> mod = def->make(48000, seeds2);
    std::vector<double> P((size_t)def->np); for (int j = 0; j < def->np; j++) { P[(size_t)j] = def->params[j].def; for (size_t k = 0; k < names.size(); k++) if (names.at(k).asStr() == def->params[j].name) P[(size_t)j] = mp.at(k).asNum(); }
    const int B = modfx::kBlock; std::vector<float> buf((size_t)(3 + def->nout) * (size_t)B, 0.f); modfx::IO io; int inL = -1, inR = -1, outL = 0, outR = -1;
    for (int k = 0; k < def->nin; k++) { io.in[k] = buf.data(); io.linked[k] = false; if (!std::strcmp(def->in[k].name, "in")) inL = k; if (!std::strcmp(def->in[k].name, "inR")) inR = k; }
    for (int k = 0; k < def->nout; k++) { io.out[k] = buf.data() + (size_t)(3 + k) * B; if (!std::strcmp(def->out[k].name, "out")) outL = k; if (!std::strcmp(def->out[k].name, "outR")) outR = k; }
    if (inL >= 0) io.in[inL] = buf.data() + B; if (inR >= 0) { io.in[inR] = buf.data() + 2 * B; io.linked[inR] = true; }
    io.p = P.data(); modfx::Ctx ctx; ctx.sr = 48000; ctx.block = B; ctx.tempo = 120; ctx.lpb = 4; ctx.ticks = 6; ctx.playing = false; ctx.startN = 0;
    Noise nz; bool eq = true; long firstBad = -1; double energy = 0;
    for (int b = 0; b < NB && eq; b++) {
      float L[128], R[128], hl[128], hr[128]; const double amp = (b / 10) % 3 == 2 ? 0.0 : 0.6;   // bursts and silence
      for (int k = 0; k < B; k++) { L[k] = (float)(nz.next() * 2 * amp); R[k] = (float)(nz.next() * 2 * amp); hl[k] = L[k]; hr[k] = R[k]; }
      H.process(hl, hr, B, (double)b * B / 48000);
      for (int k = 0; k < B; k += 2) { L[k] = (float)((double)L[k] + 1e-18); L[k + 1] = (float)((double)L[k + 1] - 1e-18); R[k] = (float)((double)R[k] + 1e-18); R[k + 1] = (float)((double)R[k + 1] - 1e-18); }   // the host's anti-denormal dither
      if (inL >= 0) std::memcpy(buf.data() + B, L, sizeof L); if (inR >= 0) std::memcpy(buf.data() + 2 * B, R, sizeof R);
      mod->run(io, ctx); const float* oL = io.out[outL]; const float* oR = outR >= 0 ? io.out[outR] : oL;
      float el[128], er[128]; bool bad = false; for (int k = 0; k < B; k++) if (!(oL[k] > -64 && oL[k] < 64 && oR[k] > -64 && oR[k] < 64)) bad = true;
      for (int k = 0; k < B; k++) { el[k] = bad ? L[k] : oL[k]; er[k] = bad ? R[k] : oR[k]; if (el[k] > 8) el[k] = 8; else if (el[k] < -8) el[k] = -8; if (er[k] > 8) er[k] = 8; else if (er[k] < -8) er[k] = -8; }
      // (the slot naps after a long silence: the host outputs silence for what the module would still ring; that needs 30 s, far past these blocks)
      for (int k = 0; k < B; k++) { energy += std::fabs(hl[k]); if (!sameBitsF(hl[k], el[k]) || !sameBitsF(hr[k], er[k])) { eq = false; firstBad = (long)b * B + k; break; } }
    }
    check(eq, tag + ": through the host equals the module" + (eq ? "" : " (first difference at frame " + std::to_string(firstBad) + ")"));
    if (set == 0) check(energy > 0, tag + ": it made sound"); nHostMod++;   // (a control at its lowest may well be silent)
  }
  std::printf("MODFX through the host: %d slots bit-equal to the module\n", nHostMod);

  // ---- 3. a pump slot through the host: the reader's entry against the hand-made one
  int nHostPump = 0;
  if (!havePump) std::printf("pump through the host: not run (no pump library in this build)\n");
  for (size_t i = 0; i < PUMP.size() && havePump; i++) {
    Json c = PUMP.at(i); const std::string type(c.str("type")); const int set = (int)c.num("set"); if (set != 0 && set != 3) continue;
    const std::string tag = "p" + type + " set " + std::to_string(set); int pt = -1; for (int k = 0; k < kPumpTypes; k++) if (type == fx_pump_key(k)) pt = k;
    JsonDoc sd; check(slotArray(c.obj("slot"), &sd), tag + ": slot text");
    FxHost A(48000), Bh(48000);
    std::unique_ptr<FxPlan> pa(A.planFromSong(sd.root(), 120, 0)); check(pa && pa->size() == 1, tag + ": planned"); if (!pa || pa->size() != 1) continue; A.commit(pa.get());
    FxEntry e; e.t = kPumpT0 + pt; e.slot = 0; std::snprintf(e.key, kFxKey, "0:p%s", type.c_str()); e.on = c.obj("slot").boolean("on", true); e.bi = fx_pump_bypass(pt); e.cr = fx_pump_creative(pt); e.clk = fx_pump_clocked(pt);
    Json pv = c.arr("pv"); e.npv = (int)std::min<size_t>(pv.size(), kPumpMaxParams); for (int k = 0; k < e.npv; k++) e.pv[k] = (float)pv.at((size_t)k).asNum();
    std::unique_ptr<FxPlan> pb(Bh.plan(&e, 1)); Bh.commit(pb.get());
    check(A.slotCount() == 1 && Bh.slotCount() == 1 && A.slotLatency(0) == Bh.slotLatency(0), tag + ": same latency");
    Noise nz; bool eq = true; double energy = 0;
    for (int b = 0; b < 40 && eq; b++) { float L[128], R[128], l2[128], r2[128]; for (int k = 0; k < 128; k++) { L[k] = l2[k] = (float)(nz.next() * 0.8); R[k] = r2[k] = (float)(nz.next() * 0.8); }
      A.process(L, R, 128, b * 128.0 / 48000); Bh.process(l2, r2, 128, b * 128.0 / 48000);
      for (int k = 0; k < 128; k++) { energy += std::fabs(L[k]); if (!sameBitsF(L[k], l2[k]) || !sameBitsF(R[k], r2[k])) { eq = false; break; } } }
    check(eq, tag + ": the reader's entry plays as the hand-made one"); check(energy > 0, tag + ": it made sound"); nHostPump++;
  }
  if (havePump) std::printf("pump through the host: %d slots equal the hand-made entries\n", nHostPump);

  // ---- 4. slots the host does not play are counted
  { JsonDoc d; d.parse("[{\"type\":\"glue\",\"on\":true,\"sck\":2},{\"type\":\"pcomp\",\"on\":true,\"sck\":3},{\"type\":\"pvox\",\"on\":true,\"nin\":2},{\"type\":\"zzz\"},null,{\"type\":\"plate\",\"on\":true}]");
    FxHost H(48000); std::unique_ptr<FxPlan> p(H.planFromSong(d.root(), 120, 0)); H.commit(p.get());
    check(H.slotCount() == 1, "only the plate is played of the six");
    check(H.refused(kRefSidechain) == 2 && H.refused(kRefNotesFrom) == 1 && H.refused(kRefUnknown) == 1 && H.refused(kRefNoEngine) == 0, "refusals counted by reason (an empty slot is not one)");
    FxEntry e; int why = -1; FxSlotText t; JsonDoc d2; d2.parse("{\"type\":\"pmaster\",\"on\":true,\"pst\":{\"analysis\":\"abc;lufs=-10\"}}");
    check(FxHost::entryFromSlot(d2.root(), 0, 48000, 120, 0, &e, &t, &why) && e.pst && t.n == 1 && t.key[0] == "analysis" && t.val[0] == "abc;lufs=-10", "MASTER's analysis text is handed on");
    JsonDoc d3; d3.parse("{\"type\":\"ptune\",\"on\":true,\"pst\":{\"notes\":\"n\",\"curve\":\"c\"}}");
    check(FxHost::entryFromSlot(d3.root(), 0, 48000, 120, 0, &e, &t, &why) && t.n == 1 && t.key[0] == "notes", "TUNE's notes are handed on, its curve is the window's own"); }

  // ---- 4b. the master's pump latency (pass(): pumpChainLatency(S.mfx)): a slot counts when it is ON or has a bypass of its own
  if (havePump) {
    FxBank bank(48000); JsonDoc d; d.parse("[{\"type\":\"plimit\",\"on\":true},{\"type\":\"pcalm\",\"on\":false},{\"type\":\"pmeter\",\"on\":false},{\"type\":\"plate\",\"on\":true}]");
    FxHost& M = bank.host(kSongNT); std::unique_ptr<FxPlan> p(M.planFromSong(d.root(), 120, 0)); M.commit(p.get());
    int64_t want = 0; for (int i = 0; i < M.slotCount(); i++) { const bool pm = fx_is_pump(M.slotType(i)); if (pm && (M.slotOn(i) || M.slotBypass(i))) want += (int64_t)M.slotLatency(i); }
    check(M.slotCount() == 4 && bank.masterPumpLatency() == want && want > 0, "master pump latency = the on / bypassable pump slots' latencies (" + std::to_string(want) + ")");
    check(M.slotLatency(2) >= 0 && !M.slotBypass(2), "METER has no bypass: off, it adds nothing");
  }

  // ---- 5. FXV on a MODFX slot, and ELECTRO played whole
  std::string text; if (!readFile(root + "/sounds/ELECTRO.thunder", &text)) { std::printf("FAIL: ELECTRO is missing\n"); return 1; }
  Song song; JsonError je; check(song.load(text.data(), text.size(), &je), "ELECTRO loads");
  { FxBank bank(48000); bank.load(song, song.bpm); int hosted = 0, held = 0;
    for (int t = 0; t <= kSongNT; t++) hosted += bank.host(t).slotCount();
    for (int t = 0; t < kSongNT; t++) { Json a = song.tfx.at((size_t)t); for (size_t k = 0; k < a.size(); k++) if (a.at(k).isObj()) held++; }
    for (size_t k = 0; k < song.mfx.size(); k++) if (song.mfx.at(k).isObj()) held++;
    std::printf("ELECTRO: %d of %d effect slots hosted; refused: sidechain %u, notes %u, no engine %u, unknown %u\n", hosted, held, bank.refused(kRefSidechain), bank.refused(kRefNotesFrom), bank.refused(kRefNoEngine), bank.refused(kRefUnknown));
    if (!havePump) { int nonPump = 0, pumpSlots = 0; auto cnt = [&](Json o) { Json ty = o.get("type"); bool pump = false; if (ty.isStr() && ty.asStr().size() > 1 && ty.asStr()[0] == 'p') for (int pt = 0; pt < kPumpTypes; pt++) if (ty.asStr().substr(1) == fx_pump_key(pt)) pump = true; (pump ? pumpSlots : nonPump)++; };
      for (int t = 0; t < kSongNT; t++) { Json a = song.tfx.at((size_t)t); for (size_t k = 0; k < a.size(); k++) if (a.at(k).isObj()) cnt(a.at(k)); }
      for (size_t k = 0; k < song.mfx.size(); k++) if (song.mfx.at(k).isObj()) cnt(song.mfx.at(k));
      std::printf("ELECTRO without pump: %d non-pump slots, %d pump slots\n", nonPump, pumpSlots);
      check(hosted >= nonPump, "no pump here: every non-pump slot of ELECTRO is still hosted"); check((int)bank.refused(kRefNoEngine) == pumpSlots, "no pump here: exactly the pump slots are refused as no-engine"); }
    check(held == 25 && (havePump ? hosted == held : hosted + (int)bank.refused(kRefNoEngine) == held), "every effect slot of ELECTRO is played (25 of 25)" + std::string(havePump ? "" : " (no pump here: its pump slots are refused and counted)")); }
  { // FXV: a Plate slot's DECAY control moved by the sequencer's event equals the slot planned with that byte
    JsonDoc d; d.parse("[{\"type\":\"plate\",\"on\":true,\"size\":128,\"decay\":100,\"damp\":128,\"pre\":10,\"mod\":77,\"mix\":200}]");
    JsonDoc d2; d2.parse("[{\"type\":\"plate\",\"on\":true,\"size\":128,\"decay\":200,\"damp\":128,\"pre\":10,\"mod\":77,\"mix\":200}]");
    Song s2; JsonError e2; std::string t2 = text; check(s2.load(t2.data(), t2.size(), &e2), "song for FXV");
    FxBank bank(48000); FxHost& H = bank.host(1); std::unique_ptr<FxPlan> p(H.planFromSong(d.root(), 120, 0)); H.commit(p.get());
    FxHost ref(48000); std::unique_ptr<FxPlan> p2(ref.planFromSong(d2.root(), 120, 0)); ref.commit(p2.get());
    Mixer mix(48000, 128); SeqEvent ev; ev.type = SEV_FXP; ev.track = 1; ev.slot = 0; ev.pi = 1; ev.v = 200; ev.t = 0;   // control 1 = DECAY; the byte 200 (q.min 0 + round(255 * 200 / 255))
    check(bank.event(ev, 0, mix), "FXV on a MODFX slot is taken");
    Noise nz; bool eq = true; for (int b = 0; b < 30 && eq; b++) { float L[128], R[128], l2[128], r2[128]; for (int k = 0; k < 128; k++) { L[k] = l2[k] = (float)(nz.next() * 0.8); R[k] = r2[k] = (float)(nz.next() * 0.8); }
      H.process(L, R, 128, b * 128.0 / 48000); ref.process(l2, r2, 128, b * 128.0 / 48000);
      for (int k = 0; k < 128; k++) if (!sameBitsF(L[k], l2[k]) || !sameBitsF(R[k], r2[k])) { eq = false; break; } }
    check(eq, "FXV moves the control as planning with the new byte does");
    SeqEvent pe; pe.type = SEV_FXP; pe.track = 1; pe.slot = 0; pe.pi = 0; pe.v = 10; JsonDoc dp; dp.parse("[{\"type\":\"peq\",\"on\":true}]");
    FxHost& H2 = bank.host(2); if (havePump) { std::unique_ptr<FxPlan> pp(H2.planFromSong(dp.root(), 120, 0)); H2.commit(pp.get()); pe.track = 2; check(!bank.event(pe, 0, mix) && bank.fxvRefused() == 1, "FXV on a pump slot is refused and counted"); } }

  std::printf("%s\n", fails ? "test_fxsong: FAILED" : "test_fxsong: ok");
  return fails ? 1 : 0;
}
