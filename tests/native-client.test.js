// PC2 of the native page client (docs/port/N2-PAGE-CLIENT.md 4.5, 5, 6): the client's pad code against a FAKE thunderCore (an in-process object).
// Order (put -> buf.use -> buf.free, one put in flight, kit.load before pads, pads.commit once 40 ms after the last), copies (the page's arrays are never
// handed over or detached), the memory budget, removal, variations / chop data after their pad, a new kit / song, the core rebuilt.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');
const wait = ms => new Promise(r => setTimeout(r, ms));
const plain = x => JSON.parse(JSON.stringify(x));   // the client runs in a vm: its objects have another realm's prototypes

function rig({ maxPadBytes = 0, sr = 48000, transfer = true, putMs = 1, putFail = null } = {}) {
  const log = [], handlers = {}; let inflight = 0, maxInflight = 0;
  const tc = {
    host: 'android',
    send: m => log.push(['send', m]),
    ask: m => Promise.resolve({ t: 'reply', v: 1, core: 't', caps: { nativeAudio: true }, sr, outLat: 0, host: 'android', maxPadBytes, maxSongBytes: 16 << 20 }),
    put: (id, meta, arrs) => { if (ArrayBuffer.isView(meta)) { log.push(['putN0', id, Array.from(meta), arrs]); return Promise.resolve(); }   // the N0 shim's form
      inflight++; maxInflight = Math.max(maxInflight, inflight); log.push(['put', id, meta, arrs.map(a => Array.from(a)), arrs]);
      if (transfer) for (const a of arrs) { try { structuredClone(a.buffer, { transfer: [a.buffer] }); } catch (e) { /* node < 21 */ } }   // a shell that takes ownership detaches what it is given
      return wait(putMs).then(() => { inflight--; if (putFail) throw putFail; log.push(['put-done', id]); }); },
    on: (t, fn) => { (handlers[t] = handlers[t] || []).push(fn); },
    off: (t, fn) => { handlers[t] = (handlers[t] || []).filter(f => f !== fn); }
  };
  const emit = (t, m) => (handlers[t] || []).forEach(f => f(Object.assign({ t }, m)));
  const sb = { window: null, Promise, Error, JSON, Object, Array, Map, Math, isFinite, setTimeout, clearTimeout, Float32Array, Date, thunderCore: tc };
  sb.window = sb; vm.createContext(sb); vm.runInContext(H.read('thunder-core-client.js'), sb, { filename: 'thunder-core-client.js' });
  const toasts = [];
  // the page: a cache of renders, a kit, instruments
  const page = { cache: [], insts: [], kv: [{ choke: 0, syn: false, rel: 0 }], n: 4, kitHits: 0 };
  const api = { cache: i => page.cache[i] || null, nslots: () => page.n, kitV: () => page.kv, kitJSON: () => ({ app: 'thunder', voices: page.kv.map((v, i) => ({ name: 'p' + i, kv: v })) }), insts: () => page.insts,
    loopSecs: (pad, data, sr0) => data.length / sr0, slicePoints: (pad, I, data, sr0) => Array.from({ length: I.slc }, (_, k) => k / I.slc), toast: m => toasts.push(m) };
  const host = { tpLog: () => {}, build: () => 678, prerender: () => {}, now: () => Date.now() };
  const NC = sb.__thunderClient;
  const types = () => log.map(l => l[0] === 'send' ? l[1].t : l[0]);
  const render = (i, n, v, stereo) => { const c = { sr, data: new Float32Array(n).fill(v), data2: stereo ? new Float32Array(n).fill(-v) : null }; page.cache[i] = c; return c; };
  return { NC, sb, tc, log, emit, api, host, page, toasts, types, render, get maxInflight() { return maxInflight; } };
}
const on = async r => { assert.equal(await r.NC.attach(r.host), true); return r; };

