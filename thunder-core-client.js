/* thunder-core-client.js: the page's client of window.thunderCore (docs/port/N2-PAGE-CLIENT.md, docs/port/CORE-BRIDGE.md).
   PC1 = the loader's target and the `hello` handshake. PC2 = pads (put / buf.use / buf.free / pads.commit / pad.chop, kit.load, the pad memory budget). PC3 = the song and the transport: publish the whole song (song.load, debounced),
   PLAY / STOP / HOLD / bpm / loop / perf, can() and the fallback to the old engine, engine down / up, device stopped. Mixer diffs, events for the playhead and meters are later PRs (PC4, PC5).
   With `on` false (no shell, switch off) nothing here runs; with `on` true the page reacts only where index.html has an NCON() / tp.nc branch.
   Loaded by index.html's loadClient() only when window.thunderCore exists or Settings > Audio > Sequencer is "Native core (test)".
   A classic script: no import, one global (window.__thunderClient). It reads the page only through the object attach() is given. */
(function () {
  'use strict';
  if (window.__thunderClient) return;
  var BRIDGE_V = 1;          // CORE-BRIDGE hello: v
  var HELLO_MS = 5000;       // CORE-BRIDGE section 2: ask() times out after 5 s
  var host = null;           // HOST_API from index.html
  var tc = null;             // window.thunderCore
  var offs = [];             // listeners to remove in detach()
  var ASK_MS = 5000;         // CORE-BRIDGE section 2 / 7 G14: no song.load verdict within 5 s = NOT ok (reason 'no-verdict')
  var PUBLISH_MS = 250;      // N2-PAGE-CLIENT 4.4: the whole song, 250 ms after the last edit
  var SLOW_SEND_MS = 4, SLOW_PUBLISH_MS = 1000;   // 4.4's fallback for a big song: when handing it to the bridge held the page 4 ms or more, the next publish waits for a second of quiet
  var sendCost = 0;          // how long the last song.load took to hand over (the bridge stringifies it: the main-thread cost the design watches)
  var BEAT_PLAY_MS = 2000;   // can(): a `clock` event within 2 s (4.6)
  var TICK_LATE_MS = 1500;   // a watchdog tick this late skips its round
  var BEAT_LOST_MS = 2500;   // while playing: no `clock` for this long = the engine is gone (the design says 1 s; 2.5 s is the page's own audioInterrupted precedent)
  var revN = 0;              // song.load revision counter
  var verdict = null;        // {rev, ver, sref, ok, pending, reasons[], warn[]}: the answer for the song as last published
  var waiters = [];          // resolvers waiting for a verdict (pending / in flight)
  var inflight = null;       // the song.load ask now out
  var publishT = 0, watchT = 0, leadT = 0;
  var sentVer = -1, sentRef = null;   // what the last publish was made of
  var lxpSent = 0;
  var engineUp = true, lastClock = 0, songHeard = 0, quietUntil = 0;
  var lost = false;          // the watchdog or a fatal err declared the engine gone: the next `clock` event clears it (engineUp=false from an `engine {up:false}` event stays until {up:true})
  var lastTick = 0;          // when the watchdog interval last ran (a tick that comes late = the page was busy, not the engine dead)
  function down() { return !engineUp || lost; }
  var playTok = 0, playSent = false, active = false;  // playTok: a new PLAY or a STOP cancels the one still waiting for its verdict; active: a native PLAY is waiting or playing
  var failed = null;         // {ref}: the engine died while playing this song; the next PLAY of it uses the old engine (until the engine is rebuilt)
  var toasted = null, warned = null, resumeNote = false, errN = 0;   // once per song (the S object): the fallback message, the fit warnings

  function log() {
    try { if (host && host.tpLog) host.tpLog.apply(null, arguments); } catch (e) { /* the log must never throw */ }
  }

  var NC = {
    on: false,               // true only after a good hello (v 1, caps.nativeAudio, a usable sample rate)
    sr: 0,                   // the engine's sample rate (N2-PAGE-CLIENT G1): padSR() returns it while `on`
    outLat: 0,               // output latency in frames, 0 if the shell does not say (G1)
    hostName: '',            // 'android', 'web', ... (G1)
    maxPadBytes: 0,
    maxSongBytes: 0,
    caps: null,              // the hello reply's caps (nativeAudio, midi, link, plugins, inputs, render, engines[], fx[])
    core: '',                // the core's revision string from hello
    reason: '',              // why `on` is false, for the Settings reason line and the log (empty when on)
    ready: null,             // Promise: resolves with `on` once the handshake has settled; never rejects

    attach: function (hostApi) {
      host = hostApi || {};
      tc = window.thunderCore || null;
      if (!tc || typeof tc.ask !== 'function') return settle(off('no window.thunderCore'));
      NC.ready = tc.ask({ t: 'hello', v: BRIDGE_V, live: true, app: typeof host.build === 'function' ? host.build() : 0 }, HELLO_MS)
        .then(onHello, function (e) { return off('hello failed: ' + (e && e.message ? e.message : e)); })
        .then(settle);
      return NC.ready;
    },

    detach: function () {
      for (var i = 0; i < offs.length; i++) { try { offs[i](); } catch (e) { /* ignore */ } }
      offs = []; NC.on = false; NC.sr = 0;
      clearTimeout(publishT); clearInterval(watchT); clearTimeout(leadT); publishT = 0; watchT = 0; leadT = 0; playTok++; playSent = false; active = false;
    },

    // ---- PC3: the song -------------------------------------------------------------------------------------------------------------------
    /** Called from saveS(): the whole song goes 250 ms after the last edit (never song.patch: the live host takes whole text only). */
    edit: function () {
      if (!NC.on) return;
      clearTimeout(publishT); publishT = setTimeout(function () { publishT = 0; if (stale()) publish(); }, sendCost >= SLOW_SEND_MS ? SLOW_PUBLISH_MS : PUBLISH_MS);
    },

    // ---- PC3: the transport --------------------------------------------------------------------------------------------------------------
    /** Decided once per PLAY, before anything sounds (4.6). true = the native engine takes this PLAY (start() follows); false = the old engine plays it. */
    can: function (mode, o) {
      if (!NC.on || !tc) return false;
      if (o && (o.page || o._nf || o.rz || o._warm)) return false;   // the page asked for its own engine (hand-over, the fallback itself): not ours, no message
      if (!wanted()) return false;
      var why = refusal(mode, o);
      if (why) { fallbackNote(why); return false; }
      return true;
    },

    /** PLAY. The page side (host.begin) has set tp (tp.on, tp.nc, the stand-in sequencer) and returns the row / loop to play; the rest is asynchronous:
     *  kit and pads first (PC2's hooks, if present), the song with its verdict, then `play`. A verdict that is not ok hands the PLAY back to the old engine. */
    start: function (mode, o) {
      var o0 = o;
      o = host.begin(mode, o, standIn());   // (it stops the run before, which cancels its token: ours is taken after)
      var tok = ++playTok; playSent = false; active = true; NC.pendBpm = null;
      if (!resumeNote && o && o._resumed) { resumeNote = true; host.toast('Resume plays from the start of the row on the native engine.', 3000); }
      watch(true);
      var tasks = [];
      // PC2's pad side (when it is there): every pad and the kit go first, the chop data after an instrument edit, then PLAY waits (at most 1.5 s) for the pads to be audible
      try { if (typeof NC.kit === 'function') NC.kit(host.padApi ? host.padApi() : undefined); } catch (e) { log('native core client: kit hook failed', e); }
      try { if (typeof NC.chopAll === 'function') NC.chopAll(host.padApi ? host.padApi() : undefined); } catch (e) { log('native core client: chop hook failed', e); }
      try { if (typeof NC.whenPads === 'function') tasks.push(NC.whenPads(1500)); } catch (e) { log('native core client: pads hook failed', e); }
      try { host.prep && host.prep(); } catch (e) { log('native core client: prep failed', e); }
      tasks.push(verdictNow());
      Promise.all(tasks).then(function (r) {
        if (tok !== playTok) return;   // stopped, or another PLAY, meanwhile
        var v = r[r.length - 1];
        if (!v || !v.ok) { fallbackNote(reasonText(v)); return handBack(mode, o0, tok); }
        if (v.warn && v.warn.length) warnNote(v.warn);
        sendPlay(mode, o);
      }, function (e) { if (tok === playTok) { log('native core client: PLAY failed', e); fallbackNote('the native engine did not start'); handBack(mode, o0, tok); } });
      return true;
    },

    /** STOP (tStop's branch): only the sound part; the page keeps the resume point, the cursor and the buttons. */
    stop: function () {
      playTok++; var was = playSent; playSent = false; active = false; watch(false); clearTimeout(leadT); leadT = 0;
      if (was && tc) send({ t: 'stop' });
    },
    /** EXACT STOP at the next pattern edge (the page's HOLD). */
    hold: function () { if (playSent) send({ t: 'hold' }); },
    bpm: function (v) { v = +v; if (playSent && isFinite(v) && v > 0) send({ t: 'bpm', v: v }); else NC.pendBpm = v; },
    loop: function (l) { if (playSent) send(l ? { t: 'loop', has: true, y0: l.y0 | 0, y1: l.y1 | 0 } : { t: 'loop', has: false, y0: 0, y1: 0 }); },
    perf: function (p) {
      if (!playSent || !p) return;
      if (p.xf != null) send({ t: 'perf', what: 0, i: 0, v: +p.xf });
      if (p.mac) for (var i = 0; i < p.mac.length; i++) if (p.mac[i] != null) send({ t: 'perf', what: 1, i: i, v: +p.mac[i] });
      if (p.fill != null) send({ t: 'perf', what: 2, i: 0, v: p.fill ? 1 : 0 });
    },
    pendBpm: null,
    /** The page's corePost() while a native PLAY runs (branch 16): the JS core's ops mapped to bridge messages; everything else (load, patch, kit, buf, ...) is not the page's to send here. */
    legacy: function (m) {
      if (!m || !NC.on) return;
      switch (m.op) {
        case 'loop': return NC.loop(m.l);
        case 'bpm': return NC.bpm(m.v);
        case 'perf': return NC.perf(m);
        case 'hold': return NC.hold();
        case 'lead': return send({ t: 'lead', s: +m.s });
        case 'lxp': return sendLxp(m.st | 0);
        case 'stop': return;   // the page's stop goes through tStop -> NC.stop()
        default: return;
      }
    },
    /** Settings > Audio > Reset audio engine (branch 17): the shell stops, rebuilds and says {t:'engine', up:true}; the page then sends the song again. */
    reset: function () { if (NC.on && tc) { send({ t: 'engine.reset' }); } },
    /** Test and report hooks. */
    state: function () { return { engineUp: engineUp && !lost, playing: playSent, verdict: verdict, rev: revN, heard: songHeard, failed: !!failed, sendCost: sendCost }; }
  };

  // ---- helpers ----------------------------------------------------------------------------------------------------------------------------
  function send(m) { try { tc.send(m); } catch (e) { log('native core client: send ' + (m && m.t) + ' failed', e); } }
  function sendLxp(st) { if (st === lxpSent) return; lxpSent = st; send({ t: 'lxp', st: st }); }
  var nowMs = function () { return host && host.now ? host.now() : Date.now(); };

  // Native PLAY is wanted: Android's shell is the choice (the old engine only when the owner picked "Page (old)"); any other host only on the owner's pick.
  function wanted() {
    var c = host.aset && host.aset.core;
    return c === 'native' || (c !== 'page' && NC.hostName === 'android');
  }

  // The reasons this PLAY cannot be native, in the order of N2-PAGE-CLIENT 4.6. '' = none known (an unknown verdict is not a reason: start() asks).
  function refusal(mode, o) {
    if (down()) return 'the native engine is not running';
    if (!lastClock || nowMs() - lastClock > BEAT_PLAY_MS) return 'the native engine is not running';
    if (failed && failed.ref === host.S()) return 'the native engine stopped on this song earlier';
    if (NC.padProblem) return NC.padProblem;   // PC2: a pad could not go to the engine (memory limit, ...)
    var why = host.blocked ? host.blocked() : '';
    if (why) return why;
    if (current() && !verdict.ok) return reasonText(verdict);
    return '';
  }

  function noVerdict(v) { return !!(v && v.reasons && v.reasons[0] === 'no-verdict'); }   // a timeout, not an answer: never "current"
  function current() { return verdict && !verdict.pending && !noVerdict(verdict) && verdict.sref === host.S() && verdict.ver === host.songVer() && !stale(); }
  function stale() { return sentRef !== host.S() || sentVer !== host.songVer() || (host.lxp ? host.lxp() | 0 : 0) !== lxpSent; }

  var TEXT = { 'no-verdict': 'the native engine did not answer', 'no-kit': 'the kit has not reached the native engine yet' };
  function reasonText(v) { var r = v && v.reasons && v.reasons[0]; return r ? (TEXT[r] || String(r)) : 'the native engine cannot play this song'; }

  // The message, once per song (the S object: a new song is a new object), always in the playback report.
  function fallbackNote(why) {
    try { if (host.pbLog) host.pbLog('native engine: old engine plays this song: ' + why); } catch (e) { /* ignore */ }
    var ref = host.S(); if (toasted === ref) return; toasted = ref;
    host.toast('Playing with the old engine: ' + why, 4000);
  }
  function warnNote(w) {
    try { if (host.pbLog) host.pbLog('native engine: ' + w.join(' · ')); } catch (e) { /* ignore */ }
    var ref = host.S(); if (warned === ref) return; warned = ref;
    host.toast('Native engine: ' + w.join('; '), 4000);
  }

  // A verdict that is not ok: the old engine plays this PLAY. The page's own code runs from the top of tStart (host.fallback stops the stand-in first).
  function handBack(mode, o, tok) { if (tok === playTok) { playTok++; playSent = false; active = false; watch(false); host.fallback(mode, o); } }

  // The song as the shell is to hold it. `ask`, so the reply is the verdict (G14). Keys in this order: the shell reads `t` and `title` from the head, `rev` and `id` from the tail.
  function publish() {
    if (inflight) return inflight.then(function () { return stale() ? publish() : verdict; });
    if (down()) return Promise.resolve(verdict);
    var rev = ++revN, ver = host.songVer(), sref = host.S(), mine = { rev: rev, ver: ver, sref: sref, ok: false, pending: true, reasons: [], warn: [] };
    sentRef = sref; sentVer = ver; verdict = mine;
    try { sendLxp(host.lxp ? host.lxp() | 0 : 0); } catch (e) { /* ignore */ }
    var msg;
    try { msg = { t: 'song.load', title: host.title ? host.title() : '', S: sref, vars: host.vars(), rev: rev }; }
    catch (e) { log('native core client: song.load not made', e); return Promise.resolve(settleV(mine, false, ['no-verdict'], [])); }
    var p = askWithin(msg, ASK_MS).then(function (r) { return onReply(mine, r); }, function (e) {
      log('native core client: song.load got no answer', e); return settleV(mine, false, ['no-verdict'], []); });
    inflight = p; p.then(function () { if (inflight === p) inflight = null; });
    return p;
  }
  // ask() has its own 5 s timeout in the bridge (CORE-BRIDGE 2); this one is for a shell that does not: no answer, no verdict.
  function askWithin(msg, ms) {
    return new Promise(function (res, rej) {
      var t = setTimeout(function () { rej(new Error('no verdict within ' + ms + ' ms')); }, ms + 250);
      Promise.resolve().then(function () { var t0 = nowMs(), p = tc.ask(msg, ms); sendCost = nowMs() - t0; return p; }).then(function (r) { clearTimeout(t); res(r); }, function (e) { clearTimeout(t); rej(e); });
    });
  }
  // PLAY: the song as it is now. Already published and answered for this version: that answer; else publish now (the debounce timer is cancelled).
  function verdictNow() {
    clearTimeout(publishT); publishT = 0;
    if (current()) return Promise.resolve(verdict);
    if (stale() || !verdict || (!verdict.pending && noVerdict(verdict))) return publish();
    return awaitVerdict(verdict);
  }
  function awaitVerdict(v) {
    if (!v.pending) return Promise.resolve(v);
    return new Promise(function (res) { waiters.push({ v: v, res: res }); });
  }
  function onReply(mine, r) {
    if (mine !== verdict) return settleV(mine, false, ['the native engine restarted'], []);   // the engine went down and came back meanwhile: this answer is for nothing
    if (r && r.pending) {   // no kit yet: not a verdict. The shell sends a `fit` event after kit.load; wait for it (5 s), else no verdict.
      var p = awaitVerdict(mine);
      setTimeout(function () { if (mine.pending) settleV(mine, false, ['no-verdict'], []); }, ASK_MS);
      return p;
    }
    var ok = !!(r && r.ok && !r.err), reasons = r && r.reasons && r.reasons.length ? r.reasons.slice() : (r && r.err ? [String(r.err)] : []);
    if (!ok && !reasons.length) reasons = ['the native engine cannot play this song'];
    return settleV(mine, ok, ok ? [] : reasons, r && r.warn ? r.warn.slice() : []);
  }
  function settleV(v, ok, reasons, warn) {
    v.ok = ok; v.pending = false; v.reasons = reasons; v.warn = warn;
    var rest = []; for (var i = 0; i < waiters.length; i++) { if (waiters[i].v === v) waiters[i].res(v); else rest.push(waiters[i]); } waiters = rest;
    // an edit that the shell refused while the old version plays on
    if (!ok && playSent && v === verdict) { try { host.toast('Native engine: ' + reasonText(v) + '. The song keeps playing as it was before this edit.', 4000); } catch (e) { /* ignore */ } }
    return v;
  }

  function sendPlay(mode, o) {
    var m = { t: 'play', mode: mode === 'sect' || mode === 'phrase' ? mode : 'song', row: mode === 'song' ? (o.row | 0) : 0, seed: (Date.now() % 2147483647) | 0 };
    if (o.track != null) m.track = o.track | 0;
    if (o.sect != null) m.sect = o.sect | 0;
    if (o.phrase != null) m.phrase = o.phrase | 0;
    if (o.loop && o.loop.y0 != null) m.loop = { y0: o.loop.y0 | 0, y1: (o.loop.y1 != null ? o.loop.y1 : o.loop.y0) | 0 };
    if (o.only && o.only.length != null) m.only = Array.prototype.slice.call(o.only);
    playSent = true; active = false; send(m);
    // the live state the JS core got at PLAY: PERFORM, the look-ahead, the tempo set while waiting
    try { var pf = host.perf && host.perf(); if (pf) NC.perf(pf); } catch (e) { /* ignore */ }
    if (NC.pendBpm != null) { var b = NC.pendBpm; NC.pendBpm = null; NC.bpm(b); }
    lead(true);
  }
  // The look-ahead as coreLead does it: 0.6 s for the first 1.5 s, then 0.25 (the screen off: growing to 1.2 s). The shell may ignore it.
  function lead(first) {
    clearTimeout(leadT); leadT = 0; if (!playSent) return;
    if (first) { send({ t: 'lead', s: 0.6 }); leadT = setTimeout(function () { lead(false); }, 1500); return; }
    var hidden = typeof document !== 'undefined' && document.hidden;
    if (!hidden) { send({ t: 'lead', s: 0.25 }); return; }
    var s = 0.25; (function up() { if (!playSent || !document.hidden) return; s = Math.min(1.2, s + 0.2); send({ t: 'lead', s: s }); if (s < 1.2) leadT = setTimeout(up, 200); })();
  }
  function onVisible() { quietUntil = nowMs() + 3000; if (playSent) lead(false); }

  // While a native PLAY runs the engine's `clock` must keep coming; a fatal `err` or a silent engine stops the transport and says so (4.6, U37 D14: stop and tell).
  function watch(on) {
    clearInterval(watchT); watchT = 0;
    lastTick = on ? nowMs() : 0;
    if (on) watchT = setInterval(function () {
      var n = nowMs(), late = lastTick && n - lastTick > TICK_LATE_MS; lastTick = n;
      if (late) { quietUntil = n + 600; return; }   // the page's main thread was busy: the `clock` events are still queued, not missing. Skip this round, wait one more period
      if (typeof document !== 'undefined' && document.hidden) quietUntil = n + 3000;   // the screen off: the page's timers and events run late, not the engine
      if (n < quietUntil) return;
      if (lastClock && n - lastClock > BEAT_LOST_MS) died('the engine stopped answering');
    }, 500);
  }
  function died(why) {
    lost = true;   // not engineUp=false: a `clock` event proves the engine alive after all
    var was = active || playSent, sent = playSent;   // (a PLAY waiting for its verdict counts too)
    if (was) failed = { ref: host.S() };
    playTok++; playSent = false; active = false; watch(false); clearTimeout(leadT); leadT = 0;
    if (sent && tc) send({ t: 'stop' });   // best effort: the engine may only have been late
    if (was) { host.stopped(); host.toast('The native engine stopped: ' + why + '. Press PLAY: the old engine plays this song.', 5000); }
  }

  // ---- events from the shell ----------------------------------------------------------------------------------------------------------
  function subscribe() {
    if (!tc || typeof tc.on !== 'function') return;
    var on = function (t, fn) { tc.on(t, fn); offs.push(function () { if (typeof tc.off === 'function') tc.off(t, fn); }); };
    on('clock', function (m) { lastClock = nowMs(); lost = false; if (m && m.sr && m.sr !== NC.sr && m.sr > 0) { /* a rate change arrives with engine / device events: handled there */ } });
    on('song', function (m) { if (m && m.rev != null) songHeard = m.rev; });
    on('fit', function (m) {   // the shell's verdict again (after kit.load, after lxp): for the song last sent
      if (!m || !verdict || m.rev !== verdict.rev) return;
      if (m.pending) return;
      var reasons = m.reasonTexts && m.reasonTexts.length ? m.reasonTexts.slice() : [];
      settleV(verdict, !!m.ok, m.ok ? [] : (reasons.length ? reasons : ['the native engine cannot play this song']), m.warn ? m.warn.slice() : []);
    });
    on('err', function (m) {
      if (!m) return;
      if (m.fatal) return died('' + (m.where || 'error'));
      if (errN++ < 30) try { if (host.pbLog) host.pbLog('native engine: ' + (m.where || '?') + ': ' + (m.msg || '')); } catch (e) { /* ignore */ }   // non-fatal: the playback report only
    });
    on('engine', function (m) {
      if (!m) return;
      if (m.up === false) {   // being rebuilt: a song playing stops (the shell has stopped it), nothing is sent until it is up
        var was = active || playSent; engineUp = false; verdict = null; sentRef = null; inflight = null; playTok++; playSent = false; active = false; watch(false);
        if (was) { host.stopped(); host.toast('The native engine is restarting: the song stopped.', 3000); }
        return; }
      if (m.up === true) {   // rebuilt (engine.reset, a device change): the song goes again (PC2 re-sends the kit and pads itself on this event)
        engineUp = true; lost = false; lastClock = nowMs(); lxpSent = 0; verdict = null; sentRef = null; inflight = null; failed = null;
        var rate = m.sr && m.sr > 0 && m.sr !== NC.sr; if (rate) NC.sr = m.sr;
        if (rate) { try { host.prerender && host.prerender(); } catch (e) { /* ignore */ } }
        publish();
      }
    });
    on('device', function (m) {   // headphones pulled, audio focus lost, the output rebuilt: the shell has stopped; the page shows its transport stopped
      if (!m || !m.stopped) return;
      if (m.sr && m.sr > 0 && m.sr !== NC.sr){ NC.sr = m.sr; try { host.prerender && host.prerender(); } catch (e) { /* ignore */ } }
      var was = active || playSent; playTok++; playSent = false; active = false; watch(false);
      if (was){ host.stopped(); var T = { noisy: 'headphones were unplugged', focus: 'another app took the audio', stream: 'the audio output changed' }; host.toast('Stopped: ' + (T[m.reason] || 'the audio output changed') + '.', 3000); }
    });
    on('state', function (m) { if (m && m.state === 'stopped' && m.arg === 1 && playSent) { playTok++; playSent = false; active = false; watch(false); host.stopped(); } });   // the song ran out (live.h STOPPED arg 1)
    if (typeof document !== 'undefined' && document.addEventListener) { document.addEventListener('visibilitychange', onVisible); offs.push(function () { document.removeEventListener('visibilitychange', onVisible); }); }
  }

  // The stand-in for the page's sequencer while the engine plays (coreStart makes the same): the page reads its tempo and clock and sets the tempo and the loop through it.
  function standIn() {
    var P = { time: 0, tks: 6, base: null, btT: null, _bpm: 120, _alive: true, alive: function () { return this._alive; }, step: function () { return { t: this.time, evs: [], pos: null }; },
      setLoop: function (l) { NC.loop(l); } };
    Object.defineProperty(P, 'bpm', { get: function () { return this._bpm; }, set: function (v) { this._bpm = v; NC.bpm(v); } });
    return P;
  }

  function off(why) {
    NC.on = false; NC.sr = 0; NC.reason = why;
    log('native core client: off (' + why + ')');
    return false;
  }

  function settle(v) { if (!NC.ready) NC.ready = Promise.resolve(v); return v; }

  function num(x, lo, hi, dflt) { x = +x; return isFinite(x) && x >= lo && x <= hi ? x : dflt; }

  // The reply is {t:'reply', id, v, core, caps, sr?, outLat?, host?, maxPadBytes?, maxSongBytes?} (G1 fields are additive).
  function onHello(r) {
    if (!r || typeof r !== 'object') return off('empty hello reply');
    if (r.v !== BRIDGE_V) return off('bridge version ' + r.v + ' (this page speaks ' + BRIDGE_V + ')');
    var caps = r.caps || (tc && tc.caps) || null;
    if (!caps || !caps.nativeAudio) return off('shell has no native audio');
    var sr = num(r.sr, 8000, 384000, 0);
    if (!sr) return off('hello reply has no sample rate');   // an N0 shell (no G1 yet): stay on the page's own engine
    NC.caps = caps; NC.core = String(r.core || '');
    NC.sr = sr; NC.outLat = num(r.outLat, 0, 1e7, 0);
    NC.hostName = String(r.host || (tc && tc.host) || '');
    NC.maxPadBytes = num(r.maxPadBytes, 0, 1e12, 0); NC.maxSongBytes = num(r.maxSongBytes, 0, 1e12, 0);
    NC.reason = ''; NC.on = true; engineUp = true; lost = false; lastClock = 0;
    log('native core client: on (' + NC.hostName + ', ' + sr + ' Hz, core ' + NC.core + ')');
    // Pads rendered before the handshake used the page's guess: render them again at the engine's rate, once (design 4.2 step 4).
    try { if (typeof host.prerender === 'function') host.prerender(); } catch (e) { log('native core client: prerender after hello failed', e); }
    try { subscribe(); } catch (e) { log('native core client: subscribe failed', e); }
    // the kit first (the shell answers a song with `pending` until it has a kit), then the song
    try { var pa = typeof host.padApi === 'function' ? host.padApi() : null; if (pa && typeof NC.kit === 'function') NC.kit(pa); } catch (e) { log('native core client: kit after hello failed', e); }
    try { if (host.songVer) NC.edit(); } catch (e) { /* ignore */ }   // the song goes once, so the first PLAY already has its verdict
    return true;
  }

  /* ---- PC2: pads (N2-PAGE-CLIENT 4.5). The page renders pads (its Worker); the client sends each render to the core:
     put (a COPY: the page keeps cache[i] for its wave display and previews, and a shell may transfer what it is given),
     then buf.use, then buf.free (the shell's put store keeps a buffer until it is freed), then ONE pads.commit 40 ms after the last
     one. Everything goes through one queue, one put in flight at a time, so the order on the port is the order here. ---- */
  var MAX_PAD = 256;          // live.h: pads 0 .. 255
  var COMMIT_MS = 40;         // design 4.5: pads.commit this long after the last buf.use
  var COMMIT_WAIT_MS = 1500;  // how long whenPads() waits for the `pads` event after a commit before it stops waiting
  var BUDGET_FRAC = 0.9;      // design 4.5: over 90 % of maxPadBytes the client stops sending new pads
  var VAR_KEEP = 48e6;        // bytes of variations the client keeps a reference to, to send them again after their pad is replaced (the page's VAR cap)
  var P = {
    api: null, wired: false, gen: 0,
    q: [], running: false, nextId: 1, ver: 0,
    sent: new Map(),          // pad -> {data, data2, sr, bytes, vbytes, ver, kr}: what the core holds
    bytes: 0,                 // bytes held by the core for pads and variations (the budget counts these)
    vars: new Map(),          // 'ii:vk:pad' -> {ii, vk, pad, data, sr, ver (the pad version it went with; 0 = not sent), bytes, sent}
    varKeep: 0, chopSig: new Map(),
    batchN: 0, commitT: 0, commitWait: 0, committed: 0, waiters: [],
    kitSig: '', kitT: 0, kitRev: 0, toasted: false
  };
  NC.padProblem = '';         // '' or why a pad could not go to the core ('pad memory ...'): PC3's can() turns it into a fallback reason

  var PADMEM = /limit|memory|too much|budget|\brc -[45]\b/i;   // the shell's text for a refused native call: 'rc -4' E_LIMIT, 'rc -5' E_MEMORY
  function toastOnce(msg) { if (P.toasted) return; P.toasted = true; try { if (P.api && P.api.toast) P.api.toast(msg, 4500); else if (host && host.toast) host.toast(msg, 4500); } catch (e) { /* ignore */ } }
  function problem(why, pad) {
    if (!NC.padProblem) NC.padProblem = why + (pad != null ? ' (pad ' + pad + ')' : '');
    log('native core client: pads: ' + NC.padProblem);
    toastOnce('Pads are too big for the native engine: playing with the old engine');
  }
  function listen(t, fn) {
    if (!tc || typeof tc.on !== 'function') return;
    tc.on(t, fn);
    if (typeof tc.off === 'function') offs.push(function () { try { tc.off(t, fn); } catch (e) { /* ignore */ } });
  }
  function wire(api) {
    if (api) P.api = api;
    if (P.wired || !tc) return;
    P.wired = true;
    offs.push(function () { padReset(); P.wired = false; P.api = null; });
    listen('pads', function () { if (!NC.on) return; P.committed++; clearTimeout(P.commitWait); P.commitWait = 0; notify(); });
    listen('err', function (m) {
      if (!NC.on || !m) return;
      var w = String(m.where || ''), msg = String(m.msg || '');
      if (/^(buf\.use|put|pad\.chop|pads\.commit|buf\.free)$/.test(w)) { log('native core client: pads: err from ' + w + ': ' + msg); if (PADMEM.test(msg)) problem('pad memory'); }
    });
    listen('engine', function (m) { if (NC.on && m && m.up === true && P.api) { padReset(); NC.kit(P.api); } });   // the shell rebuilt the core (G13): send everything again
  }
  function forget(i) {
    var e = P.sent.get(i); if (!e) return;
    P.bytes -= e.bytes + e.vbytes; P.sent.delete(i); P.chopSig.delete(i);
    P.vars.forEach(function (v, k) { if (v.pad === i) { P.vars.delete(k); P.varKeep -= v.bytes; } });
  }
  function padReset(keepKit) {
    P.gen++; P.q.length = 0; P.running = false;
    clearTimeout(P.commitT); clearTimeout(P.commitWait); P.commitT = P.commitWait = 0;
    P.sent.clear(); P.vars.clear(); P.chopSig.clear(); P.bytes = 0; P.varKeep = 0; P.batchN = 0;
    if (!keepKit) P.kitSig = ''; P.toasted = false; NC.padProblem = '';
    var w = P.waiters; P.waiters = []; for (var i = 0; i < w.length; i++) { clearTimeout(w[i].t); w[i].res(false); }
  }
  function limitBytes() { return NC.maxPadBytes > 0 ? NC.maxPadBytes * BUDGET_FRAC : 0; }
  function idle() { return !P.running && !P.q.length && !P.commitT && !P.commitWait; }
  function notify() {
    if (!idle() || !P.waiters.length) return;
    var w = P.waiters; P.waiters = []; for (var i = 0; i < w.length; i++) { clearTimeout(w[i].t); w[i].res(true); }
  }
  function enqueue(op) {
    if (op.k === 'pad') { for (var i = 0; i < P.q.length; i++) { var o = P.q[i]; if (o.k === 'pad' && o.pad === op.pad && o.data === op.data && o.data2 === op.data2) return; } }
    if (op.k === 'var' || op.k === 'chop') { for (var j = 0; j < P.q.length; j++) { var p = P.q[j]; if (p.k === op.k && p.key === op.key) return; } }
    op.gen = P.gen; P.q.push(op); pump();
  }
  function pump() {
    if (P.running) return;
    if (!P.q.length) {
      if (P.batchN > 0 && !P.commitT) P.commitT = setTimeout(commit, COMMIT_MS);
      notify(); return;
    }
    clearTimeout(P.commitT); P.commitT = 0;
    P.running = true;
    var op = P.q.shift(), gen = P.gen, pr;
    try { pr = runOp(op); } catch (e) { pr = Promise.reject(e); }
    Promise.resolve(pr).then(null, function (e) { opFailed(op, e); }).then(function () { if (gen !== P.gen) return; P.running = false; pump(); });
  }
  function commit() {
    P.commitT = 0; if (!NC.on || !tc || P.batchN <= 0) { notify(); return; }
    var n = P.batchN; P.batchN = 0;
    tc.send({ t: 'pads.commit', n: n });
    P.commitWait = setTimeout(function () { P.commitWait = 0; notify(); }, COMMIT_WAIT_MS);   // the `pads` event ends the wait sooner
  }
  function opFailed(op, e) {
    var msg = e && e.message ? e.message : String(e);
    log('native core client: pads: ' + op.k + (op.pad != null ? ' ' + op.pad : '') + ' failed: ' + msg);
    if (PADMEM.test(msg)) problem('pad memory', op.pad);
  }
  // put the audio (copies), one in flight; the N0 shim's form (one mono Float32Array) only for host 'android-n0' (G2)
  function putCopy(id, sr, chans, frames) {
    if (typeof tc.put !== 'function') return Promise.reject(new Error('this shell has no put'));
    if (NC.hostName === 'android-n0') {
      var mono = chans[0];
      if (chans.length > 1) { for (var k = 0; k < frames; k++) mono[k] = (chans[0][k] + chans[1][k]) * 0.5; }
      return tc.put(id, mono, 1 << 20);
    }
    return tc.put(id, { sr: sr, ch: chans.length, frames: frames }, chans);
  }
  function runOp(op) {
    if (!NC.on || !tc) return;
    if (op.k === 'pad') return runPad(op);
    if (op.k === 'var') return runVar(op);
    if (op.k === 'chop') return runChop(op);
    if (op.k === 'rm') { if (op.raw || P.sent.has(op.pad)) { forget(op.pad); tc.send({ t: 'buf.use', id: -1, pad: op.pad, layer: 0, kind: 'pad', frames: 0 }); P.batchN++; } }
  }
  function runPad(op) {
    var c = P.api && P.api.cache ? P.api.cache(op.pad) : null;
    if (!c || c.data !== op.data) return;                       // a newer render (its own op follows) or a removal (its own op follows)
    var ch = c.data2 && c.data2.length === c.data.length ? 2 : 1, frames = c.data.length, bytes = frames * 4 * ch, old = P.sent.get(op.pad);
    if (NC.padProblem) return;                                  // over budget once: no new pads (the song falls back at its next PLAY)
    var lim = limitBytes();
    if (lim && P.bytes - (old ? old.bytes + old.vbytes : 0) + bytes > lim) { problem('pad memory', op.pad); return; }   // stop sending new pads (design 4.5)
    var chans = [c.data.slice()]; if (ch === 2) chans.push(c.data2.slice());   // COPIES: the page keeps cache[i]; a shell may transfer what put gets
    var id = P.nextId++, gen = op.gen;
    return putCopy(id, c.sr, chans, frames).then(function () {
      if (gen !== P.gen || !NC.on) { tc.send({ t: 'buf.free', id: id }); return; }
      tc.send({ t: 'buf.use', id: id, pad: op.pad, layer: 0, kind: 'pad' });
      tc.send({ t: 'buf.free', id: id });
      var prev = P.sent.get(op.pad);                             // the core drops a pad's variations and chop data when its audio changes: ours go again below
      if (prev) P.bytes -= prev.bytes + prev.vbytes;
      P.chopSig.delete(op.pad);
      P.sent.set(op.pad, { data: op.data, data2: op.data2, sr: c.sr, bytes: bytes, vbytes: 0, ver: ++P.ver, kr: P.kitRev }); P.bytes += bytes; P.batchN++;
      P.vars.forEach(function (v, key) { if (v.pad === op.pad) { v.sent = 0; enqueue({ k: 'var', key: key }); } });
      enqueue({ k: 'chop', pad: op.pad, key: 'c' + op.pad });
    });
  }
  function runVar(op) {
    var v = P.vars.get(op.key), e = v && P.sent.get(v.pad);
    if (!v || !e || v.sent === e.ver) return;
    if (NC.padProblem) return;
    var bytes = v.data.length * 4, lim = limitBytes();
    if (lim && P.bytes + bytes > lim) { problem('pad memory', v.pad); return; }
    var id = P.nextId++, gen = op.gen;
    return putCopy(id, v.sr, [v.data.slice()], v.data.length).then(function () {
      if (gen !== P.gen || !NC.on) { tc.send({ t: 'buf.free', id: id }); return; }
      var e2 = P.sent.get(v.pad); if (!e2 || P.vars.get(op.key) !== v) { tc.send({ t: 'buf.free', id: id }); return; }
      tc.send({ t: 'buf.use', id: id, pad: v.pad, layer: 0, kind: 'var', ii: v.ii, vk: v.vk });
      tc.send({ t: 'buf.free', id: id });
      v.sent = e2.ver; e2.vbytes += bytes; P.bytes += bytes; P.batchN++;
    });
  }
  function chopMsg(api, i) {
    var c = api.cache(i); if (!c || !c.data) return null;
    var insts = api.insts() || [], I = null, J = null;
    for (var k = 0; k < insts.length; k++) { var x = insts[k]; if (!x || x.pad !== i) continue; if ((x.slc | 0) >= 2 && x.smode) { I = x; break; } if (!J && ((x.slc | 0) >= 2 || (x.sync | 0))) J = x; }
    I = I || J; if (!I) return null;
    var n = Math.max(2, Math.min(64, I.slc | 0)), pts = [];
    if (I.smode && (I.slc | 0) >= 2) pts = api.slicePoints(i, I, c.data, c.sr).slice(0, n).map(function (s) { return Math.round(s * NC.sr); });   // FRAMES at the engine's rate (CORE-BRIDGE 7 G4): the shell divides by its rate (nativePadChop)
    return { t: 'pad.chop', pad: i, loopSecs: api.loopSecs(i, c.data, c.sr), n: n, slices: pts };
  }
  function runChop(op) {
    var i = op.pad, e = P.sent.get(i); if (!e || !P.api) return;
    var m; try { m = chopMsg(P.api, i); } catch (er) { log('native core client: chop failed', er); return; }
    var prev = P.chopSig.get(i);
    if (!m) { if (prev) { P.chopSig.delete(i); tc.send({ t: 'pad.chop', pad: i, loopSecs: 0, n: 2, slices: [] }); P.batchN++; } return; }
    var sig = JSON.stringify(m); if (sig === prev) return;
    P.chopSig.set(i, sig); tc.send(m); P.batchN++;
  }

  // ---- what index.html calls (branches 7 to 10: coreBuf, coreKit, coreKitCheck, coreVar, setSong, setKit). `api` comes from ncPadApi() in the page ----
  // A pad's render landed (or was cleared): send it, or take it out of the core.
  NC.pad = function (i, api) {
    wire(api); i = i | 0; if (!NC.on || i < 0 || i >= MAX_PAD) return;
    var c = P.api && P.api.cache ? P.api.cache(i) : null;
    if (!c || !c.data || !c.data.length) {
      for (var k = P.q.length - 1; k >= 0; k--) if (P.q[k].k === 'pad' && P.q[k].pad === i) P.q.splice(k, 1);
      if (P.sent.has(i)) enqueue({ k: 'rm', pad: i });
      return;
    }
    var e = P.sent.get(i);
    if (e && e.data === c.data && e.data2 === (c.data2 || null) && e.sr === c.sr) return;   // what the core holds already (the page's own test, W:25427)
    enqueue({ k: 'pad', pad: i, data: c.data, data2: c.data2 || null, sr: c.sr });
  };
  // A MUT / GEN variation render (ev = {ii, vk, pad}, rec = {sr, data}): sent after its pad, and again whenever its pad is replaced.
  NC.variation = function (ev, rec) {
    if (!NC.on || !ev || !rec || !rec.data || ev.ii == null || ev.pad == null) return;
    var key = ev.ii + ':' + ev.vk + ':' + ev.pad, old = P.vars.get(key);
    if (old && old.data === rec.data) return;
    if (old) { P.varKeep -= old.bytes; if (old.sent) { var oe = P.sent.get(old.pad); if (oe) { oe.vbytes -= old.bytes; P.bytes -= old.bytes; } } }
    var v = { ii: ev.ii | 0, vk: ev.vk, pad: ev.pad | 0, data: rec.data, sr: rec.sr, bytes: rec.data.length * 4, sent: 0 };
    P.vars.set(key, v); P.varKeep += v.bytes;
    P.vars.forEach(function (x, k) { if (P.varKeep > VAR_KEEP && P.vars.size > 1 && x !== v && !x.sent) { P.vars.delete(k); P.varKeep -= x.bytes; } });
    if (P.sent.has(v.pad)) enqueue({ k: 'var', key: key });   // otherwise it goes when its pad does
  };
  // Every slot of the kit that has a render and that the core does not hold yet (PLAY, a new song, the core rebuilt).
  NC.padsAll = function (api) {
    wire(api); if (!NC.on || !P.api) return;
    for (var i = 0, n = Math.min(MAX_PAD, P.api.nslots ? P.api.nslots() : 0); i < n; i++) NC.pad(i, P.api);
    NC.chopAll(P.api);
  };
  // Chop data of every pad the core holds, again if an instrument's slices or sync changed (cheap: sent only when its text changed).
  NC.chopAll = function (api) { wire(api); if (!NC.on) return; P.sent.forEach(function (e, i) { enqueue({ k: 'chop', pad: i, key: 'c' + i }); }); };
  // The kit's voice list {choke, syn, rel, live, lk, sf} per slot, then the pads. Sent only when it changed (or after a reset).
  NC.kit = function (api) {
    wire(api); if (!NC.on || !P.api) return;
    var kv, kit; try { kv = P.api.kitV(); kit = P.api.kitJSON(); } catch (e) { log('native core client: kit list failed', e); return; }
    var sig = JSON.stringify(kv); P.kitT = nowMs();
    // kit.load {kit, rev}: the page's kit (voice definitions, no sample data) is what the shell renders pads from (pad_filler renderPads); it answers `kit {empty:[slots]}`. Sent only when the voice list (kv) changed.
    if (sig !== P.kitSig) {
      P.kitSig = sig; P.kitRev++; tc.send({ t: 'kit.load', kit: kit, rev: P.kitRev });
      // the shell now renders every pad it can, replacing the page's: forget what the core holds (bookkeeping only, no rm), so padsAll puts every pad again and the page's render wins
      P.sent.clear(); P.bytes = 0; P.chopSig.clear(); P.vars.forEach(function (v) { v.sent = 0; });
    }
    NC.padsAll(P.api);
  };
  // The page's 200 ms look at whether the kit changed while the core plays (coreKitCheck).
  NC.kitCheck = function (api) {
    wire(api); if (!NC.on || !P.api) return;
    var t = nowMs(); if (t - P.kitT < 200) return; P.kitT = t;
    try { if (JSON.stringify(P.api.kitV()) !== P.kitSig) NC.kit(P.api); } catch (e) { log('native core client: kit check failed', e); }
  };
  // setKit: a new kit. Every pad the core holds goes; the new renders follow through NC.pad.
  NC.newKit = function (api) {
    wire(api); if (!NC.on) return;
    var had = Array.from(P.sent.keys()); padReset();
    for (var k = 0; k < had.length; k++) P.q.push({ k: 'rm', pad: had[k], raw: true, gen: P.gen });
    pump();
  };
  // setSong: a new song. Variations and chop data belonged to the old one: every pad is sent again (a pad that is gone is removed).
  NC.newSong = function (api) {
    wire(api); if (!NC.on) return;
    var had = Array.from(P.sent.keys()); padReset(true);
    if (P.api && P.api.cache) for (var k = 0; k < had.length; k++) { var c = P.api.cache(had[k]); if (!c || !c.data) P.q.push({ k: 'rm', pad: had[k], raw: true, gen: P.gen }); }
    NC.padsAll(P.api); pump();
  };
  // Resolves true when every queued pad is in the core and committed (the `pads` event came), false after `ms`. PLAY waits on this up to 1.5 s.
  NC.whenPads = function (ms) {
    if (idle()) return Promise.resolve(true);
    return new Promise(function (res) { var w = { res: res, t: setTimeout(function () { var j = P.waiters.indexOf(w); if (j >= 0) P.waiters.splice(j, 1); res(false); }, ms == null ? COMMIT_WAIT_MS : ms) }; P.waiters.push(w); });
  };
  NC.padStats = function () { return { sent: P.sent.size, queued: P.q.length, bytes: P.bytes, vars: P.vars.size, committed: P.committed, idle: idle() }; };

  window.__thunderClient = NC;
})();
