// PC7 of the native page client: the desktop shell's audio devices, input / takes and MIDI (docs/plans/PC7-NOTES.md, protocol docs/port/H7-NOTES.md on `native`), against a FAKE thunderCore.
// Covers: everything behind hello.caps (devices / inputs / midi / rt), dev.list / dev.open (the device's own rates only, buffer, no `in` unless asked), the dev events, rec.* and take (chunked per channel),
// midi.ports / midi.in (timestamp envelope) / midi.cfg (dedupe, clamp, resent after the engine is rebuilt); and source guards for the page side in index.html.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');
const CLIENT = H.read('thunder-core-client.js'), src = H.read('index.html');
const flush = async () => { for (let i = 0; i < 8; i++) await new Promise(r => setImmediate(r)); };

const DEVS = [
  { id: 'a', inId: 'in:a', outId: 'out:a', name: 'Cranborne 500R8', backend: 'coreaudio', inputs: 28, outputs: 28, ins: 28, outs: 28, rates: [44100, 48000, 96000], isDefaultIn: false, isDefaultOut: false },
  { id: 'b', inId: 'in:b', outId: 'out:b', name: 'MacBook Pro Speakers', backend: 'coreaudio', inputs: 1, outputs: 2, ins: 1, outs: 2, rates: [48000], isDefaultIn: true, isDefaultOut: true },
];
function rig({ caps = { nativeAudio: true, devices: true, inputs: true, midi: true, rt: true }, openReply = null, take = null } = {}) {
  const sent = [], asked = [], handlers = {}, toasts = [];
  const sb = { window: null, Promise, Error, JSON, Object, Array, Map, isFinite, Math, Date, Float32Array, setTimeout, clearTimeout, setInterval, clearInterval };
  sb.window = sb;
  const tc = {
    host: 'desktop', send: m => sent.push(JSON.parse(JSON.stringify(m))),
    ask: (m, ms) => { asked.push(Object.assign(JSON.parse(JSON.stringify(m)), { _ms: ms }));
      if (m.t === 'hello') return Promise.resolve({ t: 'reply', v: 1, core: 'h6-live', caps, sr: 48000, outLat: 256, host: 'desktop',
        audio: { open: true, name: 'MacBook Pro Speakers', id: 'out:b', sr: 48000, buf: 512, latOut: 700 }, input: { open: false } });
      if (m.t === 'dev.list') return Promise.resolve({ ok: true, devices: DEVS, out: 'out:b', in: '', audio: { open: true, name: 'MacBook Pro Speakers', id: 'out:b', sr: 48000, buf: 512, latOut: 700 }, input: { open: false } });
      if (m.t === 'dev.open') return Promise.resolve(openReply ? openReply(m) : { ok: true, rebuilt: true, fellBack: false, requested: m.out, audio: { open: true, name: 'x', id: m.out, sr: m.sr || 48000, buf: m.buf || 512, latOut: 300 }, input: m.in ? { open: true, id: m.in, name: 'In', channels: 28, sr: 48000, buf: 512, latIn: 100 } : { open: false } });
      if (m.t === 'rec.arm') return Promise.resolve({ ok: true, nch: m.ins.length });
      if (m.t === 'rec.start') return Promise.resolve({ ok: true, hostNs: 5 });
      if (m.t === 'rec.stop') return Promise.resolve({ ok: true, take: 3, frames: 6, sr: 48000, nch: 2 });
      if (m.t === 'rt.snapshot') return Promise.resolve({ ok: true, calls: 10 });
      if (m.t === 'song.load') return Promise.resolve({ ok: true, rev: m.rev, reasons: [], warn: [] });
      return Promise.resolve({}); },
    take: (id, o) => take ? take(id, o) : Promise.resolve({ sr: 48000, ch: 1, frames: o.n, data: [Float32Array.from({ length: o.n }, (_, i) => o.ch * 100 + o.off + i)], hostNs0: 9, latencyFrames: 4 }),
    on: (t, fn) => { (handlers[t] = handlers[t] || []).push(fn); }, off: (t, fn) => { handlers[t] = (handlers[t] || []).filter(f => f !== fn); },
  };
  sb.thunderCore = tc; vm.createContext(sb); vm.runInContext(CLIENT, sb, { filename: 'thunder-core-client.js' });
  const aset = { core: 'native', natSr: 0, natBuf: 0 }; let ready = 0, mcfg = null, t = 1000;
  const host = { S: () => ({}), songVer: () => 1, aset, toast: m => toasts.push(m), tpLog: () => {}, pbLog: () => {}, now: () => t, prerender: () => {}, build: () => 685, vars: () => ({}), title: () => 'x', lxp: () => 0, perf: () => null,
    blocked: () => '', prep: () => {}, begin: (m, o) => o || {}, fallback: () => {}, stopped: () => {}, h7Ready: () => { ready++; }, midiCfg: () => mcfg };
  const NC = sb.__thunderClient, emit = (type, m) => (handlers[type] || []).forEach(f => f(Object.assign({ t: type }, m)));
  return { sb, NC, host, aset, sent, asked, emit, toasts, handlers, get ready() { return ready; }, set mcfg(v) { mcfg = v; }, set now(v) { t = v; },
    msgs: (...ts) => sent.filter(m => ts.includes(m.t)), asks: (...ts) => asked.filter(m => ts.includes(m.t)) };
}
async function up(o) { const r = rig(o); await r.NC.attach(r.host); await flush(); return r; }

