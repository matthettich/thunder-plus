// (11 Nov, build 567) Garbage out of the core's per-block path, and proof that nothing else moved.
//  1. Auto.prune folds in place now (it built head / slice / concat arrays and fresh events every block per timeline); Auto.cancel
//     truncates in place (it was a filter); Auto.fill does a block of at() in one call (a call per sample handed the time and the value
//     back as new heap numbers). The OLD prune and cancel are kept below verbatim as the reference: on thousands of random timelines
//     (set, lin, target, curve, cancel, hold, prune at random times) the two leave the same event list (deep-equal: same k, t, v, tc,
//     fn, d, in the same order), the same v0, and at(t) is the same value (Object.is) at every later time; fill() equals at().
//  2. The steady playing state calls no array-making method (slice, concat, filter, map, splice) and the folds allocate nothing
//     the heap sampler can see.
const test = require('node:test'), assert = require('node:assert/strict'), fs = require('fs'), path = require('path'), vm = require('node:vm'), inspector = require('node:inspector');
const src = fs.readFileSync(path.join(__dirname, '..', 'core', 'core-processor.js'), 'utf8');
const i0 = src.indexOf('class Auto {'); let d = 0, i1 = -1;
for (let i = src.indexOf('{', i0); i < src.length; i++){ const c = src[i]; if (c === '{') d++; else if (c === '}'){ if (--d === 0){ i1 = i + 1; break; } } }
const AutoSrc = src.slice(i0, i1);
const Auto = new Function(AutoSrc + '; return Auto;')();

// The reference: build 566's prune and cancel, as they were.
const REF = new Function(AutoSrc + `; class RefAuto extends Auto {
  cancel(t){ this.ev = this.ev.filter(e => e.t < t); }
__REFPRUNE__
}
return RefAuto;`.replace('__REFPRUNE__', String.raw`  prune(t){
    const A = this.ev; if (!A.length || A[0].t > t) return;
    // No closure and no per-event object here (this runs on the audio thread, per block per timeline): a target in progress
    // lives in scalars: hs (is there one), sf (from), sT (start), so (to), sc (time constant).
    let v = this.v0, hs = false, sf = 0, sT = 0, so = 0, sc = 1, cur = null, i = 0, pt = 0;
    for (; i < A.length; i++){ const e = A[i];
      if (e.k === 1){ if (e.t > t) break; v = e.v; hs = false; pt = e.t; continue; }
      if (e.t > t) break;
      if (e.k === 0){ v = e.v; hs = false; cur = null; pt = e.t; }
      else if (e.k === 2){ sf = hs ? so + (sf - so) * Math.exp(-(e.t - sT) / sc) : v; hs = true; sT = e.t; so = e.v; sc = e.tc; cur = null; }
      else if (e.k === 3){ if (e.t + e.d > t){ cur = e; } else { v = e.fn(e.t + e.d); hs = false; cur = null; pt = e.t + e.d; } } }
    if (i === 0) return;
    // FLX CHECK 2 (5 Nov, owner: FLX stutters / "feels like it lags"): a linear ramp still running (the next event a k:1
    // later than t) starts from the last point's TIME, not t. Folding it to {t, v} restarted the ramp from v at every block,
    // so the beat clock (one 'beat' ramp a row) sat at the row's value at each block start and sawed inside it: Flex,
    // Step gate, Retrigger and every tempo-locked effect on the Audio thread sequencer heard a staircase, and ramps on live
    // engines (level fades, glides) crawled. Keep the point where it was.
    if (!hs && !cur && i < A.length && A[i].k === 1){ this.v0 = v; this.ev = [{k:0, t:pt, v}].concat(A.slice(i)); return; }
    const now = cur ? cur.fn(t) : hs ? so + (sf - so) * Math.exp(-(t - sT) / sc) : v, head = [{k:0, t, v:now}];
    if (hs) head.push({k:2, t, v:so, tc:sc});
    if (cur) head.push(cur);
    this.v0 = now; this.ev = head.concat(A.slice(i));
  }`))();

const rnd = (s => () => { let x = s += 0x6D2B79F5; x = Math.imul(x ^ x >>> 15, x | 1); x ^= x + Math.imul(x ^ x >>> 7, x | 61); return ((x ^ x >>> 14) >>> 0) / 4294967296; })(987654321);
const fns = [x => Math.sin(x * 7), x => x * x, x => 1 - Math.exp(-x * 3), x => 0.5];

