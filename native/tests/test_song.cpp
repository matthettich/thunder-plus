// U06: the JSON reader / writer and the song reader (ctest `song`).
//   1. sounds/ELECTRO.thunder: typed values and hashes against native/tools/song-golden.mjs (golden/song_golden.inc).
//   2. The three shapes (file, bare S, song.load message) give the same tables; the build guard; the core takes the message.
//   3. write(parse(text)) equals JSON.stringify(JSON.parse(text)) for ELECTRO and every golden .json under 5 MB.
//   4. Unknown fields at three depths come back; duplicate keys, number text and string escapes behave as documented.
//   5. Truncated and malformed text fails cleanly; reading a parsed tree allocates nothing.
//   6. Older songs (v < 6, v < 3) are converted as normSong does.
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <new>
#include <sstream>
#include <string>
#include <vector>

#include "rt/json.h"
#include "song/song.h"
#include "thunder/core.h"

#include "golden/song_golden.inc"

using namespace thunder;

static int fails = 0;
#define CHECK(c, msg) do { if (!(c)) { std::printf("FAIL: %s (line %d)\n", msg, __LINE__); fails++; } } while (0)

// every allocation is counted, so a test can show that reading a parsed tree makes none
static std::atomic<long> g_allocs{0};
void* operator new(size_t n) { g_allocs++; void* p = std::malloc(n ? n : 1); if (!p) std::abort(); return p; }
void* operator new[](size_t n) { g_allocs++; void* p = std::malloc(n ? n : 1); if (!p) std::abort(); return p; }
void* operator new(size_t n, const std::nothrow_t&) noexcept { g_allocs++; return std::malloc(n ? n : 1); }
void* operator new[](size_t n, const std::nothrow_t&) noexcept { g_allocs++; return std::malloc(n ? n : 1); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }
void operator delete[](void* p, size_t) noexcept { std::free(p); }

struct Fnv { uint64_t h = 0xcbf29ce484222325ull;
  void byte(unsigned char b) { h ^= b; h *= 0x100000001b3ull; }
  void i32(int32_t x) { unsigned char b[4]; std::memcpy(b, &x, 4); for (int i = 0; i < 4; i++) byte(b[i]); }
  void f64(double x) { unsigned char b[8]; std::memcpy(b, &x, 8); for (int i = 0; i < 8; i++) byte(b[i]); }
  void bytes(const std::string& s) { for (unsigned char c : s) byte(c); } };

// A checkout may have CRLF line endings (Windows, autocrlf). JSON cannot hold a raw CR inside a string, so dropping every CR
// gives the bytes the goldens were made from; whitespace outside strings never reaches the writer's output anyway.
static std::string readFile(const std::string& p) { std::ifstream f(p, std::ios::binary); std::ostringstream o; o << f.rdbuf(); std::string t = o.str(), u; u.reserve(t.size()); for (char c : t) if (c != '\r') u.push_back(c); return u; }