// ---- gating -----------------------------------------------------------------------------------------------------------------------------
test('without the H7 caps nothing is asked, listened to or sent (Android / an older shell)', async () => {
  const r = await up({ caps: { nativeAudio: true, midi: false, inputs: false } });
  assert.equal(r.NC.on, true); assert.equal(r.NC.devOn(), false); assert.equal(r.NC.inOn(), false); assert.equal(r.NC.midiOn(), false);
  assert.equal(r.asks('dev.list').length, 0); assert.equal(r.handlers.dev, undefined); assert.equal(r.handlers['midi.in'], undefined); assert.equal(r.handlers.rec, undefined);
  assert.equal(r.NC.midiCfg({ outs: ['n:x'] }), false); assert.equal(r.msgs('midi.cfg').length, 0);
  const o = await r.NC.devOpen({ out: 'out:a' }); assert.equal(o.ok, false); assert.equal(r.asks('dev.open').length, 0);
  assert.equal((await r.NC.recStart()).ok, false); assert.equal(r.asks('rec.start').length, 0);
  assert.equal(r.ready, 1, 'the host is still told the shell is ready (it decides itself)');
});
test('caps true: dev.list is asked after hello, the hello bodies and the list are the state, latency is kept', async () => {
  const r = await up();
  assert.equal(r.asks('dev.list').length, 1); const S = r.NC.devState();
  assert.equal(S.devices.length, 2); assert.equal(S.wantOut, 'out:b'); assert.equal(S.audio.latOut, 700); assert.equal(r.NC.outLat, 700);
  assert.ok(r.NC.inOn() && r.NC.midiOn() && r.NC.rtOn());
});

test('M1: an Android-shaped hello (caps.midi true, no caps.devices) leaves MIDI on the old path: no listeners, no midi.cfg, midiOn false', async () => {
  const r = await up({ caps: { nativeAudio: true, midi: true, link: false, inputs: false } });
  assert.equal(r.NC.midiOn(), false); assert.equal(r.handlers['midi.ports'], undefined); assert.equal(r.handlers['midi.in'], undefined);
  assert.equal(r.NC.midiCfg({ outs: ['n:x'] }), false); assert.equal(r.msgs('midi.cfg').length, 0);
  r.mcfg = { outs: ['n:A'] }; r.emit('engine', { up: false }); r.emit('engine', { up: true, sr: 48000 }); assert.equal(r.msgs('midi.cfg').length, 0);
});

