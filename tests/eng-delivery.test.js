// ENGINE TARGETS, the delivery side (10 Nov; docs/mod/ENGINE-TARGETS-DESIGN.md section 5): the tracker commands CUT / RES / ATK / DEC / REL and the MOD targets
// cut res env atk dec sus rel reach the live engines as one `eng` event {track, t, c, o}. No browser. makeSeq is read from core/seq-core.js (run in a vm with a
// hand-built song, as mod-run.test.js does); the page player's `eng` block and liveCut are cut out of index.html and run with fake nodes (objects whose
// parameters.get(name) gives recorders); the core's delivery runs in the core's own scope with a fake registered node. The processors' params (cCut ...) are
// not needed: every fake node says which of them it has.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const { execFileSync } = require('node:child_process');
const H = require('./helpers');
const src = H.read('index.html');

const NT = 32, MT = 32, BASE = '413958d';   // BASE: the commit before this change (its core is the "old" sequencer)
function loadCore(text){
  const ctx = H.workletScope(); vm.runInContext(text, ctx, { filename: 'seq-core.js' });
  vm.runInContext("TFX = {eq:{ps:[{k:'treble',min:0,max:255},{k:'mid',min:0,max:100}]}};", ctx);
  return ctx;
}
const NEW = loadCore(H.read('core/seq-core.js'));
const FXI = n => vm.runInContext(`FXC.indexOf('${n}')`, NEW);

/* A song: `cells` = {track: [song rows with a pattern]}; one 4-row pattern: a note on rows 0 and 2, and `fx` = {row: [[cmd, value]]} on the track's rows.
   120 bpm, 4 lines a beat: a step is 0.125 s, a song row 0.5 s. lfo = {track: [{id, sh, rt, dp}]}. insts: the song's instruments (their pads). */
function mkSong({ cells, lfo = {}, fx = {}, sc = null, insts = [{ pad: 0 }] }){
  const row = y => { const r = y % 2 ? [-1, 0, 0, 0, 0, 0, -1, 0, -1, 0, -1, 0] : [48 + y, 0, 0, 0, 0, 0, -1, 0, -1, 0, -1, 0]; (fx[y] || []).forEach(([c, v], k) => { r[6 + k * 2] = FXI(c); r[7 + k * 2] = v; }); return r; };
  const S = { bpm: 120, ticks: 6, ntr: 8, swing: 50, swingB: 2, lpb: 4, dlen: 4, plen: [4], tune: new Array(12).fill(0), insts, parts: [],
    song: Array.from({ length: 256 }, () => new Array(NT + 1).fill(-1)), chains: [[[0, 0]]], phrases: [Array.from({ length: 4 }, (_, y) => row(y))],
    mix: Array.from({ length: NT }, () => ({ vol: 128, pan: 128, mute: false, solo: false })),
    tfx: Array.from({ length: NT }, () => []), mfx: [{ type: 'eq', treble: 128, mid: 50 }],
    smute: new Array(256).fill(0), perf: { sc: [], mac: [], lk: {}, xa: 0, xb: 1, xf: 0, lfo } };
  for (const t in cells) for (const y of cells[t]) S.song[y][+t] = 0;
  if (sc) S.perf.sc = sc;
  return S;
}
function play(ctx, S, mode, o = {}, steps = 64){
  ctx.__S = S; vm.runInContext('S = __S;', ctx);
  const sq = ctx.makeSeq(S, mode, Object.assign({ row: 0, once: true, sim: true }, o)), evs = [];
  for (let i = 0; i < steps && sq.alive(); i++) evs.push(...sq.step().evs);
  return JSON.parse(JSON.stringify(evs));
}
const LIVE = { live: true, sim: false }, REND = { sim: true };
const eng = (evs, t) => evs.filter(e => e.type === 'eng' && (t == null || e.track === t));
const RATES = [1 / 8, 1 / 4, 1 / 2, 1, 2, 4, 8, 16, 32];
// the LFO's swing (no clamp, no rounding) at second t at 120 bpm: a sine of rate index rt, depth dp (the swing is dp / 2 each way)
const swing = (t, rt, dp) => { const bt = t * 2 / RATES[rt]; return Math.sin((bt - Math.floor(bt)) * 2 * Math.PI) * dp / 2; };
const near = (a, b, m) => assert.ok(Math.abs(a - b) < 0.002, m + ': ' + a + ' vs ' + b);
const DEF_C = { cut: -1, res: -1, dec: -1, atk: -1, rel: -1 }, DEF_O = { cut: 0, res: 0, env: 0, atk: 0, dec: 0, sus: 0, rel: 0 };

