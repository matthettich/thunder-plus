// BUS INTO BUS (19 Nov, build 651): a bus can feed another bus through chains; solo follows the chain; CHN labels are two characters.
// Everything is cut out of index.html / core/seq-core.js and run in a vm (no browser, no audio device); the two chains nodes of the
// "no block of delay" test are the real ThunderChains worklet. What only a browser can show is in the punch list (HANDOVER).
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm'), fs = require('fs');
const H = require('./helpers');
const src = H.read('index.html'), coreSrc = H.read('core/seq-core.js'), OLD = require('./fixtures/busplan-before-bus-into-bus.js');
function cut(re, what, from = src){ const m = from.match(re); assert.ok(m, `could not find ${what}; the code moved or changed shape, so this test needs updating`); return m[0]; }
const RE_PLAN = /let busPlanH = null, busPlanVal = null;\nfunction busPlan\(\)\{[\s\S]*?\n  return P;\n\}\n/, RE_PLAN_CORE = /let busPlanH = null;\nlet busPlanVal = null;\nfunction busPlan\(\)\{[\s\S]*?\n  return P;\n\}\n/;
const NT = 16;
const INT = (v, d) => { const n = parseInt(v, 10); return Number.isFinite(n) ? n : d; };
const d2 = n => String(n).padStart(2, '0');
const T = (id, type, track, extra) => ({ id, type, params: track == null ? {} : Object.assign({ track }, extra || {}) });
const Cb = (a, b) => ({ from: [a, 'out'], to: [b, 'in'] });
const mk = (S, extra) => {
  const c = vm.createContext(Object.assign({ console, Math, Map, Set, Array, Object, Number, String, JSON, S, NT, MT: NT, int: INT, d2, busPlanVal: null, busPlanH: 0 }, extra || {}));
  return c;
};
const BASE = () => cut(/function chainsTracks\(\)\{[\s\S]*?\n\}\n/, 'chainsTracks') + cut(/const chainsFeed = [^\n]*\n/, 'chainsFeed');
function planCtx(S, planSrc = cut(RE_PLAN, 'busPlan')){
  const c = mk(S); vm.runInContext(BASE() + planSrc + '\nthis.busPlan = busPlan;', c); return c;
}
const asArr = m => Array.from(m, ([k, v]) => [k + 1, Array.from(v, x => x + 1).sort((a, b) => a - b)]).sort((a, b) => a[0] - b[0]);   // (Map insertion order is not part of the plan)
const view = P => ({ feeds: asArr(P.feeds), targets: asArr(P.targets), pure: Array.from(P.pure, x => x + 1).sort((a, b) => a - b), pre: Array.from(P.pre, x => x + 1), loop: P.loop });
function songOf(mods, cables, on){ const S = { chainPatch: { format: 'chains.patch', modules: mods, cables }, chainOn: new Array(NT).fill(false), mix: Array.from({ length: NT }, () => ({ mute: false, solo: false })) }; for (const t of on) S.chainOn[t - 1] = true; return S; }

/* the owner's patch (19 Nov): Track In 1, 2, 3, 4, 5, 6, 9, 13 -> Stereo mixer -> Chain Out 8; Track In 8 -> Chain Out 14 */
const SRC = [1, 2, 3, 4, 5, 6, 9, 13];
function owner(order){
  let mods = [...SRC.map(t => T('ti' + t, 'trackIn', t)), T('mx', 'smixer'), T('co8', 'chainOut', 8), T('ti8', 'trackIn', 8), T('co14', 'chainOut', 14)];
  const cables = [...SRC.map(t => Cb('ti' + t, 'mx')), Cb('mx', 'co8'), Cb('ti8', 'co14')];
  if (order === 'rev') mods = mods.reverse();
  return songOf(mods, cables, [...SRC, 8, 14]);
}