test('a pad render goes put -> buf.use -> buf.free, then ONE pads.commit 40 ms after the last; the page\'s arrays are copies, never handed over', async () => {
  const r = await on(rig()); const c0 = r.render(0, 100, 0.5), c1 = r.render(1, 50, 0.25, true);
  r.NC.pad(0, r.api); r.NC.pad(1, r.api);
  await wait(10); assert.ok(!r.types().includes('pads.commit'), 'not before 40 ms of quiet');
  await r.NC.whenPads(60).then(ok => assert.equal(ok, false, 'no `pads` event yet'));
  assert.deepEqual(r.types(), ['put', 'put-done', 'buf.use', 'buf.free', 'put', 'put-done', 'buf.use', 'buf.free', 'pads.commit']);
  const puts = r.log.filter(l => l[0] === 'put');
  assert.deepEqual(plain(puts[0][2]), { sr: 48000, ch: 1, frames: 100 }); assert.deepEqual(plain(puts[1][2]), { sr: 48000, ch: 2, frames: 50 });
  assert.equal(puts[1][4].length, 2);
  for (const [k, c] of [[0, c0], [1, c1]]) { assert.notEqual(puts[k][4][0], c.data, 'a copy'); assert.equal(c.data.byteLength, c.data.length * 4, 'the page\'s array is not detached'); assert.equal(c.data[0], k ? 0.25 : 0.5); }
  assert.equal(puts[1][3][1][0], -0.25);
  const use = r.log.filter(l => l[0] === 'send' && l[1].t === 'buf.use').map(l => l[1]);
  assert.deepEqual(use.map(u => [u.pad, u.kind, u.id === puts[use.indexOf(u)][1]]), [[0, 'pad', true], [1, 'pad', true]]);
  assert.equal(r.log.filter(l => l[0] === 'send' && l[1].t === 'pads.commit')[0][1].n, 2);
  assert.equal(r.maxInflight, 1, 'one put in flight at a time');
  r.emit('pads', {}); assert.equal(await r.NC.whenPads(50), true);
});

test('buf.use is not sent before its put resolves', async () => {
  const r = await on(rig({ putMs: 60 })); r.render(0, 10, 1); r.NC.pad(0, r.api);
  await wait(20); assert.deepEqual(r.types(), ['put']); await wait(80); assert.deepEqual(r.types().slice(0, 4), ['put', 'put-done', 'buf.use', 'buf.free']);
});

test('the same render is not sent twice; a newer render replaces it and only the newest of two queued ones goes', async () => {
  const r = await on(rig()); r.render(0, 10, 1); r.NC.pad(0, r.api); await wait(20); r.NC.pad(0, r.api); await wait(20);
  assert.equal(r.log.filter(l => l[0] === 'put').length, 1);
  const slow = await on(rig({ putMs: 30 })); slow.render(5, 10, 9); slow.NC.pad(5, slow.api);   // a put in flight: what follows queues
  slow.render(0, 10, 2); slow.NC.pad(0, slow.api); slow.render(0, 10, 3); slow.NC.pad(0, slow.api); await wait(120);
  const puts = slow.log.filter(l => l[0] === 'put'); assert.equal(puts.length, 2, 'the render that was replaced before its turn is skipped'); assert.equal(puts[1][3][0][0], 3);
});

test('kit.load goes first, then the pads; it is sent again only when the voice list changed', async () => {
  const r = await on(rig()); r.render(0, 10, 1); r.render(2, 10, 1);
  r.NC.kit(r.api); await wait(30);
  assert.deepEqual(r.types().slice(0, 3), ['kit.load', 'put', 'put-done']); assert.deepEqual(plain(r.log[0][1].kit.voices.map(v => v.kv)), plain(r.page.kv)); assert.equal(r.log[0][1].kv, undefined); assert.equal(r.log[0][1].rev, 1);
  await wait(60); const kits = () => r.log.filter(l => l[0] === 'send' && l[1].t === 'kit.load').map(l => l[1]);
  const before = r.log.length; r.NC.kit(r.api); await wait(10); assert.equal(r.log.length, before, 'same list, same pads: nothing'); assert.equal(kits().length, 1);
  r.page.kv = [{ choke: 1, syn: true, rel: 0.1 }]; r.NC.kit(r.api); await wait(10); assert.equal(kits().length, 2); assert.equal(kits()[1].rev, 2);
  // the 200 ms check while playing
  r.page.kv = [{ choke: 2 }]; r.NC.kitCheck(r.api); await wait(5); assert.equal(kits().length, 2, 'throttled right after a kit'); await wait(210);
  r.NC.kitCheck(r.api); await wait(5); assert.equal(kits().length, 3);
});