test('(a) no commands and no engine LFOs: the old sequencer and the new one send the same events, and no eng', t => {
  let old; try { old = loadCore(execFileSync('git', ['show', BASE + ':core/seq-core.js'], { cwd: H.ROOT, encoding: 'utf8', maxBuffer: 1 << 28, stdio: ['ignore', 'pipe', 'ignore'] })); }
  catch (e){ t.skip('git history to ' + BASE + ' is not here'); return; }
  const shapes = [{ cells: { 0: [0, 1, 2, 3], 1: [0, 1], 2: [1, 2] } }, { cells: { 4: [0, 1, 2, 3], 0: [2, 3] }, fx: { 1: [['VOL', 99]] } },
    { cells: { 1: [0, 1, 2, 3], 5: [0] }, lfo: { 1: [{ id: 'vol:1', sh: 0, rt: 5, dp: 100 }, { id: 'pan:5', sh: 2, rt: 3, dp: 60 }] } }];
  let n = 0, total = 0;
  for (const sh of shapes) for (const [mode, o] of [['song', {}], ['song', { live: true, once: false }], ['song', { only: new Set([1]) }], ['sect', { track: 1, sect: 0 }], ['phrase', { track: 1, phrase: 0 }]]){
    const a = play(old, mkSong(sh), mode, o, 80), b = play(NEW, mkSong(sh), mode, o, 80);
    total += a.length; assert.deepEqual(b, a, mode + ' ' + JSON.stringify(o)); assert.equal(eng(b).length, 0); n++; }
  assert.equal(n, 15); assert.ok(total > 300);
});

test('(b) CUT 40 sends one eng with c.cut = 0x40, CUT FF later c.cut = -1; RES only after a RES command; ATK / DEC / REL likewise', () => {
  let E = eng(play(NEW, mkSong({ cells: { 1: [0] }, fx: { 0: [['CUT', 0x40]], 2: [['CUT', 0xFF]] } }), 'song', REND), 1);
  assert.equal(E.length, 2); assert.deepEqual(E[0].c, { ...DEF_C, cut: 0x40 }); assert.deepEqual(E[0].o, DEF_O); assert.equal(E[0].track, 1); assert.equal(E[0].t, 0);
  assert.deepEqual(E[1].c, DEF_C); assert.deepEqual(E[1].o, DEF_O); near(E[1].t, 0.25, 'CUT FF is on row 2');
  for (const [cmd, key] of [['RES', 'res'], ['ATK', 'atk'], ['DEC', 'dec'], ['REL', 'rel']]){
    E = eng(play(NEW, mkSong({ cells: { 1: [0] }, fx: key === 'res' ? { 1: [[cmd, 0x80]] } : { 1: [[cmd, 0x80]], 3: [[cmd, 0xFF]] } }), 'song', REND), 1);
    if (key === 'res'){ assert.equal(E.length, 1, 'RES stays once given (FF is a value, 255)'); assert.deepEqual(E[0].c, { ...DEF_C, res: 0x80 }); near(E[0].t, 0.125, 'on row 1'); }
    else { assert.equal(E.length, 2, cmd); assert.deepEqual(E[0].c, { ...DEF_C, [key]: 0x80 }); assert.deepEqual(E[1].c, DEF_C); near(E[1].t, 0.375, 'row 3'); }
  }
  // RES 00 is a command too (cRes 0, not -1); another track is untouched
  const ev = play(NEW, mkSong({ cells: { 1: [0], 2: [0] }, fx: { 0: [['RES', 0]] } }), 'song', REND);
  assert.equal(eng(ev, 1).length, 1, 'both tracks play the same pattern: each its own state'); assert.equal(eng(ev, 1)[0].c.res, 0); assert.equal(eng(ev, 2).length, 1);
  assert.equal(eng(play(NEW, mkSong({ cells: { 1: [0, 1] }, fx: { 0: [['CUT', 0x30]] } }), 'song', REND), 1).length, 1, 'a state that holds sends once');
});

