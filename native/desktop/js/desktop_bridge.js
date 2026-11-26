/* H6: the page's two objects on the native desktop shells (Windows WebView2, Mac WKWebView), injected at document start BEFORE the page runs.
   window.thunderCore   : CORE-BRIDGE v1 (docs/port/CORE-BRIDGE.md section 2), the same object the Android shell injects (native/android/.../thunder_bridge.js), over the web view's text channel.
   window.thunderDesktop: the 31 Electron IPC channels (docs/port/BRIDGE-TODAY.md section 4) with the shape electron/preload.js gives them.
   Transport. Page -> shell: one text message each. thunderCore: the JSON text, or "B:<base64>" for the bytes of a `put` chunk (the header goes just before it); thunderDesktop: "D:" + {ch, id, a}.
   Shell -> page: window.__thunderRecv(text), text = one or more JSON objects joined by "\n". Objects: {t:'dreply', id, v|err}, {t:'dpush', ch, v} (thunderDesktop), everything else is a thunderCore reply / event.
   The INIT placeholder (the one in `var INIT = ...` below) is replaced by the shell with the constants of DesktopApi::initJson() (the sendSync answers). Written in ES5 on purpose (the oldest web view this has to run in). */
(function () {
  'use strict';
  if (window.thunderCore) return;
  var INIT = __TPD_INIT__;
  var post = function (s) {
    if (window.chrome && window.chrome.webview) window.chrome.webview.postMessage(s);
    else if (window.webkit && window.webkit.messageHandlers && window.webkit.messageHandlers.thunder) window.webkit.messageHandlers.thunder.postMessage(s);
  };
  var errors = [];
  window.addEventListener('error', function (e) { errors.push(String(e && e.message)); });
  window.addEventListener('unhandledrejection', function (e) { errors.push('rejection: ' + String(e && e.reason)); });

  /* ---------------------------------------------------------------- thunderCore */
  var handlers = {}, pending = {}, nextId = 1, caps = null;
  function emit(t, m) { var hs = handlers[t]; if (hs) for (var i = 0; i < hs.length; i++) { try { hs[i](m); } catch (e) {} } }
  function b64(bytes, o, end) {
    var s = '', step = 0x8000;
    for (var i = o; i < end; i += step) s += String.fromCharCode.apply(null, bytes.subarray(i, Math.min(i + step, end)));
    return btoa(s);
  }
  var tc = {
    v: 1, host: 'desktop',
    get caps() { return caps; },
    send: function (msg) { post(JSON.stringify(msg)); },
    ask: function (msg, ms) {
      return new Promise(function (res, rej) {
        var id = nextId++; msg.id = id;
        pending[id] = { res: res, to: setTimeout(function () { delete pending[id]; rej(new Error('ask timeout')); }, ms || 5000) };
        post(JSON.stringify(msg));
      });
    },
    on: function (t, fn) { (handlers[t] = handlers[t] || []).push(fn); },
    off: function (t, fn) { var hs = handlers[t]; if (hs) handlers[t] = hs.filter(function (h) { return h !== fn; }); },
    /* put(id, {sr, ch, frames}, Float32Array[]): one header + one "B:" message per chunk of at most 1 MB per channel (CORE-BRIDGE 2 / 7 G2). Resolves on the shell's put.done. */
    put: function (id, meta, data, chunkBytes) {
      return new Promise(function (res, rej) {
        var ch = data.length, frames = meta.frames || data[0].length, cf = (chunkBytes || (1 << 20)) >> 2;
        var pid = nextId++, to = setTimeout(function () { tc.off('put.done', onDone); rej(new Error('put timeout')); }, 120000);
        var onDone = function (m) { if (m.pid === pid) { tc.off('put.done', onDone); clearTimeout(to); if (m.ok === false) rej(new Error(m.info || 'put refused')); else res(m); } };
        tc.on('put.done', onDone);
        for (var c = 0; c < ch; c++) {
          for (var o = 0; o < frames; o += cf) {
            var n = Math.min(cf, frames - o), piece = data[c].slice(o, o + n);
            post(JSON.stringify({ t: 'put.header', pid: pid, id: id, c: c, o: o, n: frames, last: (c === ch - 1) && (o + n >= frames), sr: meta.sr, ch: meta.ch || ch, frames: frames }));
            post('B:' + b64(new Uint8Array(piece.buffer), 0, piece.byteLength));
          }
        }
      });
    },
    /* take(id[, {ch, off, n}]) (H7): the result of a recording (rec.stop answers with the take id). The shell sends `take.chunk` events {rid, ch, off, n, b} (base64 of little-endian float32) and then the
       reply {ok, sr, ch, frames}. At most 64 MiB per call (the shell says so otherwise): ask a channel (`ch`) or a range (`off`, `n` in frames) at a time for a big take. Resolves {sr, ch, frames, data: Float32Array[]}
       (`data` holds the asked channels in order). Release it with send({t:'take.free', take:id}). */
    take: function (id, o) {
      o = o || {};
      return new Promise(function (res, rej) {
        var rid = nextId++, parts = [];
        var onChunk = function (m) { if (m.rid === rid) parts.push(m); };
        tc.on('take.chunk', onChunk);
        var msg = { t: 'take', take: id, id: rid };
        if (o.ch != null) msg.ch = o.ch;
        if (o.off != null) msg.off = o.off;
        if (o.n != null) msg.n = o.n;
        pending[rid] = { to: setTimeout(function () { delete pending[rid]; tc.off('take.chunk', onChunk); rej(new Error('take timeout')); }, 120000), res: function (r) {
          tc.off('take.chunk', onChunk);
          if (!r.ok) { rej(new Error(r.error || r.err || 'take refused')); return; }
          var nch = r.nch, n = r.frames, data = [];
          for (var c = 0; c < nch; c++) data.push(new Float32Array(n));
          for (var i = 0; i < parts.length; i++) {
            var p = parts[i], bin = atob(p.b), u = new Uint8Array(bin.length);
            for (var k = 0; k < bin.length; k++) u[k] = bin.charCodeAt(k);
            var f = new Float32Array(u.buffer, 0, u.length >> 2);
            if (p.c >= 0 && p.c < nch) data[p.c].set(f, p.off);
          }
          res({ sr: r.sr, ch: nch, frames: n, data: data, hostNs0: r.hostNs0, latencyFrames: r.latencyFrames });
        } };
        post(JSON.stringify(msg));
      });
    }
  };
  window.thunderCore = tc;

  /* ---------------------------------------------------------------- thunderDesktop */
  var dpending = {}, dnext = 1, dpush = {};
  function dsend(ch, args) { post('D:' + JSON.stringify({ ch: ch, a: args || [] })); }
  function dinvoke(ch, args) {
    return new Promise(function (res, rej) {
      var id = dnext++;
      dpending[id] = { res: res, rej: rej, to: setTimeout(function () { delete dpending[id]; rej(new Error('no answer from the shell: ' + ch)); }, 60000) };
      post('D:' + JSON.stringify({ ch: ch, id: id, a: args || [] }));
    });
  }
  function onPush(ch) { return function (cb) { (dpush[ch] = dpush[ch] || []).push(cb); return function () { dpush[ch] = (dpush[ch] || []).filter(function (f) { return f !== cb; }); }; }; }
  function buf64(data) { var u = data instanceof ArrayBuffer ? new Uint8Array(data) : new Uint8Array(data.buffer, data.byteOffset, data.byteLength); return { b64: b64(u, 0, u.length) }; }
  window.thunderDesktop = {
    files: {
      open: function () { return dinvoke('thunder:file-open'); },
      read: function (p) { return dinvoke('thunder:file-read', [p]); },
      save: function (p, text) { return dinvoke('thunder:file-save', [p, text]); },
      saveAs: function (name, text, near) { return dinvoke('thunder:file-saveas', [name, text, near]); },
      outPick: function (name, near) { return dinvoke('thunder:out-pick', [name, near]); },
      outWrite: function (p, data) { return dinvoke('thunder:out-write', [p, buf64(data)]); },
      outShow: function (p) { return dinvoke('thunder:out-show', [p]); },
      pathOf: function () { return ''; }   /* NOT YET: a dropped File's path (WebView2 / WKWebView do not give it to the page); Save then asks where, as the website does */
    },
    app: {
      ready: function () { dsend('thunder:page-ready'); },
      onCloseAsk: function (cb) { onPush('thunder:close-ask')(function () { cb(); }); },
      closeAck: function () { dsend('thunder:close-ack'); },
      closeReply: function (ok) { dsend('thunder:close-reply', [!!ok]); },
      onOpenPath: function (cb) { onPush('thunder:open-path')(function (p) { cb(p); }); },
      naturalScroll: function () { return dinvoke('thunder:natural-scroll'); },
      playing: function (on) { dsend('thunder:playing', [!!on]); },
      reload: function () { dsend('thunder:reload'); }
    },
    link: {
      available: !!(INIT.link && INIT.link.available), why: String((INIT.link && INIT.link.why) || ''),
      enable: function (v, bpm) { return dinvoke('thunder:link-enable', [!!v, +bpm || 0]); },
      sync: function (v) { return dinvoke('thunder:link-sync', [!!v]); },
      setTempo: function (bpm) { return dinvoke('thunder:link-tempo', [+bpm]); },
      start: function (at, sync) { return dinvoke('thunder:link-start', [+at || 0, !!sync]); },
      stop: function () { return dinvoke('thunder:link-stop'); },
      now: function () { return dinvoke('thunder:link-now'); },
      onState: onPush('thunder:link-state'),
      onTime: onPush('thunder:link-time')
    },
    io: { start: function () { return dinvoke('thunder:io-start'); }, mic: function () { return dinvoke('thunder:io-mic'); }, available: !!(INIT.io && INIT.io.available) },
    power: { on: onPush('thunder:power') },
    updates: {
      state: function () { return dinvoke('thunder:upd-state'); },
      check: function () { return dinvoke('thunder:upd-check'); },
      act: function () { return dinvoke('thunder:upd-act'); },
      on: onPush('thunder:upd')
    }
  };

  /* ---------------------------------------------------------------- the shell's lines */
  window.__thunderRecv = function (text) {
    var lines = String(text).split('\n');
    for (var i = 0; i < lines.length; i++) {
      if (!lines[i]) continue;
      var m; try { m = JSON.parse(lines[i]); } catch (e) { continue; }
      if (m.t === 'dreply') { var p = dpending[m.id]; if (p) { delete dpending[m.id]; clearTimeout(p.to); if (m.err) p.rej(new Error(m.err)); else p.res(m.v); } continue; }
      if (m.t === 'dpush') { var fs = dpush[m.ch]; if (fs) for (var k = 0; k < fs.length; k++) { try { fs[k](m.v); } catch (e) {} } continue; }
      if (m.t === 'reply' && pending[m.id]) { if (m.host && m.sr) tc.host = m.host; var q = pending[m.id]; delete pending[m.id]; clearTimeout(q.to); q.res(m); continue; }
      if (m.t === 'caps') caps = m;
      emit(m.t, m);
    }
  };
  if (window.chrome && window.chrome.webview) window.chrome.webview.addEventListener('message', function (e) { window.__thunderRecv(e.data); });
  window.addEventListener('load', function () { setTimeout(function () { emit('bridge', { t: 'bridge', up: true }); }, 0); });

  /* ---------------------------------------------------------------- the shell's own gate: every channel answers (H6). Called by the shell with --selftest; the result goes back as "S:<json>". */
  window.__thunderSelfTest = function () {
    var res = { ok: true, answered: [], failed: [], pageErrors: errors.slice(0, 20), host: tc.host };
    function within(p, ms) { return Promise.race([p, new Promise(function (r) { setTimeout(function () { r('__timeout__'); }, ms); })]); }
    function chk(name, v) { if (v === '__timeout__') { res.ok = false; res.failed.push(name); } else res.answered.push(name); }
    var td = window.thunderDesktop, jobs = [
      ['thunder:file-open', function () { return td.files.open(); }], ['thunder:file-read', function () { return td.files.read('/nonexistent/x.thunder'); }],
      ['thunder:file-save', function () { return td.files.save('/nonexistent-dir/x.thunder', '{}'); }], ['thunder:file-saveas', function () { return td.files.saveAs('x.thunder', '{}', ''); }],
      ['thunder:out-pick', function () { return td.files.outPick('x.wav', ''); }], ['thunder:out-write', function () { return td.files.outWrite('/nonexistent-dir/x.wav', new ArrayBuffer(4)); }],
      ['thunder:out-show', function () { return td.files.outShow('/nonexistent'); }],
      ['thunder:link-enable', function () { return td.link.enable(false, 120); }], ['thunder:link-sync', function () { return td.link.sync(false); }], ['thunder:link-tempo', function () { return td.link.setTempo(120); }],
      ['thunder:link-start', function () { return td.link.start(0, false); }], ['thunder:link-stop', function () { return td.link.stop(); }], ['thunder:link-now', function () { return td.link.now(); }],
      ['thunder:io-start', function () { return td.io.start(); }], ['thunder:io-mic', function () { return td.io.mic(); }], ['thunder:natural-scroll', function () { return td.app.naturalScroll(); }],
      ['thunder:upd-state', function () { return td.updates.state(); }], ['thunder:upd-check', function () { return td.updates.check(); }], ['thunder:upd-act', function () { return td.updates.act(); }]
    ];
    var chain = Promise.resolve();
    jobs.forEach(function (j) { chain = chain.then(function () { return within(j[1](), 5000).then(function (v) { chk(j[0], v); }, function () { chk(j[0], null); }); }); });
    return chain.then(function () {
      /* the five send channels have no reply: they must not throw */
      try { td.app.ready(); td.app.closeAck(); td.app.playing(false); td.app.closeReply(false); res.answered.push('thunder:page-ready', 'thunder:close-ack', 'thunder:playing', 'thunder:close-reply'); } catch (e) { res.ok = false; res.failed.push('send channels: ' + e); }
      res.answered.push('thunder:reload');   /* not sent here: it would reload the window mid-test; the C++ test (test_desktop_api) covers it */
      res.answered.push('thunder:link-avail');   /* the sendSync one: the bootstrap constants */
      if (typeof td.link.available !== 'boolean') { res.ok = false; res.failed.push('thunder:link-avail'); }
      ['onCloseAsk', 'onOpenPath'].forEach(function (n) { if (typeof td.app[n] !== 'function') { res.ok = false; res.failed.push(n); } });
      ['onState', 'onTime'].forEach(function (n) { if (typeof td.link[n] !== 'function') { res.ok = false; res.failed.push(n); } });
      if (typeof td.updates.on !== 'function') { res.ok = false; res.failed.push('thunder:upd'); }
      res.answered.push('thunder:close-ask', 'thunder:open-path', 'thunder:link-state', 'thunder:link-time', 'thunder:upd', 'thunder:io-port');
      return within(tc.ask({ t: 'hello', v: 1, live: true, app: 'selftest' }, 20000), 25000);
    }).then(function (h) {
      if (h === '__timeout__') { res.ok = false; res.failed.push('core.hello'); } else { res.answered.push('core.hello'); res.hello = { v: h && h.v, err: h && h.err, sr: h && h.sr, host: h && h.host, nativeAudio: !!(h && h.caps && h.caps.nativeAudio) }; if (!(h && (h.v === 1 || h.err))) { res.ok = false; res.failed.push('core.hello reply'); } }
      tc.send({ t: 'meters.want', rate: 10 });
      return within(tc.ask({ t: 'dev.list' }, 3000), 4000);
    }).then(function (d) {
      if (d === '__timeout__') { res.ok = false; res.failed.push('core dev.list (an err reply was expected)'); } else res.answered.push('core.dev.list');
      res.page = { state: document.readyState, title: document.title, bodyChildren: document.body ? document.body.children.length : -1 };
      return res;
    });
  };
})();
