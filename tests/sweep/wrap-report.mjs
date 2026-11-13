// Turns the raw files of wrap-clock.mjs (tests/sweep/out/wrap-*.json) into the per-pass tables of docs/audits/2026-11-12-wrap-clock.md.
//   node tests/sweep/wrap-report.mjs <raw.json> [...] [--cpu=1,3] [--top=14] [--json]
// Segments (all in wall-clock ms, performance.now()): "pass k" = from one wrap + 1 s to the next wrap - 1 s (pass 1 starts 2 s after PLAY),
// "wrap k" = the wrap's due time (the audio time of the first step of the new pass, mapped to wall time through the clock samples) +- 1 s.
import fs from 'fs'; import path from 'path';
const files = process.argv.slice(2).filter(a => !a.startsWith('--')); const opt = k => { const a = process.argv.find(x => x.startsWith('--' + k + '=')); return a ? a.slice(k.length + 3) : null; };
const CPU = (opt('cpu') || 'w1,w3').split(','), TOP = +(opt('top') || 14), JSONOUT = process.argv.includes('--json');
const q = (A, f) => { if (!A.length) return null; const B = [...A].sort((x, y) => x - y); return B[Math.min(B.length - 1, Math.floor(f * B.length))]; };
const f1 = v => v == null || !isFinite(v) ? '-' : Math.abs(v) >= 100 ? Math.round(v) + '' : Math.abs(v) >= 10 ? v.toFixed(1) : v.toFixed(2);