test('the core lists empty slots after kit.load: a pad sent before that kit.load is sent again, one sent after it is not', async () => {
  const r = await on(rig()); r.render(0, 10, 1); r.render(1, 10, 7); r.NC.pad(0, r.api); await wait(20);
  r.NC.kit(r.api); await wait(30); const puts0 = r.log.filter(l => l[0] === 'put').length;   // pad 1 goes after the kit.load, pad 0 stays
  r.emit('kit', { empty: [0, 1] }); await wait(30);
  const puts = r.log.filter(l => l[0] === 'put'); assert.equal(puts.length, puts0 + 1); assert.equal(puts[puts.length - 1][3][0][0], 1, 'pad 0 (value 1) again; pad 1 (value 7) went after the kit.load and stays');
});

test('budget: over 90 % of maxPadBytes no new pad is sent, one toast, padProblem says why; a smaller pad does not clear it', async () => {
  const r = await on(rig({ maxPadBytes: 1000 }));   // 900 usable = 225 floats
  r.render(0, 100, 1); r.render(1, 100, 1); r.render(2, 100, 1); r.render(3, 10, 1);
  r.NC.pad(0, r.api); r.NC.pad(1, r.api); r.NC.pad(2, r.api); r.NC.pad(3, r.api); await wait(80);
  const puts = r.log.filter(l => l[0] === 'put'); assert.equal(puts.length, 2, 'pad 2 (400 bytes more) would pass 900: refused; pad 3 is not sent after a refusal');
  assert.equal(r.NC.padProblem, 'pad memory (pad 2)'); assert.deepEqual(r.toasts, ['Pads are too big for the native engine: playing with the old engine']);
  r.render(1, 10, 2); r.NC.pad(1, r.api); await wait(30); assert.equal(r.toasts.length, 1, 'one toast only'); assert.equal(r.log.filter(l => l[0] === 'put').length, 2, 'nothing more goes once over');
  assert.ok(r.NC.padStats().bytes <= 900);
});
test('budget: a replaced pad frees its old bytes first; maxPadBytes 0 (not given) means the client sets no limit', async () => {
  const r = await on(rig({ maxPadBytes: 1000 })); r.render(0, 200, 1); r.NC.pad(0, r.api); await wait(30);   // 800 bytes
  r.render(0, 220, 2); r.NC.pad(0, r.api); await wait(30); assert.equal(r.NC.padProblem, ''); assert.equal(r.NC.padStats().bytes, 880);
  const u = await on(rig({ maxPadBytes: 0 })); u.render(0, 1e5, 1); u.NC.pad(0, u.api); await wait(30); assert.equal(u.NC.padProblem, '');
});
test('budget: the core refusing (an err event, or a put that fails with a limit) sets padProblem too', async () => {
  const r = await on(rig()); r.NC.pad(0, r.api); r.emit('err', { where: 'buf.use', msg: 'E_LIMIT pad budget', fatal: false }); assert.equal(r.NC.padProblem, 'pad memory'); assert.equal(r.toasts.length, 1);
  const q = await on(rig({ putFail: new Error('put: too much audio held') })); q.render(0, 10, 1); q.NC.pad(0, q.api); await wait(30);
  assert.match(q.NC.padProblem, /^pad memory \(pad 0\)/); assert.ok(!q.types().includes('buf.use'));
});