static void checkSong(const Song& s, const char* what) {
  std::string w = what; auto C = [&](bool ok, const char* m) { if (!ok) { std::printf("FAIL: %s: %s\n", w.c_str(), m); fails++; } };
  C(s.v == kSong_v && s.bpm == kSong_bpm && s.ntr == kSong_ntr && s.lpb == kSong_lpb && s.ticks == kSong_ticks && s.dlen == kSong_dlen, "bpm / ntr / lpb / ticks / dlen / v");
  C(s.swing == kSong_swing && s.swingB == kSong_swingB && s.master == kSong_master && s.mout == kSong_mout && s.ni == kSong_ni, "swing, swingB, master, mout, ni");
  C(s.phrasesPresent() == kSong_phNonNull, "phrases present"); C(s.phrasesWithContent() == kSong_phNonEmpty, "phrases with content");
  { Fnv H; for (int i = 0; i < kNPH; i++) { const int16_t* p = s.phrase(i); if (!p) { H.i32(0); continue; } H.i32(1); for (int k = 0; k < kPhraseRows * kRW; k++) H.i32(p[k]); } C(H.h == kSong_phHash, "phrase hash"); }
  { Fnv H; int n = 0; for (int y = 0; y < kSongRows; y++) for (int t = 0; t <= kSongNT; t++) { int c = s.cell(y, t); H.i32(c); if (c >= 0) n++; } C(H.h == kSong_gridHash, "song grid hash"); C(n == kSong_gridCells, "song grid cells"); }
  { Fnv H; int n = 0; for (int i = 0; i < kNPH; i++) { const int16_t* c = s.chain(i); if (!c) { H.i32(0); continue; } n++; H.i32(1); for (int k = 0; k < kChainRows * 2; k++) H.i32(c[k]); } C(H.h == kSong_chHash, "chain hash"); C(n == kSong_chNonNull, "chains present"); }
  { Fnv H, B; long sum = 0; for (int i = 0; i < kNPH; i++) { H.i32(s.plen[(size_t)i]); sum += s.plen[(size_t)i]; B.i32(s.plpb[(size_t)i]); } C(H.h == kSong_plenHash && sum == kSong_plenSum, "plen"); C(B.h == kSong_plpbHash, "plpb"); }
  { Fnv H; for (int y = 0; y < kSongRows; y++) H.i32(s.smute[(size_t)y]); C(H.h == kSong_smuteHash, "smute"); }
  { Fnv H; bool same = true; for (int t = 0; t < kSongNT; t++) { const SongMix& m = s.mix[t]; H.i32(m.vol); H.i32(m.trim); H.i32(m.pan); H.i32(m.mute); H.i32(m.solo);
      if (m.vol != kSong_mix[t][0] || m.trim != kSong_mix[t][1] || m.pan != kSong_mix[t][2] || (int)m.mute != kSong_mix[t][3] || (int)m.solo != kSong_mix[t][4]) same = false; }
    C(H.h == kSong_mixHash && same, "mix"); }
  { Fnv H; bool pads = true; for (int i = 0; i < kNI; i++) { const SongInst& I = s.insts[i];
      const int a[15] = {I.pad, I.tsp, I.vol, I.pan, I.rev, I.ofs, I.slc, I.smode, I.sync, I.beats, I.grain, I.mo, I.mch, I.mpt, I.pbr}; for (int x : a) H.i32(x);
      for (int k = 0; k < 4; k++) H.i32(I.cc[k]);
      const int b[15] = {I.lcc, I.lsh, I.ldp, I.lct, I.lrg, I.kf, I.hut, I.huv, I.mlen, I.eco, I.ecf, I.ecp, I.mrng, I.gtype, I.gcen}; for (int x : b) H.i32(x);
      H.f64(I.lrb); H.f64(I.ecr); if (I.pad != kSong_pads[i]) pads = false; }
    C(H.h == kSong_instHash, "inst hash"); C(pads, "inst pads"); }
  { Fnv H; for (int k = 0; k < 12; k++) H.f64(s.tune[k]); C(H.h == kSong_tuneHash, "tune"); }
  C(s.perf.missing() == !kSong_hasPerf, "perf pointer"); C(s.chainPatch.isObj() == (kSong_hasChainPatch != 0), "chainPatch pointer");
  C(s.tfx.isArr() && s.mfx.isArr() && s.mst.isObj(), "tfx / mfx / mst pointers");
}

