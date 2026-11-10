// SAVE SAFETY (10 Nov): a project / kit file must not be written without the audio of a sample that is still loading
// from IndexedDB (smpToJSON writes only a {ref} for it). smpSaveGate waits, then asks. The browser end to end is not run here.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict');
const H = require('./helpers');
const src = H.read('index.html');
function cut(text, re, what) { const m = text.match(re); assert.ok(m, `could not find ${what}; the code moved or changed shape, so this test needs updating`); return m[0]; }

// A gate over a fake kit. resolve(kit, state) plays resolveSamples: it may fill samples in, or leave them missing.
function mk(layers, resolve) {
  const kit = { voices: [{ layers }] }, st = { resolving: false, asked: [], answer: true };
  const body = cut(src, /function smpNoAudio\(\)\{[^\n]*\nasync function smpSaveGate\([\s\S]*?\n\}\n/, 'smpNoAudio + smpSaveGate');
  const f = new Function('kit', 'st', 'resolveSamples', 'askYes', 'let smpResolving = false; const sync = () => { smpResolving = st.resolving; };\n'
    + body.replace(/smpResolving/g, 'smpResolving') + '\nreturn {gate: (c, q) => { smpResolving = st.resolving; return smpSaveGate(c, q); }, setRes: v => { smpResolving = v; }};');
  let api;
  const resolveSamples = async () => { api.setRes(true); st.resolving = true; try { await resolve(kit, st); } finally { st.resolving = false; api.setRes(false); } };
  const askYes = async (msg, ok) => { st.asked.push({ msg, ok }); return st.answer; };
  api = f(kit, st, resolveSamples, askYes);
  return { gate: api.gate, st, kit };
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

test('quiet mode (automatic backups) never asks: it just reports an incomplete kit', async () => {
  const t = mk([{ sample: notYet() }], async () => {});
  assert.equal(await t.gate(100, true), false); assert.equal(t.st.asked.length, 0);
});

test('every file-writing path goes through the gate', () => {
  const saveProj = cut(src, /async function saveProj\(as\)\{[\s\S]*?\n\}\n/, 'saveProj');
  assert.ok(saveProj.indexOf('smpSaveGate()') > 0 && saveProj.indexOf('smpSaveGate()') < saveProj.indexOf('DESK.save'));
  assert.match(cut(src, /async function shareProj\(\)\{[\s\S]*?\n\}\n/, 'shareProj'), /smpSaveGate\(\)/);
  assert.match(cut(src, /async function backupNow\(force\)\{[\s\S]*?\n\}\n/, 'backupNow'), /smpSaveGate\(10000, true\)/);
  assert.match(src, /\$\('bSaveKit'\)\.addEventListener\('click', async \(\) => \{ if \(!await smpSaveGate\(\)\) return;/);
});
