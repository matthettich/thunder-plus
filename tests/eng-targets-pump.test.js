// Engine targets on the …pump live synths (docs/mod/ENGINE-TARGETS-DESIGN.md sections 2-4): the twelve k-rate AudioParams
// (cCut cRes cDec cAtk cRel, oCut oRes oEnv oAtk oDec oSus oRel) of PsynProc. With all twelve at their defaults the worklet is
// bit-identical to the one before (git HEAD at the time of writing, kept below as the reference file's text from `git show`
// when available); a mapped type moves its cutoff / resonance / envelope in the staged values; an unmapped type ignores them.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm'), fs = require('fs'), path = require('path'), cp = require('child_process');
const H = require('./helpers');

const MAPPED = { pmonol: 'mono', ppolyl: 'poly', porbitl: 'orbit', pglossl: 'gloss', pwobl: 'wob' };
const UNMAPPED = ['pcoill', 'pfoldl', 'ptractl', 'pstrikel', 'pboingl', 'pfreezel', 'px0xl', 'plifel'];
const CUR = H.read('pump/synth/pump-synth-worklet.js');
// the worklet before this feature: the commit the work started from (7307828); if git can't show it (a shallow copy), the
// identity tests fall back to a node with the twelve params left out, which takes the same path
let REF = null; try { REF = cp.execSync('git show 7307828:pump/synth/pump-synth-worklet.js', { cwd: H.ROOT, encoding: 'utf8', stdio: ['ignore', 'pipe', 'ignore'] }); } catch (e) { /* no reference */ }

function meta() {
  const c = { console: { warn() {}, log() {} }, Math, Float32Array, Uint8Array, WebAssembly, Object, Array, Set, Map, JSON, String, Number, isFinite };
  c.globalThis = c; c.self = c; vm.createContext(c);
  vm.runInContext(H.read('pump/synth/pump-synth-meta.js'), c); vm.runInContext(H.read('pump/synth/pump-synth-engine.js'), c);
  c.PsynCore.attachModule(new WebAssembly.Module(fs.readFileSync(path.join(H.ROOT, 'pump/synth/pump-synth.wasm'))));
  return { P: c.PsynCore, M: c.PUMP_SYNTH_META };
}
const { P: CORE, M: META } = meta();
const BYTES = new Uint8Array(fs.readFileSync(path.join(H.ROOT, 'pump/synth/pump-synth.wasm')));
// a node of `kind` in its own worklet scope (src: the worklet's text)
function mk(kind, over, src) {
  const c = H.workletScope({ Date }); vm.runInContext(src || CUR, c, { filename: 'pump-synth-worklet.js' });
  const Proc = c.__registered['thunder-psyn'];
  const node = new Proc({ processorOptions: Object.assign({ p: Object.assign(CORE.xeng()[kind].d(), { tune: 0, level: 0, drive: 0 }, over || {}), bytes: BYTES }, CORE.liveSpec(kind)) });
  return { c, node };
}
const DEF = { cCut: -1, cRes: -1, cDec: -1, cAtk: -1, cRel: -1, oCut: 0, oRes: 0, oEnv: 0, oAtk: 0, oDec: 0, oSus: 0, oRel: 0 };
function params(extra, defaults) {
  const o = {}; for (let j = 0; j < 4; j++) { o['r' + j] = [1]; o['g' + j] = [0]; o['t' + j] = [0]; o['l' + j] = [0]; o['v' + j] = [100]; }
  Object.assign(o, defaults === false ? {} : DEF, extra); for (const k in o) o[k] = Float32Array.from(Array.isArray(o[k]) ? o[k] : [o[k]]); return o;
}
// run nq quanta (hold: the gate is up from quantum 1 until offAt), mod(q) gives the twelve for quantum q; returns the mono output
function run(h, nq, o = {}) {
  const out = new Float32Array(nq * 128), commits = []; const X0 = () => h.c.__psynX;
  let spied = false, cur = 0;
  for (let q = 0; q < nq; q++) { cur = q;
    const again = o.restart && q >= o.restart, on = (q >= 1 && q < (o.offAt || 1e9)) || again, ex = Object.assign({ t0: [again ? 2 : q >= 1 ? 1 : 0], g0: [on ? 1 : 0] }, o.mod ? o.mod(q) : {});
    const b = [[new Float32Array(128), new Float32Array(128)]]; h.node.process([], b, params(ex, o.omit ? false : true));
    if (!spied && X0()) { const X = X0(), f = X.psyn_commit; h.c.__psynX = Object.assign({}, X, { psyn_commit: (...a) => { commits.push(cur); return f(...a); } }); spied = true; }
    for (let i = 0; i < 128; i++) out[q * 128 + i] = 0.5 * (b[0][0][i] + b[0][1][i]);
  }
  out.commits = commits; return out;
}
const same = (a, b) => a.length === b.length && a.every((v, i) => Object.is(v, b[i]));
// energy above 2 kHz of d[from..to): a 2nd-order high-pass (RBJ), summed squares
function hf(d, from, to, fc = 2000) {
  const w = 2 * Math.PI * fc / 48000, al = Math.sin(w) / Math.SQRT2, c = Math.cos(w), a0 = 1 + al;
  const b0 = (1 + c) / 2 / a0, b1 = -(1 + c) / a0, b2 = b0, a1 = -2 * c / a0, a2 = (1 - al) / a0; let x1 = 0, x2 = 0, y1 = 0, y2 = 0, e = 0;
  for (let i = 0; i < to; i++) { const x = d[i], y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2; x2 = x1; x1 = x; y2 = y1; y1 = y; if (i >= from) e += y * y; }
  return e;
}
const energy = (d, from, to) => { let e = 0; for (let i = from; i < to; i++) e += d[i] * d[i]; return e; };
// a bright, open, sustained sound: the filter wide open and a flat envelope (so the cutoff is what the ear hears)
const BRIGHT = k => Object.assign({ cutoff: 12000, res: 10, reso: 10, filter_env: 0, env_cut: 0, a_s: 100, sustain: 100, a_a: 1, attack: 1, a_d: 600, decay: 600 }, k === 'pwobl' ? { wob_depth: 0, wob_cut: 0, filter_env: 0 } : {});
const KEYS = Object.keys(MAPPED);

