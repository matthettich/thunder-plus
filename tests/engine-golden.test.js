// Engine goldens: every factory voice / preset of the JavaScript sound engines is rendered in Node and its exact float bits are
// hashed (tests/golden/engines.json). A change that alters how an engine sounds, by even one bit, fails here. See tests/README.md
// ("Engine goldens") for what is and is not covered and how to update on purpose. A hash says "unchanged", never "correct".
//   npm test                       renders the fixed subset (every STRIDE-th voice, see SUBSET below) and checks the whole list of names
//   ENGINE_GOLDEN=all npm test     renders and checks every voice (slower)
//   UPDATE_GOLDEN=1 npm test       rewrites tests/golden/engines.json from a full render (also checks each voice renders identically twice)
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), fs = require('fs'), path = require('path');
const G = require('../tools/engine-golden');

const FILE = path.join(__dirname, 'golden', 'engines.json');
const UPDATE = !!process.env.UPDATE_GOLDEN, ALL = process.env.ENGINE_GOLDEN === 'all';
/* SUBSET: the default `npm test` renders every STRIDE-th voice of the list (about 1 in STRIDE) plus every layer type and the
   first preset of every engine, so every engine kind is touched in each run. */
const STRIDE = 6, firstOf = new Set();
const inSubset = (key, i) => { if (ALL || UPDATE || i % STRIDE === 0 || key.startsWith('layertype|')) return true;
  const m = /^engine\|[^|]+\|/.exec(key); if (m && !firstOf.has(m[0])) { firstOf.add(m[0]); return true; } return false; };
const db = (a, b) => a > 0 && b > 0 ? (20 * Math.log10(b / a)).toFixed(2) + ' dB' : (a === b ? '0 dB' : 'n/a');

test('engine goldens: every covered factory voice renders exactly as before', () => {
  const items = G.listVoices(), keys = items.map(i => i.key);
  assert.equal(new Set(keys).size, keys.length, 'duplicate voice keys');
  if (UPDATE) {
    const voices = {}, notDeterministic = [];
    for (const it of items) {
      const a = G.renderFingerprint(it), b = G.renderFingerprint(it);
      if (JSON.stringify(a) !== JSON.stringify(b)) { notDeterministic.push(it.key); continue; }
      voices[it.key] = a.fp ? { ...a.fp } : { error: a.error };
    }
    const out = { _about: 'Engine goldens: see tests/README.md. Regenerate with UPDATE_GOLDEN=1 npm test (only when a sound change is intended).',
      node: process.version, sampleRate: G.SR, count: Object.keys(voices).length, notDeterministic,
      notCovered: G.NOT_COVERED, voices };
    fs.mkdirSync(path.dirname(FILE), { recursive: true });
    fs.writeFileSync(FILE, JSON.stringify(out, null, 1) + '\n');
    return;
  }
  assert.ok(fs.existsSync(FILE), 'No tests/golden/engines.json. Create it with: UPDATE_GOLDEN=1 npm test');
  const want = JSON.parse(fs.readFileSync(FILE, 'utf8')), skip = new Set(want.notDeterministic || []);
  const nodeNote = want.node !== process.version ? '\n(golden made with Node ' + want.node + ', this is Node ' + process.version + ': Math.sin / pow can differ in the last bit between Node versions)' : '';
  const problems = [];
  const have = new Set(keys);
  for (const k of Object.keys(want.voices)) if (!have.has(k)) problems.push('MISSING  ' + k + ' (in the golden, no longer in the factory lists)');
  items.forEach((it, i) => {
    if (skip.has(it.key)) return;
    const w = want.voices[it.key];
    if (!w) { problems.push('NEW      ' + it.key + ' (a factory voice that is not in the golden)'); return; }
    if (!inSubset(it.key, i)) return;
    const r = G.renderFingerprint(it);
    if (r.error || w.error) { if (r.error !== w.error) problems.push('CHANGED  ' + it.key + ': error ' + JSON.stringify(w.error) + ' -> ' + JSON.stringify(r.error || 'renders now')); return; }
    if (r.fp.sha256 !== w.sha256) problems.push('CHANGED  ' + it.key + ': peak ' + w.peak + ' -> ' + r.fp.peak + ' (' + db(w.peak, r.fp.peak) + '), rms ' + w.rms + ' -> ' + r.fp.rms + ' (' + db(w.rms, r.fp.rms) + '), length ' + w.len + ' -> ' + r.fp.len);
  });
  assert.deepEqual(problems, [], problems.length + ' engine render(s) differ from tests/golden/engines.json. If the sound change is intended run UPDATE_GOLDEN=1 npm test; otherwise find what changed the engine.' + nodeNote + '\n' + problems.slice(0, 60).join('\n'));
});
