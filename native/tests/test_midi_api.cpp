// U35: the thunder_midi_* C API and the scheduler's entry points under hostile input (run under ASan + UBSan: -DTHUNDER_SANITIZE=ON; the three-thread case under TSan).
//   1. every call with NULL / NaN / negative / huge arguments: a code, never a crash, nothing sent
//   2. the command ring full: E_BUSY, nothing changed; the song hand-over (set_song many times, garbage, empty)
//   3. random SeqEvents (NaN, inf, out-of-range tracks / instruments / notes, arp flags with no arp) into onEvent / onStep: every record that comes out is a valid MIDI message
//   4. the three roles on three threads at once (control posts, audio processes, consumer drains): valid records, counters monotonic, no loss of order per cable
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

#include "midi_drive.h"
#include "thunder/midi.h"

using namespace mdrive;
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); fails++; } } while (0)

static bool validMsg(const thunder_midi_msg& m) {
  if (m.flags & THUNDER_MIDI_F_CLEAR) return m.len == 0;
  if (m.len < 1 || m.len > 3 || m.port >= 4 || m.b[0] < 0x80 || m.b[0] == 0xF0 || m.b[0] == 0xF7) return false;
  const int st = m.b[0], need = st < 0xC0 ? 3 : st < 0xE0 ? 2 : st < 0xF0 ? 3 : (st == 0xF1 || st == 0xF3) ? 2 : st == 0xF2 ? 3 : 1;
  if (m.len != need) return false;
  for (int i = 1; i < m.len; i++) if (m.b[i] >= 0x80) return false;
  return true;
}
static uint64_t rs = 88172645463325252ull;
static uint64_t rnd() { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return rs; }
static double rd(double lo, double hi) { return lo + (hi - lo) * (double)(rnd() % 1000001) / 1e6; }
static double evil() {
  switch (rnd() % 12) { case 0: return NAN; case 1: return INFINITY; case 2: return -INFINITY; case 3: return 1e300; case 4: return -1e300; case 5: return 0; case 6: return -1; case 7: return 1e9; case 8: return 127.5; case 9: return -0.0; default: return rd(-300, 300); }
}

static std::string readSong() { std::string s; readFile(std::string(THUNDER_ROOT) + "/native/tests/golden/midi-ledger-mono-notes.song.json", s); return s; }

