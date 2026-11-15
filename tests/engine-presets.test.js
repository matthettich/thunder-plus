// Engine presets: the 14-15 Nov 2026 "many more presets" pass (non-drum engines). Checks, per engine: the minimum number of new
// presets is there, every name is unique within its engine, every parameter exists and is inside its range, and a sample of the
// new presets measures well (finite, not silent, peak below full scale, loudness inside the engine's existing window, the pitch
// where the note says unless the preset is listed in tools/engine-preset-skip.json). The measuring is tools/engine-preset-check.js.
// Nothing here changes the app: presets are data.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), fs = require('fs'), path = require('path');
const C = require('../tools/engine-preset-check.js');
const HTML = fs.readFileSync(path.join(__dirname, '..', 'index.html'), 'utf8');

/* Machine is partly a drum engine: another pass does it (not covered here). */
const ENGINES = C.engines().filter(k => k !== 'mach');

/* Existing loudness window (loudest 300 ms, dB, pad Tune +12 = C3), as measured on the presets that were there before the pass. */
const WINDOW = require('./engine-presets-windows.json');
const MARGIN = 4;

/* rows (min / max) of the engines that keep them outside XUI: parsed from the tables in index.html */
function rowsFrom(anchor) {
  const a = HTML.indexOf(anchor); assert.ok(a >= 0, anchor); const b = HTML.indexOf('\n};', a), rows = {};
  for (const m of HTML.slice(a, b).matchAll(/^\s{2}(\w+):\{[^\n]*?min:\s*(-?[\d.]+),\s*max:\s*(-?[\d.]+)/gm)) rows[m[1]] = { min: +m[2], max: +m[3] };
  return rows;
}
const rowsOf = k => k === 'bass' ? rowsFrom('const BU = {') : k === 'vox' ? rowsFrom('const VU = {') : k === 'blip' ? rowsFrom('const BLU = {') : C.E.XUI[k].rows;
/* keys a preset may carry that are not engine rows (the pad's own) */
const PAD_KEYS = new Set(['drive', 'level', 'tune']);
/* Bass keys that have their own controls (segmented pickers / the wobble rate), not rows of BU */
const BASS_EXTRA = new Set(['vDrive', 'wRate', 'wShape', 'legato', 'wRetrig']);

test('engine presets: every engine has its minimum of new presets and unique names', () => {
  for (const k of ENGINES) {
    const L = C.presetList(k), added = L.length - C.BASELINE[k];
    assert.ok(added >= C.NEW_MIN[k], k + ': ' + added + ' new presets, needs ' + C.NEW_MIN[k]);
    const names = L.map(p => p[0].toLowerCase());
    assert.equal(new Set(names).size, names.length, k + ': duplicate preset name ' + names.find((n, i) => names.indexOf(n) !== i));
    L.forEach(p => assert.ok(/^[\w][\w .'’+&()\-\/%#]*$/.test(p[0]) && p[0].length <= 28, k + ': odd preset name "' + p[0] + '"'));
  }
});

test('engine presets: every parameter exists and is inside its range', () => {
  const bad = [];
  for (const k of ENGINES) {
    const rows = rowsOf(k), L = C.presetList(k);
    for (let i = C.BASELINE[k]; i < L.length; i++) {
      for (const [key, v] of Object.entries(L[i][1])) {
        if (PAD_KEYS.has(key) || (k === 'bass' && BASS_EXTRA.has(key))) continue;
        const r = rows[key], tag = k + ' / ' + L[i][0] + ': ';
        if (!r) { bad.push(tag + 'unknown parameter ' + key); continue; }
        if (!(typeof v === 'number' && isFinite(v) && v >= r.min && v <= r.max)) bad.push(tag + key + '=' + v + ' outside ' + r.min + '..' + r.max);
        else if (r.c === 'int' && v !== Math.round(v)) bad.push(tag + key + ' must be a whole number');
      }
    }
  }
  assert.deepEqual(bad, []);
});

test('engine presets: a sample of the new presets measures well (finite, not silent, below full scale, in the loudness window, in pitch)', () => {
  const failures = [];
  for (const k of ENGINES) {
    const L = C.presetList(k), n = L.length - C.BASELINE[k], idx = [C.BASELINE[k], C.BASELINE[k] + (n >> 1)];
    if (k !== 'vox' && n > 4) idx.push(L.length - 1);
    const skip = C.PITCH_SKIP[k] || [], w = WINDOW[k];
    for (const i of idx) {
      const m = C.measure(k, i), a1 = m.at[1], tag = k + ' / ' + m.name + ': ';
      for (const a of m.at) {
        if (a.bad || !Number.isFinite(a.pk)) failures.push(tag + 'not finite');
        if (a.rmsDb < -60) failures.push(tag + 'silent (' + a.rmsDb.toFixed(1) + ' dB)');
        if (a.pk >= 1) failures.push(tag + 'peak ' + a.pk.toFixed(2));
      }
      if (w && (a1.loudDb < w[0] - MARGIN || a1.loudDb > w[1] + MARGIN)) failures.push(tag + 'loudness ' + a1.loudDb.toFixed(1) + ' dB outside ' + w[0] + '..' + w[1] + ' (+-' + MARGIN + ')');
      if (!skip.includes('*') && !skip.includes(m.name) && Math.min(m.at[0].pitchErr, a1.pitchErr) > 60) failures.push(tag + 'pitch off by ' + Math.min(m.at[0].pitchErr, a1.pitchErr).toFixed(0) + ' cents');
    }
  }
  assert.deepEqual(failures, []);
});
