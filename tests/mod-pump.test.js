// MOD: ...pump effects as LFO targets (owner, 10 Nov). Rows of a pump slot that are continuous values (q.lfo) are targets with the usual
// id fx:track:slot:row; selectors, switches, whole-number ranges and band rows are not. No browser: the pump block and the PERFORM block
// are cut out of index.html and run in a vm (as perform.test.js does), makeSeq is read from core/seq-core.js, and the core's pump
// delivery is run in the core's own scope. The old core (git HEAD at the time of this change) pins that a song with no pump LFO is unchanged.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const { execFileSync } = require('node:child_process');
const H = require('./helpers');
const src = H.read('index.html');

function cut(re, what){ const m = src.match(re); assert.ok(m, `index.html: could not find ${what}; the code moved or changed shape, so this test needs updating`); return m[0]; }
const NT = 32, MT = 32;
const clamp = (v, a, b) => Math.min(b, Math.max(a, v));

// The page's pieces: the pump block (its parameter table from pump/pump-meta.js, the boxes, pumpPv) and the targets (PERFORM block).
function pageCtx(){
  const meta = vm.createContext({}); meta.globalThis = meta; vm.runInContext(H.read('pump/pump-meta.js'), meta);
  const c = vm.createContext({ Math, JSON, Object, Array, Map, Set, String, Number, isNaN, Number, NT, MT, TFX: {}, FX_GROUP: {}, FX_CREDIT: {}, PUMP_META: meta.PUMP_META,
    clamp, int: (v, d) => Number.isFinite(+v) && v !== null && v !== '' ? Math.round(+v) : d, d2: n => String(n).padStart(2, '0'), isBusT: () => true, globalThis: null });
  c.globalThis = c;
  vm.runInContext(cut(/const PM = globalThis\.PUMP_META[\s\S]*?\n(?=const pumpAllowed = )/, 'the pump block'), c);
  vm.runInContext(cut(/\/\* ===== PERFORM \(owner, 1 Nov[\s\S]*?\n(?=function makeSeq\(S, mode, o\)\{)/, 'the PERFORM block'), c);
  return c;
}
const PAGE = pageCtx();
const run = (code, extra = {}) => { Object.assign(PAGE, extra); return vm.runInContext(code, PAGE); };

// A song with a pump slot (EQ or COMP) in track 2's slot 0 and the master's slot 0, at the plugin's own values.
function song(type = 'comp', extra = {}){
  const mk = () => run(`pumpFresh('${type}')`);
  const S = { bpm: 120, ticks: 6, ntr: 8, swing: 50, swingB: 2, lpb: 4, dlen: 4, plen: [4], tune: new Array(12).fill(0), insts: [{}], parts: [],
    song: Array.from({ length: 256 }, () => new Array(NT + 1).fill(-1)), chains: [[[0, 0]]],
    phrases: [Array.from({ length: 4 }, (_, y) => y % 2 ? [-1, 0, 0, 0, 0, 0, -1, 0, -1, 0, -1, 0] : [48 + y, 0, 0, 0, 0, 0, -1, 0, -1, 0, -1, 0])],
    mix: Array.from({ length: NT }, () => ({ vol: 128, pan: 128, mute: false, solo: false })),
    tfx: Array.from({ length: NT }, () => []), mfx: [], smute: new Array(256).fill(0), perf: { sc: [], mac: [], lk: {}, xa: 0, xb: 1, xf: 0, lfo: {} } };
  S.tfx[2] = [JSON.parse(JSON.stringify(mk()))];
  S.mfx = [JSON.parse(JSON.stringify(mk()))];
  for (const y of [0, 1, 2, 3]) S.song[y][1] = 0;
  return Object.assign(S, extra);
}
const withSong = S => { PAGE.S = S; return S; };
const rowOf = (type, key) => run(`TFX.p${type}.ps.findIndex(q => q.k === '${key}')`);

test('tgtList: a pump COMP and EQ slot list their continuous rows, never selectors, switches or band rows', () => {
  for (const type of ['comp', 'eq']){
    const S = withSong(song(type)), L = run('tgtList(2)'), mine = L.filter(([id]) => id.startsWith('fx:2:0:'));
    assert.ok(mine.length > 0, type + ' has targets');
    const D = run(`TFX.p${type}`);
    for (const [id, label] of mine){
      const q = D.ps[+id.split(':')[3]];
      assert.ok(q.lfo, id + ' ' + label + ' is a target row'); assert.ok(!q.names && !q.band && !(q.tpl || '').includes('#') && q.min === 0 && q.max === 255, label + ' is a plain 00-FF knob');
      assert.ok(run(`tgtOk('${id}')`) && run(`tgtFx(tgtParse('${id}'))`) !== null, id + ' resolves');
      assert.match(label, new RegExp('^FX1 ' + D.n + ' · ' + q.l));
    }
    const ls = mine.map(([, l]) => l);
    if (type === 'comp'){
      for (const want of ['THRESH', 'RATIO', 'ATTACK', 'RELEASE', 'MIX']) assert.ok(ls.some(l => l.endsWith('· ' + want)), 'COMP lists ' + want);
      for (const no of ['STYLE', 'AUTO REL', 'AUTO GAIN', 'SC SOURCE', 'SC FROM']) assert.ok(!ls.some(l => l.endsWith('· ' + no)), 'COMP does not list ' + no);
    } else {
      assert.ok(ls.some(l => l.endsWith('· OUTPUT')), 'EQ lists its output gain');
      for (const no of ['BAND', 'AUTO GAIN', 'PHASE']) assert.ok(!ls.some(l => l.endsWith('· ' + no)), 'EQ does not list ' + no);
      assert.ok(!ls.some(l => /FREQ|SHAPE|Q$|SLOPE|GAIN$/.test(l) && !/OUTPUT/.test(l)), 'EQ lists no band-dependent row (the id cannot name a band)');
    }
    assert.ok(L.some(([id]) => id === 'vol:2'), 'the track\'s own targets are still there');
  }
  // the master's pump slot is listed too, and an empty-slot id does not resolve
  withSong(song('comp')); assert.ok(run('tgtList(MT)').some(([id]) => id.startsWith('fx:32:0:')));
  assert.equal(run("tgtFx(tgtParse('fx:3:0:1'))"), null);
  // rows that are not targets do not resolve either (STYLE is a choice, SC FROM a Thunder choice)
  assert.equal(run(`tgtFx(tgtParse('fx:2:0:${rowOf('comp', 'p_style')}'))`), null);
});

test('tgtOwn: a pump row\'s own value is its byte; the song is not changed by asking', () => {
  const S = withSong(song('comp')), i = rowOf('comp', 'p_threshold'), q = run(`TFX.pcomp.ps[${i}]`), before = JSON.stringify(S);
  S.tfx[2][0].p_threshold = 77;
  assert.equal(run(`tgtOwn('fx:2:0:${i}')`), 77); assert.equal(q.min, 0); assert.equal(q.max, 255);
  S.tfx[2][0].p_threshold = JSON.parse(before).tfx[2][0].p_threshold;
  assert.equal(JSON.stringify(S), before);
});

// ---- the core's sequencer: events -------------------------------------------------------------------------------------------------
function loadCore(text, tfx){
  const ctx = H.workletScope(); vm.runInContext(text, ctx, { filename: 'seq-core.js' });
  ctx.__T = JSON.parse(JSON.stringify(tfx)); vm.runInContext('TFX = __T;', ctx);
  return ctx;
}
// What the page sends the core as TFX: coreTfx() from index.html, run on the page's TFX.
const coreTfx = () => { const f = cut(/function coreTfx\(\)\{[^\n]*\n[^\n]*\n/, 'coreTfx'); run(f); return JSON.parse(JSON.stringify(run('coreTfx()'))); };
const NEW = () => loadCore(H.read('core/seq-core.js'), coreTfx());
function play(ctx, S, mode = 'song', steps = 64){
  ctx.__S = JSON.parse(JSON.stringify(S)); vm.runInContext('S = __S;', ctx);
  const sq = ctx.makeSeq(ctx.__S, mode, { row: 0, once: true, sim: true }), evs = [];
  for (let i = 0; i < steps && sq.alive(); i++) evs.push(...sq.step().evs);
  return JSON.parse(JSON.stringify(evs));
}
const RATES = [1 / 8, 1 / 4, 1 / 2, 1, 2, 4, 8, 16, 32];
const sineAt = (t, c, rt, dp) => { const bt = t * 2 / RATES[rt]; return Math.max(0, Math.min(255, Math.round(c + Math.sin((bt - Math.floor(bt)) * 2 * Math.PI) * dp / 2))); };

test('an LFO on a pump parameter: fxp events on every step, following the shape around the song\'s own byte', () => {
  const i = rowOf('comp', 'p_ratio'), S = song('comp'); S.tfx[2][0].p_ratio = 100;
  S.perf.lfo[2] = [{ id: `fx:2:0:${i}`, sh: 0, rt: 5, dp: 100 }];
  const ev = play(NEW(), S).filter(e => e.type === 'fxp');
  assert.equal(ev.length, 16 * 2, '2 points a step in a render, 16 steps');
  ev.forEach(e => { assert.equal(e.track, 2); assert.equal(e.slot, 0); assert.equal(e.pi, i); assert.equal(e.v, sineAt(e.t, 100, 5, 100), 'follows the sine at t=' + e.t); });
  const vs = ev.map(e => e.v); assert.ok(Math.max(...vs) >= 100 + 49 && Math.min(...vs) <= 100 - 49);
});

// The core's delivery (core-processor.js's fxp branch), run on a bare scope: the message it queues for the effect node.
function coreDelivery(S, evs){
  const ctx = H.workletScope(), sent = [];
  ctx.__T = coreTfx(); ctx.__S = JSON.parse(JSON.stringify(S));
  vm.runInContext(H.read('core/seq-core.js'), ctx, { filename: 'seq-core.js' });
  vm.runInContext('TFX = __T; S = __S;', ctx);
  vm.runInContext(`const __node = {__q: null}; globalThis.__node = __node; globalThis.tcNodes = () => new Map([['fx:2', __node]]);`, ctx);
  const proc = vm.runInContext('Object.create(ThunderCore.prototype)', ctx); proc.fxOv = new Map(); proc.seq = null;
  const handled = evs.map(e => proc.coreEv ? proc.coreEv(e) : null);
  return { node: ctx.__node, proc, ctx, handled };
}

test('delivery: the page and the core both build {pset:{slot, pv}} from the same byte, pv inside the parameter\'s range', () => {
  const i = rowOf('comp', 'p_ratio'), S = withSong(song('comp')), f = S.tfx[2][0], fx = run("pumpFx('comp')"), idx = run("pumpIdx.comp.ratio"), p = fx.params[idx];
  assert.ok(p.scale && p.max > p.min);
  const own = JSON.stringify(f.pump);
  for (const v of [0, 1, 77, 128, 200, 255]){
    const ov = {}, q = run(`TFX.pcomp.ps[${i}]`); ov[q.k] = q.min + Math.round((q.max - q.min) * v / 255);
    const pv = run('pumpPv(TFX.pcomp, S.tfx[2][0], ov)', { ov });
    assert.equal(pv.length, fx.params.length); assert.ok(pv[idx] >= p.min && pv[idx] <= p.max, 'pv in range at ' + v);
    assert.equal(pv[idx], run(`pumpClamp(pumpFx('comp').params[${idx}], pumpUnbyte(pumpFx('comp').params[${idx}], ${ov[q.k]}))`), 'the FX screen\'s conversion of the byte');
    assert.equal(pv[idx], run(`pumpFrom(pumpFx('comp').params[${idx}], ${v} / 255)`), 'byte v is v/255 of the knob curve');
    fx.params.forEach((pp, j) => { if (j !== idx) assert.equal(pv[j], f.pump[pp.s] ?? pp.def, 'every other value stays the slot\'s own: ' + pp.s); });
    // the core gets the same list from the data the page sent it
    const C = loadCore(H.read('core/seq-core.js'), coreTfx());
    C.__f = JSON.parse(JSON.stringify(f)); C.__ov = ov;
    assert.deepEqual(JSON.parse(JSON.stringify(vm.runInContext('pumpPv(TFX.pcomp, __f, __ov)', C))), JSON.parse(JSON.stringify(pv)), 'core pumpPv == page pumpPv at ' + v);
  }
  assert.equal(JSON.stringify(f.pump), own, 'f.pump is the user\'s own, untouched');
  assert.deepEqual(Array.from(run('pumpPv(TFX.pcomp, S.tfx[2][0], null)')), Array.from(fx.params, pp => f.pump[pp.s] ?? pp.def), 'no override: the slot\'s own list');
});

test('delivery on the core: an fxp for a pump row is queued as pset{slot, pv} at its time; stop puts the slot\'s own back; f.pump is unchanged', () => {
  const i = rowOf('comp', 'p_threshold'), S = song('comp'), own = JSON.stringify(S.tfx[2][0]);
  const C = loadCore(H.read('core/seq-core.js'), coreTfx());
  C.__S = JSON.parse(JSON.stringify(S)); vm.runInContext('S = __S;', C);
  const queued = [];
  C.__tcNodes = new Map([['fx:2', {}]]);
  vm.runInContext('globalThis.__tcAt = tcAt; tcAt = (proc, at, msg) => globalThis.__out.push([at, msg]);', C); C.__out = queued;
  // the core's event handler is a method of ThunderCore: find it by running an fxp through it
  const core = new C.__registered['thunder-core'](); const names = Object.getOwnPropertyNames(Object.getPrototypeOf(core));
  const h = names.find(n => /^(ev|play|doEv|coreEv|onEv|handle)/i.test(n) && String(core[n]).includes("ev.type === 'fxp'"));
  assert.ok(h, 'found the core\'s event method that handles fxp');
  const q = run(`TFX.pcomp.ps[${i}]`);
  const fx = run("pumpFx('comp')"), idx = run('pumpIdx.comp.threshold'), p = fx.params[idx];
  for (const v of [0, 100, 255]){
    assert.equal(core[h]({ type: 'fxp', t: 1 + v / 1000, track: 2, slot: 0, pi: i, v }), true, 'the core takes it');
    const [at, m] = queued[queued.length - 1];
    assert.ok(at >= 1 && m.pset && m.pset.slot === 0 && Array.isArray(m.pset.pv)); assert.equal(m.pset.pv.length, fx.params.length);
    assert.ok(m.pset.pv[idx] >= p.min && m.pset.pv[idx] <= p.max);
    assert.equal(m.pset.pv[idx], run(`pumpFrom(pumpFx('comp').params[${idx}], ${v} / 255)`));
  }
  assert.equal(core[h]({ type: 'fxp', t: 2, track: 2, slot: 0, pi: rowOf('comp', 'p_style'), v: 5 }), false, 'a row that is not a target stays unhandled');
  assert.equal(JSON.stringify(C.__S.tfx[2][0]), own, 'the song\'s slot is untouched');
  // stop: the slot's own value list goes back to the node at the stop time, and the moved rows are forgotten
  const n0 = queued.length; core.stopAll(5);
  assert.equal(queued.length, n0 + 1, 'one reset message for the one moved slot');
  const [rat, rm] = queued[n0]; assert.equal(rat, 5);
  assert.deepEqual(JSON.parse(JSON.stringify(rm.pset.pv)), Array.from(fx.params, pp => S.tfx[2][0].pump[pp.s] ?? pp.def), 'the user\'s own values');
  assert.equal(core.fxOv.size, 0);
});

test('the page: the fxp for a pump row builds the same message; a row that is no target is ignored', () => {
  const S = withSong(song('comp')), i = rowOf('comp', 'p_ratio'), j = rowOf('comp', 'p_style');
  const blk = cut(/  function fxpMsg\(t, k, ov\)\{[\s\S]*?\n(?=  function tempoTo)/, 'fxpMsg / fxpTo');
  const sent = [], ctx = vm.createContext(Object.assign(Object.create(null), { Math, Object, Map, Array, isPumpT: k => run(`isPumpT('${k}')`), TFX: PAGE.TFX, S, MT, pumpPv: PAGE.pumpPv,
    ac: { sampleRate: 48000 }, bpmNow: 120, fxOv: new Map(), fxNodeOf: () => ({ port: { postMessage: m => sent.push(m) } }), atTime: (t, f) => f() }));
  vm.runInContext(blk, ctx);
  const m = ctx.fxpMsg(2, 0, {}); assert.ok(m.pset && m.pset.slot === 0 && Array.isArray(m.pset.pv), 'the slot\'s own message has a value list');
  const fx = run("pumpFx('comp')"), idx = run('pumpIdx.comp.ratio'), p = fx.params[idx];
  ctx.fxpTo({ t: 1, track: 2, slot: 0, pi: i, v: 255 });
  ctx.fxpTo({ t: 1, track: 2, slot: 0, pi: j, v: 255 });   // STYLE: not a target
  assert.equal(sent.length, 1);
  assert.equal(sent[0].pset.pv[idx], p.max, 'byte FF is the top of the range');
  assert.equal(S.tfx[2][0].pump.ratio, run('pumpFresh("comp")').pump.ratio, 'the song\'s own value is kept');
});

test('a song with no pump LFO: the old core and the new one send the same events (a pump slot in the song or not)', t => {
  let old; try { old = loadCore(execFileSync('git', ['show', 'HEAD:core/seq-core.js'], { cwd: H.ROOT, encoding: 'utf8', maxBuffer: 1 << 28, stdio: ['ignore', 'pipe', 'ignore'] }), coreTfx()); }
  catch (e){ t.skip('git history is not here'); return; }
  const cur = NEW(), nat = { type: 'eq', treble: 128, mid: 50 };
  let total = 0;
  for (const pumpSlot of [false, true]){
    const S = song('comp'); if (!pumpSlot){ S.tfx[2] = []; S.mfx = []; }
    S.tfx[3] = [nat]; S.perf.lfo = { 3: [{ id: 'vol:3', sh: 1, rt: 4, dp: 80 }], 1: [{ id: 'pan:1', sh: 0, rt: 5, dp: 60 }] };
    const a = play(old, S), b = play(cur, S); total += a.length;
    assert.deepEqual(b, a, 'pump slot in the song: ' + pumpSlot);
  }
  assert.ok(total > 100, 'events were produced (' + total + ')');
});