export function analyse(R) {
  const clk = R.clk, rows = R.rows;
  // audio time -> wall time (ms): first clock sample at or after it, interpolated
  const a2w = a => { let lo = 0, hi = clk.length - 1; if (!clk.length || a > clk[hi][1]) return null; while (lo < hi) { const m = (lo + hi) >> 1; if (clk[m][1] >= a) hi = m; else lo = m + 1; }
    if (lo === 0) return clk[0][0]; const c0 = clk[lo - 1], c1 = clk[lo]; return c1[1] === c0[1] ? c1[0] : c0[0] + (a - c0[1]) / (c1[1] - c0[1]) * (c1[0] - c0[0]); };
  // row boundaries and wraps from the rows the sequencer stepped
  const bounds = []; let last = -1, lastDue = -1;
  for (const r of rows) { const [pn, ct, due, row] = r; if (row < 0) continue; if (last >= 0 && row !== last) bounds.push({ due, row, from: last, wrap: row < last, push: pn, lead: due - ct }); last = row; lastDue = due; }
  const wraps = bounds.filter(b => b.wrap).map(b => ({ ...b, wall: a2w(b.due) })).filter(b => b.wall != null);
  const startWall = rows.length ? (a2w(rows[0][2]) ?? rows[0][0]) + 3000 : 0, endWall = clk.length ? clk[clk.length - 1][0] : 0;
  const segs = []; let from = startWall;
  wraps.forEach((w, k) => { segs.push({ name: `pass ${k + 1}`, a: from, b: w.wall - 1000, pass: k + 1 }); segs.push({ name: `wrap ${k + 1}`, a: w.wall - 1000, b: w.wall + 1000, wrap: k + 1 }); from = w.wall + 1000; });
  segs.push({ name: `pass ${wraps.length + 1} (partial)`, a: from, b: endWall, pass: wraps.length + 1 });
  const inRange = (t, s) => t >= s.a && t < s.b;
  const M = R.metrics, cm = R.metrics.map(m => ({ pn: m.a.pn, ...m }));
  const out = segs.map(s => {
    const o = { name: s.name, secs: (s.b - s.a) / 1000, a: s.a, b: s.b };
    // events the page player books (not the core's own notes)
    const evn = R.evN, coreTakeCode = evn['coreTake'];
    const E = R.ev.filter(e => inRange(e[0], s)), page = E.filter(e => e[1] !== coreTakeCode && e[2] != null), take = E.filter(e => e[1] === coreTakeCode && e[2] != null);
    const L = page.map(e => e[2] * 1000), Lt = take.map(e => e[2] * 1000);
    o.pageEv = L.length; o.pageLate = L.filter(v => v < 0).length; o.pageMin = L.length ? Math.min(...L) : null; o.pageP01 = q(L, .01); o.pageMed = q(L, .5);
    o.takeEv = Lt.length; o.takeMin = Lt.length ? Math.min(...Lt) : null; o.takeLate = Lt.filter(v => v < 0).length;
    o.evCostMax = E.length ? Math.max(...E.map(e => e[3])) : null; o.evCostSum = E.reduce((a, e) => a + e[3], 0);
    const first = E.filter(e => e[4] === 1); o.firstNotes = first.length; o.firstCost = first.reduce((a, e) => a + e[3], 0);
    // rows' lead at the page when they arrive (due - clock)
    const RW = rows.filter(r => inRange(r[0], s)).map(r => (r[2] - r[1]) * 1000); o.rowLeadMin = RW.length ? Math.min(...RW) : null; o.rowLeadMed = q(RW, .5);
    // core's own counters (coreStats every 500 ms)
    const C = M.filter(m => m.cs && inRange(m.a.pn, s)); o.coreLate = C.reduce((a, m) => a + m.cs.late, 0); o.coreN = C.reduce((a, m) => a + m.cs.n, 0); o.coreWorst = C.length ? Math.max(...C.map(m => m.cs.worst)) * 1000 : null;
    const lead = C.map(m => m.cs.lead).filter(v => v != null); o.coreLeadMin = lead.length ? Math.min(...lead) * 1000 : null; o.coreMsBlk = C.length ? C.reduce((a, m) => a + m.cs.ms, 0) / Math.max(1, C.reduce((a, m) => a + m.cs.blocks, 0)) : null;
    const cmx = C.length ? C.map(m => m.cs.ms / Math.max(1, m.cs.blocks)) : []; o.coreMsBlkMax = cmx.length ? Math.max(...cmx) : null;
    // long tasks and long animation frames
    const LT = R.lt.filter(l => inRange(l[0], s)); o.lt = LT.length; o.ltMs = LT.reduce((a, l) => a + l[1], 0); o.ltMax = LT.length ? Math.max(...LT.map(l => l[1])) : 0;
    const LF = R.loaf.filter(l => inRange(l.t, s)); o.loaf = LF.length; o.loafMax = LF.length ? Math.max(...LF.map(l => l.d)) : 0;
    // the audio clock against the wall clock, 100 ms samples
    const K = clk.filter(c => inRange(c[0], s)); let maxE = 0, big = 0, gapMax = 0, ol = [], bl = []; const eAbs = [];
    for (let i = 1; i < K.length; i++) { const dc = (K[i][1] - K[i - 1][1]) * 1000, dw = K[i][0] - K[i - 1][0], e = dc - dw; gapMax = Math.max(gapMax, dw); eAbs.push(Math.abs(e)); if (Math.abs(e) > maxE) maxE = Math.abs(e); if (Math.abs(e) > 30) big++; }
    for (const c of K) { if (c[2] != null) ol.push(c[2] * 1000); if (c[3] != null) bl.push(c[3] * 1000); }
    o.clkN = K.length; o.rate = K.length > 1 ? (K[K.length - 1][1] - K[0][1]) / ((K[K.length - 1][0] - K[0][0]) / 1000) : null; o.clkMaxStep = maxE; o.clkBig = big; o.sampGapMax = gapMax;
    // 1 s windows' rate extremes
    let rmin = 9, rmax = 0; for (let i = 0; i < K.length; i++) { let j = i + 1; while (j < K.length && K[j][0] - K[i][0] < 1000) j++; if (j < K.length) { const r = (K[j][1] - K[i][1]) / ((K[j][0] - K[i][0]) / 1000); rmin = Math.min(rmin, r); rmax = Math.max(rmax, r); } }
    o.rate1min = rmin === 9 ? null : rmin; o.rate1max = rmax || null; o.olMin = ol.length ? Math.min(...ol) : null; o.olMax = ol.length ? Math.max(...ol) : null; o.blMax = bl.length ? Math.max(...bl) : null;
    // offset ct - pn: how far it wandered inside the segment (the band the phone resync watches)
    const OF = K.map(c => (c[1] * 1000 - c[0])); if (OF.length) { const m = OF.reduce((a, b) => a + b, 0) / OF.length; o.offRange = Math.max(...OF) - Math.min(...OF); }
    // playback counters: deltas over the segment (500 ms samples)
    // (13 Nov report fix) an increment between two samples belongs to the segment holding the LATER sample: the baseline is the last sample before the segment
    const pre = M.filter(m => m.a.pn < s.a).slice(-1).map(m => m.a), A = pre.concat(M.filter(m => inRange(m.a.pn, s)).map(m => m.a)), d = k => A.length > 1 && A[0][k] != null && A[A.length - 1][k] != null ? A[A.length - 1][k] - A[0][k] : null;
    o.underruns = d('psEv'); o.underrunMs = d('psDur') != null ? d('psDur') * 1000 : null; o.glitch = d('glitch');
    o.pgGap = A.length ? Math.max(...A.map(a => a.pg ? a.pg.gap : 0)) : null; o.pgRe = A.length > 1 && A[0].pg && A[A.length - 1].pg ? A[A.length - 1].pg.re - A[0].pg.re : null; o.pgRs = A.length > 1 && A[0].pg && A[A.length - 1].pg ? A[A.length - 1].pg.rs - A[0].pg.rs : null;
    o.pbLateMax = null; const PBr = R.pb.filter(p => inRange(p[0], s)); o.pbDrop = PBr.reduce((a, p) => a + p[3], 0); o.pbLateMax = PBr.length ? Math.max(...PBr.map(p => p[2])) * 1000 : null; o.pbAheadMin = null;
    o.skips = d('skips'); o.raf = d('raf') != null && A.length > 1 ? d('raf') / ((A[A.length - 1].pn - A[0].pn) / 1000) : null;
    // the page's own functions over 1.5 ms
    const SL = R.slow.filter(x => inRange(x[0], s)), by = {}; for (const x of SL) { const b = by[x[1]] || (by[x[1]] = { n: 0, ms: 0, max: 0 }); b.n++; b.ms += x[2]; if (x[2] > b.max) b.max = x[2]; } o.slow = by;
    { const Mm = M.filter(m => inRange(m.a.pn, s)); if (Mm.length > 1 && Mm[0].chromeCpu != null) { const a = Mm[0], b = Mm[Mm.length - 1], dw = b.wallS - a.wallS; if (a.aCpu != null && b.aCpu != null) { o.audioPct = (b.aCpu - a.aCpu) / dw * 100; o.mainCpuPct = (b.mCpu - a.mCpu) / dw * 100; } o.machBusy = Mm.reduce((x, m) => x + m.busy, 0) / Mm.length; o.chromePct = (b.chromeCpu - a.chromeCpu) / dw / (R.machine.cpus || 2) * 100; o.otherPct = Math.max(0, o.machBusy - o.chromePct); } }
    const mmS = M.filter(m => m.a.pn < s.a).slice(-1).concat(M.filter(m => inRange(m.a.pn, s))); if (mmS.length > 1) { const a = mmS[0].m, b = mmS[mmS.length - 1].m, el = (mmS[mmS.length - 1].a.pn - mmS[0].a.pn) / 1000;   // (13 Nov report fix) divided by the time between the two metric reads, not the segment's length
      o.taskPct = (b.task - a.task) / el * 100; o.scriptPct = (b.script - a.script) / el * 100; o.layoutPct = (b.layout - a.layout) / el * 100; o.stylePct = (b.style - a.style) / el * 100; o.heapMB = b.heap / 1e6; }
    return o;
  });
  // row boundaries: the window [due - 200 ms, due + 600 ms] of every boundary, by pass (first-time patterns are the boundaries of pass 1)
  const bw = [], passOf = w => { let k = 1; for (const x of wraps) if (w >= x.wall) k++; return k; };
  for (const b of bounds) { const w = a2w(b.due); if (w == null) continue; const E = R.ev.filter(e => e[0] >= w - 200 && e[0] < w + 600 && e[1] !== R.evN['coreTake'] && e[2] != null), LT = R.lt.filter(l => l[0] < w + 600 && l[0] + l[1] > w - 200);
    bw.push({ wrap: b.wrap, pass: passOf(w - 1), row: b.row, from: b.from, pageMin: E.length ? Math.min(...E.map(e => e[2])) * 1000 : null, pageLate: E.filter(e => e[2] < 0).length, ltMax: LT.length ? Math.max(...LT.map(l => l[1])) : 0, evCostMax: E.length ? Math.max(...E.map(e => e[3])) : 0, firstN: E.filter(e => e[4] === 1).length, leadAtPage: (b.lead) * 1000 }); }
  return { tag: R.tag, args: R.args, env: R.env, wraps: wraps.map(w => ({ due: w.due, wall: w.wall, from: w.from, row: w.row })), segs: out, bw };
}