static void testElectro(const std::string& root) {
  const std::string text = readFile(root + "/sounds/ELECTRO.thunder");
  CHECK(text.size() == 856625, "ELECTRO.thunder read");
  Song* s = new Song(); JsonError e;
  CHECK(s->load(text.data(), text.size(), &e), "ELECTRO loads");
  CHECK(s->isFile && s->build == kSong_build && !s->newerBuild, "file shape, build 296 is not newer");
  CHECK(s->kit.isObj() && (int)s->kit.arr("voices").size() == kSong_nVoices, "kit pointer, 40 voices");
  checkSong(*s, "file");
  // the bare S and the message shape
  const std::string sText = json_write(s->S);
  { Song b; CHECK(b.load(sText.data(), sText.size(), &e), "bare S loads"); CHECK(!b.isFile && b.rev == 0, "bare S: not a file, rev 0"); checkSong(b, "bare S"); }
  { const std::string msg = "{\"t\":\"song.load\",\"rev\":7,\"S\":" + sText + "}"; Song m; CHECK(m.load(msg.data(), msg.size(), &e), "message loads"); CHECK(m.rev == 7 && !m.isFile, "message: rev 7"); checkSong(m, "message"); }
  // the version guard
  { std::string t2 = text; const size_t at = t2.find("\"build\":296"); CHECK(at != std::string::npos, "build field found"); t2.replace(at, 11, "\"build\":9999");
    Song n; CHECK(n.load(t2.data(), t2.size(), &e), "a newer build still loads"); CHECK(n.newerBuild && n.build == 9999, "newer build sets the flag"); checkSong(n, "newer build"); }
  // reading allocates nothing
  { const long before = g_allocs.load(); double acc = 0; size_t cnt = 0;
    struct W { static void walk(Json v, double& acc, size_t& cnt) { cnt++; if (v.isNum()) acc += v.asNum(); else if (v.isArr()) for (size_t i = 0; i < v.size(); i++) walk(v.at(i), acc, cnt); else if (v.isObj()) for (size_t i = 0; i < v.size(); i++) { acc += (double)v.keyAt(i).size(); walk(v.valAt(i), acc, cnt); } } };
    W::walk(s->root, acc, cnt); acc += s->S.num("bpm"); acc += (double)s->S.str("name").size(); acc += s->S.arr("insts").at(3).num("pad");
    CHECK(g_allocs.load() == before, "walking and looking up in a parsed tree allocates nothing"); CHECK(cnt > 100000, "the walk saw the whole tree"); (void)acc; }
  // the core takes the message and acknowledges it; a malformed one queues nothing
  { thunder_core* c = thunder_create(48000, 256); const std::string msg = "{\"t\":\"song.load\",\"rev\":11,\"S\":" + sText + "}";
    CHECK(thunder_post(c, msg.data(), (int)msg.size()) == 1, "core: song.load posted");
    const std::string cut = msg.substr(0, msg.size() / 2); CHECK(thunder_post(c, cut.data(), (int)cut.size()) == 0, "core: a truncated song.load returns 0");
    const std::string junk = "{\"t\":\"song.load\",\"rev\":12,\"S\":{\"bpm\":}}"; CHECK(thunder_post(c, junk.data(), (int)junk.size()) == 0, "core: a malformed song.load returns 0");
    float l[256], r[256]; thunder_process(c, l, r, 256);
    char buf[256]; int songs = 0; long rev = -1; for (int n; (n = thunder_poll(c, buf, sizeof buf)) > 0;) if (std::strstr(buf, "\"t\":\"song\"")) { songs++; const char* p = std::strstr(buf, "\"rev\":"); if (p) rev = std::atol(p + 6); }
    CHECK(songs == 1 && rev == 11, "core: exactly one song event, rev 11"); thunder_destroy(c); }
  delete s;
}

static void testRoundTrip(const std::string& root) {
  for (int i = 0; i < kSongRoundTripN; i++) {
    const std::string text = readFile(root + "/" + kSongRoundTrip[i].file); JsonDoc d; JsonError e;
    if (!d.parse(text, &e)) { std::printf("FAIL: %s does not parse (offset %zu: %s)\n", kSongRoundTrip[i].file, e.offset, e.msg); fails++; continue; }
    const std::string out = json_write(d.root()); Fnv H; H.bytes(out);
    if (out.size() != kSongRoundTrip[i].len || H.h != kSongRoundTrip[i].hash) { std::printf("FAIL: %s: write(parse(text)) differs from JSON.stringify(JSON.parse(text)) (%zu bytes, expected %llu)\n", kSongRoundTrip[i].file, out.size(), kSongRoundTrip[i].len); fails++; }
  }
}