static void hostileC() {
  char err[64];
  CHECK(thunder_midi_create(NAN, err, sizeof err) == nullptr && err[0], "create(NaN) is NULL with a text");
  CHECK(thunder_midi_create(100, nullptr, 0) == nullptr, "create(100) is NULL");
  CHECK(thunder_midi_create(1e9, err, 1) == nullptr, "create(1e9) is NULL");
  thunder_midi* h = thunder_midi_create(48000, err, sizeof err); CHECK(h != nullptr, "create 48000");
  thunder_midi_free(nullptr);
  const uint8_t ok3[3] = {0x90, 60, 100};
  CHECK(thunder_midi_set_port(nullptr, 0, 1, 0, 0, 0) == THUNDER_MIDI_E_ARG, "null handle");
  CHECK(thunder_midi_set_port(h, -1, 1, 0, 0, 0) == THUNDER_MIDI_E_ARG && thunder_midi_set_port(h, 4, 1, 0, 0, 0) == THUNDER_MIDI_E_ARG, "port range");
  CHECK(thunder_midi_set_port(h, 0, 1, 0, NAN, 0) == THUNDER_MIDI_E_ARG && thunder_midi_set_port(h, 0, 1, 0, 0, -1) == THUNDER_MIDI_E_ARG && thunder_midi_set_port(h, 0, 1, 0, 0, 251) == THUNDER_MIDI_E_ARG, "gear / cable");
  CHECK(thunder_midi_set_lead(h, NAN) < 0 && thunder_midi_set_lead(h, 0) < 0 && thunder_midi_set_lead(h, 9) < 0, "lead");
  // U37 P4: the cap is 1.2 s (was 0.5): 0.5 and 1.2 are accepted, anything above is E_ARG, the floor is still 0.005
  CHECK(thunder_midi_set_lead(h, 0.5) == 0 && thunder_midi_set_lead(h, 1.2) == 0 && thunder_midi_set_lead(h, 0.005) == 0, "lead 0.005 / 0.5 / 1.2 accepted");
  CHECK(thunder_midi_set_lead(h, 1.2000001) == THUNDER_MIDI_E_ARG && thunder_midi_set_lead(h, 1.21) == THUNDER_MIDI_E_ARG && thunder_midi_set_lead(h, 0.0049) == THUNDER_MIDI_E_ARG && thunder_midi_set_lead(h, INFINITY) == THUNDER_MIDI_E_ARG, "lead above 1.2 / below 0.005 refused");
  CHECK(thunder_midi_set_fx_latency(h, -1) < 0 && thunder_midi_set_fx_latency(h, INFINITY) < 0 && thunder_midi_set_fx_latency(h, 3) < 0, "fx latency");
  CHECK(thunder_midi_start(h, -1) < 0 && thunder_midi_start(h, 16384) < 0, "spp");
  CHECK(thunder_midi_port_clock(h, 9, 1) < 0 && thunder_midi_set_cable(h, 0, 1, 999) < 0, "port clock / cable");
  CHECK(thunder_midi_key_on(h, 128, 0, 60, 100) < 0 && thunder_midi_key_on(h, 0, 64, 60, 100) < 0 && thunder_midi_key_on(h, 0, 0, 9999, 100) < 0 && thunder_midi_key_on(h, 0, 0, 60, 300) < 0, "key_on");
  CHECK(thunder_midi_key_off(h, -1) < 0, "key_off");
  CHECK(thunder_midi_raw(h, 0, nullptr, 3, 0) < 0 && thunder_midi_raw(h, 0, ok3, 0, 0) < 0 && thunder_midi_raw(h, 0, ok3, 4, 0) < 0 && thunder_midi_raw(h, 0, ok3, 2, 0) < 0, "raw: null / length");
  { const uint8_t sx[3] = {0xF0, 1, 2}, hi[3] = {0x90, 200, 1}, lo[3] = {0x10, 1, 1}, ok1[1] = {0xF8}; CHECK(thunder_midi_raw(h, 0, sx, 3, 0) < 0 && thunder_midi_raw(h, 0, hi, 3, 0) < 0 && thunder_midi_raw(h, 0, lo, 3, 0) < 0, "raw: SysEx / data byte / no status");
    CHECK(thunder_midi_raw(h, 0, ok1, 1, NAN) < 0 && thunder_midi_raw(h, 0, ok1, 1, -1) < 0 && thunder_midi_raw(h, 0, ok1, 1, 99) < 0, "raw: extra time"); }
  CHECK(thunder_midi_set_song(h, nullptr, 5) == THUNDER_MIDI_E_ARG && thunder_midi_set_song(h, (const uint8_t*)"x", 0) == THUNDER_MIDI_E_ARG, "set_song null / empty");
  CHECK(thunder_midi_set_song(h, (const uint8_t*)"not json at all", 15) == THUNDER_MIDI_E_SONG, "set_song garbage");
  const int rEmpty = thunder_midi_set_song(h, (const uint8_t*)"{}", 2); CHECK(rEmpty == THUNDER_MIDI_E_SONG || rEmpty == 0, "set_song {} is a song or an error: %d", rEmpty); const int pre = rEmpty == 0 ? 1 : 0;
  CHECK(thunder_midi_set_song(h, (const uint8_t*)"x", (size_t)1 << 40) == THUNDER_MIDI_E_ARG, "set_song over the limit");
  CHECK(thunder_midi_process(h, NAN, 0, 0, 120, 0) < 0 && thunder_midi_process(h, -1, 0, 0, 120, 0) < 0 && thunder_midi_process(h, 0, 0, NAN, 120, 0) < 0 && thunder_midi_process(h, 0, 0, 0, 0, 0) < 0 && thunder_midi_process(h, 0, 0, 0, NAN, 0) < 0 && thunder_midi_process(h, 0, NAN, 0, 120, 0) < 0 && thunder_midi_process(h, 0, -1, 0, 120, 0) < 0 && thunder_midi_process(nullptr, 0, 0, 0, 120, 0) < 0, "process: NaN / null");
  thunder_midi_msg buf[4];
  CHECK(thunder_midi_drain(nullptr, buf, 4) < 0 && thunder_midi_drain(h, nullptr, 4) < 0 && thunder_midi_drain(h, buf, -1) < 0 && thunder_midi_drain(h, nullptr, 0) == 0 && thunder_midi_drain(h, buf, 0) == 0, "drain args");
  thunder_midi_stats st; std::memset(&st, 0, sizeof st);
  CHECK(thunder_midi_stats_get(h, &st) < 0, "stats with size 0");
  st.size = sizeof st; CHECK(thunder_midi_stats_get(h, &st) == 0 && thunder_midi_stats_get(h, nullptr) < 0 && thunder_midi_stats_get(nullptr, &st) < 0, "stats");
  // the command ring full: BUSY, then room after a block
  int busy = 0, okc = 0; for (int i = 0; i < 1000; i++) { const int r = thunder_midi_set_lead(h, 0.06); if (r == THUNDER_MIDI_E_BUSY) busy++; else if (r == 0) okc++; }
  const int pre3 = pre + 3;   // + the three lead commands of the U37 P4 check above
  CHECK(busy > 0 && okc + pre3 == 256, "the ring (256) fills: %d accepted (+%d before), %d busy", okc, pre3, busy);
  CHECK(thunder_midi_process(h, 0.01, 0, 0, 120, 0) == okc + pre3, "process applies the %d posted commands", okc + pre3);
  CHECK(thunder_midi_set_lead(h, 0.06) == 0, "room again");
  // songs: more than the hand-over allows in flight: BUSY, never a leak (ASan checks at free)
  const std::string song = readSong(); int sb = 0, so = 0;
  for (int i = 0; i < 40; i++) { const int r = thunder_midi_set_song(h, (const uint8_t*)song.data(), song.size()); if (r == 0) so++; else if (r == THUNDER_MIDI_E_BUSY) sb++; }
  CHECK(so >= 1 && sb >= 1, "set_song: %d accepted, %d busy", so, sb);
  thunder_midi_process(h, 0.02, 0, 0, 120, 0); thunder_midi_process(h, 0.03, 0, 0, 120, 0);
  CHECK(thunder_midi_set_song(h, (const uint8_t*)song.data(), song.size()) == 0, "set_song after the audio thread took the first");
  thunder_midi_free(h);   // (a song still posted, one current, some retired: all freed)
  std::printf("C API arguments: done\n");
}