test('(c) an LFO on cut:t: 4 eng a step live, 2 in a render; o.cut follows the shape about 0 (not clamped); a CUT 60 changes c only', () => {
  const lfo = { 1: [{ id: 'cut:1', sh: 0, rt: 5, dp: 200 }] };
  for (const [o, per] of [[LIVE, 4], [REND, 2]]){
    const E = eng(play(NEW, mkSong({ cells: { 1: [0, 1, 2, 3] }, lfo }), 'song', o), 1);
    assert.equal(E.length, 16 * per, per + ' a step, 16 steps');
    E.forEach(e => { near(e.o.cut, swing(e.t, 5, 200), 'o.cut at ' + e.t); assert.deepEqual(e.c, DEF_C); for (const k of ['res', 'env', 'atk', 'dec', 'sus', 'rel']) assert.equal(e.o[k], 0); });
    const v = E.map(e => e.o.cut); assert.ok(Math.max(...v) > 95 && Math.min(...v) < -95, 'both ways round the own value (cut:t clamps at 255 on sampled pads: here it does not)');
    const t = E.map(e => e.t); assert.ok(t.every((x, i) => !i || x > t[i - 1]));
    // with CUT 60 in force: the same offsets, c.cut = 0x60
    const C = eng(play(NEW, mkSong({ cells: { 1: [0, 1, 2, 3] }, lfo, fx: { 0: [['CUT', 0x60]] } }), 'song', o), 1);
    assert.equal(C.length, E.length); C.forEach((e, i) => { assert.equal(e.c.cut, 0x60); assert.equal(e.o.cut, E[i].o.cut); assert.equal(e.t, E[i].t); });
  }
  // the offset is clamped to +-255 (several LFOs on one target add up)
  const big = eng(play(NEW, mkSong({ cells: { 1: [0] }, lfo: { 1: [{ id: 'res:1', sh: 4, rt: 5, dp: 255 }, { id: 'res:1', sh: 4, rt: 5, dp: 255 }, { id: 'res:1', sh: 4, rt: 5, dp: 255 }] } }), 'song', REND), 1);
  assert.ok(big.every(e => Math.abs(e.o.res) <= 255) && big.some(e => e.o.res === 255 || e.o.res === -255));
});

test('(d) the same on env, atk, sus and rel; res and dec too', () => {
  for (const k of ['env', 'atk', 'sus', 'rel', 'res', 'dec']){
    for (const [o, per] of [[LIVE, 4], [REND, 2]]){
      const E = eng(play(NEW, mkSong({ cells: { 1: [0, 1, 2, 3] }, lfo: { 1: [{ id: k + ':1', sh: 1, rt: 5, dp: 160 }] } }), 'song', o), 1);
      assert.equal(E.length, 16 * per, k);
      E.forEach(e => { const bt = e.t * 2 / RATES[5], ph = bt - Math.floor(bt), tri = (ph < 0.5 ? ph * 4 - 1 : 3 - ph * 4) * 80; near(e.o[k], tri, k + ' at ' + e.t);
        for (const q of Object.keys(DEF_O)) if (q !== k) assert.equal(e.o[q], 0); assert.deepEqual(e.c, DEF_C); });
      const v = E.map(e => e.o[k]); assert.ok(Math.max(...v) > 70 && Math.min(...v) < -70, k + ' swings both ways');
    }
  }
  // a command in force makes no difference to the offsets of the same target (ATK 30 + atk LFO), and no command for env / sus exists
  const A = eng(play(NEW, mkSong({ cells: { 1: [0, 1, 2, 3] }, lfo: { 1: [{ id: 'atk:1', sh: 0, rt: 5, dp: 100 }] }, fx: { 0: [['ATK', 30]] } }), 'song', LIVE), 1);
  A.forEach(e => { assert.equal(e.c.atk, 30); near(e.o.atk, swing(e.t, 5, 100), 'atk'); });
  // a scene value on a target is an offset from tgtOwn while no command is in force; a command wins over it
  const sc = [{ n: 'A', v: { 'cut:1': 100, 'env:1': 40 } }, { n: 'B', v: {} }];
  const S1 = eng(play(NEW, mkSong({ cells: { 1: [0] }, sc }), 'song', REND), 1);
  assert.equal(S1.length, 1); assert.deepEqual(S1[0].o, { ...DEF_O, cut: 100 - 255, env: 40 }); assert.deepEqual(S1[0].c, DEF_C);
  const S2 = eng(play(NEW, mkSong({ cells: { 1: [0] }, sc, fx: { 0: [['CUT', 0x20]] } }), 'song', REND), 1);
  assert.equal(S2[0].o.cut, 0); assert.equal(S2[0].c.cut, 0x20); assert.equal(S2[0].o.env, 40);
});

