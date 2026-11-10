// SAVE SAFETY (10 Nov): a project / kit file must not be written without the audio of a sample that is still loading
// from IndexedDB (smpToJSON writes only a {ref} for it). smpSaveGate waits, then asks. The browser end to end is not run here.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict');
const H = require('./helpers');
const src = H.read('index.html');
function cut(text, re, what) { const m = text.match(re); assert.ok(m, `could not find ${what}; the code moved or changed shape, so this test needs updating`); return m[0]; }

// A gate over a fake kit, song presets and impulse responses. resolve(world, state) plays resolveSamples: it may fill samples in, or leave them missing.
// o = {layers, presets: [{sample:{ref}}] (layer presets), irs: [{L:{ref}}], held: Set of ids on hand, ir: Set of ids in IRDATA, fill, irsRes}
function mk(layers, resolve, o) {
  o = o || {};
  const kit = { voices: [{ layers }] }, st = { resolving: false, asked: [], answer: true };
  const S = { irs: o.irs || [], presets: { voices: [], layers: (o.presets || []).map(l => ({ l })) } };
  const held = o.held || new Set(), IRDATA = o.ir || new Set();
  const body = cut(src, /var smpFillBusy = 0;[^\n]*\nfunction smpNoAudio\(\)\{[\s\S]*?\nasync function smpSaveGate\([\s\S]*?\n\}\n/, 'smpNoAudio + smpSaveGate');
  const f = new Function('kit', 'S', 'SP', 'smpById', 'IRDATA', 'resolveSamples', 'presetSmpFill', 'irsResolve', 'askYes', 'let smpResolving = false;\n'
    + body + '\nreturn {gate: (c, q) => smpSaveGate(c, q), setRes: v => { smpResolving = v; }, count: () => smpNoAudio()};');
  let api;
  const resolveSamples = async () => { api.setRes(true); st.resolving = true; try { await resolve(kit, st); } finally { st.resolving = false; api.setRes(false); } };
  const askYes = async (msg, ok) => { st.asked.push({ msg, ok }); return st.answer; };
  const SP = () => S.presets, smpById = id => held.has(id) ? {} : null;
  api = f(kit, S, SP, smpById, IRDATA, resolveSamples, o.fill || (async () => {}), o.irsRes || (async () => {}), askYes);
  return { gate: api.gate, count: api.count, st, kit, S, held, IRDATA };
}
const loaded = { id: 'a', data: new Float32Array(4) }, notYet = () => ({ id: 'b', data: null });

test('nothing is pending: the gate passes without asking or waiting', async () => {
  const t = mk([{ sample: loaded }, { sample: null }], async () => assert.fail('must not look anything up'));
  assert.equal(await t.gate(), true); assert.equal(t.st.asked.length, 0);
});

test('a sample still loading is waited for, then the save goes ahead without a question', async () => {
  const S = notYet(); const t = mk([{ sample: S }], async () => { await new Promise(r => setTimeout(r, 30)); S.data = new Float32Array(8); });
  assert.equal(await t.gate(), true); assert.equal(t.st.asked.length, 0);
});

test('a sample that cannot be found: the user is asked, and Save anyway / cancel is honoured', async () => {
  const t = mk([{ sample: notYet() }, { sample: notYet() }], async () => {});
  t.st.answer = false; assert.equal(await t.gate(), false);
  assert.match(t.st.asked[0].msg.title, /2 samples could not be found/); assert.match(t.st.asked[0].msg.body, /without them/);
  t.st.answer = true; assert.equal(await t.gate(), true);
});

test('a load that never finishes does not block a save beyond the cap', async () => {
  const t = mk([{ sample: notYet() }], () => new Promise(() => {}));
  const t0 = Date.now(); t.st.answer = false;
  assert.equal(await t.gate(200), false); assert.ok(Date.now() - t0 < 2000, 'returned after the cap');
  assert.match(t.st.asked[0].msg.title, /1 sample not loaded yet/);
});

test('quiet mode (automatic backups) never asks: a sample still loading skips the backup, one that is gone for good does not (lead, 10 Nov)', async () => {
  const gone = mk([{ sample: notYet() }], async () => {});   // the lookup finished: not in storage
  assert.equal(await gone.gate(100, true), true, 'backups carry on without it, as before'); assert.equal(gone.st.asked.length, 0);
  const slow = mk([{ sample: notYet() }], () => new Promise(() => {}));   // a load that never finishes
  assert.equal(await slow.gate(150, true), false, 'still loading: skip this backup'); assert.equal(slow.st.asked.length, 0);
});

test('song-preset samples and impulse responses are counted, waited for, and named in the question', async () => {
  const t = mk([{ sample: loaded }], async () => {}, { presets: [{ sample: { ref: 'p1' } }, { sample: { ref: 'p2' } }], irs: [{ L: { ref: 'i1' }, R: { ref: 'i1r' } }] });
  assert.deepEqual({ ...t.count() }, { samples: 0, presets: 2, irs: 1, n: 3 });
  t.st.answer = false; assert.equal(await t.gate(300), false);
  assert.match(t.st.asked[0].msg.title, /2 preset samples and 1 impulse response could not be found/); assert.match(t.st.asked[0].msg.body, /without them/);
  const u = mk([{ sample: notYet() }], async () => {}, { irs: [{ L: { ref: 'i1' } }] }); u.st.answer = false; await u.gate(300);
  assert.match(u.st.asked[0].msg.title, /1 sample and 1 impulse response could not be found/);
});

test('a preset sample already on a pad or held, and one with its audio inline, is not counted twice or at all', () => {
  const S1 = { id: 'x', data: null };
  const t = mk([{ sample: S1 }], async () => {}, { presets: [{ sample: { ref: 'x' } }, { sample: { ref: 'h' } }, { sample: { b64: 'AA', ref: 'q' } }], held: new Set(['h']) });
  assert.deepEqual({ ...t.count() }, { samples: 1, presets: 0, irs: 0, n: 1 });
});

test('impulse responses and preset samples that arrive during the wait let the save go ahead silently', async () => {
  const o = { presets: [{ sample: { ref: 'p1' } }], irs: [{ L: { ref: 'i1' } }], held: new Set(), ir: new Set() };
  o.fill = async () => { await new Promise(r => setTimeout(r, 20)); o.held.add('p1'); };
  o.irsRes = async () => { await new Promise(r => setTimeout(r, 20)); o.ir.add('i1'); };
  const t = mk([{ sample: loaded }], async () => {}, o);
  assert.equal(await t.gate(), true); assert.equal(t.st.asked.length, 0);
});

test('quiet mode: an impulse response / preset sample gone for good does not stop a backup, one still loading does', async () => {
  const gone = mk([], async () => {}, { presets: [{ sample: { ref: 'p1' } }], irs: [{ L: { ref: 'i1' } }] });
  assert.equal(await gone.gate(200, true), true); assert.equal(gone.st.asked.length, 0);
  const slow = mk([], async () => {}, { irs: [{ L: { ref: 'i1' } }], irsRes: () => new Promise(() => {}) });
  assert.equal(await slow.gate(150, true), false); assert.equal(slow.st.asked.length, 0);
  const slowP = mk([], async () => {}, { presets: [{ sample: { ref: 'p1' } }], fill: () => new Promise(() => {}) });
  slowP.st.answer = false; assert.equal(await slowP.gate(150), false); assert.match(slowP.st.asked[0].msg.title, /1 preset sample not loaded yet/);
});

test('wavetables, resynth data and frozen samples are inline JSON / ordinary layer samples: they are not part of the gate', () => {
  const g = cut(src, /var smpFillBusy = 0;[\s\S]*?\nasync function smpSaveGate\([\s\S]*?\n\}\n/, 'gate');
  assert.ok(!/wtData|rsData|frzSample/.test(g.replace(/\/\/[^\n]*/g, '')), 'no code path for them');
});

test('every file-writing path goes through the gate', () => {
  const saveProj = cut(src, /async function saveProj\(as\)\{[\s\S]*?\n\}\n/, 'saveProj');
  assert.ok(saveProj.indexOf('smpSaveGate()') > 0 && saveProj.indexOf('smpSaveGate()') < saveProj.indexOf('DESK.save'));
  assert.match(cut(src, /async function shareProj\(\)\{[\s\S]*?\n\}\n/, 'shareProj'), /smpSaveGate\(\)/);
  assert.match(cut(src, /async function backupNow\(force\)\{[\s\S]*?\n\}\n/, 'backupNow'), /smpSaveGate\(10000, true\)/);
  assert.match(src, /\$\('bSaveKit'\)\.addEventListener\('click', async \(\) => \{ if \(!await smpSaveGate\(\)\) return;/);
});