// ---- dev.open: the device's own rates ---------------------------------------------------------------------------------------------------
test('devRates: the device\'s own list; "" is the system default output; an unknown device has none', async () => {
  const r = await up();
  const J = x => JSON.stringify(x); assert.equal(J(r.NC.devRates('out:a')), '[44100,48000,96000]'); assert.equal(J(r.NC.devRates('out:b')), '[48000]'); assert.equal(J(r.NC.devRates('')), '[48000]'); assert.equal(J(r.NC.devRates('out:zz')), '[]');
});
test('HARD RULE: dev.open sends only a rate the output device has, else 0; buffer 0 or 16..8192', async () => {
  const r = await up();
  await r.NC.devOpen({ out: 'out:a', sr: 96000, buf: 128 }); let m = r.asks('dev.open').pop();
  assert.equal(m.sr, 96000); assert.equal(m.buf, 128); assert.equal(m.out, 'out:a'); assert.equal('in' in m, false, 'the input is left alone unless named');
  await r.NC.devOpen({ out: 'out:a', sr: 88200, buf: 4 }); m = r.asks('dev.open').pop(); assert.equal(m.sr, 0, '88.2 k is not a rate of the 500R8'); assert.equal(m.buf, 0);
  await r.NC.devOpen({ out: 'out:b', sr: 96000 }); m = r.asks('dev.open').pop(); assert.equal(m.sr, 0, 'a rate of ANOTHER device is not offered either');
  await r.NC.devOpen({ out: '', sr: 48000, buf: 9000 }); m = r.asks('dev.open').pop(); assert.equal(m.sr, 48000, 'the system default output is the built-in one: its own rate'); assert.equal(m.buf, 0);
  r.aset.natSr = 192000; r.aset.natBuf = 256; await r.NC.devOpen({ out: 'out:a' }); m = r.asks('dev.open').pop(); assert.equal(m.sr, 0, 'the saved choice goes through the same rule'); assert.equal(m.buf, 256);
  await r.NC.devOpen({ out: 'out:a', sr: 48000, in: 'in:a' }); m = r.asks('dev.open').pop(); assert.equal(m.in, 'in:a'); assert.ok(m._ms >= 30000, 'an input open can wait on the microphone prompt');
});
test('dev.open: the reply becomes the state; a refusal is returned, never thrown; a timeout too', async () => {
  const r = await up({ openReply: m => ({ ok: true, rebuilt: true, fellBack: true, requested: m.out, audio: { open: true, name: 'Speakers', id: 'out:b', sr: 48000, buf: 512, latOut: 650 }, input: { open: false } }) });
  const seen = []; r.NC.listen('dev', m => seen.push(m.kind));
  const a = await r.NC.devOpen({ out: 'out:a' }); assert.equal(a.fellBack, true); assert.equal(r.NC.devState().audio.latOut, 650); assert.equal(r.NC.outLat, 650); assert.deepEqual(seen, ['opened']);
  const r2 = await up({ openReply: () => ({ ok: false, error: 'the device is busy' }) }); const b = await r2.NC.devOpen({ out: 'out:a' }); assert.equal(b.ok, false); assert.equal(b.error, 'the device is busy');
  const r3 = await up(); r3.sb.thunderCore.ask = () => Promise.reject(new Error('ask timeout')); const c = await r3.NC.devOpen({ out: 'out:a' }); assert.equal(c.ok, false); assert.match(c.error, /ask timeout/);
});

