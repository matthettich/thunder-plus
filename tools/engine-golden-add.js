'use strict';
// Adds the NEW factory voices to tests/golden/engines.json without touching any existing entry (additions only):
//   node tools/engine-golden-add.js        renders each missing key twice (it must repeat), and writes them into the golden.
// Existing keys are never re-rendered or changed. Used for the "many more presets" pass; the general way is UPDATE_GOLDEN=1.
const fs = require('fs'), path = require('path'), G = require('./engine-golden');
const FILE = path.join(__dirname, '..', 'tests', 'golden', 'engines.json');
const gold = JSON.parse(fs.readFileSync(FILE, 'utf8'));
let added = 0; const bad = [];
for (const it of G.listVoices()) {
  if (gold.voices[it.key]) continue;
  const a = G.renderFingerprint(it), b = G.renderFingerprint(it);
  if (JSON.stringify(a) !== JSON.stringify(b)) { bad.push(it.key); continue; }
  gold.voices[it.key] = a.fp ? { ...a.fp } : { error: a.error }; added++;
}
gold.count = Object.keys(gold.voices).length;
if (bad.length) { console.log('NOT DETERMINISTIC (not added):', bad); process.exitCode = 1; }
fs.writeFileSync(FILE, JSON.stringify(gold, null, 1) + '\n');
console.log('added', added, 'voices; golden now', gold.count);