static void testUnknown() {
  // unknown fields at three depths, in a file, an S, an instrument, a mix entry
  const std::string t = "{\"app\":\"thunder-project\",\"v\":1,\"build\":616,\"zTop\":{\"deep\":[1,2,{\"x\":null}]},\"song\":{\"app\":\"thunder-song\",\"v\":6,\"bpm\":100,\"future\":{\"a\":{\"b\":{\"c\":[true,false,\"s\"]}}},"
    "\"insts\":[{\"pad\":3,\"newField\":{\"k\":[1.50,2e3,-0.0]}}],\"mix\":[{\"vol\":200,\"extra\":\"e\xc3\xa9\"}]},\"kit\":{\"app\":\"thunder\",\"x\":{\"y\":{\"z\":1}}}}";
  Song s; JsonError e; CHECK(s.load(t.data(), t.size(), &e), "unknown-fields file loads");
  CHECK(s.bpm == 100 && s.insts[0].pad == 3 && s.mix[0].vol == 200 && s.isFile, "known fields read beside the unknown ones");
  CHECK(json_write(s.root) == t, "unknown fields at three depths come back byte for byte (number text kept: 1.50, 2e3, -0.0)");
  CHECK(s.S.obj("future").obj("a").obj("b").arr("c").size() == 3, "unknown fields stay reachable in the doc");
}

static void testJson() {
  JsonDoc d; JsonError e;
  CHECK(d.parse("{\"a\":1,\"a\":2,\"b\":[]}", &e), "duplicate keys parse"); Json r = d.root();
  CHECK(r.size() == 2 + 1 && r.num("a") == 2 && r.keyAt(0) == "a" && r.keyAt(1) == "a", "duplicates kept in order; get() gives the last");
  CHECK(json_write(r) == "{\"a\":1,\"a\":2,\"b\":[]}", "duplicates written back as they came");
  CHECK(r.num("zz", 5) == 5 && r.str("a", "d") == "d" && r.arr("a").missing() && r.get("b").isArr() && r.at(0).missing() && r.get("zz").at(3).num("q", 9) == 9, "defaults and missing values");
  // strings: the escapes JSON.stringify gives (checked against Node: see the report) and lone surrogates
  CHECK(d.parse("[\"\\u00e9\\ud83d\\ude00\\ud800\\n\\u0001\\u2028\\/\\\"\\\\\\u007f\\udc00x\"]", &e), "escapes parse");
  CHECK(json_write(d.root()) == "[\"\xc3\xa9\xf0\x9f\x98\x80\\ud800\\n\\u0001\xe2\x80\xa8/\\\"\\\\\x7f\\udc00x\"]", "strings written as JSON.stringify writes them");
  CHECK(d.parse("[\"a\\u0000b\"]", &e) && d.root().at(0).asStr().size() == 3 && json_write(d.root()) == "[\"a\\u0000b\"]", "an embedded NUL survives");
  CHECK(d.parse("  [1e400, 123456789012345678, 0.1, -0, 5E-1 ] ", &e), "numbers"); { Json a = d.root(); CHECK(std::isinf(a.at(0).asNum()) && a.at(1).asNum() == 123456789012345678.0 && a.at(2).asNum() == 0.1 && a.at(4).asNum() == 0.5, "number values"); CHECK(json_write(a) == "[1e400,123456789012345678,0.1,-0,5E-1]", "number text kept"); }
  CHECK(d.parse("null", &e) && d.root().isNull() && d.parse("true", &e) && d.root().asBool() && d.parse(" \"x\" ", &e) && d.root().asStr() == "x", "scalars at the top");
}

static void expectFail(const char* text, size_t len, long wantOff, const char* what) {
  JsonDoc d; JsonError e; const bool ok = d.parse(text, len, &e);
  if (ok) { std::printf("FAIL: should not parse: %s\n", what); fails++; return; }
  if (wantOff >= 0 && e.offset != (size_t)wantOff) { std::printf("FAIL: %s: error offset %zu, expected %ld (%s)\n", what, e.offset, wantOff, e.msg); fails++; }
  if (e.offset > len || !e.msg) { std::printf("FAIL: %s: bad error record\n", what); fails++; }
  if (d.root().type() != JType::Missing) { std::printf("FAIL: %s: a failed parse left a root\n", what); fails++; }
}
#define EF(s, off) expectFail(s, std::strlen(s), off, s)