test('eng-targets pump: the target table matches the plugin metadata (symbols, limits) and the staged order', () => {
  const ids = { pmonol: 0, ppolyl: 1, porbitl: 3, pglossl: 5, pwobl: 12 };
  for (const k of KEYS) {
    const h = mk(k), tm = (() => { const m = META.synth[MAPPED[k]]; return m; })(), src = CUR.match(new RegExp('\\n  ' + ids[k] + ':\\{[^\\n]*'))[0];
    const spec = CORE.liveSpec(k); assert.equal(spec.type, ids[k], k + ': the wasm type id');
    for (const m of src.matchAll(/(\w+):\['(\w+)', ([-\d.]+), ([-\d.]+)\]/g)) {
      const p = tm.params.find(q => q.s === m[2]); assert.ok(p, k + ': ' + m[2] + ' is a symbol'); assert.ok(spec.syms.includes(m[2]), k + ': ' + m[2] + ' is staged');
      assert.equal(+m[3], p.min, k + ' ' + m[2] + ' min'); assert.equal(+m[4], p.max, k + ' ' + m[2] + ' max');
    }
    assert.equal((src.match(/\['/g) || []).length, 7, k + ': seven targets'); assert.ok(h.node.tm, k + ': mapped');
  }
  for (const k of UNMAPPED) assert.equal(mk(k).node.tm, null, k + ': unmapped');
});

test('eng-targets pump: all twelve at their defaults: bit-identical to the worklet before, with the same commits at the same times', () => {
  for (const k of KEYS) {
    const o = { offAt: 150, mod: q => q === 60 ? {} : {} }, a = run(mk(k, BRIGHT(k)), 200, o), b = run(mk(k, BRIGHT(k)), 200, Object.assign({ omit: true }, o));
    assert.ok(same(a, b), k + ': the twelve at defaults differ from the twelve left out');
    assert.ok(a.some(v => Math.abs(v) > 1e-3), k + ': the note sounds');
    if (REF) {
      const r = run(mk(k, BRIGHT(k), REF), 200, Object.assign({ omit: true }, o));
      assert.ok(same(a, r), k + ': differs from the worklet before this feature'); assert.deepEqual(a.commits, r.commits, k + ': commits');
    }
  }
});

test('eng-targets pump: a {set} message with the twelve at defaults commits as before (once, next quantum)', () => {
  for (const k of KEYS) {
    const h = mk(k, BRIGHT(k)), ref = REF ? mk(k, BRIGHT(k), REF) : null;
    const run2 = x => { const o = []; for (let q = 0; q < 40; q++) { if (q === 20) x.node.port.onmessage({ data: { set: Object.assign({}, x.node.p, { cutoff: 3000 }) } }); const b = [[new Float32Array(128), new Float32Array(128)]]; x.node.process([], b, params({ t0: [1], g0: [1] }, x !== ref)); o.push(...b[0][0]); } return Float32Array.from(o); };
    if (ref) assert.ok(same(run2(h), run2(ref)), k + ': a set differs from before');
  }
});

test('eng-targets pump: unmapped types ignore all twelve (bit-identical under any values)', () => {
  const ext = { cCut: 20, cRes: 200, cDec: 10, cAtk: 100, cRel: 255, oCut: -255, oRes: 255, oEnv: 255, oAtk: 255, oDec: -255, oSus: -255, oRel: 255 };
  for (const k of UNMAPPED) {
    const a = run(mk(k), 60, { offAt: 40 }), b = run(mk(k), 60, { offAt: 40, mod: () => ext });
    assert.ok(same(a, b), k + ': changed by the twelve'); assert.deepEqual(a.commits, b.commits, k + ': commits');
  }
});

// held notes: where does a changed cutoff act? (printed; the contract for the lead)
const HELD = {};
test('eng-targets pump: a held note follows oCut (steps) and cCut within a few quanta, or the type is recorded as note-on only', () => {
  for (const k of KEYS) {
    const base = run(mk(k, BRIGHT(k)), 140), lo = run(mk(k, BRIGHT(k)), 140, { mod: q => q >= 60 ? { oCut: -255 } : {} });
    const eb = hf(base, 100 * 128, 140 * 128), el = hf(lo, 100 * 128, 140 * 128), ratio = el / eb;
    // before the step the two are the same
    assert.ok(same(base.subarray(0, 60 * 128), lo.subarray(0, 60 * 128)), k + ': identical before the step');
    // cCut low from the start vs none
    const cl = run(mk(k, BRIGHT(k)), 140, { mod: () => ({ cCut: 20 }) }), ec = hf(cl, 100 * 128, 140 * 128);
    // cCut stepped in on a held note
    const cs = run(mk(k, BRIGHT(k)), 140, { mod: q => q >= 60 ? { cCut: 20 } : {} }), ecs = hf(cs, 100 * 128, 140 * 128);
    const hi = run(mk(k, Object.assign(BRIGHT(k), { cutoff: 300 })), 140, { mod: q => q >= 60 ? { oCut: 255 } : {} }), hib = run(mk(k, Object.assign(BRIGHT(k), { cutoff: 300 })), 140);
    const up = hf(hi, 100 * 128, 140 * 128) / Math.max(1e-30, hf(hib, 100 * 128, 140 * 128));
    HELD[k] = { oCutDown: ratio, cCutFromStart: ec / eb, cCutStepped: ecs / eb, oCutUp: up };
    assert.ok(ec / eb < 0.5, k + ': cCut low from the start lowers the high band (' + (ec / eb).toFixed(4) + ')');
    assert.ok(ratio < 0.5 || ratio > 0.99, k + ' ratio ' + ratio);
  }
  console.log('held-note reaction (high-band energy ratio vs. unmodulated, 100-140 quanta):'); console.table(HELD);
});

test('eng-targets pump: cRel / oRel change the tail, oRes / oEnv / oAtk / oDec / oSus / cDec / cAtk / cRes change the sound', () => {
  for (const k of KEYS) {
    const b = Object.assign(BRIGHT(k), { a_r: 50, release: 50, a_s: 60, sustain: 60, filter_env: k === 'pwobl' ? 2 : 60, env_cut: 60 });
    const plain = run(mk(k, b), 220, { offAt: 40 });
    const tail = d => energy(d, 80 * 128, 160 * 128);
    const long = run(mk(k, b), 220, { offAt: 40, mod: () => ({ cRel: 220 }) });
    assert.ok(tail(long) > 20 * tail(plain) + 1e-12, k + ': cRel makes the tail longer (' + tail(long) + ' vs ' + tail(plain) + ')');
    const longO = run(mk(k, b), 220, { offAt: 40, mod: () => ({ oRel: 254 }) });
    assert.ok(tail(longO) > 20 * tail(plain) + 1e-12, k + ': oRel makes the tail longer');
    for (const m of [{ oRes: 255 }, { oEnv: 255 }, { oEnv: -255 }, { oAtk: 254 }, { oDec: -254 }, { oSus: -255 }, { cDec: 5 }, { cAtk: 200 }, { cRes: 250 }]) {
      const d = run(mk(k, b), 120, { offAt: 90, mod: () => m });
      assert.ok(!same(d, run(mk(k, b), 120, { offAt: 90 })), k + ': ' + JSON.stringify(m) + ' changes nothing');
    }
  }
});

test('eng-targets pump: this.p (the pad\'s own values) is never written; the plain sound returns when the modulation ends', () => {
  for (const k of KEYS) {
    const b = BRIGHT(k), h = mk(k, b), before = JSON.stringify(h.node.p);
    run(h, 80, { mod: q => ({ cCut: 30, oCut: 100 * Math.sin(q), oRes: 90, oEnv: 50, oAtk: 40, oDec: -40, oSus: 70, oRel: 100, cRel: 100, cAtk: 50, cDec: 60, cRes: 40 }) });
    assert.equal(JSON.stringify(h.node.p), before, k + ': this.p changed');
    // back to defaults: the next note (after the old one has died away) is the plain sound
    const a = mk(k, b), c = mk(k, b);
    const seq = (x, mod) => run(x, 700, { offAt: 20, restart: 600, mod: q => q < 60 ? (mod || {}) : {} });
    // (cRel / oSus change how long the old note rings, so the wasm's free-running state (drift, noise) is advanced by a different
    // amount before the worker sleeps it: not a leftover of the modulation, so those two are checked on the staged values below)
    const ma = seq(a, { cCut: 30, oRes: 200, oEnv: 100, cDec: 100, oAtk: 100, oCut: 5 }), mc = seq(c, null);
    const na = ma.subarray(600 * 128, 650 * 128), nc = mc.subarray(600 * 128, 650 * 128);
    assert.ok(na.some(v => Math.abs(v) > 1e-3), k + ': the restarted note sounds');
    let md = 0; for (let i = 0; i < na.length; i++) md = Math.max(md, Math.abs(na[i] - nc[i]));
    assert.ok(md < 1e-6, k + ': the note after modulation differs from the plain one by ' + md);
    assert.ok(ma.commits.includes(60), k + ': one commit with the plain values when the modulation returns to defaults');
    const full = mk(k, b), plainN = mk(k, b), st = x => Array.from(new Float32Array(x.c.__psynX.memory.buffer, x.c.__psynX.psyn_values(x.node.h), x.node.syms.length));
    run(full, 90, { mod: q => q < 60 ? { cCut: 30, oCut: 50, cRes: 40, oRes: 90, oEnv: 80, cAtk: 50, oAtk: 40, cDec: 60, oDec: -40, oSus: -70, cRel: 10, oRel: 100 } : {} }); run(plainN, 90);
    assert.deepEqual(st(full), st(plainN), k + ': the staged values are the pad\'s own again');
  }
});

test('eng-targets pump: extremes give finite, bounded output', () => {
  const ext = [{ cCut: 255, oCut: 255, cRes: 255, oRes: 255, oEnv: 255, cAtk: 255, oAtk: 255, cDec: 255, oDec: 255, oSus: 255, cRel: 255, oRel: 255 },
    { cCut: 0, oCut: -255, cRes: 0, oRes: -255, oEnv: -255, cAtk: 0, oAtk: -255, cDec: 0, oDec: -255, oSus: -255, cRel: 0, oRel: -255 }, { cCut: 300, oCut: 1e6, oSus: NaN, cRel: -50 }];
  for (const k of KEYS) for (const e of ext) {
    const d = run(mk(k, BRIGHT(k)), 120, { offAt: 80, mod: () => e });
    let pk = 0; for (const v of d) { assert.ok(Number.isFinite(v), k + ': ' + JSON.stringify(e) + ' gives a non-finite sample'); pk = Math.max(pk, Math.abs(v)); }
    assert.ok(pk < 20, k + ': peak ' + pk);
  }
});

test('eng-targets pump: cost of a commit while modulating (printed)', () => {
  const out = {};
  for (const k of KEYS) {
    const h = mk(k, BRIGHT(k)); run(h, 4); const X = h.c.__psynX, f = X.psyn_commit; let t = 0, n = 0;
    h.c.__psynX = Object.assign({}, X, { psyn_commit: (...a) => { const t0 = process.hrtime.bigint(); const r = f(...a); t += Number(process.hrtime.bigint() - t0); n++; return r; } });
    const log = []; for (let q = 0; q < 400; q++) { const b = [[new Float32Array(128), new Float32Array(128)]]; const t0 = process.hrtime.bigint(); h.node.process([], b, params({ t0: [1], g0: [1], oCut: 120 * Math.sin(q / 5), oRes: 60 * Math.cos(q / 7) })); log.push(Number(process.hrtime.bigint() - t0)); }
    out[k] = { commits: n, commitUs: +(t / n / 1e3).toFixed(1), quantumUs: +(log.reduce((a, v) => a + v, 0) / log.length / 1e3).toFixed(1) };
    assert.ok(n > 300 && n <= 400, k + ': a commit in each quantum where the values changed (' + n + ')');
  }
  console.log('commit cost:'); console.table(out);
});