// ---- dev events ---------------------------------------------------------------------------------------------------------------------------
test('dev events: changed replaces the list; lost / wantedBack / reopened reach the page; reopened asks dev.list again; an input lost marks it closed', async () => {
  const r = await up(); const seen = []; r.NC.listen('dev', m => seen.push(m));
  r.emit('dev', { kind: 'changed', devices: [DEVS[1]] }); assert.equal(r.NC.devState().devices.length, 1);
  r.emit('dev', { kind: 'changed', wantedBack: 'out', id: 'out:a' }); r.emit('dev', { kind: 'lost', which: 'out', id: 'out:a', fellBack: true, name: 'Speakers' });
  const n = r.asks('dev.list').length; r.emit('dev', { kind: 'reopened', which: 'out', name: 'Speakers', sr: 44100, buf: 256, fellBack: true });
  assert.equal(r.NC.devState().audio.sr, 44100); await flush(); assert.equal(r.asks('dev.list').length, n + 1);
  r.NC.devState().input = { open: true }; r.emit('dev', { kind: 'lost', which: 'in', id: 'in:a' }); assert.equal(r.NC.devState().input.open, false);
  assert.deepEqual(seen.map(m => m.kind + (m.wantedBack ? ':back' : '') + (m.which ? ':' + m.which : '')), ['changed', 'changed:back', 'lost:out', 'reopened:out', 'list', 'lost:in']);
});

// ---- rec / take ---------------------------------------------------------------------------------------------------------------------------
test('rec.arm / start / stop are asks with the shell\'s fields; rec and rec.done events reach listeners', async () => {
  const r = await up(); const got = [];
  r.NC.listen('rec', m => got.push('rec' + m.pk.length)); r.NC.listen('rec.done', m => got.push('done' + m.take));
  assert.equal((await r.NC.recArm([4, 5])).nch, 2); assert.deepEqual(r.asks('rec.arm')[0].ins, [4, 5]);
  assert.equal((await r.NC.recStart()).hostNs, 5); assert.equal((await r.NC.recStop()).take, 3);
  r.emit('rec', { pk: [0.1, 0.2], frames: 10 }); r.emit('rec.done', { take: 7, partial: true, why: 'the input device went away' }); assert.deepEqual(got, ['rec2', 'done7']);
});
test('recTake fetches channel by channel in pieces of at most 8M frames (the shell\'s 64 MiB limit) and frees nothing by itself', async () => {
  const calls = [], r = await up({ take: (id, o) => { calls.push([id, o.ch, o.off, o.n]); return Promise.resolve({ sr: 96000, ch: 1, frames: o.n, data: [new Float32Array(o.n).fill(o.ch + 1)], hostNs0: 11, latencyFrames: 3 }); } });
  const N = 8 * 1024 * 1024 + 10, g = await r.NC.recTake(3, [1, 0], N);
  assert.deepEqual(calls, [[3, 1, 0, 8388608], [3, 1, 8388608, 10], [3, 0, 0, 8388608], [3, 0, 8388608, 10]]);
  assert.equal(g.ok, true); assert.equal(g.sr, 96000); assert.equal(g.data.length, 2); assert.equal(g.data[0].length, N); assert.equal(g.data[0][N - 1], 2); assert.equal(g.data[1][5], 1); assert.equal(g.hostNs0, 11); assert.equal(g.latencyFrames, 3);
  assert.equal(r.msgs('take.free').length, 0); r.NC.recFree(3); assert.deepEqual(r.msgs('take.free')[0].take, 3);
  const bad = await up({ take: () => Promise.reject(new Error('take: at most 64 MiB per call')) }); const e = await bad.NC.recTake(3, [0], 4); assert.equal(e.ok, false); assert.match(e.error, /64 MiB/);
});
test('rt.snapshot is an ask, rt.reset a send; both only with caps.rt', async () => {
  const r = await up(); assert.equal((await r.NC.rtSnapshot()).calls, 10); r.NC.rtReset(); assert.equal(r.msgs('rt.reset').length, 1);
  const n = await up({ caps: { nativeAudio: true, devices: true } }); assert.equal(await n.NC.rtSnapshot(), null); n.NC.rtReset(); assert.equal(n.msgs('rt.reset').length, 0);
});