static void testMalformed(const std::string& root) {
  EF("", 0); EF("   ", 3); EF("{", 1); EF("[", 1); EF("[1,]", 3); EF("[1 2]", 3); EF("{\"a\":}", 5); EF("{\"a\" 1}", 5); EF("{a:1}", 1); EF("{\"a\":1,}", 7); EF("01", 1); EF("1.", 2); EF("-", 1); EF(".5", 0);
  EF("1e", 2); EF("+1", 0); EF("\"abc", 4); EF("\"\\x\"", 1); EF("\"\\u12\"", 1); EF("tru", 0); EF("nul", 0); EF("[1] x", 4); EF("'a'", 0); EF("\"a\nb\"", 2); EF("NaN", 0); EF("[1,2", 4);
  expectFail("\"\xff\"", 3, 1, "invalid UTF-8 (0xff)"); expectFail("\"\xc0\x80\"", 4, 1, "overlong UTF-8"); expectFail("\"\xed\xa0\x80\"", 5, 1, "UTF-8 surrogate"); expectFail("\"\xe2\x82\"", 4, 1, "truncated UTF-8");
  expectFail("\xef\xbb\xbf{}", 5, 0, "a byte order mark (JSON.parse refuses it too)");
  { std::string deep(100000, '['); expectFail(deep.data(), deep.size(), -1, "100000 levels of nesting"); std::string ok(kJsonMaxDepth, '['); ok += std::string(kJsonMaxDepth, ']'); JsonDoc d; CHECK(d.parse(ok, nullptr), "nesting at the limit parses"); }
  { Song s; JsonError e; CHECK(!s.load("[1,2]", 5, &e), "a song that is not an object is refused"); CHECK(!s.load("{\"bpm\":", 7, &e) && e.offset == 7, "a song with broken JSON is refused with the offset"); }
  // truncations of valid files: every prefix of a short one, a stride over the big one, all fail (a prefix of an object or array is never complete)
  const char* small[] = {"tests/golden/master.json", "tests/golden/chains-modules.json", "tests/golden/waves-processor.json"};
  for (const char* f : small) { std::string t = readFile(root + "/" + f); while (!t.empty() && (t.back() == '\n' || t.back() == ' ' || t.back() == '\t')) t.pop_back();   // a prefix that only drops trailing whitespace is valid JSON: not a truncation
    int bad = 0; for (size_t n = 0; n < t.size(); n++) { JsonDoc d; JsonError e; if (d.parse(t.data(), n, &e) || e.offset > n) bad++; } CHECK(bad == 0, "every truncation of a golden fails cleanly"); }
  { const std::string t = readFile(root + "/sounds/ELECTRO.thunder"); int bad = 0, tried = 0; for (size_t n = 0; n < t.size(); n += 1237) { JsonDoc d; JsonError e; tried++; if (d.parse(t.data(), n, &e) || e.offset > n) bad++; Song s; if (s.load(t.data(), n, &e)) bad++; } CHECK(bad == 0 && tried > 600, "truncations of ELECTRO.thunder fail cleanly"); }
  // byte flips: never a crash (a flip may still leave valid JSON)
  { std::string t = readFile(root + "/tests/golden/chains-modules.json"); uint32_t x = 12345; int parsed = 0, failed = 0;
    for (int i = 0; i < 3000; i++) { std::string u = t; for (int k = 0; k < 3; k++) { x = x * 1664525u + 1013904223u; u[(x >> 8) % u.size()] = (char)(x >> 24); } JsonDoc d; JsonError e; if (d.parse(u, &e)) { parsed++; (void)json_write(d.root()); } else failed++; }
    CHECK(parsed + failed == 3000, "byte-flip fuzz ran"); }
}

