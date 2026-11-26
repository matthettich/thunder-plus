#include "live_host.h"

#include "host_clock.h"

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

#include "pad_block.h"
#include "pad_filler.h"
#include "thunder/live.h"
#ifdef THUNDER_HAVE_LXR
#include "thunder/lxr_module.h"
#include "thunder/lxr_seam.h"
#endif

namespace tpd {

namespace {
int64_t steadyNs() { return hostNowNs(); }   // H7: the shell's one clock (host_clock.h)
long long numOf(std::string_view s, long long def) { if (s.empty()) return def; return std::strtoll(std::string(s).c_str(), nullptr, 10); }
LoadResult fail(int rc, const std::string& err) { LoadResult r; r.ok = false; r.rc = rc; r.err = err; return r; }
}  // namespace

// THE AUDIO CALLBACK (the output's thread): the A5 logger (atomics), LiveEngine::render (flush-to-zero, thunder_live_process, interleave), then, MIDI on, the MIDI service's clock map (the Android shell's
// dataCallback order: AFTER process), and the load figure. No allocation, no lock, no system call beyond the clock read, no log.
void LiveHost::audioCb(void* user, float* out, int frames, int64_t wallNs) {
  LiveHost* h = static_cast<LiveHost*>(user);
  tpa::LiveEngine* e = h->audioEng_.load(std::memory_order_acquire);
  if (!e) { std::memset(out, 0, sizeof(float) * 2 * (size_t)frames); return; }
  const int64_t t0 = steadyNs();
  rtlog_begin(&h->rt_, t0);
  const int64_t hostF = e->hostFrameNow();
  const int64_t wall = wallNs ? wallNs : t0;
  e->render(out, frames, wall);
  if (MidiService* m = h->audioMidi_.load(std::memory_order_acquire)) m->audioBlock(frames, hostF, e->engineOffsetRT(), wall);
  const int64_t t1 = steadyNs();
  rtlog_end(&h->rt_, t1, frames);
  const double period = (double)frames / h->sr_.load(std::memory_order_relaxed), work = (double)(t1 - t0) * 1e-9, prev = h->load_.load(std::memory_order_relaxed), now = work / period;
  h->load_.store(now > prev ? now : prev * 0.95 + now * 0.05, std::memory_order_relaxed);
}

int LiveHost::start(bool chains, std::string* err) {
  std::lock_guard<std::mutex> lk(life_);
  if (out_) return (int)sr_.load();
#ifdef THUNDER_HAVE_LXR
  thunder_lxr_module_register();   // idempotent registry of one pointer (lxr_seam.h)
#endif
  // The configured device first; when it will not open (unplugged since the page chose it) the system default, and the note says so.
  note_ = OpenNote();
  std::unique_ptr<AudioOut> out; double sr = 0; std::string e;
  for (int attempt = 0; attempt < 2; attempt++) {
    if (attempt == 1 && cfg_.outId.empty()) break;   // there is no other device to try
    AudioConfig c = cfg_; if (attempt == 1) { c.outId.clear(); c.sampleRate = 0; c.bufferFrames = 0; }
    out = factory_();
    if (!out) break;
    out->configure(c);
    std::string oe;
    if (out->open(&LiveHost::audioCb, this, &sr, &oe)) { if (attempt == 1) { note_.fellBack = true; note_.requested = cfg_.outId; } break; }
    if (attempt == 0) e = oe;
    out.reset();
  }
  if (!out) { if (err) *err = e.empty() ? "no audio output could be opened" : e; return -1; }
  note_.actualName = out->name(); note_.actualId = out->deviceId();
  sr_.store(sr);
  auto eng = std::make_shared<tpa::LiveEngine>();
  tpa::LiveEngine::Config ec; ec.sampleRate = sr; ec.maxBlock = 4096; ec.chains = chains;
  ec.padBytes = (uint64_t)512 << 20; ec.chainsBytes = (uint64_t)512 << 20;   // a desktop has the memory a phone has not (decision for the lead, H6-NOTES)
  if (!eng->create(ec, &e)) { out->close(); if (err) *err = e; return -2; }
  // H7, the Android shell's order (live_jni.cpp): the MIDI host on the live handle's thunder_midi BEFORE the control thread starts (this thread is the control thread until then), then the threads,
  // then the control runner and the MIDI-in reader.
  midiOn_ = midi_ && midi_->engineAttach(eng, sr);
  tpa::LiveEngine* rawEng = eng.get();
  eng->setTickHook([this, rawEng] {   // the poller thread, every ~100 ms: the output's latency for the core's stats and the MIDI clock map's floor (atomics only: it never takes life_)
    AudioOut* o = outRaw_.load(std::memory_order_acquire);
    if (!o) return;
    const int lat = o->latencyFrames();
    rawEng->setHostStats(lat > 0 ? lat : 0, 0);
    const int rr = o->reroutes();
    if (rr != lastReroutes_.exchange(rr, std::memory_order_relaxed)) if (MidiService* m = audioMidi_.load(std::memory_order_acquire)) m->streamRemap();
    const double r = sr_.load(std::memory_order_relaxed);
    if (lat > 0 && r > 0) if (MidiService* m = audioMidi_.load(std::memory_order_acquire)) m->setOutLatencyNs((int64_t)((double)lat * 1e9 / r));
  });
  if (midiOn_) eng->setStateHook([this](int st) { if (st == THUNDER_LIVE_STATE_ENDED) if (MidiService* m = audioMidi_.load(std::memory_order_acquire)) m->songEnded(); });   // a song that ends by itself: the clock boxes go quiet (play and stop set the flag themselves)
  eng->startThreads();
  if (midiOn_) midi_->engineStarted();
  rtlog_reset(&rt_, (int)sr);
  lastReroutes_.store(0);
  eng_.set(eng);
  out_ = std::move(out);
  outRaw_.store(out_.get(), std::memory_order_release);
  audioEng_.store(eng.get(), std::memory_order_release);   // before the stream starts: the callback never sees a half-made engine
  audioMidi_.store(midiOn_ ? midi_ : nullptr, std::memory_order_release);
  std::string se;
  if (!out_->start(&se)) {
    audioEng_.store(nullptr, std::memory_order_release);
    teardownLocked();
    if (err) *err = se.empty() ? "the audio output did not start" : se;
    return -3;
  }
  return (int)sr;
}

// The destroy order (live.h destroy; the Android shell's teardownLocked): stop the transport, let two callbacks run it, stop the stream (no more process), MIDI in off, join the threads, MIDI out stopped
// (the sender delivers the Stop, thunder_midiout freed), destroy. The output OBJECT outlives the engine's threads (the poller reads its latency): it is closed first and freed after stopThreads. life_ held.
void LiveHost::teardownLocked() {
  std::shared_ptr<tpa::LiveEngine> eng = eng_.take();
  if (!eng && !out_) return;
  if (eng && out_) {
    eng->call([](thunder_live* L) { return thunder_live_stop(L, THUNDER_LIVE_NOW); }, 500);
    if (midiOn_ && midi_) eng->call([this](thunder_live*) { midi_->transport(false); return 0; }, 500);
    const int64_t c0 = eng->audioCalls();
    for (int i = 0; i < 100 && eng->audioCalls() < c0 + 2; i++) std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  if (out_) out_->close();   // no callback after this
  audioEng_.store(nullptr, std::memory_order_release);
  audioMidi_.store(nullptr, std::memory_order_release);
  if (midiOn_ && midi_) midi_->engineStopping();
  if (eng) eng->stopThreads();   // the control thread is the live handle's ONE control thread: it ends before the MIDI control calls below
  outRaw_.store(nullptr, std::memory_order_release);
  out_.reset();
  if (midiOn_ && midi_) { midi_->engineDetached(); }
  midiOn_ = false;
  if (eng) eng->destroy();
  eng.reset();
  lastSlots_.clear();
  songText_.reset(); kitText_.reset(); kitDoc_.reset();   // the page re-sends song and kit after an engine reset: a verdict must never be judged with the old kit
  sr_.store(0); load_.store(0);
}

void LiveHost::shutdown() { std::lock_guard<std::mutex> lk(life_); teardownLocked(); }

std::string LiveHost::audioName() const { std::lock_guard<std::mutex> lk(life_); return out_ ? out_->name() : std::string(); }

int LiveHost::call(std::function<int(thunder_live*)> fn, int timeoutMs) {
  auto eng = eng_.get(); if (!eng) return -2;
  return eng->call(std::move(fn), timeoutMs);
}

LoadResult LiveHost::songLoad(const std::string& raw) {
  std::lock_guard<std::mutex> ll(loadM_);
  std::shared_ptr<tpa::LiveEngine> eng; long long nextRev = 0;
  { std::lock_guard<std::mutex> lk(life_); eng = eng_.get(); if (eng) nextRev = ++rev_; }
  if (!eng) return fail(-2, "engine not started");
  const std::string_view mv(raw);
  std::string_view S = tpa::extractMember(mv, "S"), V = tpa::extractMember(mv, "vars");
  if (S.empty()) return fail(-3, "song.load without S");
  if (V == "null") V = std::string_view();
  const long long rev = numOf(tpa::extractMember(mv, "rev"), nextRev);
  auto text = std::make_shared<std::string>(S);
  auto vars = std::make_shared<std::string>(V);
  auto serr = std::make_shared<std::array<char, 256>>(); serr->fill(0);
  const int rc = eng->call([=](thunder_live* L) { return thunder_live_song_publish(L, (const uint8_t*)text->data(), text->size(), vars->empty() ? nullptr : (const uint8_t*)vars->data(), vars->size(), rev, serr->data(), serr->size()); }, 20000);
  if (rc == 0) {
    double bpm = 120, lpb = 4;
    thunder::JsonDoc sd; if (sd.parse(*text)) { bpm = sd.root().num("bpm", 120); lpb = sd.root().num("lpb", 4); }
    std::lock_guard<std::mutex> lk(life_);
    if (eng_.get() == eng) { songText_ = text; bpm_ = bpm; lpb_ = lpb; }   // only to the engine the song went to
  }
  LoadResult r; r.ok = rc == 0; r.rc = rc; r.err = serr->data();
  return r;
}

LoadResult LiveHost::kitLoad(const std::string& raw) {
  std::lock_guard<std::mutex> ll(loadM_);
  std::shared_ptr<tpa::LiveEngine> eng; double bpm = 120, lpb = 4, sr = 0; std::vector<int> prevSlots;
  { std::lock_guard<std::mutex> lk(life_); eng = eng_.get(); if (eng) { bpm = bpm_; lpb = lpb_; sr = sr_.load(); prevSlots = lastSlots_; } }
  if (!eng) return fail(-2, "engine not started");
  const std::string_view kitT = tpa::extractMember(std::string_view(raw), "kit");
  if (kitT.empty()) return fail(-3, "kit.load without kit");
  auto kitText = std::make_shared<std::string>(kitT);
  auto kitDoc = std::make_shared<thunder::JsonDoc>();
  if (!kitDoc->parse(*kitText)) {
    std::lock_guard<std::mutex> lk(life_);
    if (eng_.get() == eng) { kitDoc_.reset(); kitText_.reset(); }
    return fail(-3, "the kit is unreadable");
  }
  auto psP = std::make_shared<tpa::PadSet>();
  tpa::renderPads(*kitText, bpm, lpb, sr, psP.get());
  std::vector<int> slots; for (auto& p : psP->pads) slots.push_back(p.slot);
  auto goneP = std::make_shared<std::vector<int>>(); for (int s : prevSlots) { bool keep = false; for (int t : slots) if (t == s) keep = true; if (!keep) goneP->push_back(s); }
  const int rc = eng->call([=](thunder_live* L) -> int {
    int r;
    for (int s : *goneP) { r = thunder_live_pad_set(L, s, nullptr, nullptr, 0, sr); if (r) return r; }
    for (auto& p : psP->pads) {
      if (p.kitDone) continue;   // an E_BUSY retry repeats the lambda from the start: a pad whose take AND kit worked is in
      if (p.block) {             // a taken pad (block == nullptr) is never taken again; only its kit may be missing
        r = thunder_live_pad_take(L, p.slot, p.block, 0, p.frames, sr); if (r) return r;
        p.block = nullptr;
      }
      r = thunder_live_pad_kit(L, p.slot, p.choke, p.syn ? 1 : 0, p.rel); if (r) return r;
      p.kitDone = true;
    }
    return thunder_live_pads_publish(L);
  }, 20000);
  { std::lock_guard<std::mutex> lk(life_);   // write back only to the engine this load ran on
    if (eng_.get() == eng) {
      kitText_ = kitText; kitDoc_ = kitDoc;
      if (rc == 0) lastSlots_ = slots;
      else { for (int t : slots) { bool have = false; for (int s : lastSlots_) if (s == t) have = true; if (!have) lastSlots_.push_back(t); } }   // a failed load may have put some pads in: the next load removes the union
    } }
  LoadResult r; r.ok = rc == 0; r.rc = rc; r.pads = (int)psP->pads.size(); r.skipped = psP->skipped; r.dry = psP->dry; r.renderMs = psP->ms;
  std::string empty;
  const auto voices = kitDoc->root().arr("voices");
  for (size_t i = 0; i < voices.size() && i < 256; i++) { bool have = false; for (int s : slots) if (s == (int)i) have = true; if (!have && voices.at(i).isObj()) { if (!empty.empty()) empty += ","; empty += std::to_string(i); } }
  r.empty = empty;
  return r;
}

bool LiveHost::fit(int64_t rev, int flags, tpa::FitResult* out, std::string* json) {
  std::lock_guard<std::mutex> lk(life_);
  auto eng = eng_.get();
  if (!eng || !songText_) { if (json) *json = "{\"t\":\"fit\",\"ok\":false,\"reasons\":[{\"code\":\"none\",\"text\":\"no song loaded\"}]}"; return false; }
  uint32_t caps = thunder_live_caps();
#ifdef THUNDER_LIVE_F_CHAINS
  if (!eng->chainsOn()) caps &= ~(uint32_t)THUNDER_LIVE_CAP_CHAINS;
#endif
  tpa::FitContext ctx; ctx.lxpActive = (flags & 1) != 0; ctx.clockIn = (flags & 2) != 0;
  tpa::FitResult r = tpa::songFit(*songText_, kitText_ ? std::string_view(*kitText_) : std::string_view(), caps, 0, hasLxr(), ctx);
  if (json) *json = r.json(rev);
  if (out) *out = std::move(r);
  return true;
}

int LiveHost::padTake(int pad, const uint8_t* l, const uint8_t* r, int frames, double rate) {
  std::shared_ptr<tpa::LiveEngine> eng; int choke = 0; bool syn = false; double rel = 0;
  { std::lock_guard<std::mutex> lk(life_);
    eng = eng_.get(); if (!eng) return -2;
    if (frames > 0 && pad >= 0 && pad < THUNDER_LIVE_PADS && kitDoc_) tpa::kitEntryOf(kitDoc_->root(), (size_t)pad, &choke, &syn, &rel); }
  if (frames <= 0 || pad < 0 || pad >= THUNDER_LIVE_PADS || !l) return -1;
  const int ch = r ? 2 : 1;
  float* b = thunder_live_alloc((int64_t)frames * ch);
  if (!b) return THUNDER_LIVE_E_MEMORY;
  auto blk = std::make_shared<tpa::PadBlock>(b);   // freed by the holder on every failure, including a task dropped at the timeout
  std::memcpy(b, l, (size_t)frames * 4);
  if (r) std::memcpy(b + frames, r, (size_t)frames * 4);
  return eng->call([=](thunder_live* L) {
    if (blk->owned) {   // an E_BUSY retry repeats the lambda from the start: once the core has the block (owned == false) pad_take must NOT be called again, only pad_kit
      const int x = thunder_live_pad_take(L, pad, blk->p, r ? 1 : 0, frames, rate);
      if (x) return x;
      blk->owned = false;
    }
    return thunder_live_pad_kit(L, pad, choke, syn ? 1 : 0, rel);
  }, 10000);
}

int LiveHost::padRemove(int pad) {
  const double sr = sr_.load();
  if (pad < 0 || pad >= THUNDER_LIVE_PADS) return -1;
  return call([=](thunder_live* L) { return thunder_live_pad_set(L, pad, nullptr, nullptr, 0, sr); }, 10000);
}

int LiveHost::padsPublish() { return call([](thunder_live* L) { return thunder_live_pads_publish(L); }, 10000); }

int LiveHost::varSet(int ii, int kind, int num, int pad, const uint8_t* l, int frames, double rate) {
  if (!eng_.get()) return -2;
  if (!l || frames <= 0) return -1;
  auto v = std::make_shared<std::vector<float>>((size_t)frames);
  std::memcpy(v->data(), l, (size_t)frames * 4);
  return call([=](thunder_live* L) { return thunder_live_var_set(L, ii, kind, num, pad, v->data(), frames, rate); }, 10000);
}

int LiveHost::padChop(int pad, double loopSecs, int nSlices, const std::vector<double>& framePts) {
  const double sr = sr_.load();
  if (sr <= 0) return -2;
  auto v = std::make_shared<std::vector<double>>(framePts);
  for (auto& x : *v) x /= sr;   // the page sends frames at the engine rate; the core takes seconds (render.h CHOP DATA)
  return call([=](thunder_live* L) {
    int r = thunder_live_pad_chop(L, pad, loopSecs > 0 ? loopSecs : 0.0); if (r) return r;
    const int cnt = (int)v->size();
    if (cnt == 0 || nSlices < 2) return thunder_live_pad_hits(L, pad, 2, nullptr, 0);
    return thunder_live_pad_hits(L, pad, nSlices > 64 ? 64 : nSlices, v->data(), cnt > nSlices ? nSlices : cnt);
  }, 10000);
}

int LiveHost::play(int mode, int row, int track, int sect, int phrase, int hasLoop, int y0, int y1, int64_t t0, int hasSeed, int seed, int pdc, uint64_t onlyBits, int hasOnly) {
  const uint32_t rs = (uint32_t)(steadyNs() & 0x7fffffff) | 1u;
  return call([=](thunder_live* L) {
    thunder_live_play_opts po; std::memset(&po, 0, sizeof po);
    po.size = (uint32_t)sizeof po; po.t0 = t0 < 0 ? THUNDER_LIVE_NOW : t0; po.mode = mode; po.row = row; po.track = track; po.sect = sect; po.phrase = phrase;
    po.has_loop = hasLoop; po.loop_y0 = y0; po.loop_y1 = y1; po.has_seed = hasSeed; po.seed = (uint32_t)seed; po.random_seed = rs; po.spp = -1; po.pdc_mode = pdc;
    po.only = onlyBits; po.has_only = hasOnly;
    if (midiOn_ && midi_) midi_->transport(true);   // before play: a port set after play() is in force for its Start (live.h), the transport flag first
    return thunder_live_play(L, &po);
  });
}

int LiveHost::stop(int64_t at) {
  return call([=](thunder_live* L) { const int r = thunder_live_stop(L, at); if (r == 0 && midiOn_ && midi_) midi_->transport(false); return r; });
}

LiveHost::AudioInfo LiveHost::audioInfo() const {
  std::lock_guard<std::mutex> g(life_);
  AudioInfo i;
  if (!out_) return i;
  i.open = true; i.name = out_->name(); i.id = out_->deviceId(); i.sampleRate = sr_.load(); i.periodFrames = out_->periodFrames(); i.latencyFrames = out_->latencyFrames(); i.lost = out_->lost();
  return i;
}

void LiveHost::rtReset() { std::lock_guard<std::mutex> g(life_); if (out_) rtlog_reset(&rt_, (int)sr_.load()); }

std::string LiveHost::rtSnapshotJson() const {
  rtlog* g = const_cast<rtlog*>(&rt_);
  const rtlog_snapshot sn = rtlog_read(g);
  char b[400];
  std::snprintf(b, sizeof b, "\"sr\":%d,\"frames\":%lld,\"calls\":%lld,\"late\":%lld,\"under\":%lld,\"audioS\":%.6f,\"wallS\":%.6f,\"ratioPpm\":%.2f,\"maxGapMs\":%.3f,\"maxWorkMs\":%.3f,\"pass\":%d",
    g->sample_rate, (long long)sn.frames, (long long)sn.calls, (long long)sn.late, (long long)sn.underruns, sn.audio_s, sn.wall_s, sn.ratio_ppm, sn.max_gap_ms, sn.max_work_ms, sn.pass);
  return b;
}

std::string LiveHost::drainEvents() { auto e = eng_.get(); return e ? e->drainEvents() : std::string(); }
bool LiveHost::chainsOn() const { auto e = eng_.get(); return e && e->chainsOn(); }
bool LiveHost::hasLxr() {
#ifdef THUNDER_HAVE_LXR
  return thunder_lxr_api() != nullptr;
#else
  return false;
#endif
}
uint32_t LiveHost::liveCaps() const { return thunder_live_caps(); }
std::string LiveHost::statsJson() const { auto e = eng_.get(); return e ? e->statsJson() : "{}"; }
std::string LiveHost::statusJson() const { auto e = eng_.get(); return e ? e->statusJson() : "{}"; }

}  // namespace tpd
