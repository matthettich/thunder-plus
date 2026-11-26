// FLEX / STEP GATE GOLDEN: writes tests/fixtures/flex-core-golden.json, the committed reference that tests/flex-golden.test.js
// compares FlexCore (patterns 1..511) and StepGateCore (0..15) against. It replaces the old comparison with build 562 read from
// git history (that one failed locally once build 647 changed Flex on purpose, and skipped in CI where checkout is shallow).
//
//   node tools/make-flex-golden.js      regenerate from the CURRENT index.html. Do this ONLY when a change to FlexCore /
//                                       StepGateCore / clkSpb is meant to change the sound; review the diff of the JSON
//                                       (which patterns moved) before committing, and say why in HANDOVER.
//
// Captured at build 657 (20 Nov 2026). With git history for b0cb7cc (build 562) available, the "build562" note is recomputed:
// which patterns still match build 562 bit for bit and which moved. Without that history the existing note is kept.
'use strict';
const fs = require('node:fs'), path = require('node:path'), cp = require('node:child_process');
const L = require('../tests/flex-golden-lib'), ROOT = path.resolve(__dirname, '..');
const OUT = path.join(ROOT, 'tests/fixtures/flex-core-golden.json');
const BUILD = (fs.readFileSync(path.join(ROOT, 'index.html'), 'utf8').match(/const BUILD = (\d+);/) || [])[1];
const cur = L.hashes(fs.readFileSync(path.join(ROOT, 'index.html'), 'utf8'));
let prev = null; try { prev = JSON.parse(fs.readFileSync(OUT, 'utf8')); } catch (e) {}
let old = null; try { old = cp.execFileSync('git', ['show', 'b0cb7cc:index.html'], { cwd: ROOT, encoding: 'utf8', maxBuffer: 1 << 28, stdio: ['ignore', 'pipe', 'ignore'] }); } catch (e) {}
let note562;
if (old) {
  const o = L.hashes(old), moved = [], movedGate = [];
  for (const k in cur.flex) if (o.flex[k] !== cur.flex[k]) moved.push(+k);
  for (const k in cur.gate) if (o.gate[k] !== cur.gate[k]) movedGate.push(+k);
  note562 = { flexPatternsDifferingFromBuild562: moved, gatePatternsDifferingFromBuild562: movedGate,
    why: 'Build 647 (FLX CHECK 3) fixed FlexCore.rd: buffer index 0 with a delay of about 1e-12 made N - D round to N, b[N] was undefined, one NaN sample (now x >= N wraps to 0). Build 562 output has a NaN at those patterns; the fix gives a finite sample. Every other pattern, and every Step gate pattern, still equals build 562 bit for bit.' };
} else if (prev && prev.build562) note562 = prev.build562;
else { console.error('no git history for b0cb7cc and no earlier file: cannot write the build562 note (git fetch --unshallow)'); process.exit(2); }
const doc = { _about: 'Reference hashes (sha256, first 16 hex of the float32 bytes) of FlexCore and StepGateCore output: 22 blocks of 128 stereo samples of tests/helpers.js testSignal, 48 kHz. Regenerate deliberately with node tools/make-flex-golden.js (see its header).',
  capturedAtBuild: +BUILD, build562: note562, flex: cur.flex, gate: cur.gate };
fs.writeFileSync(OUT, JSON.stringify(doc, null, 1) + '\n'); console.log('wrote ' + path.relative(ROOT, OUT) + ' at build ' + BUILD + '; moved vs 562: flex ' + JSON.stringify(note562.flexPatternsDifferingFromBuild562) + ' gate ' + JSON.stringify(note562.gatePatternsDifferingFromBuild562));