test('removal: a pad whose render is cleared is taken out of the core (zero frames); a pad the core never had is not mentioned; a queued send is cancelled', async () => {
  const r = await on(rig()); r.render(0, 10, 1); r.NC.pad(0, r.api); await wait(30); r.page.cache[0] = null; r.NC.pad(0, r.api); r.NC.pad(3, r.api); await wait(60);
  const rm = r.log.filter(l => l[0] === 'send' && l[1].frames === 0).map(l => l[1]); assert.equal(rm.length, 1); assert.equal(rm[0].pad, 0); assert.equal(rm[0].kind, 'pad');
  assert.equal(r.NC.padStats().bytes, 0); assert.equal(r.NC.padStats().sent, 0);
  const q = await on(rig({ putMs: 30 })); q.render(0, 10, 1); q.render(1, 10, 1); q.NC.pad(0, q.api); q.NC.pad(1, q.api); q.page.cache[1] = null; q.NC.pad(1, q.api); await wait(100);
  assert.equal(q.log.filter(l => l[0] === 'put').length, 1, 'pad 1 was cleared before its turn'); assert.equal(q.log.filter(l => l[0] === 'send' && l[1].frames === 0).length, 0);
});

test('variations go after their pad, again after the pad is replaced, and are released with it', async () => {
  const r = await on(rig()); r.render(0, 10, 1); const vd = new Float32Array(5).fill(0.5);
  r.NC.variation({ ii: 2, vk: 'm3', pad: 0 }, { sr: 48000, data: vd }); await wait(10); assert.equal(r.log.filter(l => l[0] === 'put').length, 0, 'its pad is not in the core yet: it waits');
  r.NC.pad(0, r.api); await wait(60);
  const uses = r.log.filter(l => l[0] === 'send' && l[1].t === 'buf.use').map(l => l[1]); assert.deepEqual(uses.map(u => u.kind), ['pad', 'var']);
  assert.deepEqual([uses[1].ii, uses[1].vk, uses[1].pad], [2, 'm3', 0]); assert.notEqual(r.log.filter(l => l[0] === 'put')[1][4][0], vd);
  r.NC.variation({ ii: 2, vk: 'm3', pad: 0 }, { sr: 48000, data: vd }); await wait(10); assert.equal(r.log.filter(l => l[0] === 'put').length, 2, 'the same variation again: nothing');
  r.render(0, 12, 3); r.NC.pad(0, r.api); await wait(80);
  assert.deepEqual(r.log.filter(l => l[0] === 'send' && l[1].t === 'buf.use').map(l => l[1].kind), ['pad', 'var', 'pad', 'var'], 'the core dropped it with the old pad audio: sent again');
  assert.equal(r.NC.padStats().bytes, 12 * 4 + 5 * 4);
  r.page.cache[0] = null; r.NC.pad(0, r.api); await wait(40); assert.equal(r.NC.padStats().bytes, 0); assert.equal(r.NC.padStats().vars, 0);
});

test('chop data follows its pad: loop seconds, slices only with SMODE, again after a new render, removed when the instrument stops using it', async () => {
  const r = await on(rig()); r.page.insts = [{ pad: 0, slc: 4, smode: 1 }, { pad: 1, sync: 2 }]; r.render(0, 48000, 1); r.render(1, 24000, 1); r.render(2, 100, 1);
  r.NC.padsAll(r.api); await wait(80);
  const chops = () => r.log.filter(l => l[0] === 'send' && l[1].t === 'pad.chop').map(l => l[1]);
  assert.deepEqual(plain(chops()), [{ t: 'pad.chop', pad: 0, loopSecs: 1, n: 4, slices: [0, 12000, 24000, 36000] }, { t: 'pad.chop', pad: 1, loopSecs: 0.5, n: 2, slices: [] }], 'pad 2 has no chopping instrument');
  const order = r.types(); assert.ok(order.indexOf('pad.chop') > order.indexOf('buf.use'), 'after the pad');
  r.NC.chopAll(r.api); await wait(20); assert.equal(chops().length, 2, 'nothing changed: nothing sent');
  r.page.insts = [{ pad: 0, slc: 8, smode: 0 }]; r.NC.chopAll(r.api); await wait(20); assert.equal(chops().length, 4); assert.equal(chops()[2].slices.length, 0); assert.equal(chops()[2].n, 8);
  assert.deepEqual(plain(chops()[3]), { t: 'pad.chop', pad: 1, loopSecs: 0, n: 2, slices: [] }, 'removed');
  r.render(0, 48000, 2); r.NC.pad(0, r.api); await wait(60); assert.equal(chops().length, 5, 'a new render lost its chop data in the core: sent again');
  const lastCommit = r.log.filter(l => l[0] === 'send' && l[1].t === 'pads.commit').pop(); assert.ok(lastCommit[1].n >= 1);
});

