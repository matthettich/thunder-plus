/* thunder-core-client.js: the page's client of window.thunderCore (docs/port/N2-PAGE-CLIENT.md, docs/port/CORE-BRIDGE.md).
   PC1 = the loader's target and the `hello` handshake. PC3 = the song and the transport: publish the whole song (song.load, debounced), PLAY / STOP / HOLD / bpm / loop / perf,
   can() and the fallback to the old engine, engine down / up, device stopped. Pads, kit, mixer diffs, events for the playhead and meters are later PRs (PC2, PC4, PC5).
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
  var BEAT_PLAY_MS = 2000;   // can(): a `clock` event within 2 s (4.6)
  var BEAT_LOST_MS = 2500;   // while playing: no `clock` for this long = the engine is gone (the design says 1 s; 2.5 s is the page's own audioInterrupted precedent)
  var revN = 0;              // song.load revision counter
  var verdict = null;        // {rev, ver, sref, ok, pending, reasons[], warn[]}: the answer for the song as last published
  var waiters = [];          // resolvers waiting for a verdict (pending / in flight)
  var inflight = null;       // the song.load ask now out
  var publishT = 0, watchT = 0, leadT = 0;
  var sentVer = -1, sentRef = null;   // what the last publish was made of
  var lxpSent = 0;
  var engineUp = true, lastClock = 0, songHeard = 0, quietUntil = 0;
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
      NC.ready = tc.ask({ t: 'hello', v: BRIDGE_V, app: typeof host.build === 'function' ? host.build() : 0 }, HELLO_MS)
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
      clearTimeout(publishT); publishT = setTimeout(function () { publishT = 0; if (stale()) publish(); }, PUBLISH_MS);
    },

    // ---- PC3: the transport --------------------------------------------------------------------------------------------------------------
    /** Decided once per PLAY, before anything sounds (4.6). true = the native engine takes this PLAY (start() follows); false = the old engine plays it. */
    can: function (mode, o) {
      if (!NC.on || !tc) return false;
      if (o && (o.page || o._nf || o.rz)) return false;   // the page asked for its own engine (hand-over, the fallback itself): not ours, no message
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
      try { if (typeof NC.kit === 'function') NC.kit(); } catch (e) { log('native core client: kit hook failed', e); }
      try { if (typeof NC.settle === 'function') tasks.push(NC.settle(1500)); } catch (e) { log('native core client: pads hook failed', e); }
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
    state: function () { return { engineUp: engineUp, playing: playSent, verdict: verdict, rev: revN, heard: songHeard, failed: !!failed }; }
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
    if (!engineUp) return 'the native engine is not running';
    if (!lastClock || nowMs() - lastClock > BEAT_PLAY_MS) return 'the native engine is not running';
    if (failed && failed.ref === host.S()) return 'the native engine stopped on this song earlier';
    var why = host.blocked ? host.blocked() : '';
    if (why) return why;
    if (current() && !verdict.ok) return reasonText(verdict);
    return '';
  }

  function current() { return verdict && !verdict.pending && verdict.sref === host.S() && verdict.ver === host.songVer(); }
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
    if (!engineUp) return Promise.resolve(verdict);
    var rev = ++revN, ver = host.songVer(), sref = host.S(), mine = { rev: rev, ver: ver, sref: sref, ok: false, pending: true, reasons: [], warn: [] };
    sentRef = sref; sentVer = ver; verdict = mine;
    try { sendLxp(host.lxp ? host.lxp() | 0 : 0); } catch (e) { /* ignore */ }
    var msg;
    try { msg = { t: 'song.load', title: host.title ? host.title() : '', S: sref, vars: host.vars(), rev: rev }; }
    catch (e) { log('native core client: song.load not made', e); return Promise.resolve(settleV(mine, false, ['no-verdict'], [])); }
    var p = Promise.resolve().then(function () { return tc.ask(msg, ASK_MS); }).then(function (r) { return onReply(mine, r); }, function (e) {
      log('native core client: song.load got no answer', e); return settleV(mine, false, ['no-verdict'], []); });
    inflight = p; p.then(function () { if (inflight === p) inflight = null; });
    return p;
  }
  // PLAY: the song as it is now. Already published and answered for this version: that answer; else publish now (the debounce timer is cancelled).
  function verdictNow() {
    clearTimeout(publishT); publishT = 0;
    if (current()) return Promise.resolve(verdict);
    if (stale() || !verdict) return publish();
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
    if (on) watchT = setInterval(function () {
      var n = nowMs();
      if (typeof document !== 'undefined' && document.hidden) quietUntil = n + 3000;   // the screen off: the page's timers and events run late, not the engine
      if (n < quietUntil) return;
      if (lastClock && n - lastClock > BEAT_LOST_MS) died('the engine stopped answering');
    }, 500);
  }
  function died(why) {
    engineUp = false;
    var was = active || playSent;   // (a PLAY waiting for its verdict counts too)
    if (was) failed = { ref: host.S() };
    playTok++; playSent = false; active = false; watch(false);
    if (was) { host.stopped(); host.toast('The native engine stopped: ' + why + '. Press PLAY: the old engine plays this song.', 5000); }
  }

  // ---- events from the shell ----------------------------------------------------------------------------------------------------------
  function subscribe() {
    if (!tc || typeof tc.on !== 'function') return;
    var on = function (t, fn) { tc.on(t, fn); offs.push(function () { if (typeof tc.off === 'function') tc.off(t, fn); }); };
    on('clock', function (m) { lastClock = nowMs(); if (m && m.sr && m.sr !== NC.sr && m.sr > 0) { /* a rate change arrives with engine / device events: handled there */ } });
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
      if (m.up === true) {   // rebuilt (engine.reset, a device change): the song goes again, then the kit and the pads (PC2's hook)
        engineUp = true; lastClock = nowMs(); lxpSent = 0; verdict = null; sentRef = null; inflight = null; failed = null;
        var rate = m.sr && m.sr > 0 && m.sr !== NC.sr; if (rate) NC.sr = m.sr;
        try { if (typeof NC.resend === 'function') NC.resend(); } catch (e) { log('native core client: resend failed', e); }
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
    NC.reason = ''; NC.on = true; engineUp = true; lastClock = 0;
    log('native core client: on (' + NC.hostName + ', ' + sr + ' Hz, core ' + NC.core + ')');
    // Pads rendered before the handshake used the page's guess: render them again at the engine's rate, once (design 4.2 step 4).
    try { if (typeof host.prerender === 'function') host.prerender(); } catch (e) { log('native core client: prerender after hello failed', e); }
    try { subscribe(); } catch (e) { log('native core client: subscribe failed', e); }
    try { if (host.songVer) NC.edit(); } catch (e) { /* ignore */ }   // the song goes once, so the first PLAY already has its verdict
    return true;
  }

  window.__thunderClient = NC;
})();