static void fuzzEntryPoints() {
  Case C; loadCase(THUNDER_ROOT, "overlap-poly", C); Song song; JsonError je; song.load(C.songText.data(), C.songText.size(), &je);
  for (int i = 0; i < 16; i++) { song.insts[i].mo = (int)(rnd() % 2); song.insts[i].mch = (int)(rnd() % 40) - 4; song.insts[i].mpt = (int)(rnd() % 9) - 2; song.insts[i].pbr = (int)(rnd() % 100) - 20; song.insts[i].lcc = (int)(rnd() % 200) - 30; song.insts[i].eco = (int)(rnd() % 40); song.insts[i].lrg = (int)(rnd() % 2); song.insts[i].mlen = (int)(rnd() % 10) - 2; song.insts[i].cc[0] = (int)(rnd() % 300) - 50; }
  MidiSched s(44100); s.setSong(&song);
  for (int p = 0; p < 4; p++) s.setPort(p, true, p != 1, evil(), (int)p);
  s.setFxLatency(evil()); s.setLead(evil());
  std::vector<double> arp(2 * 200); for (double& v : arp) v = evil();
  long bad = 0, total = 0; std::vector<MidiMsg> out(4096); double now = 0;
  for (int it = 0; it < 60000; it++) {
    SeqEvent e; e.type = (uint8_t)(rnd() % (SEV_COUNT + 3)); e.track = (int)(rnd() % 40) - 4; e.ii = (rnd() % 5 == 0) ? evil() : (double)(rnd() % 70) - 3; e.mn = evil(); e.vel = evil(); e.t = (rnd() % 7 == 0) ? evil() : now + rd(-0.5, 2);
    e.poly = rnd() & 1; e.col = (int)(rnd() % 40) - 4; e.has = (uint32_t)rnd(); e.cut = evil(); e.rel = evil(); e.tick = evil(); e.v = evil(); e.k = (int)(rnd() % 9) - 2; e.ramp = evil(); e.cols = (uint8_t)rnd(); e.arpN = (int)(rnd() % 200) - 20; e.arpOff = 0;
    e.slideFrom = evil(); e.rate = evil(); e.slideDur = evil(); e.bendMul = evil(); e.bendT1 = evil(); e.vibSpd = evil(); e.vibDep = evil(); e.vibT1 = evil(); e.mul = evil(); e.vc = evil();
    const double bpm = (rnd() % 9 == 0) ? evil() : rd(20, 300);
    s.onEvent(e, (e.has & SH_ARP) ? (rnd() % 4 ? arp.data() : nullptr) : nullptr, bpm, rnd() % 5 == 0, now);
    if (rnd() % 50 == 0) { SeqEvent b; b.type = SEV_BEAT; b.t = now + rd(0, 1); b.v = rd(0, 100); SeqStep st; Seq* none = nullptr; (void)none; (void)b; (void)st; }
    if (rnd() % 20 == 0) { now += rd(0, 0.2); s.book(now, now + rd(-1, 3), (rnd() % 9 == 0) ? evil() : rd(20, 300), rnd() & 1); }
    const unsigned r = (unsigned)(rnd() % 400);
    if (r == 0) s.stop(now); else if (r == 1) s.release(now, rnd() & 1); else if (r == 2) s.panic(now); else if (r == 3) s.start((int)(rnd() % 20000) - 100, now);
    else if (r == 4) s.portToggled((int)(rnd() % 6) - 1, rnd() & 1, now); else if (r == 5) s.cableChanged((int)(rnd() % 6) - 1, rnd() & 1, (int)(rnd() % 6), now);
    else if (r < 10) { const int h = s.liveNoteOn((int)(rnd() % 70) - 3, (int)(rnd() % 600) - 250, (int)(rnd() % 300) - 20, (int)(rnd() % 40) - 5, now); if (h >= 0 && (rnd() & 1)) s.liveNoteOff(h, now); else if (rnd() & 1) s.liveNoteOff((int)(rnd() % 50) - 5, now); }
    else if (r < 14) { uint8_t b[4] = {(uint8_t)rnd(), (uint8_t)rnd(), (uint8_t)rnd(), (uint8_t)rnd()}; s.liveRaw((int)(rnd() % 6) - 1, (rnd() % 8 == 0) ? nullptr : b, (int)(rnd() % 6) - 1, evil(), now); }
    if (it % 64 == 0) { int64_t n; while ((n = s.drain(out.data(), (int64_t)out.size())) > 0) for (int64_t i = 0; i < n; i++) { total++; thunder_midi_msg m; std::memcpy(&m, &out[(size_t)i], sizeof m); if (!validMsg(m)) bad++; } }
  }
  CHECK(bad == 0, "fuzz: %ld invalid records of %ld", bad, total);
  CHECK(total > 1000, "fuzz: the run produced messages (%ld)", total);
  std::printf("fuzz: %ld records, all valid; counters: badEvents %llu badArgs %llu pendFull %llu ringFull %llu heldFull %llu\n", total, (unsigned long long)s.stats().badEvents, (unsigned long long)s.stats().badArgs, (unsigned long long)s.stats().pendFull, (unsigned long long)s.stats().ringFull, (unsigned long long)s.stats().heldFull);
}