test('a new kit removes every pad the core held (raw), then the new renders go; a new song sends every pad again and removes the ones that are gone', async () => {
  const r = await on(rig()); r.render(0, 10, 1); r.render(1, 10, 1); r.NC.padsAll(r.api); await wait(60);
  r.page.cache[0] = null; r.page.cache[1] = null; r.NC.newKit(r.api); await wait(60);
  const rms = () => r.log.filter(l => l[0] === 'send' && l[1].frames === 0).map(l => l[1].pad); assert.deepEqual(rms(), [0, 1]); assert.equal(r.NC.padStats().sent, 0);
  r.render(1, 10, 5); r.NC.pad(1, r.api); await wait(40); assert.equal(r.NC.padStats().sent, 1);
  const n = r.log.length; r.render(2, 10, 1); r.NC.newSong(r.api); await wait(80);
  const after = r.log.slice(n); assert.equal(after.filter(l => l[0] === 'put').length, 2, 'pads 1 and 2 again'); assert.equal(rms().length, 2, 'nothing removed: both still have renders');
  r.page.cache[1] = null; r.NC.newSong(r.api); await wait(60); assert.deepEqual(rms(), [0, 1, 1]);
});

test('the core rebuilt (engine up): the kit and every pad are sent again', async () => {
  const r = await on(rig()); r.render(0, 10, 1); r.NC.kit(r.api); await wait(40); const n = r.log.length;
  r.emit('engine', { up: false }); r.emit('engine', { up: true }); await wait(60);
  const after = r.log.slice(n).map(l => l[0] === 'send' ? l[1].t : l[0]); assert.deepEqual(after.slice(0, 4), ['kit.load', 'put', 'put-done', 'buf.use']);
});

test('off or detached: nothing is sent, no timer is left, pads of a detached client are dropped', async () => {
  const r = rig(); r.render(0, 10, 1); r.NC.pad(0, r.api); await wait(20); assert.equal(r.log.length, 0, 'not on: the page would not have called, and calling does nothing');
  const q = await on(rig({ putMs: 30 })); q.render(0, 10, 1); q.NC.pad(0, q.api); q.NC.detach(); await wait(80);
  assert.ok(!q.types().includes('buf.use') && !q.types().includes('pads.commit'), q.types().join());
  assert.equal(await q.NC.whenPads(5), true);
});

test('host android-n0 (the N0 shim) gets put(id, one mono Float32Array, 1 MB), the stereo pad folded to mono', async () => {
  const r = rig(); r.tc.ask = () => Promise.resolve({ t: 'reply', v: 1, caps: { nativeAudio: true }, sr: 48000, host: 'android-n0' }); await on(r);
  r.render(0, 4, 1, true); r.NC.pad(0, r.api); await wait(30);
  const p = r.log.find(l => l[0] === 'putN0'); assert.ok(p, 'the shim form'); assert.deepEqual(p[2], [0, 0, 0, 0], 'L = 1, R = -1: the mean'); assert.equal(p[3], 1 << 20);
  assert.equal(r.page.cache[0].data[0], 1, 'the page\'s own array is untouched');
});