function cpuWindows(R, A) {   // --cpu=w1,w3 (the 2 s around wrap 1 and 3) or p1,p3 (the whole of pass 1 and pass 3, per second) or any pair
  const res = {};
  for (const which of ['mainProf', 'wkProf']) { const P = R[which]; if (!P) continue; res[which] = {};
    for (const k of CPU) { const kind = k[0], n = +k.slice(1); let sg; if (kind === 't') { const p0 = +((R.log.find(l => /PLAY song/.test(l)) || '0 ').split(' ')[0]) * 1000, [x, y] = k.slice(1).split('-').map(Number); sg = { a: p0 + x * 1000, b: p0 + y * 1000 }; } else sg = A.segs.find(x => x.name === (kind === 'w' ? 'wrap ' : 'pass ') + n); if (!sg) continue;
      const a = sg.a, b = sg.b; const self = {}, incl = {}; let tot = 0; const secs = (b - a) / 1000;
      for (let bk = Math.floor(a / 100); bk < Math.floor(b / 100); bk++) { for (const [f, v] of Object.entries(P.self[bk] || {})) { self[f] = (self[f] || 0) + v / secs; tot += v / secs; } for (const [f, v] of Object.entries(P.incl[bk] || {})) incl[f] = (incl[f] || 0) + v / secs; }
      res[which][k] = { self, incl, tot }; } }
  return res;
}

