/* window.thunderCore for the Android shell (CORE-BRIDGE section 2). Injected at document start. N0: JSON strings. */
(function () {
  if (window.thunderCore) return;
  var handlers = {}, pending = {}, nextId = 1, port = null, queue = [], caps = null;
  function emit(t, m) { var hs = handlers[t]; if (hs) for (var i = 0; i < hs.length; i++) { try { hs[i](m); } catch (e) {} } }
  function raw(x) { if (port) port.postMessage(x); else queue.push(x); }
  window.addEventListener('message', function (ev) {
    if (ev.data !== 'thunder-port' || !ev.ports || !ev.ports[0]) return;
    port = ev.ports[0];
    port.onmessage = function (e) {
      var d = e.data; if (typeof d !== 'string') return;
      var m; try { m = JSON.parse(d); } catch (x) { return; }
      if (m.t === 'reply' && pending[m.id]) { var p = pending[m.id]; delete pending[m.id]; clearTimeout(p.to); p.res(m); return; }
      if (m.t === 'caps') caps = m;
      emit(m.t, m);
    };
    var q = queue; queue = []; for (var i = 0; i < q.length; i++) port.postMessage(q[i]);
    emit('bridge', { t: 'bridge', up: true });
  });
  var tc = {
    v: 1, host: 'android-n0',
    get caps() { return caps; },
    send: function (msg) { raw(JSON.stringify(msg)); },
    ask: function (msg, ms) {
      return new Promise(function (res, rej) {
        var id = nextId++; msg.id = id;
        pending[id] = { res: res, to: setTimeout(function () { delete pending[id]; rej(new Error('ask timeout')); }, ms || 5000) };
        raw(JSON.stringify(msg));
      });
    },
    on: function (t, fn) { (handlers[t] = handlers[t] || []).push(fn); },
    off: function (t, fn) { var hs = handlers[t]; if (hs) handlers[t] = hs.filter(function (h) { return h !== fn; }); },
    /* Path (a): ArrayBuffer chunks on the port. `f32` is one Float32Array; chunkBytes <= 1 MB, or 0 for one message. */
    put: function (id, f32, chunkBytes) {
      return new Promise(function (res, rej) {
        if (!port) { rej(new Error('bridge not up')); return; }
        var pid = nextId++, total = f32.length, cf = chunkBytes ? (chunkBytes >> 2) : total;
        var to = setTimeout(function () { delete pending[pid]; rej(new Error('put timeout')); }, 120000);
        pending[pid] = { res: res, to: to };
        var onDone = function (m) { if (m.pid === pid) { tc.off('put.done', onDone); clearTimeout(to); delete pending[pid]; res(m); } };
        tc.on('put.done', onDone);
        for (var o = 0; o < total; o += cf) {
          var n = Math.min(cf, total - o);
          var piece = f32.slice(o, o + n);   // a copy: the buffer is transferred below
          port.postMessage(JSON.stringify({ t: 'put.header', pid: pid, id: id, o: o, n: total, last: o + n >= total }));
          port.postMessage(piece.buffer, [piece.buffer]);
        }
      });
    },
    /* Path (b): base64 chunks through thunderFile (addWebMessageListener); the shell writes a file, then the core reads it. */
    putFile: function (id, f32, chunkBytes) {
      return new Promise(function (res, rej) {
        if (!window.thunderFile) { rej(new Error('thunderFile missing')); return; }
        var pid = nextId++, bytes = new Uint8Array(f32.buffer, f32.byteOffset, f32.byteLength), total = bytes.length, cb = chunkBytes || (1 << 20);
        var to = setTimeout(function () { rej(new Error('putFile timeout')); }, 120000);
        window.thunderFile.onmessage = function (e) {
          var m; try { m = JSON.parse(e.data); } catch (x) { return; }
          if (m.t === 'file.done' && m.pid === pid) { clearTimeout(to); res(m); }
        };
        var o = 0;
        (function next() {
          var end = Math.min(o + cb, total), s = '', i, step = 0x8000;
          for (i = o; i < end; i += step) s += String.fromCharCode.apply(null, bytes.subarray(i, Math.min(i + step, end)));
          window.thunderFile.postMessage(pid + '|' + id + '|' + o + '|' + total + '|' + (end >= total ? 1 : 0) + '|' + btoa(s));
          o = end;
          if (o < total) setTimeout(next, 0);
        })();
      });
    }
  };
  window.thunderCore = tc;
})();
