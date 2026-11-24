// FX1: the golden for the song reader of pump and MODFX effect slots (core/src/fx/fxsong.cpp), made from the PAGE's own code:
//   node native/tools/fxsong-dump.mjs --write    writes native/tests/golden/fxsong_cases.json (committed)
//   node native/tools/fxsong-dump.mjs --check    exits 1 when the committed file differs from what index.html + the pinned pump give now (needs THUNDER_DEPS_DIR, as pump-table.mjs)
// MODFX: the page's own loop that makes TFX[k] for a track-effect MODFX (index.html, "The MODFX effects as track effects") is run on the MODFX spec, normTfx's rule
// (clamp(int(o[q], d), min, max)) gives the slot's bytes and TFX[k].map(f) the module's values, for the default slot and for stressed ones.
// pump: pumpNorm1 + tfxChain's `pv` (f.pump[p.s] ?? p.def, the value clamped by the page's own pumpClamp, PUMP_START under a fresh slot) over the pinned pump's parameter tables.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { pinMeta } from './pump-table.mjs';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const OUT = path.join(ROOT, 'native/tests/golden/fxsong_cases.json');
const h = fs.readFileSync(path.join(ROOT, 'index.html'), 'utf8').replace(/\r\n/g, '\n');
const slice = (a, b, from = 0) => { const i = h.indexOf(a, from), j = h.indexOf(b, i); if (i < 0 || j < 0) throw new Error('not in index.html: ' + a); return h.slice(i, j); };

export function cases() {
  // ---- MODFX
  const NT = 32, clamp = (v, a, b) => Math.min(b, Math.max(a, v)), int = (v, d) => Number.isFinite(+v) && v !== null && v !== '' ? Math.round(+v) : d;
  const u8 = v => v === 128 ? 0.5 : clamp(v, 0, 255) / 255;
  const P_ = (k, l, d, fmt, min = 0, max = 255) => ({ k, l, d, fmt, min, max }), EN = (k, l, d, names) => ({ k, l, d, min: 0, max: names.length - 1, names });
  const fxKey = p => p === 'type' || p === 'on' ? p + '_' : p;
  const MODFX = new Function(slice('const MODFX = {', '/* ===MODFX END=== */') + ';return MODFX;')();
  const TFX = {};
  const loop = slice('for (const k in MODFX){ const M = MODFX[k]; if (!M.track) continue;', '\n}\n', h.indexOf('/* The MODFX effects as track effects'));
  new Function('MODFX', 'TFX', 'P_', 'EN', 'fxKey', 'u8', 'clamp', 'NT', loop + '\n}')(MODFX, TFX, P_, EN, fxKey, u8, clamp, NT);
  const norm = (k, o) => { const f = { type: k, on: o.on !== false }; for (const p of TFX[k].ps) f[p.k] = clamp(int(o[p.k], p.d), p.min, p.max); return f; };
  const mod = [];
  for (const k of Object.keys(TFX)) {
    const ps = TFX[k].ps, sets = [{}, {}, {}, {}];
    ps.forEach((p, i) => { sets[1][p.k] = p.min; sets[2][p.k] = p.max; sets[3][p.k] = i % 3 === 0 ? (p.min + p.max) / 2 + 0.4 : i % 3 === 1 ? p.max + 100 : 'x'; });   // (the default; every control at its lowest, at its highest; a fraction, past the top, not a number)
    sets[3].on = false;
    sets.forEach((s, n) => { const f = norm(k, s), mp = TFX[k].map(f), q = ps.map(p => f[p.k]);
      const names = Object.keys(mp); if (names.length !== ps.length) throw new Error(k + ': mp / ps length');
      mod.push({ type: k, set: n, slot: Object.assign({ type: k }, s), on: f.on, bytes: q, mp: ps.map((p, i) => mp[names[i]]), names }); });
  }
  // ---- pump
  const M = pinMeta();
  const pumpClamp = new Function('Number', 'Math', 'PF_INT', 'PF_BOOL', slice('function pumpClamp(p, v){', '\n}\n') + '\n}\nreturn pumpClamp;')(Number, Math, 2, 4);
  const start = new Function('return ' + /const PUMP_START = (\{[^;\n]*\});/.exec(h)[1])();
  const pump = [];
  for (const t of M.types) {
    const P = M.fx[t].params, mk = f => { const out = {}; P.forEach(p => { const v = f(p, P.indexOf(p)); if (v !== undefined) out[p.s] = v; }); return out; };
    const sets = [mk(() => undefined), mk(p => p.min), mk(p => p.max), mk((p, i) => i % 4 === 0 ? (p.min + p.max) / 3 : i % 4 === 1 ? p.max + 1000 : i % 4 === 2 ? 'x' : i % 7 === 3 ? true : undefined)];
    sets.forEach((src, n) => {
      const fresh = {}; P.forEach(p => fresh[p.s] = p.def); Object.assign(fresh, start[t] || {});   // pumpFresh
      for (const p of P) if (src[p.s] !== undefined) fresh[p.s] = pumpClamp(p, src[p.s]);        // pumpNorm1
      pump.push({ type: t, set: n, slot: { type: 'p' + t, on: n !== 3, pump: src }, pv: P.map(p => fresh[p.s] ?? p.def) });   // tfxChain: f.pump[p.s] ?? p.def
    });
  }
  return { version: 1, mod, pump };
}
if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const t = JSON.stringify(cases()) + '\n', a = process.argv[2];
  if (a === '--write') { fs.writeFileSync(OUT, t); console.log('wrote ' + OUT + ' (' + t.length + ' bytes)'); }
  else if (a === '--check') { if (!fs.existsSync(OUT) || fs.readFileSync(OUT, 'utf8') !== t) { console.error('fxsong_cases.json differs from index.html / the pinned pump now: run node native/tools/fxsong-dump.mjs --write'); process.exit(1); } console.log('fxsong_cases.json is current'); }
  else process.stdout.write(t);
}