const POOL = process.argv.includes('--pool');
if (POOL) {   // pooled over the files given (a group of runs that differ only in what you want to average out): per segment kind, the median and the worst over its segments
  const G = { 'pass 1': s => s.pass === 1, 'wrap 1': s => s.wrap === 1, 'wraps 2+': s => s.wrap >= 2, 'passes 2+': s => s.pass >= 2 };
  const acc = {}; for (const k in G) acc[k] = []; let nRuns = 0, others = [];
  for (const f of files) { const R = JSON.parse(fs.readFileSync(f, 'utf8')), A = analyse(R), isCore = R.args.SEQ === 'core'; nRuns++;
    for (const s of A.segs) { const m = /^(pass|wrap) (\d+)/.exec(s.name); if (!m || /partial/.test(s.name)) continue; const o = { pass: m[1] === 'pass' ? +m[2] : 0, wrap: m[1] === 'wrap' ? +m[2] : 0 };
      for (const k in G) if (G[k](o)) acc[k].push({ ...s, isCore }); if (s.otherPct != null) others.push(s.otherPct); } }
  const med = a => { const b = a.filter(v => v != null && isFinite(v)).sort((x, y) => x - y); return b.length ? b[Math.floor(b.length / 2)] : null; }, mx = a => { const b = a.filter(v => v != null && isFinite(v)); return b.length ? Math.max(...b) : null; }, mn = a => { const b = a.filter(v => v != null && isFinite(v)); return b.length ? Math.min(...b) : null; };
  console.log(`${nRuns} runs; others' CPU while running: median ${f1(med(others))} % of the machine\n`);
  console.log('| group | segments | long tasks per s (median) | worst long task ms (median / max) | long frames per s | main thread task % (median) | least lead ms (min / median of segments) | late events | largest clock step ms (median / max) | segments with a step > 30 ms | underruns per minute | glitch.n |\n|---|---|---|---|---|---|---|---|---|---|---|---|');
  for (const k in G) { const S2 = acc[k]; if (!S2.length) continue; const lead = s => s.isCore ? s.coreLeadMin : s.pageMin, late = s => s.isCore ? s.coreLate : s.pageLate; const secs = S2.reduce((a, s) => a + s.secs, 0);
    console.log(`| ${k} | ${S2.length} | ${f1(med(S2.map(s => s.lt / s.secs)))} | ${f1(med(S2.map(s => s.ltMax)))} / ${f1(mx(S2.map(s => s.ltMax)))} | ${f1(med(S2.map(s => s.loaf / s.secs)))} | ${f1(med(S2.map(s => s.taskPct)))} | ${f1(mn(S2.map(lead)))} / ${f1(med(S2.map(lead)))} | ${S2.reduce((a, s) => a + late(s), 0)} | ${f1(med(S2.map(s => s.clkMaxStep)))} / ${f1(mx(S2.map(s => s.clkMaxStep)))} | ${S2.filter(s => s.clkBig > 0).length} | ${f1(S2.reduce((a, s) => a + (s.underruns || 0), 0) / secs * 60)} | ${S2.reduce((a, s) => a + (s.glitch || 0), 0)} |`); }
  process.exit(0);
}
const SUMMARY = process.argv.includes('--summary');
if (SUMMARY) {
  // one line per run: for the groups pass 1, wrap 1, wraps 2+, passes 2+: least lead of a booked event (page-booked, or the core's own at its booking; ms), late events, longest long task (ms), long tasks per minute, largest 100 ms clock step error (ms), underruns
  const rowsS = [];
  for (const f of files) { const R = JSON.parse(fs.readFileSync(f, 'utf8')), A = analyse(R), isCore = R.args.SEQ === 'core';
    const G = { 'pass 1': s => s.pass === 1, 'wrap 1': s => s.wrap === 1, 'wraps 2+': s => s.wrap >= 2, 'passes 2+': s => s.pass >= 2 };
    const cells = []; for (const [g, fn] of Object.entries(G)) { const S2 = A.segs.map((s, i) => ({ ...s, ...(A.segs[i]) })).filter(s => { const m = /^(pass|wrap) (\d+)/.exec(s.name); if (!m) return false; const o = { pass: m[1] === 'pass' ? +m[2] : 0, wrap: m[1] === 'wrap' ? +m[2] : 0 }; return fn(o) && !/partial/.test(s.name); });
      if (!S2.length) { cells.push('-'); continue; }
      const lead = Math.min(...S2.map(s => isCore ? (s.coreLeadMin ?? 1e9) : (s.pageMin ?? 1e9))), late = S2.reduce((a, s) => a + (isCore ? s.coreLate : s.pageLate), 0), ltMax = Math.max(...S2.map(s => s.ltMax)), secs = S2.reduce((a, s) => a + s.secs, 0), ltN = S2.reduce((a, s) => a + s.lt, 0), clk = Math.max(...S2.map(s => s.clkMaxStep)), ur = S2.reduce((a, s) => a + (s.underruns || 0), 0), rowL = Math.min(...S2.map(s => s.rowLeadMin ?? 1e9));
      cells.push(`${f1(lead)} / ${late} / ${f1(ltMax)} / ${f1(ltN / secs * 60)} / ${f1(clk)} / ${ur}`); }
    rowsS.push(`| ${R.tag} | ${cells.join(' | ')} |`); }
  console.log('Each cell: least lead ms (core: its own booking; page: the player\'s) / late events / longest long task ms / long tasks per minute / largest 100 ms clock-step error ms / underrun events\n');
  console.log('| run | pass 1 | wrap 1 | wraps 2+ | passes 2+ |\n|---|---|---|---|---|'); for (const r of rowsS) console.log(r); process.exit(0);
}
for (const f of files) {
  const R = JSON.parse(fs.readFileSync(f, 'utf8')), A = analyse(R);
  if (JSONOUT) { console.log(JSON.stringify(A)); continue; }
  if (process.argv.includes('--log')) { const flt = /slow |long frame|stall|held the song|kick|worklet module|core PLAY|moved|clock \+|audio clock|dropped|stray|switched|hidden/; const lines = R.log.filter(l => flt.test(l) && !/media session/.test(l));
    console.log(`\n### ${R.tag}: playback report lines (seconds after PLAY; wraps at ${A.wraps.map(w => ((w.wall / 1000) - ((R.log.find(l => /PLAY song/.test(l)) || '0').split(' ')[0] * 1)).toFixed(1)).join(', ')} s)\n`); const p0 = +((R.log.find(l => /PLAY song/.test(l)) || '0 ').split(' ')[0]);
    for (const l of lines) { const t = +l.split(' ')[0]; console.log(`${(t - p0).toFixed(2).padStart(7)}  ${l.slice(l.indexOf(' ') + 1).slice(0, 220)}`); } continue; }
  if (process.argv.includes('--first')) { const p0 = +((R.log.find(l => /PLAY song/.test(l)) || '0 ').split(' ')[0]) * 1000, code = R.evN['coreTake']; console.log(`\n### ${R.tag}: first note of each pad on the page player (seconds after PLAY; wraps at ${A.wraps.map(w => ((w.wall - p0) / 1000).toFixed(1)).join(', ')} s)\n\n| at s | pad | play() ms | lead ms | long tasks over it (ms) |\n|---|---|---|---|---|`);
    for (const e of R.ev.filter(e => e[4] === 1)) { const LT = R.lt.filter(l => l[0] < e[0] + 600 && l[0] + l[1] > e[0] - 200).map(l => f1(l[1])).join(', '); console.log(`| ${((e[0] - p0) / 1000).toFixed(1)} | ${e[5]} | ${f1(e[3])} | ${f1(e[2] * 1000)} | ${LT || '-'} |`); }
    const L2 = R.log.filter(l => /first buffer|variation|render on the page|CUT\/RES|tempo stretch|worklet module/.test(l)); console.log('\nrelated playback report lines:\n'); for (const l of L2) console.log('  ' + ((+l.split(' ')[0] * 1000 - p0) / 1000).toFixed(2) + ' ' + l.slice(l.indexOf(' ') + 1).slice(0, 120)); continue; }
  console.log(`\n### ${R.tag}\n`); console.log(`seq=${R.args.SEQ} screen=${R.args.SCREEN} follow=${R.args.FOLLOW} throttle=${R.args.THR}x song=${R.args.SONG}${R.args.LOOP ? ' loop ' + R.args.LOOP : ''} size=${R.args.SIZE}; sample rate ${R.env.sr}; wraps at audio ${A.wraps.map(w => w.due.toFixed(2)).join(', ')} (rows ${A.wraps.map(w => w.from + '>' + w.row).join(', ')})\n`);
  const segs = A.segs, cols = segs.map(s => s.name), rowsT = [
    ['seconds', s => f1(s.secs)],
    ['page-booked events (n)', s => s.pageEv], ['  late (lead < 0)', s => s.pageLate], ['  least lead ms', s => f1(s.pageMin)], ['  1st percentile lead ms', s => f1(s.pageP01)], ['  median lead ms', s => f1(s.pageMed)],
    ['core notes seen at page (n) / least lead ms', s => s.takeEv + ' / ' + f1(s.takeMin)], ['rows: least lead at page ms', s => f1(s.rowLeadMin)],
    ['core: late notes / of', s => SEQ(R) ? s.coreLate + ' / ' + s.coreN : '-'], ['core: worst lateness ms', s => SEQ(R) ? f1(s.coreWorst) : '-'], ['core: least lead ms', s => SEQ(R) ? f1(s.coreLeadMin) : '-'], ['core: process() ms/block (avg, worst 0.5 s)', s => SEQ(R) ? f1(s.coreMsBlk) + ' / ' + f1(s.coreMsBlkMax) : '-'],
    ['core skips (delta)', s => s.skips], ['PB dropped notes', s => s.pbDrop],
    ['long tasks n / total ms / max ms', s => `${s.lt} / ${f1(s.ltMs)} / ${f1(s.ltMax)}`], ['long animation frames n / max ms', s => `${s.loaf} / ${f1(s.loafMax)}`],
    ['clock rate (ct s per wall s)', s => f1(s.rate)], ['  1 s windows min / max', s => `${f1(s.rate1min)} / ${f1(s.rate1max)}`], ['  largest 100 ms step error ms', s => f1(s.clkMaxStep)], ['  steps over 30 ms (n)', s => s.clkBig], ['  longest gap between samples ms', s => f1(s.sampGapMax)], ['  ct - wall offset range ms', s => f1(s.offRange)],
    ['outputLatency ms min / max', s => `${f1(s.olMin)} / ${f1(s.olMax)}`],
    ['underrun events / ms (playbackStats delta)', s => `${s.underruns} / ${f1(s.underrunMs)}`], ['glitch.n delta', s => s.glitch],
    ['page ticks: longest gap s (cum.)', s => f1(s.pgGap)], ['page: moved on / held (delta)', s => `${s.pgRe} / ${s.pgRs}`],
    ['main thread task % / script % / layout % / style %', s => `${f1(s.taskPct)} / ${f1(s.scriptPct)} / ${f1(s.layoutPct)} / ${f1(s.stylePct)}`], ['rAF per second', s => f1(s.raf)],
    ['machine busy % (of 2 CPUs) / of it chrome / others', s => `${f1(s.machBusy)} / ${f1(s.chromePct)} / ${f1(s.otherPct)}`],
    ['audio thread CPU % / main thread CPU % (of one CPU, from /proc)', s => `${f1(s.audioPct)} / ${f1(s.mainCpuPct)}`],
    ['first-time pad notes (n) / cost ms', s => `${s.firstNotes} / ${f1(s.firstCost)}`], ['costliest play() call ms', s => f1(s.evCostMax)],
  ];
  console.log('| | ' + cols.join(' | ') + ' |\n|---|' + cols.map(() => '---').join('|') + '|');
  for (const [n, fn] of rowsT) console.log(`| ${n} | ${segs.map(s => fn(s)).join(' | ')} |`);
  // functions over 1.5 ms per segment
  console.log('\nPage functions over 1.5 ms per segment (calls / total ms / max ms):\n');
  const names = new Set(); for (const s of segs) for (const k of Object.keys(s.slow)) names.add(k);
  if (names.size) { console.log('| function | ' + cols.join(' | ') + ' |\n|---|' + cols.map(() => '---').join('|') + '|'); for (const n of names) console.log(`| ${n} | ${segs.map(s => s.slow[n] ? `${s.slow[n].n} / ${f1(s.slow[n].ms)} / ${f1(s.slow[n].max)}` : '').join(' | ')} |`); }
  // boundaries
  console.log('\nRow boundaries (window -200..+600 ms of each boundary), by pass: worst least-lead of page-booked events (ms) / late events / longest long task (ms) / costliest play() (ms)\n');
  const byPass = {}; for (const b of A.bw) (byPass[b.pass] || (byPass[b.pass] = { w: [], n: [] }))[b.wrap ? 'w' : 'n'].push(b);
  const agg = L => L.length ? `${f1(Math.min(...L.map(b => b.pageMin ?? 1e9).filter(v => v < 1e8)))} / ${L.reduce((a, b) => a + b.pageLate, 0)} / ${f1(Math.max(...L.map(b => b.ltMax)))} / ${f1(Math.max(...L.map(b => b.evCostMax)))} (n=${L.length}, first-time notes ${L.reduce((a, b) => a + b.firstN, 0)})` : '-';
  console.log('| pass | ordinary boundaries | wrap boundary |\n|---|---|---|'); for (const p of Object.keys(byPass)) console.log(`| ${p} | ${agg(byPass[p].n)} | ${agg(byPass[p].w)} |`);
  // CPU profile windows
  const cw = cpuWindows(R, A);
  for (const which of Object.keys(cw)) { const ks = Object.keys(cw[which]); if (ks.length < 2) continue; const P = R[which];
    console.log(`\n${which === 'mainProf' ? 'Main thread' : 'Worklet thread'} CPU profile, +-1 s windows ${ks.join(' vs ')} (ms per second of wall time; self, then inclusive; sampled every 1 ms; total sampled ${ks.map(k => f1(cw[which][k].tot)).join(' / ')} ms/s)\n`);
    for (const kind of ['self', 'incl']) { const a = cw[which][ks[0]][kind], b = cw[which][ks[1]][kind]; const all = [...new Set([...Object.keys(a), ...Object.keys(b)])].map(f => [f, a[f] || 0, b[f] || 0]).filter(([f]) => !/^\((idle|root|program)\)/.test(P.fns[f]));
      console.log(`${kind}: biggest excess (${ks[0]} minus ${ks[1]})\n\n| function | ${ks[0]} | ${ks[1]} | difference |\n|---|---|---|---|`);
      for (const [f, x, y] of all.sort((p, q) => (q[1] - q[2]) - (p[1] - p[2])).slice(0, TOP)) console.log(`| ${P.fns[f]} | ${f1(x)} | ${f1(y)} | ${f1(x - y)} |`);
      console.log(''); } }
}
function SEQ(R) { return R.args.SEQ === 'core'; }
