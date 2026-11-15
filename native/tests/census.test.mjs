// Unit test for native/tools/census.mjs (run: node --test native/tests/census.test.mjs). Uses sounds/ELECTRO.thunder.
import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { censusSong, FXC } from '../tools/census.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..');
const electro = JSON.parse(fs.readFileSync(path.join(root, 'sounds', 'ELECTRO.thunder'), 'utf8'));

test('ELECTRO: 40 voices, engines add up, no samples (all synthesised)', () => {
  const c = censusSong(electro);
  assert.equal(c.scalars.songs, 1);
  assert.equal(c.scalars.voices, 40);
  assert.equal(Object.values(c.engines).reduce((a, b) => a + b, 0), 40);
  assert.equal(c.scalars.samples || 0, 0);
  assert.ok(c.engines['layers (layer stack)'] >= 1);
  assert.ok(Object.values(c.commands).reduce((a, b) => a + b, 0) > 0);
  assert.ok(Object.keys(c.trackFx).length > 0);
});

test('a sample layer is counted with its length', () => {
  const b64 = Buffer.alloc(48000 * 2).toString('base64');   // 1 s of int16 at 48 kHz
  const song = { app: 'thunder-project', v: 1, build: 1, song: { v: 6, ntr: 1, phrases: [[[60, -1, -1, -1, -1, 0, FXC.indexOf('VOL'), 5, -1, -1, 9999, 1]]] },
    kit: { voices: [{ engine: null, layers: [{ on: true, type: 'sample', sample: { sr: 48000, b64 } }] }] } };
  const c = censusSong(song);
  assert.equal(c.scalars.samples, 1);
  assert.equal(c.scalars.sampleSeconds, 1);
  assert.equal(c.layerTypes.sample, 1);
  assert.equal(c.commands.VOL, 1);
  assert.equal(c.commands['unknown code 9999'], 1);   // unknown things are printed, never skipped
});

test('FXC list equals the one in index.html', () => {
  const html = fs.readFileSync(path.join(root, 'index.html'), 'utf8');
  const m = /const FXC = (\[[^\]]*\]);/.exec(html);
  assert.ok(m, 'FXC not found in index.html');
  assert.deepEqual(JSON.parse(m[1].replace(/'/g, '"')), FXC);
});