// ---- MIDI -----------------------------------------------------------------------------------------------------------------------------------
test('midi.ports: the state; "changed" is true only when the device lists move (late / sent change every 3 s)', async () => {
  const r = await up(); const seen = []; r.NC.listen('midi.ports', m => seen.push(m.changed));
  const P = { devs: [{ id: 'n:IAC Bus 1', name: 'IAC Bus 1' }], ins: [{ id: 'n:IAC Bus 1', name: 'IAC Bus 1', port: 3 }], ports: [], late: 0, sent: 5 };
  r.emit('midi.ports', P); r.emit('midi.ports', Object.assign({}, P, { late: 2, sent: 9 })); r.emit('midi.ports', Object.assign({}, P, { ins: [] }));
  assert.equal(JSON.stringify(seen), '[true,false,true]'); const M = r.NC.midiState(); assert.equal(M.heard, true); assert.equal(M.late, 0); assert.equal(M.devs[0].id, 'n:IAC Bus 1');
});
test('midi.in: bytes and port token to listeners; the timestamp is the shell\'s stamp plus the lowest arrival delay (spacing kept, jitter not added)', async () => {
  const r = await up(); const got = []; r.NC.listen('midi.in', m => got.push(m));
  r.now = 5000; r.emit('midi.in', { f: 1, wallNs: 4000e6, port: 3, bytes: [0x90, 60, 100] });   // arrives 1000 ms after its stamp
  r.now = 5502; r.emit('midi.in', { f: 2, wallNs: 4500e6, port: 3, bytes: [0x80, 60, 0] });     // the page was 2 ms late this time
  r.now = 5600; r.emit('midi.in', { wallNs: 5000e6, port: 3, bytes: [] }); r.emit('midi.in', { wall: 5000e6, port: 4, bytes: [0xF8] });
  assert.equal(got.length, 3); assert.deepEqual(got[0].bytes, [0x90, 60, 100]); assert.equal(got[0].port, 3);
  assert.equal(got[0].ts, 5000); assert.ok(Math.abs(got[1].ts - got[0].ts - 500) < 0.1, 'spacing as the shell stamped it, not as the page saw it'); assert.equal(got[2].port, 4);
});
test('midi.cfg: four ids, four clock flags, four delays clamped to -50..200; sent once per change; again after the engine comes back up', async () => {
  const r = await up();
  assert.equal(r.NC.midiCfg({ outs: ['n:A', '', 7, 'n:D'], clockOut: [1, 0, 0, true], gearDelay: [500, -99, 12.4, 'x'], clockIn: 1 }), true);
  const m = r.msgs('midi.cfg')[0]; assert.equal(JSON.stringify([m.outs, m.clockOut, m.gearDelay]), JSON.stringify([['n:A', '', '', 'n:D'], [true, false, false, true], [200, -50, 12, 0]])); assert.equal(m.clockIn, true);
  assert.equal(r.NC.midiCfg({ outs: ['n:A', '', 7, 'n:D'], clockOut: [1, 0, 0, true], gearDelay: [500, -99, 12.4, 'x'], clockIn: 1 }), false, 'the same again is not sent');
  r.mcfg = { outs: ['n:A'], clockOut: [], gearDelay: [], clockIn: false }; r.emit('engine', { up: false }); r.emit('engine', { up: true, sr: 44100 });
  assert.equal(r.msgs('midi.cfg').length, 2, 'the rebuilt shell gets the ports again');
});
test('detach drops the listeners and the state', async () => {
  const r = await up(); let n = 0; r.NC.listen('dev', () => n++); r.NC.detach(); r.emit('dev', { kind: 'lost', which: 'out' }); assert.equal(n, 0); assert.equal(r.NC.devState().devices.length, 0);
});