test('owner\'s patch: the plan has two buses, 08 feeds 14, the eight sources and 08 are pure, no loop', () => {
  const p = view(planCtx(owner()).busPlan());
  assert.deepEqual(p.feeds, [[8, SRC], [14, [8]]]);
  assert.deepEqual(p.targets, [[1, [8]], [2, [8]], [3, [8]], [4, [8]], [5, [8]], [6, [8]], [8, [14]], [9, [8]], [13, [8]]]);
  assert.deepEqual(p.pure, [1, 2, 3, 4, 5, 6, 8, 9, 13]);
  assert.equal(p.loop, false);
});
test('owner\'s patch: the closures (everything upstream and downstream of a track)', () => {
  const P = planCtx(owner()).busPlan();
  assert.deepEqual(Array.from(P.srcAll.get(13), x => x + 1), [...SRC, 8].sort((a, b) => a - b), 'bus 14 has 08 and every source upstream');
  assert.deepEqual(Array.from(P.srcAll.get(7), x => x + 1), SRC);
  assert.deepEqual(Array.from(P.busAll.get(0), x => x + 1), [8, 14], 'track 01 feeds both buses, one through the other');
  assert.deepEqual(Array.from(P.busAll.get(7), x => x + 1), [14]);
  assert.equal(P.busAll.has(13), false);
});
test('the core\'s copy of the plan (audio-thread sequencer) gives the same plan, and both sequencers drop the notes of 08 and 14', () => {
  const S = owner(), pg = view(planCtx(S).busPlan()), co = view(planCtx(S, cut(RE_PLAN_CORE, 'core busPlan', coreSrc)).busPlan());
  assert.deepEqual(co, pg);
  for (const [name, text] of [['page', src], ['core', coreSrc]]) assert.match(text, /const busT = new Set\(\); for \(let t = 0; t < NT; t\+\+\) if \(isBusT\(t\)\) busT\.add\(t\);/, name + ': makeSeq drops the notes of every track that is a bus');
  assert.match(coreSrc, /const isBusT = t => t >= 0 && t < NT && busPlan\(\)\.feeds\.has\(t\);/);
  assert.deepEqual([...Array(NT).keys()].filter(t => planCtx(S).busPlan().feeds.has(t)).map(t => t + 1), [8, 14], 'busT = {08, 14}');
});

/* ---- labels and status lines ---- */
function textCtx(S){
  const c = planCtx(S);
  vm.runInContext(cut(/const isBusT = [^\n]*\n/, 'isBusT') + cut(/const busOf = [^\n]*\n/, 'busOf') + cut(/function chainOutTargetsOf\(t\)\{[\s\S]*?\n\}\n/, 'chainOutTargetsOf') + cut(/function chainInSourcesOf\(b\)\{[\s\S]*?\n\}\n/, 'chainInSourcesOf')
    + cut(/function chnTx\(t, P, ct\)\{[\s\S]*?\n\}\n/, 'chnTx') + cut(/function chnSay\(t\)\{[\s\S]*?\n\}\n/, 'chnSay')
    + '\nconst trkLab = t => "TRACK " + d2(t + 1); const trkKind = t => isBusT(t) ? " BUS" : " MONO"; this.chnTx = chnTx; this.chnSay = chnSay;', c);
  return c;
}
const labels = c => { const P = c.busPlan(), ct = c.chainsTracks(); return Array.from({ length: NT }, (_, t) => c.chnTx(t, P, ct)); };