static void threads() {
  char err[64]; thunder_midi* h = thunder_midi_create(48000, err, sizeof err); const std::string song = readSong();
  std::atomic<bool> stop{false}; std::atomic<long> posted{0}, bad{0}, drained{0};
  std::thread control([&] {
    uint64_t r = 12345; thunder_midi_set_port(h, 0, 1, 1, 0, 0);
    long accepted = 0; for (int i = 0; i < 2000000 && !stop && accepted < 3000; i++) {
      r = r * 6364136223846793005ull + 1442695040888963407ull; const int k = (int)((r >> 33) % 12);
      int rc = 0;
      switch (k) { case 0: rc = thunder_midi_start(h, 0); break; case 1: rc = thunder_midi_stop(h); break; case 2: rc = thunder_midi_key_on(h, (int)(r >> 40) & 127, 0, 48 + (int)((r >> 20) & 15), 100); break;
        case 3: rc = thunder_midi_key_off(h, (int)(r >> 40) & 127); break; case 4: rc = thunder_midi_panic(h); break; case 5: rc = thunder_midi_set_song(h, (const uint8_t*)song.data(), song.size()); break;
        case 6: rc = thunder_midi_set_port(h, (int)(r >> 50) & 3, 1, (int)(r >> 52) & 1, (double)((int)(r >> 20) % 100), (int)(r >> 50) & 3); break; case 7: rc = thunder_midi_release(h, (int)(r >> 41) & 1); break;
        case 8: { const uint8_t b[3] = {0xB0, 7, 100}; rc = thunder_midi_raw(h, 0, b, 3, 0.01); break; } case 9: rc = thunder_midi_port_clock(h, (int)(r >> 50) & 3, (int)(r >> 52) & 1); break;
        case 10: rc = thunder_midi_set_fx_latency(h, 0.01); break; default: rc = thunder_midi_set_audible(h, r); break; }
      if (rc == 0) { posted++; accepted++; } else if (rc != THUNDER_MIDI_E_BUSY) bad++;
      if (i % 16 == 0) std::this_thread::yield();
    }
    stop = true;
  });
  std::thread audio([&] { double now = 0; while (!stop) { thunder_midi_process(h, now, 128.0 / 48000, now + 0.1, 120, 1); now += 128.0 / 48000; } thunder_midi_process(h, now, 0, now, 120, 0); });
  std::thread consumer([&] {
    thunder_midi_msg b[256]; uint64_t last = 0; bool first = true;
    while (!stop || true) { const int64_t n = thunder_midi_drain(h, b, 256); if (n < 0) { bad++; break; } for (int64_t i = 0; i < n; i++) if (!validMsg(b[i])) bad++; drained += n;
      thunder_midi_stats s; s.size = sizeof s; thunder_midi_stats_get(h, &s); if (!first && s.sent < last) bad++; last = s.sent; first = false;
      if (stop && n == 0) break;
      std::this_thread::yield(); }
  });
  control.join(); audio.join(); consumer.join();
  CHECK(bad == 0, "threads: %ld bad records / codes", bad.load());
  CHECK(posted >= 3000 && drained > 0, "threads: %ld commands posted, %ld records drained", posted.load(), drained.load());
  thunder_midi_free(h);
  std::printf("threads: %ld commands, %ld records\n", posted.load(), drained.load());
}

