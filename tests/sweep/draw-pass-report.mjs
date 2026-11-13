// Turns tests/sweep/out/draw-<prefix><seq>-<size>-x<N>.json into markdown tables (draw-pass.mjs makes the files).
//   node tests/sweep/draw-pass-report.mjs --prefix=before- [--after=after-] [--seq=page,core] [--size=open,cover] [--thr=4,6,1]
// One table per run. With --after the cells read "before -> after" for the columns that matter (draw ms a second, long tasks).
import fs from 'fs'; import path from 'path'; import { ROOT } from './cdp-lib.mjs';
const arg = (k, d) => { const a = process.argv.find(x => x.startsWith('--' + k + '=')); return a ? a.slice(k.length + 3) : d; };
const P0 = arg('prefix', 'before-'), P1 = arg('after', ''), SEQ = arg('seq', 'page,core').split(','), SIZE = arg('size', 'open,cover').split(','), THR = arg('thr', '4,6,1').split(',');
const ORDER = ['song', 'phrase', 'pv', 'sect', 'inst', 'mixer', 'fx', 'mod', 'project'], NAME = { song: 'Song / Arrange', phrase: 'Phrase', pv: 'Pattern view', sect: 'Sect', inst: 'Inst', mixer: 'Mixer', fx: 'FX', mod: 'MOD', project: 'Project' };
const load = (p, s, z, t) => { const f = path.join(ROOT, 'tests/sweep/out', `draw-${p}${s}-${z}-x${t}.json`); return fs.existsSync(f) ? JSON.parse(fs.readFileSync(f, 'utf8')) : null; };
const f1 = x => x == null ? '-' : x.toFixed(1), f0 = x => x == null ? '-' : x.toFixed(0);
const cells = r => r && { dps: r.draws / r.secs, dms: r.drawMs / r.secs, per: r.drawMs / Math.max(1, r.draws), ltN: r.ltN / r.secs * 10, ltMs: r.ltMs / r.secs * 10, ltMax: r.ltMax, lay: r.layoutN / r.secs, sty: r.styleN / r.secs, task: r.taskMs / r.secs, lead: r.leadMin == null ? null : r.leadMin * 1000, late: r.late, cl: r.core ? r.core.late : null };
const pair = (a, b, f) => b !== undefined && b !== null ? `${f(a)} -> ${f(b)}` : f(a);   // (13 Nov) an after value of 0 is a value: only a missing one prints the before alone
for (const t of THR) for (const z of SIZE) for (const s of SEQ) {
  const A = load(P0, s, z, t), B = P1 ? load(P1, s, z, t) : null; if (!A) continue;
  console.log(`\n### ${s} sequencer, ${z === 'open' ? 'unfolded 884x1020' : 'folded 412x915'}, ${t}x CPU throttle${B ? ' (before -> after)' : ''}\n`);
  console.log('| screen | draws/s | draw ms/s | ms/draw | long tasks /10 s | long ms /10 s | longest ms | task ms/s | layout /s | style /s | ' + (s === 'page' ? 'least lead ms / late |' : 'core late |'));
  console.log('|---|---|---|---|---|---|---|---|---|---|---|');
  for (const sc of ORDER) { const a = cells(A.screens[sc]), b = B ? cells(B.screens[sc]) : null; if (!a) continue;
    console.log(`| ${NAME[sc]} | ${pair(a.dps, b ? b.dps : null, f1)} | ${pair(a.dms, b ? b.dms : null, f0)} | ${pair(a.per, b ? b.per : null, f1)} | ${pair(a.ltN, b ? b.ltN : null, f0)} | ${pair(a.ltMs, b ? b.ltMs : null, f0)} | ${pair(a.ltMax, b ? b.ltMax : null, f0)} | ${pair(a.task, b ? b.task : null, f0)} | ${pair(a.lay, b ? b.lay : null, f0)} | ${pair(a.sty, b ? b.sty : null, f0)} | ${s === 'page' ? pair(a.lead, b ? b.lead : null, f0) + ' / ' + pair(a.late, b ? b.late : null, x => String(x)) : pair(a.cl, b ? b.cl : null, x => String(x))} |`); }
}