test('owner\'s patch: the CHN row says BS on the eight, ON on 08 and 14, -- elsewhere; every label is two characters', () => {
  const L = labels(textCtx(owner()));
  assert.deepEqual(L.map((x, i) => x + (i + 1)).filter(x => !x.startsWith('--')), ['BS1', 'BS2', 'BS3', 'BS4', 'BS5', 'BS6', 'ON8', 'BS9', 'BS13', 'ON14']);
  assert.ok(L.every(x => x.length === 2));
});
test('owner\'s patch: the status lines', () => {
  const c = textCtx(owner());
  assert.equal(c.chnSay(0), 'TRACK 01 MONO · chains BS: feeds the bus on track 08 (heard there, not on its own) · EDIT toggles');
  assert.equal(c.chnSay(7), 'TRACK 08 BUS · chains ON (track runs through the chains patch) · bus of tracks 01, 02, 03, 04, 05, 06, 09, 13 · feeds the bus on track 14 (heard there, not on its own)');
  assert.equal(c.chnSay(13), 'TRACK 14 BUS · chains ON (track runs through the chains patch) · bus of track 08');
});
test('every label the CHN row can produce is exactly two characters (random patches, every CHN combination)', () => {
  let seed = 7; const rnd = () => (seed = (seed * 1103515245 + 12345) & 0x7fffffff) / 0x7fffffff, seen = new Set();
  for (let k = 0; k < 400; k++){
    const mods = [], cables = [], n = 2 + Math.floor(rnd() * 6);
    for (let i = 0; i < n; i++){ mods.push(T('i' + i, 'trackIn', 1 + Math.floor(rnd() * 6))); mods.push(T('o' + i, 'chainOut', 1 + Math.floor(rnd() * 6))); cables.push(Cb('i' + i, 'o' + Math.floor(rnd() * n))); }
    const S = songOf(mods, cables, [1, 2, 3, 4, 5, 6].filter(() => rnd() < .7)), c = textCtx(S);
    for (const x of labels(c)) { seen.add(x); assert.equal(x.length, 2, 'label ' + JSON.stringify(x)); }
    for (let t = 0; t < 6; t++) assert.ok(c.chnSay(t).includes(' · chains '));
  }
  assert.ok(['ON', 'BS', 'MS', '--'].every(x => seen.has(x)), 'the random run reached ' + [...seen]);
});
test('the other Mixer rows can not exceed two characters (TRM and PAN are hex, MUT and SOL two letters, FX at most 8, SC ON / --)', () => {
  const draw = src.slice(src.indexOf("[{t:'TRM '"), src.indexOf("// Level meters, 8 lines"));
  assert.match(draw, /y === 0 \? hx\(m\.trim == null \? TRM_0|y === 0 \? hx\(m\.trim == null \? TRIM_0 : m\.trim\) : y === 1 \? hx\(m\.pan\) : y === 2 \? \(m\.mute \? 'MU' : '--'\) : \(m\.solo \? 'SO' : '--'\)/);
  assert.match(draw, /t:n \? d2\(n\) : '--'/); assert.match(draw, /t:on \? 'ON' : '--'/); assert.match(draw, /const tx = chnTx\(t, BP, ct\)|tx = chnTx\(t, BP, ct\)/);
  assert.match(src, /const FXSLOTS = 8;/);
});
test('CHN off on the first bus: the sources say MS and say why in full, 14 says nothing reaches it', () => {
  const S = owner(); S.chainOn[7] = false; const c = textCtx(S), L = labels(c);
  assert.equal(L[0], 'MS'); assert.equal(L[7], '--'); assert.equal(L[13], 'ON');
  assert.match(c.chnSay(0), /MISS: its Chain Out feeds track 08, whose CHN is off \(switch that track on, or this one\)/);
  assert.match(c.chnSay(13), /chains ON .*nothing reaches it yet: track 08 has CHN off/);
});
test('the MISS sentence no longer blames a CHN that is on: a track whose bus has CHN on never says "whose CHN is off"', () => {
  // an own-return track (Track In 8 -> Chain Out 8 and out 14) with Track In 1 into Chain Out 8: not a bus (accepted 25 Oct rule), so 01 is MISS for another reason
  const S = songOf([T('i1', 'trackIn', 1), T('i8', 'trackIn', 8), T('o8', 'chainOut', 8)], [Cb('i1', 'o8'), Cb('i8', 'o8')], [1, 8]), c = textCtx(S);
  assert.equal(c.chnTx(0, c.busPlan(), c.chainsTracks()), 'MS');
  assert.doesNotMatch(c.chnSay(0), /whose CHN is off/);
  assert.match(c.chnSay(0), /MISS: the patch needs a Chain Out for this track/);
});
test('bus 08 feeds a bus 14 whose CHN is off: 08 plays on its own and says so', () => {
  const S = owner(); S.chainOn[13] = false; const c = textCtx(S), P = c.busPlan();
  assert.deepEqual(view(P).feeds, [[8, SRC]]); assert.equal(P.pure.has(7), false, '08 is not routed away: it plays to the master');
  assert.match(c.chnSay(7), /chains ON .*bus of tracks 01.*its Track In feeds track 14, whose CHN is off, so it plays on its own \(switch that track on\)/);
});
test('a source feeding two buses says BS with both', () => {
  const S = owner(); S.chainPatch.modules.push(T('co14b', 'chainOut', 14)); S.chainPatch.cables.push(Cb('ti1', 'co14b'));
  const c = textCtx(S); assert.equal(c.chnSay(0), 'TRACK 01 MONO · chains BS: feeds the buses on tracks 08, 14 (heard there, not on its own) · EDIT toggles');
});
test('a muted middle bus says nothing reaches the next one', () => {
  const S = owner(); S.mix[7].mute = true; assert.match(textCtx(S).chnSay(7), /muted: nothing reaches track 14 from it/);
});

/* ---- loops ---- */
function loopSong(order){
  let mods = [T('a', 'trackIn', 8), T('oa', 'chainOut', 14), T('b', 'trackIn', 14), T('ob', 'chainOut', 8)];
  if (order) mods = mods.reverse();
  return songOf(mods, [Cb('a', 'oa'), Cb('b', 'ob')], [8, 14]);
}
test('a loop: the cable of the higher source is left out, always the same one, said as LP and, in the line, LOOP', () => {
  for (const order of [0, 1]){
    const c = textCtx(loopSong(order)), P = c.busPlan();
    assert.equal(P.loop, true); assert.deepEqual(Array.from(P.loopEdges, ([t, b]) => [t + 1, b + 1]), [[14, 8]], 'order ' + order);
    assert.deepEqual(view(P).feeds, [[14, [8]]], '08 feeds 14; 14 does not feed 08');
    const L = labels(c); assert.equal(L[13], 'LP'); assert.equal(L[7], 'ON'); assert.ok(L.every(x => x.length === 2));
    assert.equal(c.chnSay(13), 'TRACK 14 BUS · chains LOOP: track 14 would feed track 08, which already feeds it; the cable from 14 is left out so nothing feeds back (the rest plays) · EDIT toggles');
  }
});
test('a loop through three tracks leaves out one cable and the plan is acyclic', () => {
  const S = songOf([T('a', 'trackIn', 1), T('oa', 'chainOut', 2), T('b', 'trackIn', 2), T('ob', 'chainOut', 3), T('c', 'trackIn', 3), T('oc', 'chainOut', 1)], [Cb('a', 'oa'), Cb('b', 'ob'), Cb('c', 'oc')], [1, 2, 3]);
  const P = planCtx(S).busPlan(); assert.deepEqual(Array.from(P.loopEdges, ([t, b]) => [t + 1, b + 1]), [[3, 1]]);
  for (const t of P.srcAll.keys()) assert.ok(!P.srcAll.get(t).includes(t), 'no track upstream of itself');
});

/* ---- compatibility: the new plan equals build 647's on every shape except the newly working one ---- */
const OLDP = OLD + '\nthis.busPlan = busPlan;';
test('compatibility: the shipped songs have the same plan before and after, with their own CHN flags and with every CHN on', () => {
  for (const f of ['sounds/ELECTRO.thunder', 'sounds/BETA-TESTING-ELECTRO.thunder']){
    const song = JSON.parse(H.read(f)).song; assert.ok(song.chainPatch, f + ' has a chains patch');
    for (const allOn of [false, true]){
      const on = Array.from({ length: NT }, (_, t) => allOn || !!(song.chainOn && song.chainOn[t])), S = { chainPatch: song.chainPatch, chainOn: on, mix: [] };
      const a = view(planCtx(S, OLD).busPlan()), b = view(planCtx(S).busPlan());
      assert.deepEqual(b, a, f + (allOn ? ' (all CHN on)' : ''));
    }
  }
});
test('compatibility: 6000 random patches: identical plans unless the patch has the newly working shape or a loop', () => {
  let seed = 99; const rnd = () => (seed = (seed * 1103515245 + 12345) & 0x7fffffff) / 0x7fffffff; let same = 0, newShape = 0, loops = 0;
  for (let k = 0; k < 6000; k++){
    const mods = [], cables = [], n = 2 + Math.floor(rnd() * 7), m = 1 + Math.floor(rnd() * 3);
    for (let i = 0; i < n; i++){ mods.push(T('i' + i, 'trackIn', 1 + Math.floor(rnd() * 7), rnd() < .3 ? { pre: 1 } : {})); }
    for (let i = 0; i < m; i++) mods.push(T('o' + i, 'chainOut', 1 + Math.floor(rnd() * 7)));
    for (let i = 0; i < 2; i++) mods.push(T('x' + i, 'smixer'));
    const ids = mods.map(q => q.id);
    for (let i = 0; i < 4 + Math.floor(rnd() * 5); i++){ const a = ids[Math.floor(rnd() * ids.length)], b = ids[Math.floor(rnd() * ids.length)]; if (!a.startsWith('o') && !b.startsWith('i') && a !== b) cables.push(Cb(a, b)); }
    const S = songOf(mods, cables, [1, 2, 3, 4, 5, 6, 7].filter(() => rnd() < .8));
    const a = planCtx(S, OLD).busPlan(), b = planCtx(S).busPlan();
    if (a.loop || b.loop){ loops++; assert.equal(a.loop, b.loop, 'a loop is a loop in either order'); continue; }
    // the newly working shape: a track with a Track In, none of which reaches its own Chain Out, is fed by another track's Track In
    const differs = JSON.stringify(view(a)) !== JSON.stringify(view(b));
    if (differs){ newShape++; assert.ok(Array.from(b.feeds.keys()).some(t => a.targets.size >= 0 && !a.feeds.has(t) && mods.some(q => q.type === 'trackIn' && q.params.track === t + 1)), 'a plan changed without the newly working shape: ' + JSON.stringify([view(a), view(b)])); }
    else same++;
  }
  assert.ok(same > 2000 && newShape > 20, `same ${same}, newly working ${newShape}, loops ${loops}`);
});
test('the three 25 Oct bus tests still give their old answers (send with its own outs, pure sources, both ends need CHN)', () => {
  const send = songOf([T('i4', 'trackIn', 4), T('i5', 'trackIn', 5), T('mx', 'smixer'), T('rv', 'reverb'), T('o4', 'chainOut', 4), T('o5', 'chainOut', 5), T('o6', 'chainOut', 6)], [Cb('i4', 'mx'), Cb('i5', 'mx'), Cb('mx', 'rv'), Cb('rv', 'o4'), Cb('rv', 'o5'), Cb('rv', 'o6')], [4, 5, 6]);
  const p = view(planCtx(send).busPlan()); assert.deepEqual(p.feeds, [[6, [4, 5]]]); assert.deepEqual(p.pure, []); assert.equal(p.loop, false);
});

/* ---- solo / mute ---- */
function mixCtx(S){
  const c = planCtx(S);
  vm.runInContext(cut(/const audible = t => \{[\s\S]*?return false; \};\n/, 'audible') + cut(/function soloWhy\(t\)\{[\s\S]*?\n\}\n/, 'soloWhy') + '\nthis.audible = audible; this.soloWhy = soloWhy;', c);
  return c;
}
const heard = c => [...Array(NT).keys()].filter(t => c.audible(t)).map(t => t + 1);
const ALL = [...SRC, 8, 14].sort((a, b) => a - b);
function withMix(set){ const S = owner(); for (const [t, k] of set) S.mix[t - 1][k] = true; return S; }
test('solo: nothing soloed, everything is heard', () => assert.deepEqual(heard(mixCtx(owner())), [...Array(NT).keys()].map(t => t + 1)));
test('solo the end bus 14: 14, 08 and all eight sources are heard, nothing else', () => assert.deepEqual(heard(mixCtx(withMix([[14, 'solo']]))), ALL));
test('solo the middle bus 08: the eight sources and bus 14 stay', () => assert.deepEqual(heard(mixCtx(withMix([[8, 'solo']]))), ALL));
test('solo one source (01): 08 and 14 stay; the other sources of the same bus go silent', () => assert.deepEqual(heard(mixCtx(withMix([[1, 'solo']]))), [1, 8, 14]));
test('solo a source (01) plus an unrelated track (3 is in the bus, 10 is not): both soloed tracks play, the rest of the bus does not', () => {
  assert.deepEqual(heard(mixCtx(withMix([[1, 'solo'], [10, 'solo']]))), [1, 8, 10, 14]);
  assert.deepEqual(heard(mixCtx(withMix([[10, 'solo']]))), [10], 'an unrelated track alone');
});
test('mute the middle bus: it is silent (what passes through it is), the other tracks are not touched; mute beats the bus rule under solo', () => {
  assert.deepEqual(heard(mixCtx(withMix([[8, 'mute']]))), [...Array(NT).keys()].map(t => t + 1).filter(t => t !== 8));
  assert.deepEqual(heard(mixCtx(withMix([[14, 'solo'], [8, 'mute']]))), ALL.filter(t => t !== 8), 'solo 14 keeps what feeds it, but a muted 08 stays muted');
  assert.deepEqual(heard(mixCtx(withMix([[1, 'solo'], [14, 'mute']]))), [1, 8], 'solo 01 keeps 08, the muted 14 stays muted');
});
test('solo over mute as before: a muted track that is soloed plays, a muted source is not brought back by a soloed bus', () => {
  assert.deepEqual(heard(mixCtx(withMix([[14, 'solo'], [14, 'mute']]))), ALL);
  assert.equal(heard(mixCtx(withMix([[14, 'solo'], [3, 'mute']]))).includes(3), false);
});
test('the one-level rule is unchanged for one bus (1, 2 -> 7): solo 7 keeps 1 and 2, solo 1 keeps 7 and not 2', () => {
  const S = songOf([T('i1', 'trackIn', 1), T('i2', 'trackIn', 2), T('mx', 'smixer'), T('o7', 'chainOut', 7)], [Cb('i1', 'mx'), Cb('i2', 'mx'), Cb('mx', 'o7')], [1, 2, 7]);
  S.mix[6].solo = true; assert.deepEqual(heard(mixCtx(S)), [1, 2, 7]); S.mix[6].solo = false; S.mix[0].solo = true; assert.deepEqual(heard(mixCtx(S)), [1, 7]);
});
test('the Mixer says why a track that is not soloed is still heard', () => {
  let c = mixCtx(withMix([[14, 'solo']])); assert.equal(c.soloWhy(7), 'heard: feeds soloed bus 14'); assert.equal(c.soloWhy(0), 'heard: feeds soloed bus 14'); assert.equal(c.soloWhy(13), '');
  c = mixCtx(withMix([[1, 'solo']])); assert.equal(c.soloWhy(7), 'heard: takes in soloed track 01'); assert.equal(c.soloWhy(13), 'heard: takes in soloed track 01'); assert.equal(c.soloWhy(1), '');
  assert.equal(mixCtx(owner()).soloWhy(0), '', 'nothing soloed');
});

/* ---- render and stems ---- */
test('stems: in the owner\'s patch only bus 14 gets a stem; sources and 08 are in it; the render keeps the whole chain', () => {
  const c = mixCtx(owner()); vm.runInContext(cut(/function busStemPlan\(keepT, BP, aud\)\{[\s\S]*?\n\}\n/, 'busStemPlan') + cut(/function busClosure\(keep\)\{[\s\S]*?\n\}\n/, 'busClosure') + '\nthis.busStemPlan = busStemPlan; this.busClosure = busClosure;', c);
  const keepT = [...SRC].map(t => t - 1), inBus = c.busStemPlan(keepT, c.busPlan(), c.audible);
  assert.deepEqual(keepT.map(t => t + 1), [...SRC, 8, 14].sort((a, b) => a - b), 'the buses are added, in track order, whatever the plan\'s order');
  assert.deepEqual(keepT.filter(t => !inBus(t)).map(t => t + 1), [14], 'one stem: 14\'s');
  assert.deepEqual([...c.busClosure(new Set([13]))].map(t => t + 1).sort((a, b) => a - b), ALL, 'bus 14\'s render takes 08 and the eight sources with it');
  assert.deepEqual([...c.busClosure(new Set([7]))].map(t => t + 1).sort((a, b) => a - b), [...SRC, 8].sort((a, b) => a - b), 'and bus 08\'s takes the eight');
  assert.deepEqual([...c.busClosure(new Set([0]))].map(t => t + 1), [1], 'a source alone: no bus (its stem plays dry)');
});
test('stems: a muted middle bus does not pull in what only feeds it', () => {
  const S = withMix([[8, 'mute']]), c = mixCtx(S); vm.runInContext(cut(/function busClosure\(keep\)\{[\s\S]*?\n\}\n/, 'busClosure') + '\nthis.busClosure = busClosure;', c);
  assert.deepEqual([...c.busClosure(new Set([13]))].map(t => t + 1), [14]);
});
test('stems: the render code uses the plan helpers (pass and renderOut)', () => {
  assert.match(src, /keep = busClosure\(keep\);/); assert.match(src, /const inBus = busStemPlan\(keepT, BP, audible\);/);
});

/* ---- latency ---- */
test('latency plan follows the chain: arrival at 14 = its own + the latest at 08 = 08\'s own + the latest source; sources line up on 08', () => {
  const c = mixCtx(owner()); c.aset = { pdcMode: 'on' }; c.pumpChainLatency = (A, sr) => A[0] || 0; c.S.tfx = Array.from({ length: NT + 1 }, () => [0]);
  c.S.tfx[0] = [480]; c.S.tfx[1] = [4800]; c.S.tfx[7] = [2400]; c.S.tfx[13] = [1200];   // samples at 48 kHz: 10, 100, 50, 25 ms
  vm.runInContext(cut(/function pdcPlan\(sr\)\{[\s\S]*?\n\}\n/, 'pdcPlan') + '\nthis.pdcPlan = pdcPlan;', c);
  const pl = c.pdcPlan(48000), ms = v => Math.round(v * 1000 * 1000) / 1000;
  assert.equal(ms(pl.T), 175, 'T = 100 (source 02) + 50 (08) + 25 (14) ms'); assert.equal(ms(pl.d.get(13)), 0, '14 is the latest');
  assert.equal(ms(pl.d.get(7)), 0, '08 is pure, heard only through 14: no direct delay'); assert.equal(ms(pl.d.get(1)), 0);
  assert.equal(ms(pl.f.get(0)), 90, 'source 01 (10 ms) waits 90 ms to meet 02 (100 ms) on bus 08'); assert.equal(ms(pl.f.get(1)), 0); assert.equal(ms(pl.f.get(2)), 100);
  assert.equal(ms(pl.f.get(7)), 0, '08 is the only source of 14');
  assert.equal(ms(pl.c.get(13)), 150, '14\'s effects hear their input 150 ms late (the beat clock follows)'); assert.equal(ms(pl.c.get(7)), 100);
});

/* ---- no block of delay: the real worklet, two nodes, the host's wiring ---- */
test('no block of delay: an impulse into track 01 comes out of Chain Out 14 in the same block, bit for bit what bus 08 returned', () => {
  const B = 128, ctx = H.freezeClock(H.workletScope()); H.load(ctx, 'chains/chains-worklet.js');
  const patch = { format: 'chains.patch', modules: [{ id: 'i1', type: 'trackIn', scope: 'mono', params: { track: 1 } }, { id: 'i2', type: 'trackIn', scope: 'mono', params: { track: 2 } }, { id: 'mx', type: 'smixer', scope: 'mono', params: {} },
    { id: 'o8', type: 'chainOut', scope: 'mono', params: { track: 8 } }, { id: 'i8', type: 'trackIn', scope: 'mono', params: { track: 8 } }, { id: 'o14', type: 'chainOut', scope: 'mono', params: { track: 14 } }],
    cables: [{ from: ['i1', 'out'], to: ['mx', 'in1'] }, { from: ['i2', 'out'], to: ['mx', 'in2'] }, { from: ['mx', 'out'], to: ['o8', 'in'] }, { from: ['i8', 'out'], to: ['o14', 'in'] }, { from: ['i8', 'outR'], to: ['o14', 'inR'] }] };
  const mkNode = (track, busMap) => { const p = new ctx.__registered['thunder-chains']({ processorOptions: { quality: 'full', track, patch: JSON.parse(JSON.stringify(patch)), busMap } }); p.port.postMessage = () => {}; return p; };
  const A = mkNode(7, [0, 1]), Bn = mkNode(13, [7]);
  const ins = (n, ch) => { const a = [[], [], [], [], [], []]; a[0] = [new Float32Array(B), new Float32Array(B)]; a[4] = ch; return a; };
  let firstA = -1, firstB = -1, same = true;
  for (let blk = 0; blk < 4; blk++){
    const s1 = new Float32Array(B), s2 = new Float32Array(B); if (blk === 0){ s1[0] = 0.5; s2[0] = 0; }
    const oa = [[new Float32Array(B), new Float32Array(B)], [new Float32Array(B)]]; A.process(ins(A, [s1, s1.slice(), s2, s2.slice()]), oa, {});
    const ob = [[new Float32Array(B), new Float32Array(B)], [new Float32Array(B)]]; Bn.process(ins(Bn, [oa[0][0], oa[0][1]]), ob, {});   // the same block: the host's graph runs 08 before 14
    for (let i = 0; i < B; i++){ if (firstA < 0 && Math.abs(oa[0][0][i]) > 1e-6) firstA = blk * B + i; if (firstB < 0 && Math.abs(ob[0][0][i]) > 1e-6) firstB = blk * B + i; if (ob[0][0][i] !== oa[0][0][i]) same = false; }
  }
  assert.equal(firstA, 0, 'bus 08 returns the impulse at sample 0 (the mixer adds no latency)'); assert.equal(firstB, 0, 'and so does 14, in the same block');
  assert.ok(same, '14 is bit for bus 08 (a Track In to a Chain Out is a wire)');
});

/* ---- the native fixtures are the web app's own answers ---- */
test('tests/fixtures/bus-into-bus-native.json is up to date (node tools/make-bus-fixtures.mjs)', () => {
  const { execFileSync } = require('child_process');
  execFileSync(process.execPath, [require('path').join(__dirname, '..', 'tools', 'make-bus-fixtures.mjs'), '--check'], { stdio: 'pipe' });
});