// T2 / T3: thunder_midi_process forwards block_sec to the scheduler (the outbox releases a message when it is within lead + block of now) and rejects block_sec > 1 without touching anything.
// A live key with 100 ms of FX latency is a note due 101 ms ahead: with the default 60 ms lead it waits in the outbox for a block of 0 and is released at once for a block of 80 ms.
static int countOn(thunder_midi* h) { thunder_midi_msg b[64]; int n = 0; for (;;) { const int64_t k = thunder_midi_drain(h, b, 64); if (k <= 0) break; for (int64_t i = 0; i < k; i++) if ((b[i].b[0] & 0xF0) == 0x90 && b[i].b[2] > 0) n++; } return n; }
static void blockForwarding() {
  const std::string song = readSong(); int released[2] = {-1, -1};
  for (int v = 0; v < 2; v++) {
    thunder_midi* h = thunder_midi_create(48000, nullptr, 0);
    CHECK(h && thunder_midi_set_song(h, (const uint8_t*)song.data(), song.size()) == 0 && thunder_midi_set_port(h, 0, 1, 0, 0, 0) == 0 && thunder_midi_set_fx_latency(h, 0.1) == 0, "T2: setup");
    CHECK(thunder_midi_process(h, 0.0, 0.0, 0.0, 120, 0) == 3, "T2: the three setup commands are applied");
    CHECK(thunder_midi_key_on(h, 5, 0, 60, 100) == 0, "T2: key on");
    CHECK(thunder_midi_process(h, 0.0, v ? 0.08 : 0.0, 0.0, 120, 0) == 1, "T2: the key command is applied");
    released[v] = countOn(h);
    if (v == 0) { CHECK(thunder_midi_process(h, 0.05, 0.0, 0.05, 120, 0) == 0, "T2: process again"); CHECK(countOn(h) == 1, "T2: with a block of 0 the note is released once it is within the 60 ms lead"); }
    thunder_midi_free(h);
  }
  CHECK(released[0] == 0 && released[1] == 1, "T2: block_sec reaches the scheduler: a note 101 ms ahead is held for block 0 (%d released) and released for block 0.08 (%d released)", released[0], released[1]);
  thunder_midi* h = thunder_midi_create(48000, nullptr, 0);
  CHECK(thunder_midi_set_port(h, 0, 1, 0, 0, 0) == 0, "T3: setup");
  CHECK(thunder_midi_process(h, 0.0, 1.0000001, 0.0, 120, 0) == THUNDER_MIDI_E_ARG && thunder_midi_process(h, 0.0, 1e9, 0.0, 120, 0) == THUNDER_MIDI_E_ARG && thunder_midi_process(h, 0.0, 2.0, 0.0, 120, 0) == THUNDER_MIDI_E_ARG, "T3: block_sec above 1 s is E_ARG");
  CHECK(thunder_midi_process(h, 0.0, -0.001, 0.0, 120, 0) == THUNDER_MIDI_E_ARG && thunder_midi_process(h, 0.0, NAN, 0.0, 120, 0) == THUNDER_MIDI_E_ARG, "T3: negative / NaN block_sec is E_ARG");
  CHECK(thunder_midi_process(h, 0.0, 1.0, 0.0, 120, 0) == 1, "T3: the rejected calls applied nothing (the posted command is still there), and exactly 1 s is accepted");
  thunder_midi_free(h);
}

int main(int argc, char** argv) {
  const bool tsanOnly = argc > 1 && std::string(argv[1]) == "--threads";
  if (!tsanOnly) { hostileC(); fuzzEntryPoints(); blockForwarding(); }
  threads();
  std::printf("%d failures\n", fails);
  return fails ? 1 : 0;
}
