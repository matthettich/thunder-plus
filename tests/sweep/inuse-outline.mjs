// IN USE OUTLINE (build 654): the Synth page's pads outlined when a note on the song plays them. Opt-in (not part of `npm test`):
//   node tests/sweep/inuse-outline.mjs [--shots=1]      (--shots=1 also saves screenshots to tests/sweep/out/)
// Loads each shipped song, opens the Synth page, and checks: every pad's class against an independent count of the song's notes
// (read from the song file, not from the app's own scan), a known used and a known free pad, no change in any pad's size with the
// outline, the legend, and (--shots) the look on desktop and a folded Z Fold 6 in a light, a dark and the IDM skin.
import fs from 'fs'; import path from 'path';
import { ROOT, sleep, serve, launch } from './cdp-lib.mjs';
const arg = (k, d) => { const a = process.argv.find(x => x.startsWith('--' + k + '=')); return a ? a.slice(k.length + 3) : d; };
const SHOTS = arg('shots', '0') === '1', OUT = path.join(ROOT, 'tests/sweep/out'); fs.mkdirSync(OUT, { recursive: true });
const { srv, port } = await serve(); const C = await launch(); const { send } = C;
const { targetInfos } = await send('Target.getTargets'); const pt = targetInfos.find(t => t.type === 'page');
const { sessionId: S } = await send('Target.attachToTarget', { targetId: pt.targetId, flatten: true });
const P = (m, p) => send(m, p, S);
const ev = async expr => { const r = await P('Runtime.evaluate', { expression: expr, awaitPromise: true, returnByValue: true }); if (r.exceptionDetails) throw new Error(JSON.stringify(r.exceptionDetails).slice(0, 400)); return r.result.value; };
const app = src => ev(`window.__seqTest.ev(${JSON.stringify('(async () => {' + src + '\n})()')})`);
let fails = 0; const ok = (c, m) => { console.log((c ? 'PASS ' : 'FAIL ') + m); if (!c) fails++; };
await P('Page.enable'); await P('Runtime.enable');
const shot = async name => { if (!SHOTS) return; const r = await P('Page.captureScreenshot', { format: 'png' }); fs.writeFileSync(path.join(OUT, `inuse-${name}.png`), Buffer.from(r.data, 'base64')); };
await P('Emulation.setDeviceMetricsOverride', { width: 1440, height: 900, deviceScaleFactor: 1, mobile: false });
await P('Page.navigate', { url: `http://localhost:${port}/index.html?seqtest=1` });
for (let i = 0; i < 200; i++) { if (await ev('!!window.__seqTest').catch(() => false)) break; await sleep(300); }
await sleep(2500);
await ev(`(()=>{const b=[...document.querySelectorAll('button')].find(x=>/^Got it/.test(x.textContent)); if(b) b.click(); return 1})()`); await sleep(500);
for (const name of ['ELECTRO', 'BETA-TESTING-ELECTRO']) {
  const text = fs.readFileSync(path.join(ROOT, `sounds/${name}.thunder`), 'utf8'), j = JSON.parse(text), s = j.song;
  await ev(`window.__seqTest.load(${JSON.stringify(text)}, '${name}.thunder')`); await sleep(600);
  // independent count: which pads do notes in phrases on the song reach? (the song file's own arrays, no app code)
  const NCOL = 4, IC = 5, FX0 = 6, RW = 12, onP = new Set(); for (const row of s.song) for (let t = 0; t < 32; t++) { const v = row[t]; if (v >= 0 && s.chains[v]) for (const q of s.chains[v]) if (q && q[0] >= 0) onP.add(q[0]); }
  const on = new Set(), off = new Set();
  s.phrases.forEach((p, id) => { if (!p) return; let carry = 0; const L = Math.min((s.plen && s.plen[id]) || s.dlen || 16, p.length);
    for (let y = 0; y < L; y++) { const r = p[y]; if (!r) continue; let n = 0; for (let c = 0; c < NCOL; c++) if (r[c] >= 0 && r[c] < 120) n++; if (!n) continue;
      let ii = r[IC] >= 0 ? r[IC] : carry; for (let m = FX0; m < RW; m += 2) if (r[m] === 7 && r[m + 1] >= 0) ii = r[m + 1]; carry = ii; const I = s.insts[ii] || s.insts[0]; (onP.has(id) ? on : off).add(I.pad); } });
  await app(`setView('synth'); return 1;`); await sleep(500);
  const st = await app(`const o = []; padEls.forEach((e, i) => o.push(e.p.classList.contains('uOn') ? 1 : e.p.classList.contains('uOff') ? 2 : 0)); return {o, fxc: FXC.indexOf('INS'), vtrk};`);
  ok(st.fxc === 7, `${name}: INS is command 7 (the independent count assumes it)`);
  if (st.vtrk) { console.log(`SKIP ${name}: a kit slot per track is on`); continue; }
  let bad = []; st.o.forEach((v, i) => { const want = on.has(i) ? 1 : off.has(i) ? 2 : 0; if (v !== want) bad.push(`${i}:${v}/${want}`); });
  ok(!bad.length, `${name}: all ${st.o.length} pads match the file's own notes (${st.o.filter(v => v === 1).length} solid, ${st.o.filter(v => v === 2).length} dotted, ${st.o.filter(v => v === 0).length} free)` + (bad.length ? ' bad: ' + bad.join(' ') : ''));
  const used = st.o.indexOf(1), free = st.o.indexOf(0);
  ok(used >= 0 && free >= 0, `${name}: a known used pad (${used}) and a known free pad (${free}) exist`);
  const geo = await app(`const o = {}; for (const i of [${used}, ${free}]) { const e = padEls[i].p, r = e.getBoundingClientRect(), cs = getComputedStyle(e); o[i] = {w: r.width, h: r.height, out: cs.outlineStyle, ow: cs.outlineWidth, bw: cs.borderTopWidth}; } return o;`);
  ok(geo[used].out === 'solid' && geo[free].out === 'none', `${name}: used pad has a solid outline (${geo[used].ow}), the free one none`);
  const same = await app(`const e = padEls[${used}].p, a = e.getBoundingClientRect(); e.classList.remove('uOn'); const b = e.getBoundingClientRect(); e.classList.add('uOn'); return a.width === b.width && a.height === b.height && a.left === b.left && a.top === b.top;`);
  ok(same, `${name}: the outline moves nothing`);
  const leg = await app(`return !$('padKey').hidden && $('padKey').getBoundingClientRect().width > 0;`); ok(leg, `${name}: the legend shows`);
  const txt = await app(`select(${used}); refreshAll(); return {v: $('vUsed').textContent, w: $('wMeta').textContent};`);
  ok(/used by instrument/.test(txt.v) && /notes? on the song/.test(txt.v) && /in use: inst/.test(txt.w), `${name}: the info line names instruments and notes: "${txt.v}" / "${txt.w}"`);
  const cost = await app(`const t = performance.now(); padUseC = null; padUseMap(); const a = performance.now() - t; const t2 = performance.now(); for (let k = 0; k < 1000; k++) padUseMap(); return {scan: a, hit: (performance.now() - t2) / 1000, phr: S.phrases.filter(Boolean).length};`);
  console.log(`NOTE ${name}: scan ${cost.scan.toFixed(2)} ms (${cost.phr} phrases), cached call ${(cost.hit * 1000).toFixed(1)} us`);
  if (name === 'BETA-TESTING-ELECTRO') {
    // an edit: put a note on the free pad's instrument into a phrase on the song, then take it away again
    const ed = await app(`const I = S.insts.findIndex(x => x.pad === ${free}); const pid = S.chains[S.song.flat().find(v => v >= 0)][0][0]; const r = S.phrases[pid][S.phrases[pid].length - 1]; const keep = r.slice();
      if (I < 0) return {none: true}; r[0] = 48; r[5] = I; saveS(); renderPads(); const a = padEls[${free}].p.className; r.splice(0, r.length, ...keep); saveS(); renderPads(); const b = padEls[${free}].p.className; return {I, a, b};`);
    if (!ed.none) ok(/uOn/.test(ed.a) && !/uOn|uOff/.test(ed.b), `${name}: a note on the free pad's instrument outlines it at once, and deleting it takes the outline away`);
    else console.log('SKIP edit: no instrument on the free pad');
  }
  if (SHOTS) {
    for (const [tag, w, h, mob] of [['desk', 1440, 900, false], ['fold', 412, 915, true]]) {
      await P('Emulation.setDeviceMetricsOverride', { width: w, height: h, deviceScaleFactor: mob ? 2.5 : 1, mobile: mob });
      for (const skin of ['light', 'idm', 'matrix']) { await app(`applySkin('${skin}'); setView('synth'); select(${used}); refreshAll(); return 1;`); await sleep(400); await shot(`${name}-${tag}-${skin}-sel`);
        await app(`select(${free}); refreshAll(); return 1;`); await sleep(300); await shot(`${name}-${tag}-${skin}-free`); }
    }
    await P('Emulation.setDeviceMetricsOverride', { width: 1440, height: 900, deviceScaleFactor: 1, mobile: false });
  }
}
console.log(fails ? `${fails} FAIL` : 'all PASS'); await C.close(); srv.close(); process.exit(fails ? 1 : 0);