test('prune / cancel / hold in place: same event list, same v0, same at() as build 566 (random timelines)', () => {
  let runs = 0, probes = 0, prunes = 0, folded = 0, withTarget = 0, withCurve = 0, withRamp = 0, grew = 0;
  for (let run = 0; run < 6000; run++){
    const v0 = rnd() * 2 - 1, A = new Auto(v0), B = new REF(v0); let t = rnd() * 0.5; runs++;
    const both = f => { f(A); f(B); };
    for (let step = 0; step < 40; step++){
      const r = rnd();
      // events booked ahead (as the sequencer does), ties and out-of-order inserts included
      if (r < 0.40){ const n = 1 + (rnd() * 3 | 0); for (let j = 0; j < n; j++){ const at = rnd() < 0.15 ? t : t + rnd() * 0.3, k = rnd(), v = rnd() * 2 - 1, tc = 0.0005 + rnd() * 0.05, dd = 0.005 + rnd() * 0.1, fn = fns[rnd() * fns.length | 0];
          if (k < 0.3) both(X => X.set(v, at)); else if (k < 0.6) both(X => X.lin(v, at)); else if (k < 0.82) both(X => X.target(v, at, tc)); else both(X => X.curve(fn, at, dd)); } }
      else if (r < 0.50){ const ct = t + (rnd() - 0.3) * 0.2; both(X => X.cancel(ct)); }
      else if (r < 0.55){ const ht = t + rnd() * 0.1; both(X => X.hold(ht)); }
      else { // the block clock moves on and prunes (sometimes by a lot, sometimes not at all, sometimes backwards)
        t += rnd() < 0.1 ? -rnd() * 0.05 : rnd() * (rnd() < 0.2 ? 0.5 : 0.01);
        const hadEv = A.ev.length; A.prune(t); B.prune(t); prunes++; if (A.ev.length < hadEv) folded++; if (A.ev.length > hadEv) grew++;
        if (A.ev.some(e => e.k === 2)) withTarget++; if (A.ev.some(e => e.k === 3)) withCurve++; if (A.ev.some(e => e.k === 1)) withRamp++; }
      assert.ok(Object.is(A.v0, B.v0), 'v0 differs: ' + A.v0 + ' vs ' + B.v0);
      assert.deepStrictEqual(A.ev, B.ev);
      for (let q = 0; q < 6; q++){ const u = t + rnd() * 0.4; probes++; const a = A.at(u), b = B.at(u); if (!Object.is(a, b)) assert.fail('run ' + run + ': at(' + u + ') = ' + a + ' vs ' + b); }
    }
    // fill(): a block of at() in one call, from any start index
    const F = new Float64Array(128), G = new Float64Array(128), dt = 1 / 44100, i0 = rnd() * 128 | 0; F.fill(-7); G.fill(-7);
    A.fill(F, t, dt, 128, i0); for (let i = i0; i < 128; i++) G[i] = B.at(t + i * dt);
    for (let i = 0; i < 128; i++) if (!Object.is(F[i], G[i])) assert.fail('run ' + run + ': fill()[' + i + '] = ' + F[i] + ' vs at() ' + G[i]);
  }
  assert.ok(probes > 1.4e6 && prunes > 100000, 'compared ' + probes + ' reads over ' + prunes + ' prunes in ' + runs + ' timelines');
  // (the branches that matter were reached: folds, a target kept, a curve kept, a ramp kept, lists that grew by a head)
  assert.ok(folded > 5000 && withTarget > 2000 && withCurve > 1000 && withRamp > 5000 && grew > 100, JSON.stringify({ folded, withTarget, withCurve, withRamp, grew }));
});

