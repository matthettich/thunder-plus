/* thunder-core-client.js: the page's client of window.thunderCore (docs/port/N2-PAGE-CLIENT.md, docs/port/CORE-BRIDGE.md).
   PC1 = the loader's target and the `hello` handshake. PC2 = pads (put / buf.use / buf.free / pads.commit / pad.chop, kit.load, the pad memory budget). Nothing here changes how the page sounds: it sets `on`, and
   with `on` true the only page line that reacts is padSR() (pads render at the engine's rate, not an AudioContext's).
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
    }
  };

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
    NC.reason = ''; NC.on = true;
    log('native core client: on (' + NC.hostName + ', ' + sr + ' Hz, core ' + NC.core + ')');
    // Pads rendered before the handshake used the page's guess: render them again at the engine's rate, once (design 4.2 step 4).
    try { if (typeof host.prerender === 'function') host.prerender(); } catch (e) { log('native core client: prerender after hello failed', e); }
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

  function nowMs() { return typeof host.now === 'function' ? host.now() : Date.now(); }
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
    listen('kit', onKitEvent);
    listen('err', function (m) {
      if (!NC.on || !m) return;
      var w = String(m.where || ''), msg = String(m.msg || '');
      if (/^(buf\.use|put|pad\.chop|pads\.commit|buf\.free)$/.test(w)) { log('native core client: pads: err from ' + w + ': ' + msg); if (/limit|memory|too much|budget/i.test(msg)) problem('pad memory'); }
    });
    listen('engine', function (m) { if (NC.on && m && m.up === true && P.api) { padReset(); NC.kit(P.api); } });   // the shell rebuilt the core (G13): send everything again
  }
  function onKitEvent(m) {
    // After kit.load the core lists the slots it holds no pad for. A pad this client sent BEFORE that kit.load is gone from it: send it again.
    if (!NC.on || !m || !m.empty || !P.api) return;
    for (var k = 0; k < m.empty.length; k++) {
      var i = m.empty[k] | 0, e = P.sent.get(i);
      if (e && e.kr < P.kitRev) { forget(i); NC.pad(i, P.api); }
    }
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
    if (/limit|memory|too much|budget/i.test(msg)) problem('pad memory', op.pad);
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
    if (sig !== P.kitSig) { P.kitSig = sig; P.kitRev++; tc.send({ t: 'kit.load', kit: kit, rev: P.kitRev }); }
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
