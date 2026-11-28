// BUS INTO BUS (build 651): the fixtures a native test reads (docs/BUS-INTO-BUS-NATIVE.md). Cases in the shape native/tests/test_busplan.cpp reads
// ({id, S:{chainPatch, chainOn, mix:[{mute, solo}]}, want:{...}}), with the new fields: loopEdges, srcAll / busAll (masks), labels, stems.
// The answers are the web app's own functions, cut out of index.html and run in a vm, so they are the reference.
//   node tools/make-bus-fixtures.mjs           write tests/fixtures/bus-into-bus-native.json
//   node tools/make-bus-fixtures.mjs --check   exit 1 if the file is out of date (tests/bus-into-bus.test.js runs this)
import fs from 'fs'; import path from 'path'; import vm from 'vm'; import { fileURLToPath } from 'url';
const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..'), OUT = path.join(ROOT, 'tests/fixtures/bus-into-bus-native.json');
const src = fs.readFileSync(path.join(ROOT, 'index.html'), 'utf8'), NT = 16;
const cut = re => { const m = src.match(re); if (!m) throw new Error('not found: ' + re); return m[0]; };
const T = (id, type, track, extra) => ({ id, type, scope: 'mono', params: track == null ? {} : Object.assign({ track }, extra || {}) });
const Cb = (a, b) => ({ from: [a, 'out'], to: [b, 'in'] });
const SRC = [1, 2, 3, 4, 5, 6, 9, 13];
const owner = () => ({ mods: [...SRC.map(t => T('ti' + t, 'trackIn', t)), T('mx', 'smixer'), T('co8', 'chainOut', 8), T('ti8', 'trackIn', 8), T('co14', 'chainOut', 14)], cables: [...SRC.map(t => Cb('ti' + t, 'mx')), Cb('mx', 'co8'), Cb('ti8', 'co14')], on: [...SRC, 8, 14] });
const chain4 = () => ({ mods: [T('a', 'trackIn', 1), T('oa', 'chainOut', 2), T('b', 'trackIn', 2), T('ob', 'chainOut', 3), T('c', 'trackIn', 3), T('oc', 'chainOut', 4)], cables: [Cb('a', 'oa'), Cb('b', 'ob'), Cb('c', 'oc')], on: [1, 2, 3, 4] });
const send = () => ({ mods: [T('i4', 'trackIn', 4), T('i5', 'trackIn', 5), T('mx', 'smixer'), T('rv', 'reverb'), T('o4', 'chainOut', 4), T('o5', 'chainOut', 5), T('o6', 'chainOut', 6)], cables: [Cb('i4', 'mx'), Cb('i5', 'mx'), Cb('mx', 'rv'), Cb('rv', 'o4'), Cb('rv', 'o5'), Cb('rv', 'o6')], on: [4, 5, 6] });
const loop2 = rev => { let mods = [T('a', 'trackIn', 8), T('oa', 'chainOut', 14), T('b', 'trackIn', 14), T('ob', 'chainOut', 8)]; if (rev) mods = mods.reverse(); return { mods, cables: [Cb('a', 'oa'), Cb('b', 'ob')], on: [8, 14] }; };
const cases = [];
const add = (id, p, o = {}) => cases.push({ id, p, on: o.on || p.on, solo: o.solo || [], mute: o.mute || [], notes: o.notes || null });
add('owner', owner(), { notes: SRC }); add('owner solo 14', owner(), { solo: [14] }); add('owner solo 08', owner(), { solo: [8] }); add('owner solo 01', owner(), { solo: [1] });
add('owner solo 01 + 10', owner(), { solo: [1, 10] }); add('owner mute 08', owner(), { mute: [8] }); add('owner solo 14 mute 08', owner(), { solo: [14], mute: [8] }); add('owner solo 01 mute 14', owner(), { solo: [1], mute: [14] });
add('owner solo 14 mute 03', owner(), { solo: [14], mute: [3] }); add('owner solo 14 + mute 14', owner(), { solo: [14], mute: [14] });
add('owner, CHN off on 08', owner(), { on: [...SRC, 14] }); add('owner, CHN off on 14', owner(), { on: [...SRC, 8] }); add('owner, notes on 14 too', owner(), { notes: [...SRC, 14] });
add('chain 1-2-3-4', chain4(), { notes: [1] }); add('chain solo 04', chain4(), { solo: [4] }); add('chain solo 02', chain4(), { solo: [2] }); add('chain solo 01', chain4(), { solo: [1] });
add('send 4, 5 -> 6 (own outs)', send(), { notes: [4, 5] }); add('loop 08 <-> 14', loop2(false)); add('loop 08 <-> 14, modules reversed', loop2(true));
const BASE = () => cut(/function chainsTracks\(\)\{[\s\S]*?\n\}\n/) + cut(/const chainsFeed = [^\n]*\n/);
export function build(){
  const out = [], mask = L => L.reduce((a, t) => a | (1 << t), 0);
  for (const c of cases){
    const S = { chainPatch: { format: 'chains.patch', modules: c.p.mods, cables: c.p.cables }, chainOn: Array.from({ length: NT }, (_, t) => c.on.includes(t + 1)), mix: Array.from({ length: NT }, (_, t) => ({ mute: c.mute.includes(t + 1), solo: c.solo.includes(t + 1) })) };
    const ctx = vm.createContext({ Math, Map, Set, Array, Object, Number, String, JSON, S, NT, MT: NT, int: (v, d) => { const n = parseInt(v, 10); return Number.isFinite(n) ? n : d; }, d2: n => String(n).padStart(2, '0'), busPlanVal: null, busPlanH: 0 });
    vm.runInContext(BASE() + cut(/let busPlanH = null, busPlanVal = null;\nfunction busPlan\(\)\{[\s\S]*?\n  return P;\n\}\n/) + cut(/const audible = t => \{[\s\S]*?return false; \};\n/) + cut(/function chnTx\(t, P, ct\)\{[\s\S]*?\n\}\n/)
      + cut(/function busStemPlan\(keepT, BP, aud\)\{[\s\S]*?\n\}\n/) + '\nthis.f = {busPlan, audible, chnTx, chainsFeed, chainsTracks, busStemPlan};', ctx);
    const f = ctx.f, P = f.busPlan(), A = (m, t) => Array.from(m.get(t) || []), ct = f.chainsTracks();
    const per = fn => Array.from({ length: NT }, (_, t) => fn(t));
    const want = {
      feeds: per(t => mask(A(P.feeds, t))), targets: per(t => mask(A(P.targets, t))), lists: Object.fromEntries([...P.feeds].map(([b, l]) => [String(b), Array.from(l)])),
      pure: mask([...P.pure]), pre: mask([...P.pre]), loop: P.loop, loopEdges: Array.from(P.loopEdges, e => Array.from(e)),
      srcAll: per(t => mask(A(P.srcAll, t))), busAll: per(t => mask(A(P.busAll, t))), chainsFeed: mask(per(t => f.chainsFeed(t)).map((v, t) => v ? t : -1).filter(t => t >= 0)),
      audible: mask(per(t => f.audible(t)).map((v, t) => v ? t : -1).filter(t => t >= 0)), labels: per(t => f.chnTx(t, P, ct)) };
    if (c.notes){ const keepT = c.notes.map(t => t - 1).filter(t => f.audible(t)), inBus = f.busStemPlan(keepT, P, f.audible); want.keepT = Array.from(keepT); want.stems = keepT.filter(t => !inBus(t)); }
    out.push({ id: c.id, S, want });
  }
  return JSON.stringify(out, null, 1) + '\n';
}
if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)){
  const text = build();
  if (process.argv.includes('--check')){ const have = fs.existsSync(OUT) ? fs.readFileSync(OUT, 'utf8') : ''; if (have !== text){ console.error('tests/fixtures/bus-into-bus-native.json is out of date: run node tools/make-bus-fixtures.mjs'); process.exit(1); } console.log('bus-into-bus-native.json is up to date'); }
  else { fs.writeFileSync(OUT, text); console.log('wrote ' + OUT + ' (' + cases.length + ' cases)'); }
}