static void testOlder() {
  JsonError e;
  // v5 song (before the SWING NOTE conversion): LPB 8 with swingB 2 plays as LPB 4, 12 ticks; a TIC command (index 27) scales; bpm stays
  { const std::string t = "{\"v\":5,\"bpm\":120,\"swingB\":2,\"lpb\":8,\"ticks\":6,\"plpb\":[0,8],\"phrases\":[[[60,-1,-1,-1,-1,-1,27,3,-1,-1,-1,-1]]]}";
    Song s; CHECK(s.load(t.data(), t.size(), &e), "v5 loads"); CHECK(s.lpb == 4 && s.ticks == 12 && s.bpm == 120 && s.plpb[1] == 4 && s.plpb[0] == 0, "v5: lpb 4, ticks 12, plpb 4");
    const int16_t* p = s.phrase(0); CHECK(p && p[0] == 60 && p[6] == 27 && p[7] == 6, "v5: TIC value 3 becomes 6"); CHECK(!s.phrase(1), "a missing phrase is null"); }
  // an LPB the factor does not divide: the tempo carries it
  { const std::string t = "{\"v\":4,\"bpm\":120,\"swingB\":2,\"lpb\":3,\"ticks\":6}"; Song s; CHECK(s.load(t.data(), t.size(), &e), "v4 loads"); CHECK(s.lpb == 3 && s.bpm == 60 && s.ticks == 12, "v4: odd lpb keeps lpb, halves bpm"); }
  // v2: one note column [note, vol, inst, fx...]
  { const std::string t = "{\"v\":2,\"phrases\":[[[60,100,2,3,4,5,6,7,8]]]}"; Song s; CHECK(s.load(t.data(), t.size(), &e), "v2 loads"); const int16_t* p = s.phrase(0);
    CHECK(p && p[0] == 60 && p[1] == -1 && p[2] == -1 && p[3] == -1 && p[4] == 100 && p[5] == 2 && p[6] == 3 && p[7] == 4 && p[8] == 5 && p[9] == 6 && p[10] == 7 && p[11] == 8, "v2: one note column widened to four"); }
  // clamps and what is empty: a command past FXC is dropped with its value; an empty phrase takes dlen; junk is -1
  { const std::string t = "{\"v\":6,\"dlen\":8,\"plen\":[16,16,16],\"phrases\":[[[60,-1,-1,-1,-1,-1,200,5,81,7,80,9]],[],null,[[-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1]]],\"song\":[[0,\"x\",5000,null,1.5]],\"insts\":[null,{\"pad\":999,\"rev\":\"yes\",\"kf\":0}],\"smute\":[4294967295,\"7\"]}";
    Song s; CHECK(s.load(t.data(), t.size(), &e), "clamp song loads"); const int16_t* p = s.phrase(0);
    CHECK(p && p[6] == -1 && p[7] == -1 && p[8] == -1 && p[9] == -1 && p[10] == 80 && p[11] == 9, "commands past FXC dropped with their values");
    CHECK(s.plen[0] == 16 && s.plen[1] == 8 && s.plen[2] == 8 && s.plen[3] == 8, "plen: kept for a phrase with content, dlen for empty, null and all-negative");
    CHECK(s.phrase(1) && !s.phrase(2) && s.phrase(3), "an empty array phrase is present (32 empty rows); null is not");
    CHECK(s.cell(0, 0) == 0 && s.cell(0, 1) == -1 && s.cell(0, 2) == 4095 && s.cell(0, 3) == -1 && s.cell(0, 4) == 2 && s.cell(0, 5) == -1, "grid cell clamps (1.5 rounds to 2)");
    CHECK(s.insts[0].pad == 0 && s.insts[1].pad == 63 && s.insts[1].rev == 1 && s.insts[1].kf == 0 && s.insts[9].pad == 9 && s.insts[9].kf == 1 && s.insts[20].pad == 4, "inst defaults and clamps (freshInst pad = i % 16)");
    CHECK(s.smute[0] == -1 && s.smute[1] == 7, "smute is read as |0"); }
}

int main(int argc, char** argv) {
  const std::string root = argc > 1 ? argv[1] : THUNDER_ROOT;
  testElectro(root); testRoundTrip(root); testUnknown(); testJson(); testMalformed(root); testOlder();
  if (fails) { std::printf("%d song test(s) failed\n", fails); return 1; }
  std::printf("song: all passed\n"); return 0;
}