test('(e) sampled pads: the note events (tw, fdi, rlo, ...) and PF.pno are exactly as before; the old core gives the same non-eng events', t => {
  let old; try { old = loadCore(execFileSync('git', ['show', BASE + ':core/seq-core.js'], { cwd: H.ROOT, encoding: 'utf8', maxBuffer: 1 << 28, stdio: ['ignore', 'pipe', 'ignore'] })); }
  catch (e){ t.skip('git history to ' + BASE + ' is not here'); return; }
  const shapes = [
    { cells: { 1: [0, 1] }, fx: { 0: [['CUT', 0x40], ['RES', 0x90]], 2: [['DEC', 0x50], ['ATK', 0x20]] } },
    { cells: { 1: [0, 1] }, lfo: { 1: [{ id: 'cut:1', sh: 0, rt: 5, dp: 200 }, { id: 'res:1', sh: 2, rt: 4, dp: 120 }, { id: 'dec:1', sh: 1, rt: 3, dp: 255 }] } },
    { cells: { 1: [0, 1] }, lfo: { 1: [{ id: 'cut:1', sh: 3, rt: 5, dp: 100 }, { id: 'env:1', sh: 0, rt: 5, dp: 100 }] }, fx: { 2: [['REL', 0x60]] } },
    { cells: { 1: [0, 1] }, sc: [{ n: 'A', v: { 'cut:1': 90, 'res:1': 200 } }, { n: 'B', v: {} }] }];
  let notes = 0;
  for (const sh of shapes) for (const o of [REND, LIVE]){
    const a = play(old, mkSong(sh), 'song', o), b = play(NEW, mkSong(sh), 'song', o).filter(e => e.type !== 'eng');
    assert.deepEqual(b, a, JSON.stringify(sh.lfo || sh.fx || sh.sc) + ' ' + JSON.stringify(o)); notes += a.filter(e => e.type === 'note').length; }
  assert.ok(notes > 10);
  // and the notes really carry tw / fdi / rlo
  const n = play(NEW, mkSong(shapes[0]), 'song', REND).filter(e => e.type === 'note');
  assert.ok(n.some(e => e.tw && e.tw.cut === 0x40 && e.tw.res === 0x90) && n.some(e => e.tw && e.tw.dec === 0x50) && n.some(e => e.fdi > 0) );
  assert.ok(play(NEW, mkSong(shapes[2]), 'song', REND).some(e => e.type === 'note' && e.rlo > 0));
});

