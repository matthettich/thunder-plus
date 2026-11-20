// SOLO + EFFECT CHANGE + UN-SOLO (19 Nov 2026, owner's report: "I soloed a track and then changed FX and the whole song went quiet until I reset audio").
// Plays sounds/BETA-TESTING-ELECTRO.thunder (the owner's song) in headless Chrome on the chosen sequencer, solos a track, changes an effect (add / remove /
// retype / parameter / bypass, on the soloed track, on a silenced one, on the master), un-solos, and prints the master's level and every track's state
// (fx node rest flag, fader gain, delay) before / during / after. node tests/sweep/solo-fx.mjs [--seq=core|page] [--only=name] [--secs=4]
import fs from 'fs'; import path from 'path'; import { ROOT, sleep, serve, launch } from './cdp-lib.mjs';
const arg = (k, d) => { const a = process.argv.find(x => x.startsWith('--' + k + '=')); return a ? a.slice(k.length + 3) : d; };
const SEQ = arg('seq', 'core'), ONLY = arg('only', ''), SECS = +arg('secs', 4), SONG = arg('song', 'sounds/BETA-TESTING-ELECTRO.thunder'), HOLD = +arg('hold', 1.5);   // HOLD: seconds soloed before the edit (a silenced track's effects fall asleep after 10 s of rest)
const { srv, port } = await serve(); const C = await launch(); const { send } = C;
const { targetInfos } = await send('Target.getTargets'); const pt = targetInfos.find(t => t.type === 'page');
const { sessionId: S } = await send('Target.attachToTarget', { targetId: pt.targetId, flatten: true });
const P = (m, p) => send(m, p, S);
const ev = async e => { const r = await Promise.race([P('Runtime.evaluate', { expression: e, awaitPromise: true, returnByValue: true }), sleep(30000).then(() => { throw new Error('page did not answer in 30 s: ' + e.slice(0, 80)); })]); if (r.exceptionDetails) throw new Error(r.exceptionDetails.exception ? r.exceptionDetails.exception.description : r.exceptionDetails.text); return r.result.value; };
const app = src => ev(`window.__seqTest.ev(${JSON.stringify('(async () => {' + src + '\n})()')})`);
await P('Page.enable'); await P('Runtime.enable');
await P('Emulation.setDeviceMetricsOverride', { width: 1400, height: 900, deviceScaleFactor: 1, mobile: false });
const errs = []; P('Runtime.enable'); 
await P('Page.navigate', { url: `http://127.0.0.1:${port}/index.html?seqtest=1` });
for (let i = 0; i < 200; i++) { if (await ev('!!window.__seqTest').catch(() => false)) break; await sleep(300); }
await sleep(2500); await ev(`(()=>{const b=[...document.querySelectorAll('button')].find(x=>/^Got it/.test(x.textContent)); if(b) b.click(); return 1})()`); await sleep(800);
await ev(`window.__seqTest.load(${JSON.stringify(fs.readFileSync(path.join(ROOT, SONG), 'utf8'))}, 'x.thunder')`); await sleep(800);
console.log('seq on core:', await ev(`window.__seqTest.coreOn(${SEQ === 'core'})`));
await sleep(1500);
// helpers in the page
await app(`window.__sf = {
  an: [], buf: new Float32Array(2048),
  rms(a){ a.getFloatTimeDomainData(this.buf); let s = 0; for (const x of this.buf) s += x * x; return Math.sqrt(s / this.buf.length); },
  init(){ if (this.an.length) return; for (let t = 0; t < NT; t++){ const a = ctx.createAnalyser(); a.fftSize = 2048; rt.pl.buses[t].p.connect(a); this.an.push(a); } this.ma = ctx.createAnalyser(); this.ma.fftSize = 2048; master.connect(this.ma); },
  async level(ms){ this.init(); const n = Math.max(1, Math.round(ms / 100)), tr = new Array(NT).fill(0); let m = 0, pk = 0; for (let i = 0; i < n; i++){ await new Promise(r => setTimeout(r, 100)); const r = this.rms(this.ma); m += r * r; this.ma.getFloatTimeDomainData(this.buf); for (const x of this.buf) pk = Math.max(pk, Math.abs(x)); for (let t = 0; t < NT; t++){ const q = this.rms(this.an[t]); tr[t] += q * q; } }
    const db = x => x > 1e-12 ? +(10 * Math.log10(x)).toFixed(1) : -120; return {master: db(m / n), peak: +(20 * Math.log10(pk || 1e-9)).toFixed(1), tracks: tr.map(x => db(x / n))}; },
  state(){ return rt.pl.buses.slice(0, NT).map((b, t) => ({t, aud: audible(t), g: +b.g.gain.value.toFixed(3), cg: +b.cg.gain.value.toFixed(3), fx: !!b.fx, rest: b.fx ? b.fx._rest : null, chn: !!b.chn, dl: +b.dl.delayTime.value.toFixed(4), fdl: b.fdl ? +b.fdl.delayTime.value.toFixed(4) : null})); }
}; return 1;`);
const lv = (ms = 1500) => app(`return await window.__sf.level(${ms})`);
const st = () => app('return window.__sf.state()');
const fmt = L => `master ${L.master} dB (peak ${L.peak})  tracks ${L.tracks.map((x, t) => x > -100 ? t + ':' + x : '').filter(Boolean).join(' ')}`;
const clockOk = async () => { const a = await app('return ctx.currentTime'); await sleep(400); const b = await app('return ctx.currentTime'); return b - a > 0.1; };
const play = async () => { await app(`tStart('song', {row:0}); return tp.on`); await sleep(SECS * 1000); };
const stop = async () => { await app(`tStop(true); return 1`); await sleep(700); };
console.log('tp.core after PLAY:', await app(`tStart('song', {row:0}); const c = !!tp.core; tStop(true); return c;`)); await sleep(800);
const results = {};
async function scenario(name, fn, stopped){
  if (ONLY && !(arg('exact', '') ? name === ONLY : name.includes(ONLY))) return;
  await app(`S.mix.forEach(m => { m.solo = false; m.mute = false; }); mixChanged(); return 1`);
  await play(); const base = await lv(); console.log(`\n== ${name}\n  base      : ${fmt(base)}`);
  await fn({ lv, st, app, sleep, fmt, play, stop });
  await sleep(300); await stop(); await play(); const after = await lv();
  console.log(`  after STOP+PLAY: ${fmt(after)}`);
  const rows = await st(); const stuck = rows.filter(r => r.fx && r.rest === true).map(r => r.t);
  console.log(`  flags: rest-stuck tracks ${JSON.stringify(stuck)}  gains<1 w/o mute/solo: ${JSON.stringify(rows.filter(r => r.g < 0.01 && r.aud).map(r => r.t))}`);
  results[name] = { base: base.master, after: after.master, drop: +(after.master - base.master).toFixed(1) };
  await stop();
}
const SOLO = t => app(`S.mix.forEach((m, i) => m.solo = i === ${t}); mixChanged(); return 1`);
const UNSOLO = () => app(`S.mix.forEach(m => m.solo = false); mixChanged(); return 1`);
const FXEDIT = (t, js) => app(`const t = ${t}; ${js}; fxChanged(t); return 1`);
// each: [name, soloed track, edited track, the edit]
const ADD = type => `const A = t >= NT ? S.mfx : S.tfx[t]; const k = A.findIndex(f => !f); fxSwap(t, k, A, '${type}')`;
const RETYPE = type => `const A = t >= NT ? S.mfx : S.tfx[t]; const k = A.findIndex(f => f); fxSwap(t, k, A, '${type}')`;
const REMOVE = `const A = t >= NT ? S.mfx : S.tfx[t]; const k = A.findIndex(f => f); fxSwap(t, k, A, null)`;
const BYPASS = `const A = t >= NT ? S.mfx : S.tfx[t]; const f = A.find(f => f); f.on = !f.on`;
const PARAM = `const A = t >= NT ? S.mfx : S.tfx[t]; const f = A.find(f => f && !TFX[f.type].pump && TFX[f.type].ps.length); const p = TFX[f.type].ps[0]; f[p.k] = f[p.k] === p.max ? p.min : p.max`;
// each: [name, soloed track, edited track (32 = the master), the edit]
const REMOVE_ALL = `const A = t >= NT ? S.mfx : S.tfx[t]; for (let k = 0; k < A.length; k++) if (A[k]) fxSwap(t, k, A, null)`;
const EDITS = [
  ['add on the soloed track (0)', 0, 0, ADD('delay')],
  ['add on a silenced track (9)', 0, 9, ADD('delay')],
  ['remove on a silenced track (9)', 0, 9, REMOVE],
  ['retype on a silenced track (9)', 0, 9, RETYPE('delay')],
  ['bypass on a silenced track (9)', 0, 9, BYPASS],
  ['param on a silenced track (9)', 0, 9, PARAM],
  ['master: bypass', 0, 32, BYPASS],
  ['master: add', 0, 32, ADD('delay')],
  ['bus 7: bypass', 0, 7, BYPASS],
  ['solo the bus (7), edit the bus', 7, 7, BYPASS],
  ['solo the bus (7), edit a source (9)', 7, 9, BYPASS],
  ['STOPPED: add on a silenced track (9)', 0, 9, ADD('delay'), true],
  ['STOPPED: remove on a silenced track (9)', 0, 9, REMOVE, true],
  ['STOPPED: bypass on a silenced track (9)', 0, 9, BYPASS, true],
  ['STOPPED: master bypass', 0, 32, BYPASS, true],
  ['STOPPED: add on the soloed track (0)', 0, 0, ADD('delay'), true],
  ['bus 7: add', 1, 7, ADD('delay')],
  ['bus 7: remove all', 1, 7, REMOVE_ALL],
  ['bus 7: retype first', 1, 7, RETYPE('room')],
  ['bus 7: param', 1, 7, PARAM],
  ['bus 7: remove one', 0, 7, REMOVE],
  // the soloed track is the one edited, on tracks that have effects and (1, 8, 9, 10) run through chains or feed a bus
  ['solo 1 (chain + 2 fx), remove all fx', 1, 1, REMOVE_ALL],
  ['solo 1, retype', 1, 1, RETYPE('delay')],
  ['solo 1, bypass', 1, 1, BYPASS],
  ['solo 8 (fx), remove all fx', 8, 8, REMOVE_ALL],
  ['solo 8, retype', 8, 8, RETYPE('room')],
  ['solo 9, remove all fx', 9, 9, REMOVE_ALL],
  ['solo 10, remove all fx', 10, 10, REMOVE_ALL],
  ['solo 10, param', 10, 10, PARAM],
  ['solo 1, edit 10 (silenced), remove all', 1, 10, REMOVE_ALL],
  ['solo 1, edit 7 (the bus), remove all', 1, 7, REMOVE_ALL],
  ['solo 1, edit master, remove all', 1, 32, REMOVE_ALL],
];
for (const [name, solo, tr, js, stopped] of EDITS) await scenario(name, async X => {
  // stopped: the solo, the edit and the un-solo all happen with the song stopped (the usual way to edit effects), then PLAY
  if (stopped) await stop();
  await SOLO(solo); await sleep(HOLD * 1000);
  if (!stopped) console.log('  soloed    :', fmt(await lv()), (await clockOk()) ? '' : '  [AUDIO CLOCK STOPPED]');
  await FXEDIT(tr, js); await sleep(1500);
  if (!stopped) console.log('  after edit:', fmt(await lv()), (await clockOk()) ? '' : '  [AUDIO CLOCK STOPPED]');
  await UNSOLO(); await sleep(1500);
  if (stopped) await play();
  console.log('  un-soloed :', fmt(await lv()), (await clockOk()) ? '' : '  [AUDIO CLOCK STOPPED]');
  const rows = await st(); console.log('  rest flags:', JSON.stringify(rows.filter(r => r.fx).map(r => r.t + ':' + r.rest)));
}, stopped);
console.log('\nSUMMARY', JSON.stringify(results, null, 1));
await C.close(); srv.close(); process.exit(0);