test('the core\'s per-block path calls no array-making method in the steady playing state', () => {
  let time = 0; class AWP { constructor(){ this.port = { postMessage(){}, onmessage: null }; } }
  const reg = {}, NT = 32, SR = 44100, N = 128, dt = 1 / SR;
  const ctx = { console, Math, Float32Array, Float64Array, Int32Array, Uint32Array, Uint8Array, Map, Set, JSON, Date, Number, String, Object, Array, isFinite, parseInt, parseFloat,
    AudioWorkletProcessor: AWP, registerProcessor: (n, c) => { reg[n] = c; }, sampleRate: SR };
  Object.defineProperty(ctx, 'currentTime', { get: () => time }); ctx.globalThis = ctx; vm.createContext(ctx);
  vm.runInContext(fs.readFileSync(path.join(__dirname, '..', 'core', 'seq-core.js'), 'utf8'), ctx, { filename: 'seq-core.js' });
  const proc = new reg['thunder-core']({});
  const AP = vm.runInContext('Object.getPrototypeOf([])', ctx), counts = {}, saved = {};   // (the core's own Array.prototype)
  const names = ['slice', 'concat', 'filter', 'map', 'splice', 'flat', 'flatMap'];
  let counting = false;
  for (const n of names){ saved[n] = AP[n]; counts[n] = 0; AP[n] = function(...a){ if (counting) counts[n]++; return saved[n].apply(this, a); }; }
  try {
    const mk = () => { const o = []; for (let i = 0; i < NT + 5; i++){ const ch = i < NT ? 2 : i === NT ? 1 : i === NT + 1 || i === NT + 2 ? NT : 1; const a = []; for (let c = 0; c < ch; c++) a.push(new Float32Array(N)); o.push(a); } return o; };
    const out = mk(), mkVoice = vm.runInContext(`(function(proc, tr, data){ const v = {data, data2:null, sr:44100, r:new Auto(1), g:new Auto(1), f:new Auto(1), pn:new Auto(0), trm:null, apn:null, base:1, rate:1, choke:null, col:0, ended:false, stopAt:null, syn:false, rel:0, track:tr, end:Infinity, pos:0, t0:0};
      proc.alive.add(v); proc.T(tr).push(v); return v; })`, ctx);
    const big = new Float32Array(2000000).fill(0.1), voices = [mkVoice(proc, 0, big), mkVoice(proc, 1, big), mkVoice(proc, 2, big)];
    proc.seq = { alive: () => true, time: 1e9, base: null };   // a song that is playing, with no step due in this block
    proc.clkOn = true; proc.mg[0] = vm.runInContext('new Auto(1)', ctx); proc.mp[1] = vm.runInContext('new Auto(0)', ctx);
    let blocks = 0, events = 0;
    const block = b => { time = 1 + b * N / SR; for (const o of out) for (const c of o) c.fill(0); proc.process1([[]], out); blocks++; };
    for (let b = 0; b < 700; b++){
      counting = false;   // (booking an event is per note: Auto.ins splices, and is not counted)
      if (b % 3 === 0){ proc.clk.lin(b / 3, time + 0.25 + 3 * N / SR); events++; }
      if (b % 5 === 0){ voices[b % 3].g.lin(0.5 + (b % 7) / 20, time + 0.1); voices[b % 3].pn.target(((b % 9) - 4) / 5, time + 0.05, 0.01); proc.mg[0].target(0.5 + (b % 4) / 8, time + 0.02, 0.004); proc.mp[1].lin((b % 5) / 5, time + 0.03); events += 4; }
      counting = b >= 100;
      block(b);
    }
    counting = false;
    assert.ok(events > 500 && blocks === 700);
    assert.deepEqual(counts, Object.fromEntries(names.map(n => [n, 0])), 'array-making calls inside process1() over 600 steady blocks');
  } finally { for (const n of names) AP[n] = saved[n]; }
});

test('the heap sampler sees nothing allocated by prune / cancel (it saw megabytes before)', () => {
  const sampled = which => {
    const A = new which(0); let t = 0, ops = 0;
    for (let i = 0; i < 300; i++){ A.lin(i, 0.01 * i + 0.02); }   // warm up
    const s = new inspector.Session(); s.connect(); s.post('HeapProfiler.startSampling', { samplingInterval: 32, includeObjectsCollectedByMinorGC: true, includeObjectsCollectedByMajorGC: true });
    for (let b = 0; b < 40000; b++){ t = 1 + b * 0.0029; if (b % 3 === 0) A.lin(b, t + 0.25); if (b % 4 === 0) A.target(b & 7, t + 0.2, 0.01); A.prune(t); if (b % 50 === 0){ A.cancel(t + 0.1); A.set(1, t + 0.1); } ops++; }
    let bytes = 0; s.post('HeapProfiler.stopSampling', (e, r) => { const walk = n => { if (/^(prune|cancel)$/.test(n.callFrame.functionName)) bytes += n.selfSize; n.children.forEach(walk); }; walk(r.profile.head); }); s.disconnect(); return bytes;
  };
  // (insertion's own splice / event objects are not in these two functions' own bytes)
  const before = sampled(REF), after = sampled(Auto);
  assert.ok(before > 100000, 'the sampler should see the old prune allocating (saw ' + before + ' bytes)');
  assert.ok(after < before / 20, 'prune / cancel allocate ' + after + ' bytes sampled (old: ' + before + ')');
});
