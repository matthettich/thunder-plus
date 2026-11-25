/* thunder-core-client.js: the page's client of window.thunderCore (docs/port/N2-PAGE-CLIENT.md, docs/port/CORE-BRIDGE.md).
   PC1 = the loader's target and the `hello` handshake only. Nothing here changes how the page sounds: it sets `on`, and
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
      NC.ready = tc.ask({ t: 'hello', v: BRIDGE_V, app: typeof host.build === 'function' ? host.build() : 0 }, HELLO_MS)
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

  window.__thunderClient = NC;
})();