test('(h) a mid-song start: the state in force goes out at the first step; a RESUME replay keeps the last eng of each track (CHASE_T)', () => {
  const lfo = { 1: [{ id: 'cut:1', sh: 0, rt: 5, dp: 100 }] };
  // a song started at row 2 (the commands of rows 0-1 never ran): its first step sends the LFO and the scene state at once
  const S = mkSong({ cells: { 1: [0, 1, 2, 3] }, lfo, sc: [{ n: 'A', v: { 'env:1': 30 } }, { n: 'B', v: {} }] });
  const E = eng(play(NEW, S, 'song', { ...REND, row: 2 }), 1);
  assert.ok(E.length >= 2 && E[0].t === 0 || Math.abs(E[0].t) < 1e-9); assert.equal(E[0].o.env, 30);
  // a command in force at the start of the first step
  const E2 = eng(play(NEW, mkSong({ cells: { 1: [0, 1] }, fx: { 0: [['CUT', 0x55]] } }), 'song', REND, 1), 1);
  assert.equal(E2.length, 1); assert.equal(E2[0].c.cut, 0x55); assert.equal(E2[0].t, 0);
  // the replay the page runs up to the step it stopped on keeps the LAST eng per track, which is the state in force (it is a complete state, not a change)
  const evs = play(NEW, mkSong({ cells: { 1: [0, 1] }, fx: { 0: [['CUT', 0x55]], 2: [['ATK', 0x33]] } }), 'song', REND);
  const last = new Map(); for (const e of evs) if (e.type === 'eng') last.set(e.track, e);
  assert.deepEqual(last.get(1).c, { ...DEF_C, cut: 0x55, atk: 0x33 });
  assert.match(src, /const CHASE_T = new Set\(\[[^\]]*'eng'/, 'the page keeps eng events of a RESUME replay and of CHASE');
});

// ---------- the page player ----------
function fakeNode(has){
  const rec = {}, nodeObj = { __rec: rec, parameters: { get: n => { if (!has.includes(n)) return undefined;
    return rec[n] || (rec[n] = { calls: [], setValueAtTime(v, t){ this.calls.push(['set', v, t]); }, linearRampToValueAtTime(v, t){ this.calls.push(['ramp', v, t]); }, cancelScheduledValues(t){ this.calls.push(['cancel', t]); } }); } } };
  return nodeObj;
}
const ALL12 = ['cCut', 'cRes', 'cDec', 'cAtk', 'cRel', 'oCut', 'oRes', 'oEnv', 'oAtk', 'oDec', 'oSus', 'oRel'];
function pageCtx(){
  const eb = src.match(/  const engSt = new Map\(\)[\s\S]*?\n(?=  function bassNote\(ev, b, T, ck, fk\)\{)/), lc = src.match(/  function liveCut\(b, t, tw\)\{[\s\S]*?\n  \}\n/), ec = src.match(/const ENG_CUT = new Set\([^\n]*\n/);
  assert.ok(eb && lc && ec, 'index.html: the eng block, liveCut and ENG_CUT are where this test cuts them from');
  const made = [], ctx = vm.createContext({ Math, Object, Map, Array, Number, clamp: (v, a, b) => Math.min(b, Math.max(a, v)), cutHz: v => 30 * Math.pow(600, v / 255), bassN: new Map(),
    ac: { currentTime: 0, sampleRate: 48000, createBiquadFilter(){ const f = { frequency: fakeNode([]).parameters.get && { setValueAtTime(){}, cancelScheduledValues(){} }, Q: { setValueAtTime(){}, cancelScheduledValues(){} }, connect(){} }; made.push(f); return f; } } });
  vm.runInContext(ec[0] + eb[0] + lc[0], ctx); ctx.__made = made; return ctx;
}
const put = (ctx, key, kind, has) => { const node = fakeNode(has); node.disconnect = () => {}; node.connect = () => {}; ctx.bassN.set(key, { node, kind, g: {} }); return node; };

test('(f) page player: eng sets cX with setValueAtTime and oX with a ramp from the previous point; guarded; a later node starts from the state; STOP resets', () => {
  const ctx = pageCtx(), run = c => vm.runInContext(c, ctx);
  const a = put(ctx, '1:5:bass', 'bass', ALL12), b = put(ctx, '1:6:acid', 'acid', ['cCut', 'oCut']), other = put(ctx, '2:5:bass', 'bass', ALL12), noParams = put(ctx, '1:7:pstrikel', 'pstrikel', []), twelve = put(ctx, '11:5:bass', 'bass', ALL12);
  ctx.__e1 = { type: 'eng', track: 1, t: 2, c: { cut: 0x40, res: -1, dec: -1, atk: 10, rel: -1 }, o: { cut: 12.5, res: 0, env: 0, atk: 0, dec: 0, sus: 0, rel: 0 } };
  run('engTo(__e1)');
  const r = a.__rec;
  assert.deepEqual(r.cCut.calls, [['set', 0x40, 2]]); assert.deepEqual(r.cRes.calls, [['set', -1, 2]]); assert.deepEqual(r.cAtk.calls, [['set', 10, 2]]);
  assert.deepEqual(r.oCut.calls, [['set', 12.5, 2]], 'the first point a node gets is a step');
  assert.deepEqual(Object.keys(r).sort(), [...ALL12].sort());
  assert.deepEqual(Object.keys(b.__rec).sort(), ['cCut', 'oCut'], 'a node without the other params: skipped, no throw');
  assert.equal(Object.keys(other.__rec).length, 0, 'track 2 untouched'); assert.equal(Object.keys(twelve.__rec).length, 0, 'track 11 is not track 1');
  ctx.__e2 = { type: 'eng', track: 1, t: 2.03125, c: ctx.__e1.c, o: { ...ctx.__e1.o, cut: -40 } }; run('engTo(__e2)');
  assert.deepEqual(r.oCut.calls[1], ['ramp', -40, 2.03125], 'the next point: a ramp to it'); assert.deepEqual(r.cCut.calls[1], ['set', 0x40, 2.03125]);
  // a node made later, and a note on it, starts from the state in force
  const late = put(ctx, '1:8:vox', 'vox', ALL12); ctx.__late = late;
  assert.equal(Object.keys(late.__rec).length, 0);
  run("engPut(__late, engSt.get(1), 3, false)");   // what bassNote does at a note's start
  assert.deepEqual(late.__rec.oCut.calls, [['set', -40, 3]]); assert.deepEqual(late.__rec.cCut.calls, [['set', 0x40, 3]]);
  assert.match(src, /const st = ev\.track != null \? engSt\.get\(ev\.track\) : null; if \(st\) engPut\(b\.node, st, t, false\);/, 'bassNote applies it');
  // a default state forgets the track (a node made later is left alone), and sets the defaults on the live nodes
  ctx.__e3 = { type: 'eng', track: 1, t: 4, c: { cut: -1, res: -1, dec: -1, atk: -1, rel: -1 }, o: { cut: 0, res: 0, env: 0, atk: 0, dec: 0, sus: 0, rel: 0 } }; run('engTo(__e3)');
  assert.equal(run('engSt.has(1)'), false); assert.deepEqual(r.cCut.calls.at(-1), ['set', -1, 4]);
  // STOP: all twelve back to their defaults on every node that was given any, and the state forgotten
  run('engTo(__e1)'); run('engReset(9)');
  assert.equal(run('engSt.size'), 0);
  for (const n of [a, late]) for (const k of ALL12){ const c = n.__rec[k].calls, last = c.at(-1); assert.deepEqual(last, ['set', k[0] === 'c' ? -1 : 0, 9], k); assert.deepEqual(c.at(-2), ['cancel', 9]); }
  assert.deepEqual(b.__rec.cCut.calls.at(-1), ['set', -1, 9], 'the node with only some of the params: those reset');
  const nb = b.__rec.cCut.calls.length; run('engReset(11)'); assert.equal(b.__rec.cCut.calls.length, nb, 'a node already at defaults is not touched again');
  assert.equal(Object.keys(other.__rec).length, 0, 'a node that never got one is never touched (a song with no commands: zero AudioParam calls)');
});

test('(f) liveCut is skipped for an engine with its own cutoff (and cCut present); other kinds keep it exactly', () => {
  const ctx = pageCtx(), run = c => vm.runInContext(c, ctx), tw = { cut: 0x40, res: 0x20 };
  for (const kind of ['bass', 'acid', 'mach', 'vox', 'pmonol', 'ppolyl', 'porbitl', 'pglossl', 'pwobl']){
    const n = fakeNode(['cCut']); n.connect = n.disconnect = () => {}; ctx.__b = { node: n, kind, g: {} }; run(`liveCut(__b, 1, ${JSON.stringify(tw)})`);
    assert.equal(n.__lp, undefined, kind + ': no post-engine filter'); }
  assert.equal(ctx.__made.length, 0);
  for (const [kind, has] of [['pfoldl', ['cCut']], ['pstrikel', []], ['pcoill', []], ['bass', []]]){   // (bass without cCut: a processor that has not got it yet keeps today's behaviour)
    const n = fakeNode(has); n.connect = n.disconnect = () => {}; ctx.__b = { node: n, kind, g: {} }; run(`liveCut(__b, 1, ${JSON.stringify(tw)})`);
    assert.ok(n.__lp, kind + ': liveCut as today'); }
  assert.equal(ctx.__made.length, 4);
});

// ---------- the core ----------
function coreWith(){
  const C = H.workletScope(); vm.runInContext(H.read('core/seq-core.js'), C, { filename: 'seq-core.js' });
  C.__tcNodes = new Map();
  vm.runInContext("TFX = {};", C);
  const core = new C.__registered['thunder-core'](); core.kv[5] = { lk: { kind: 'bass', vox: false, ps: false } };
  return { C, core };
}
const at = (A, t) => A.at(t);
test('(g) core: eng sets override timelines on the track\'s registered nodes, applied in liveStart, cleared at STOP', () => {
  const { C, core } = coreWith(), proc = {}, other = {}; C.__tcNodes.set('1:5:bass', proc); C.__tcNodes.set('2:5:bass', other);
  const note = (track, t) => ({ type: 'note', track, pad: 5, t, rate: 1, gain: 1, pan: 128, ii: 0 });
  // an eng before any note: remembered; the note's liveStart applies it
  assert.equal(core.ev({ type: 'eng', track: 1, t: 1, c: { cut: 0x40, res: -1, dec: -1, atk: -1, rel: 0x20 }, o: { cut: 10, res: 0, env: 0, atk: 0, dec: 0, sus: 0, rel: 0 } }), false, 'the page player sets its own too');
  assert.equal(proc.__ov, undefined, 'no node known yet: nothing to set');
  assert.equal(core.ev(note(1, 2)), true);
  let p = proc.__ov.p; assert.equal(at(p.get('cCut'), 2.5), 0x40); assert.equal(at(p.get('cRel'), 2.5), 0x20); assert.equal(at(p.get('cRes'), 2.5), -1); assert.equal(at(p.get('oCut'), 2.5), 10);
  assert.equal(p.get('cCut').from, 2, 'from the note\'s time');
  assert.equal(other.__ov, undefined);
  // a held note: a point sets cX and ramps oX from the last point
  core.ev({ type: 'eng', track: 1, t: 3, c: { cut: 0x40, res: -1, dec: -1, atk: -1, rel: 0x20 }, o: { cut: 10, res: 0, env: 0, atk: 0, dec: 0, sus: 0, rel: 0 } });
  core.ev({ type: 'eng', track: 1, t: 3.5, c: { cut: 0x50, res: -1, dec: -1, atk: -1, rel: 0x20 }, o: { cut: -30, res: 0, env: 0, atk: 0, dec: 0, sus: 0, rel: 0 } });
  assert.equal(at(p.get('cCut'), 3.4), 0x40); assert.equal(at(p.get('cCut'), 3.6), 0x50);
  near(at(p.get('oCut'), 3.25), -10 + 0 * 1, 'halfway through the ramp from 10 (t 3) to -30 (t 3.5)'); assert.equal(at(p.get('oCut'), 3.5), -30);
  // the override reads in __tcOv like the others: the node's declared params get the constants
  C.currentTime = 4; const Q = C.__tcOv(proc, { cCut: [-1], oCut: [0], rate: [1], cDec: [-1] }, 128); assert.deepEqual(Array.from(Q.cCut), [0x50]); assert.equal(Q.oCut[0], -30); assert.deepEqual(Array.from(Q.cDec), [-1]);
  const noDecl = C.__tcOv(proc, { rate: [1] }, 128); assert.deepEqual(Object.keys(noDecl), ['rate'], 'a param the node does not declare is never reached');
  // STOP: all twelve back to their defaults at the stop time, on every node; the state forgotten
  core.stopAll(6);
  for (const k of ALL12){ const A = p.get(k); assert.equal(at(A, 6.01), k[0] === 'c' ? -1 : 0, k); }
  assert.equal(core.engSt[1], null); assert.ok(proc.__ov.until <= 6 + 4 + 1e-9);
  // a note after STOP starts clean
  assert.equal(core.ev(note(1, 7)), true); assert.equal(at(proc.__ov.p.get('cCut'), 7.1), -1);
  // a default state does not wake a node that has none; once it has one it gets the defaults
  core.ev({ type: 'eng', track: 1, t: 8, c: { cut: 0x10, res: -1, dec: -1, atk: -1, rel: -1 }, o: {} }); assert.equal(at(proc.__ov.p.get('cCut'), 8.1), 0x10);
  core.ev({ type: 'eng', track: 1, t: 9, c: { cut: -1, res: -1, dec: -1, atk: -1, rel: -1 }, o: { cut: 0, res: 0, env: 0, atk: 0, dec: 0, sus: 0, rel: 0 } }); assert.equal(at(proc.__ov.p.get('cCut'), 9.1), -1);
  assert.equal(core.engSt[1], null);
});

test('(g) core: a RESUME / chase keeps the state the silent steps left, and sets it on the nodes at the start', () => {
  const { C, core } = coreWith(), proc = {}; C.__tcNodes.set('1:5:bass', proc);
  const S = mkSong({ cells: { 1: [0, 1] }, fx: { 0: [['CUT', 0x44]] } }); C.__S = JSON.parse(JSON.stringify(S)); vm.runInContext('S = __S;', C);
  core.lnode(1, 5, 0);   // the core has played this node before
  core.play({ op: 'play', mode: 'song', o: { row: 0, once: true }, rs: { ref: 1, row: 1, step: 0 }, t0: 5, gen: 1 });
  assert.equal(core.engSt[1] && core.engSt[1].c.cut, 0x44);
  assert.equal(at(proc.__ov.p.get('cCut'), 5.001), 0x44, 'on the node at the start');
});
