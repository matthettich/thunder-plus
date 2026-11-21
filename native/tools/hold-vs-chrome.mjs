// D7 proof: the page's cancelAndHoldAtTime (Chrome's real AudioParam, an OfflineAudioContext) against the emulation the decision-log tool uses (d7-hold.mjs applied to the core's Auto, core/seq-core.js).
// Random lanes of setValueAtTime / linearRampToValueAtTime events and a hold time (before, inside, at the start, at the end of a ramp, at a set, after everything), then the page's next note (a set at the hold time):
// every sample of the lane must agree (float32 tolerance). Needs Chrome (google-chrome) and playwright; run by hand: node native/tools/hold-vs-chrome.mjs [cases]
import fs from 'node:fs';
import path from 'node:path';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';
import { D7_HOLD } from './d7-hold.mjs';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const src = fs.readFileSync(path.join(ROOT, 'core/seq-core.js'), 'utf8');
const a0 = src.indexOf('class Auto {'); let d = 0, a1 = a0;
for (let i = src.indexOf('{', a0); i < src.length; i++) { if (src[i] === '{') d++; else if (src[i] === '}') { d--; if (d === 0) { a1 = i + 1; break; } } }
const Auto = new Function(src.slice(a0, a1) + (process.env.HOLD === 'core' ? '' : D7_HOLD) + '\nreturn Auto;')();   // (HOLD=core: the core's own Auto.hold, to see the tool catch the fault D7 corrects)

const SR = 32768, LEN = 6554,  // (a power of two: every T / SR is exact, so that a hold exactly AT an event is a real tie and not float noise)
   CASES = Number(process.argv[2]) || 3000;
let seed = 12345; const rnd = () => { seed = (Math.imul(seed, 1664525) + 1013904223) >>> 0; return seed / 4294967296; };
const grid = x => Math.round(x * SR) / SR, half = x => (Math.floor(x * SR) + 0.5) / SR;   // (half: between two samples, exact too)
const WHOLE = process.argv[3] !== 'frac';   // the live route books on whole samples (sample_at_or_after); 'frac' also tries times between samples
const cases = [];
for (let c = 0; c < CASES; c++) {
  const n = 1 + Math.floor(rnd() * 5), ev = []; let t = rnd() * 0.02;
  for (let i = 0; i < n; i++) { t += 0.002 + rnd() * 0.03;   /* (strictly increasing times: two events at one time are ordered by rules of their own, not what D7 is about) */ ev.push({ k: rnd() < 0.5 ? 'set' : 'lin', t: WHOLE || rnd() < 0.5 ? grid(t) : half(t), v: Math.round((rnd() * 4 - 1) * 100) / 100 }); }
  if (ev[0].k === 'lin' && rnd() < 0.5) ev[0].k = 'set';
  let ht; const m = rnd(), pick = ev[Math.floor(rnd() * ev.length)];
  if (m < 0.2) ht = pick.t;                                       // exactly at an event (a set, a ramp's end)
  else if (m < 0.3) ht = ev[0].t * rnd();                         // before the first event
  else if (m < 0.4) ht = ev[ev.length - 1].t + 0.001 + rnd() * 0.02;   // after everything
  else if (m < 0.5) ht = pick.t + 1 / SR;                         // the next sample after an event
  else if (m < 0.6) ht = Math.max(0, pick.t - 1 / SR);            // the sample before
  else { const i = Math.floor(rnd() * ev.length), a = i ? ev[i - 1].t : 0; ht = a + (ev[i].t - a) * rnd(); }   // between two events (inside a ramp when the next is one)
  ht = WHOLE || rnd() < 0.6 ? grid(ht) : ht;
  cases.push({ ev, ht, v2: Math.round((rnd() * 4 - 1) * 100) / 100 });
}

function emu(cs) {
  const A = new Auto(0);
  for (const e of cs.ev) { if (e.k === 'set') A.set(e.v, e.t); else A.lin(e.v, e.t); }
  A.hold(cs.ht); A.set(cs.v2, cs.ht);   // (the page's next note sets its rate at the hold time)
  const o = new Float32Array(LEN); for (let i = 0; i < LEN; i++) o[i] = A.at(i / SR); return o;
}

const require = createRequire('/opt/npm-tools/node_modules/');
const { chromium } = require('playwright');
const browser = await chromium.launch({ executablePath: '/usr/bin/google-chrome', headless: true, args: ['--no-sandbox', '--autoplay-policy=no-user-gesture-required'] });
const page = await browser.newPage(); await page.goto('about:blank');
const got = await page.evaluate(async ({ cases, SR, LEN }) => {
  const out = [];
  for (const cs of cases) {
    const ctx = new OfflineAudioContext(1, LEN, SR), s = ctx.createConstantSource(); s.offset.value = 0; s.connect(ctx.destination);
    const P = s.offset; P.setValueAtTime(0, 0);
    for (const e of cs.ev) { if (e.k === 'set') P.setValueAtTime(e.v, e.t); else P.linearRampToValueAtTime(e.v, e.t); }
    let err = null; try { P.cancelAndHoldAtTime(cs.ht); } catch (x) { err = String(x); P.cancelScheduledValues(cs.ht); }
    P.setValueAtTime(cs.v2, cs.ht); s.start(0);
    const b = await ctx.startRendering(); out.push({ err, d: Array.from(b.getChannelData(0)) });
  }
  return out;
}, { cases, SR, LEN });
await browser.close();

let bad = 0, errs = 0, worst = 0; const kinds = {};
for (let c = 0; c < cases.length; c++) {
  if (got[c].err) errs++;
  const e = emu(cases[c]); let mx = 0, at = -1;
  for (let i = 0; i < LEN; i++) { const dlt = Math.abs(e[i] - got[c].d[i]); if (dlt > mx) { mx = dlt; at = i; } }
  worst = Math.max(worst, mx);
  if (mx > 2e-5) { bad++; if (bad <= 5) console.log('DIFF case', c, JSON.stringify(cases[c]), 'max', mx, 'at sample', at, 'emu', e[at], 'chrome', got[c].d[at]); }
}
console.log(`hold-vs-chrome: ${cases.length} lanes, ${bad} differ, ${errs} cancelAndHoldAtTime errors, worst abs difference ${worst.toExponential(2)}`);
process.exit(bad || errs ? 1 : 0);