// ---- the page side (index.html): source guards ------------------------------------------------------------------------------------------
test('index.html: the Electron helper path is not taken on the desktop shell (IOAPI is null when window.thunderCore exists); Electron\'s line is otherwise as before', () => {
  const line = src.split('\n').find(l => /^const IOAPI = /.test(l)); assert.ok(line);
  const run = (w) => { const sb = Object.assign({ window: null }, w); sb.window = sb; vm.createContext(sb); return vm.runInContext(line.replace(/\/\/.*$/, '') + '\nIOAPI', sb); };
  const io = { start() {} };
  assert.equal(run({ thunderDesktop: { io } }), io, 'Electron: no thunderCore, the helper');
  assert.equal(run({ thunderDesktop: { io }, thunderCore: {} }), null, 'the native desktop shell: thunderCore wins');
  assert.equal(run({}), null, 'the website');
});
test('index.html: Settings > Audio offers only the device\'s own rates (naRate is built from devRates and nothing else) and the old Rate row is hidden on the shell', () => {
  assert.ok(/fill\(\$\('naRate'\), \[\[0, 'Device'\]\]\.concat\(rates\.map\(r => \[r, naFmt\(r, 'Hz'\)\]\)\)/.test(src), 'naRate = Device + rates');
  assert.ok(/const rates = C\.devRates\(\$\('naOut'\)\.value\)/.test(src));
  assert.ok(/for \(const el of \[\$\('audOutRow'\), \$\('segLat'\)\.parentElement, \$\('segARate'\)\.parentElement\]\) if \(el\) el\.hidden = !!C;/.test(src), 'the web context\'s Output / Latency / Rate rows (44.1 k / 48 k) are hidden');
  assert.equal((src.match(/natSr/g) || []).length >= 3, true);
  assert.ok(!/naRate[^\n]*(44100|48000|96000)/.test(src), 'no rate number is written next to naRate');
});
test('index.html: every PC7 entry point is behind natDev() / natIn() / natMidi() (the hello caps); the old paths keep their text', () => {
  assert.ok(/const natDev = \(\) => \{ const c = natC\(\); return c && c\.devOn && c\.devOn\(\) \? c : null; \};/.test(src));
  assert.ok(/if \(natIn\(\)\)\{ const okN = await rcdOpenNat\(\);/.test(src), 'rcdOpen1');
  assert.ok(/if \(natMidi\(\)\)\{ const acc = await ncMidiConnect\(\);/.test(src), 'connectMidi1');
  assert.ok(/function audStatus\(\)\{\n  const st = \$\('audStat'\); if \(!st\) return;\n  if \(natDev\(\)\)\{ natAudSync\(\); return; \}/.test(src), 'audStatus');
  assert.ok(/keys:midi\.keys, names:midi\.names\}\)\); \} catch\(e\)\{\} try \{ midiCfgPush\(\); \} catch\(e\)\{\} \};/.test(src), 'saveMidi pushes midi.cfg');
  assert.ok(/\n  if \(natIn\(\) && !E\)\{ ?/.test(src) || /if \(natIn\(\) && !E\)\{/.test(src), 'the Record window\'s input list');
  assert.ok(/h7Ready:\(\) => natAudInit\(\)/.test(src) && /midiCfg:\(\) => \(\{outs:midi\.outIds\.slice\(\)/.test(src), 'the host gives the client its two hooks');
  assert.ok(/if \(rcdSet\.mode === 'free' \|\| \(RCD\.io && RCD\.io\.nat\)\)/.test(src), 'pattern mode records free on the native input');
  // the page never writes a PC7 message itself: only the client does
  for (const t of ['dev.open', 'dev.list', 'rec.arm', 'rec.start', 'rec.stop', 'midi.cfg']) assert.ok(!new RegExp("t:\\s*'" + t.replace('.', '\\.') + "'").test(src), t + ' is sent by the client only');
});
test('the client file and the service worker name the same ?v=', () => {
  const sw = H.read('sw.js'); assert.ok(sw.includes("'./thunder-core-client.js?v=8'")); assert.ok(src.includes("s.src = 'thunder-core-client.js?v=8'"));
  assert.ok(/const BUILD = 685;/.test(src)); assert.ok(sw.includes("'thunderplus-v685'"));
});
