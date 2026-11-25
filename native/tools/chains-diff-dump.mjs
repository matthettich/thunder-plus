// C2a: THE GOLDEN OF chains_diff (docs/port/C2A-NOTES.md). README (all of it is this comment).
//
// WHAT IT IS. The page's own `chnDiff` (and `chnClone`, which it calls) are EXTRACTED BY NAME from the repository's index.html and run in a Node vm (nothing retyped; the tool fails
// loudly if either is not found or does not parse). Over a list of (prev, next) pairs it records what the page gives: the ops, or THAT IT THROWS. It then classifies as the native
// `chains_diff` must (native/core/src/chains/cdiff.h): Same (no ops); Params (every op a `param` op with a finite numeric value and a string module id: expectParams, in the page's order);
// Structural (anything else, a throw included). ctest `chains_diff` (native/tests/test_chains_diff.cpp) reads native/tests/golden/chains_diff_cases.json and must agree on every pair.
//
// HOW TO RUN (Node >= 20, no packages; from the repository root):
//   node native/tools/chains-diff-dump.mjs            writes native/tests/golden/chains_diff_cases.json
//   node native/tools/chains-diff-dump.mjs --check    regenerates in memory and exits 1 if the committed file differs (drift in index.html's chnDiff / chnClone, or in this tool)
//   node native/tools/chains-diff-dump.mjs --list     one line per case
// A case is {name, prev?, next?, prevText?, nextText?, ops | throws, expectKind, expectParams}. A key that is absent is `undefined` (a missing patch); `null` is null. prevText / nextText are RAW JSON
// text (for what JSON.stringify cannot write: duplicate keys, 1e999, -0, a lone surrogate); the page side parses it with JSON.parse exactly as the native side parses it.
// BASES: sounds/*.thunder (song.chainPatch) and the patches of native/tests/golden/chains_song_cases.json (the chains host's golden songs).
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import { fileURLToPath } from 'node:url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const OUT = path.join(ROOT, 'native/tests/golden/chains_diff_cases.json');
const html = fs.readFileSync(path.join(ROOT, 'index.html'), 'utf8').replace(/\r\n/g, '\n');

// ---- the page's code, by name ----
function grab(name, re) {
  const m = re.exec(html);
  if (!m) { console.error('chains-diff-dump: `' + name + '` not found in index.html (the page changed: update the extraction)'); process.exit(2); }
  return m[0];
}
const cloneSrc = grab('chnClone', /^const chnClone = [^\n]*;$/m);
const diffSrc = grab('chnDiff', /^function chnDiff\(prev, next\)\{\n[\s\S]*?\n\}$/m);
const ctx = vm.createContext({});
try { vm.runInContext(cloneSrc + '\n' + diffSrc + '\nthis.chnDiff = chnDiff;', ctx); } catch (e) { console.error('chains-diff-dump: the extracted page code does not run: ' + e.message); process.exit(2); }
if (typeof ctx.chnDiff !== 'function') { console.error('chains-diff-dump: chnDiff is not a function after extraction'); process.exit(2); }
const J = x => (x === undefined ? undefined : JSON.parse(JSON.stringify(x)));

// ---- the bases ----
const bases = {};
for (const f of fs.readdirSync(path.join(ROOT, 'sounds')).sort()) if (f.endsWith('.thunder')) {
  const d = JSON.parse(fs.readFileSync(path.join(ROOT, 'sounds', f), 'utf8'));
  const p = d.song && d.song.chainPatch; if (p && p.format === 'chains.patch') bases[f.replace(/\.thunder$/, '').toLowerCase()] = p;
}
const gs = JSON.parse(fs.readFileSync(path.join(ROOT, 'native/tests/golden/chains_song_cases.json'), 'utf8'));
for (const c of gs.cases) if (c.song && c.song.chainPatch && /^(c|f|g|k)\//.test(c.name)) bases[c.name.replace('/', '-')] = c.song.chainPatch;
if (!bases.electro) { console.error('chains-diff-dump: sounds/ELECTRO.thunder has no chainPatch'); process.exit(2); }

// deep structural equality as the native deep_eq: same types, same key sets (JSON.parse already made duplicates last-wins), equal values, arrays by length and element, numbers by value; containers nested past
// DEEP_MAX levels (the param value is level 1) are NOT equal
const DEEP_MAX = 64;
function deepEq(a, b, level) {
  if (a === null || b === null || typeof a !== 'object' || typeof b !== 'object') return a === b;
  if (Array.isArray(a) !== Array.isArray(b)) return false;
  if (level > DEEP_MAX) return false;
  if (Array.isArray(a)) return a.length === b.length && a.every((x, i) => deepEq(x, b[i], level + 1));
  const ka = Object.keys(a), kb = Object.keys(b);
  return ka.length === kb.length && ka.every(k => Object.prototype.hasOwnProperty.call(b, k) && deepEq(a[k], b[k], level + 1));
}
// prev's module for a param op: the LAST module with that id (the page's Map), its params || {}, member `name`
function prevParamValue(prev, o) {
  let q; for (const m of prev.modules) if (m.id === o.id) q = m;
  return (q.params || {})[o.name];
}
const cases = [];
function run(name, prev, next, raw) {
  const c = { name };
  if (raw) { c.prevText = raw[0]; c.nextText = raw[1]; if (raw[0] !== undefined) prev = JSON.parse(raw[0]); else prev = undefined; if (raw[1] !== undefined) next = JSON.parse(raw[1]); else next = undefined; }
  else { if (prev !== undefined) c.prev = J(prev); if (next !== undefined) c.next = J(next); }
  let ops;
  try { ops = ctx.chnDiff(J(prev) === undefined ? undefined : (raw ? prev : J(prev)), raw ? next : J(next)); c.ops = J(ops); }
  catch (e) { c.throws = String(e && e.constructor && e.constructor.name) + ': ' + String(e && e.message).slice(0, 80); ops = null; }
  if (ops === null) { c.expectKind = 'Structural'; c.expectParams = []; }
  else if (ops.length === 0) { c.expectKind = 'Same'; c.expectParams = []; }
  else {
    // THE ONE PLACE WHERE THE EXPECTATION DEPARTS FROM THE PAGE (docs/port/C2A-NOTES.md "Departures from the page"): a `param` op whose value is an object / array that is DEEP-EQUAL to prev's value is dropped
    // (the page's `!==` sees two clones as different and sends a harmless repeated op; the live host always has two separately parsed songs). The ops recorded in `ops` stay the page's own.
    const kept = ops.filter(o => !(o.op === 'param' && o.value !== null && typeof o.value === 'object' && deepEq(prevParamValue(prev, o), o.value, 1)));
    if (kept.length === 0) { c.expectKind = 'Same'; c.expectParams = []; }
    else if (kept.every(o => o.op === 'param' && typeof o.value === 'number' && Number.isFinite(o.value) && typeof o.id === 'string')) { c.expectKind = 'Params'; c.expectParams = kept.map(o => ({ id: o.id, name: o.name, v: o.value })); }
    else { c.expectKind = 'Structural'; c.expectParams = []; }
  }
  cases.push(c);
}
const ed = (p, f) => { const q = J(p); f(q); return q; };
const mod = (p, type) => p.modules.find(m => m.type === type);
const NOP = () => {};

for (const [bn, P] of Object.entries(bases)) {
  const big = bn === 'electro';
  run(bn + '/identical', P, P);
  if (!big) { run(bn + '/pos-and-view-ignored', P, ed(P, q => { q.modules[0].pos = [1, 2]; q.view = { x: 1 }; (q.meta = q.meta || {}).modified = 5; q.modules[0].collapsed = !q.modules[0].collapsed; q.cables[0].bend = 3; }));
    const smx = mod(P, 'smixer'); if (smx) { run(bn + '/param-one', P, ed(P, q => { q.modules.find(x => x.id === smx.id).params.l1 = 0.33; })); run(bn + '/param-to-string', P, ed(P, q => { q.modules.find(x => x.id === smx.id).params.l1 = 'x'; })); }
    run(bn + '/disconnect-last', P, ed(P, q => { q.cables.pop(); })); run(bn + '/add-module', P, ed(P, q => { q.modules.push({ id: 'newmod1', type: 'vca', params: {} }); })); continue; }
  const sm = mod(P, 'smixer'), tin = mod(P, 'trackIn'), co = mod(P, 'chainOut');
  const setp = (m, k, v) => q => { q.modules.find(x => x.id === m.id).params[k] = v; };
  run(bn + '/pos-and-view-ignored', P, ed(P, q => { q.modules[0].pos = [1, 2]; q.view = { x: 1 }; (q.meta = q.meta || {}).modified = 5; q.modules[0].collapsed = !q.modules[0].collapsed; q.cables[0].bend = 3; q.groups = [1]; q.custom = [2]; q.transport.tempo = 99; }));
  run(bn + '/param-one', P, ed(P, setp(sm, 'l1', 0.5)));
  run(bn + '/param-two-modules-order', P, ed(P, q => { setp(co, 'level', 0.25)(q); setp(sm, 'l2', 0.1)(q); setp(sm, 'l1', 0.9)(q); setp(tin, 'pre', 1)(q); }));
  run(bn + '/param-same-value', P, ed(P, setp(sm, 'l1', 0.8)));
  run(bn + '/param-to-string', P, ed(P, setp(sm, 'l1', '0.5')));
  run(bn + '/param-to-bool', P, ed(P, setp(sm, 'l1', true)));
  run(bn + '/param-to-null-nan-like', P, ed(P, setp(sm, 'l1', null)));
  run(bn + '/param-to-array', P, ed(P, setp(sm, 'l1', [0.5])));
  run(bn + '/param-to-object', P, ed(P, setp(sm, 'l1', {})));
  run(bn + '/param-number-and-string-mixed', P, ed(P, q => { setp(sm, 'l1', 0.5)(q); setp(sm, 'l2', 'x')(q); }));
  run(bn + '/param-new-key-in-next', P, ed(P, setp(sm, 'brandNew', 3)));
  run(bn + '/param-key-only-in-prev-ignored', P, ed(P, q => { delete q.modules.find(x => x.id === sm.id).params.l16; }));
  run(bn + '/params-object-dropped-in-next', P, ed(P, q => { delete q.modules.find(x => x.id === sm.id).params; }));
  run(bn + '/params-null-in-next', P, ed(P, q => { q.modules.find(x => x.id === sm.id).params = null; }));
  run(bn + '/params-null-in-prev', ed(P, q => { q.modules.find(x => x.id === sm.id).params = null; }), P);
  run(bn + '/add-module', P, ed(P, q => { q.modules.push({ id: 'newmod1', type: 'vca', scope: 'mono', pos: [0, 0], params: { gain: 1 } }); }));
  run(bn + '/add-module-with-text', P, ed(P, q => { q.modules.push({ id: 'newmod2', type: 'expr', pos: [0, 0], params: {}, text: 'a * k1' }); }));
  run(bn + '/remove-module', P, ed(P, q => { q.modules.pop(); }));
  run(bn + '/remove-and-add', P, ed(P, q => { q.modules.pop(); q.modules.push({ id: 'other', type: 'vca', params: {} }); }));
  run(bn + '/rename-id', P, ed(P, q => { q.modules[0].id = 'renamed'; }));
  run(bn + '/module-order-swapped', P, ed(P, q => { q.modules.reverse(); }));
  run(bn + '/connect', P, ed(P, q => { q.cables.push({ from: [tin.id, 'out'], to: [co.id, 'in'], bend: 0 }); }));
  run(bn + '/connect-fb', P, ed(P, q => { q.cables.push({ from: [tin.id, 'out'], to: [co.id, 'in'], bend: 0, fb: true }); }));
  run(bn + '/disconnect', P, ed(P, q => { q.cables.pop(); }));
  run(bn + '/disconnect-first', P, ed(P, q => { q.cables.shift(); }));
  run(bn + '/fb-mark-set', P, ed(P, q => { q.cables[0].fb = true; }));
  run(bn + '/fb-mark-cleared', ed(P, q => { q.cables[0].fb = true; }), P);
  run(bn + '/fb-false-vs-missing', P, ed(P, q => { q.cables[0].fb = false; }));
  run(bn + '/fb-string-true-vs-missing', P, ed(P, q => { q.cables[0].fb = 'true'; }));
  run(bn + '/fb-1-vs-true', ed(P, q => { q.cables[0].fb = true; }), ed(P, q => { q.cables[0].fb = 1; }));
  run(bn + '/cable-reordered-only', P, ed(P, q => { q.cables.reverse(); }));
  run(bn + '/text-added', P, ed(P, q => { q.modules[1].text = 'a + b'; }));
  run(bn + '/text-removed', ed(P, q => { q.modules[1].text = 'a + b'; }), P);
  run(bn + '/text-changed', ed(P, q => { q.modules[1].text = 'a + b'; }), ed(P, q => { q.modules[1].text = 'a - b'; }));
  run(bn + '/text-equal', ed(P, q => { q.modules[1].text = 'a + b'; }), ed(P, q => { q.modules[1].text = 'a + b'; }));
  run(bn + '/text-beyond-200-units-ignored', ed(P, q => { q.modules[1].text = 'x'.repeat(200) + 'AAA'; }), ed(P, q => { q.modules[1].text = 'x'.repeat(200) + 'BBB'; }));
  run(bn + '/text-within-200-units-differs', ed(P, q => { q.modules[1].text = 'x'.repeat(199) + 'AAA'; }), ed(P, q => { q.modules[1].text = 'x'.repeat(199) + 'BBB'; }));
  run(bn + '/text-non-string-ignored', ed(P, q => { q.modules[1].text = 5; }), ed(P, q => { q.modules[1].text = null; }));
  run(bn + '/text-number-vs-string', ed(P, q => { q.modules[1].text = 5; }), ed(P, q => { q.modules[1].text = '5'; }));
  run(bn + '/st2-removed', P, ed(P, q => { delete q.st2; }));
  run(bn + '/ti2-removed', P, ed(P, q => { delete q.ti2; }));
  run(bn + '/st2-set-in-next-only', ed(P, q => { delete q.st2; }), P);
  run(bn + '/st2-zero-vs-missing-both-falsy', ed(P, q => { delete q.st2; }), ed(P, q => { q.st2 = 0; }));
  run(bn + '/st2-string-0-vs-1-both-truthy', ed(P, q => { q.st2 = '0'; }), P);
  run(bn + '/st2-and-ti2-both-gone', ed(P, q => { delete q.st2; delete q.ti2; }), ed(P, q => { delete q.st2; delete q.ti2; q.modules[0].params.track = 2; }));
  run(bn + '/prev-missing', undefined, P);
  run(bn + '/prev-null', null, P);
  run(bn + '/prev-not-a-patch', ed(P, q => { q.format = 'chains.song'; }), P);
  run(bn + '/prev-format-missing', ed(P, q => { delete q.format; }), P);
  run(bn + '/prev-modules-not-array', ed(P, q => { q.modules = {}; }), P);
  run(bn + '/next-missing', P, undefined);
  run(bn + '/next-null', P, null);
  run(bn + '/next-false', P, false);
  run(bn + '/next-empty-object-truthy', P, {});
  run(bn + '/next-not-a-patch-format-same-shape', P, ed(P, q => { q.format = 'other'; }));
}

// ---- small synthetic patches ----
const M = (id, params, extra) => Object.assign({ id, type: 'vca', params }, extra || {});
const C = (a, b, extra) => Object.assign({ from: a, to: b }, extra || {});
const S = (modules, cables, extra) => Object.assign({ format: 'chains.patch', version: 1, modules, cables: cables || [] }, extra || {});
const S0 = S([M('a', { x: 1, y: 2 }), M('b', { z: 3 })], [C(['a', 'out'], ['b', 'in'])], { st2: 1, ti2: 1 });
run('syn/both-missing', undefined, undefined);
run('syn/both-null', null, null);
run('syn/prev-string-next-patch', 'chains.patch', S0);
run('syn/prev-number-next-patch', 7, S0);
run('syn/prev-array-next-patch', [S0], S0);
run('syn/next-number-prev-patch', S0, 5);
run('syn/next-number-prev-empty-patch-no-st2', S([], []), 5);
run('syn/next-string-prev-patch-without-st2', S([M('a', {})], []), 'x');
run('syn/next-zero-string-empty', S0, 0);
run('syn/next-empty-string', S0, '');
run('syn/empty-patches', S([], []), S([], []));
run('syn/modules-missing-in-prev', { format: 'chains.patch' }, S([]));
run('syn/modules-null-in-next-prev-empty', S([], [], {}), { format: 'chains.patch', modules: null, cables: null });
run('syn/modules-null-in-next-prev-full', S0, ed(S0, q => { q.modules = null; }));
run('syn/modules-zero-and-false-in-next', S([], []), { format: 'chains.patch', modules: 0 });
run('syn/modules-object-in-next-throws', S0, ed(S0, q => { q.modules = {}; }));
run('syn/modules-string-in-next-throws', S0, ed(S0, q => { q.modules = 'abc'; }));
run('syn/modules-number-in-next-throws', S0, ed(S0, q => { q.modules = 3; }));
run('syn/modules-true-in-next-throws', S0, ed(S0, q => { q.modules = true; }));
run('syn/module-null-in-next-throws', S0, ed(S0, q => { q.modules.push(null); }));
run('syn/module-null-in-prev-throws', ed(S0, q => { q.modules.push(null); }), S0);
run('syn/module-primitives-no-id', S([5, 'x', true, []], []), S([5, 'x', true, []], []));
run('syn/module-objects-without-id-same', S([{ type: 'vca' }], []), S([{ type: 'vca' }], []));
run('syn/module-without-params', S([{ id: 'a' }], []), S([{ id: 'a' }], []));
run('syn/module-without-params-then-gains-one', S([{ id: 'a' }], []), S([M('a', { k: 1 })], []));
run('syn/params-string-same', S([M('a', 'abc')], []), S([M('a', 'abc')], []));
run('syn/params-string-differs', S([M('a', 'abc')], []), S([M('a', 'abd')], []));
run('syn/params-string-vs-object', S([M('a', { 0: 'a', 1: 'b' })], []), S([M('a', 'ab')], []));
run('syn/params-array-numbers', S([M('a', [1, 2, 3])], []), S([M('a', [1, 5, 3])], []));
run('syn/params-array-vs-object-index-keys', S([M('a', { 0: 1, 1: 2 })], []), S([M('a', [1, 9])], []));
run('syn/params-array-grows', S([M('a', [1])], []), S([M('a', [1, 2])], []));
run('syn/params-array-length-quirk', S([M('a', [1, 2])], []), S([M('a', { length: 2 })], []));
run('syn/params-array-length-quirk-differs', S([M('a', [1, 2])], []), S([M('a', { length: 3 })], []));
run('syn/params-number-and-bool-no-keys', S([M('a', 5)], []), S([M('a', true)], []));
run('syn/params-zero-and-empty-string-falsy', S([M('a', 0)], []), S([M('a', { k: 1 })], []));
run('syn/params-string-unicode', S([M('a', 'aé😀')], []), S([M('a', 'aé😀')], []));
run('syn/params-string-unicode-differs-in-pair', S([M('a', 'a😀')], []), S([M('a', 'a😁')], []));
run('syn/param-order-index-keys-first', S([M('a', { b: 1, 2: 5, a: 2, 1: 3, 10: 4 })], []), S([M('a', { b: 2, 2: 6, a: 3, 1: 4, 10: 5 })], []));
run('syn/param-order-big-index-is-a-string-key', S([M('a', { b: 1, 4294967295: 5, 4294967294: 6, '01': 7 })], []), S([M('a', { b: 2, 4294967295: 6, 4294967294: 7, '01': 8 })], []));
run('syn/param-order-leading-zero-key', ((o) => S([M('a', o)], []))({ '01': 1, '1': 2, '0': 3 }), S([M('a', { '01': 2, '1': 3, '0': 4 })], []));
run('syn/param-negative-and-float-values', S([M('a', { x: 1 })], []), S([M('a', { x: -1.5e-7 })], []));
run('syn/param-large-and-tiny', S([M('a', { x: 1, y: 2 })], []), S([M('a', { x: 1e300, y: 5e-324 })], []));
run('syn/param-constructor-name', S([M('a', {})], []), S([M('a', { constructor: 1 })], []));
run('syn/param-tostring-name', S([M('a', {})], []), S([M('a', { toString: 2, hasOwnProperty: 3 })], []));
run('syn/param-proto-name-text', S([M('a', {})], []), null, ['{"format":"chains.patch","modules":[{"id":"a","params":{}}]}', '{"format":"chains.patch","modules":[{"id":"a","params":{"__proto__":4}}]}']);
run('syn/param-name-empty-and-spaces', S([M('a', {})], []), S([M('a', { '': 1, ' x ': 2 })], []));
run('syn/ids-number-vs-string', S([M(1, { x: 1 })], []), S([M('1', { x: 2 })], []));
run('syn/ids-number-param-change', S([M(1, { x: 1 })], []), S([M(1, { x: 2 })], []));
run('syn/ids-null-param-change', S([M(null, { x: 1 })], []), S([M(null, { x: 2 })], []));
run('syn/ids-missing-param-change', S([{ type: 'vca', params: { x: 1 } }], []), S([{ type: 'vca', params: { x: 2 } }], []));
run('syn/ids-bool-same', S([M(true, { x: 1 })], []), S([M(true, { x: 1 })], []));
run('syn/ids-array-never-equal', S([M([1], { x: 1 })], []), S([M([1], { x: 1 })], []));
run('syn/ids-empty-string', S([M('', { x: 1 })], []), S([M('', { x: 2 })], []));
run('syn/ids-minus-zero-vs-zero', S([M(0, { x: 1 })], []), null, ['{"format":"chains.patch","modules":[{"id":0,"params":{"x":1}}]}', '{"format":"chains.patch","modules":[{"id":-0,"params":{"x":1}}]}']);
run('syn/dup-ids-last-wins-change-in-last', S([M('a', { x: 1 }), M('a', { x: 2 })], []), S([M('a', { x: 1 }), M('a', { x: 3 })], []));
run('syn/dup-ids-first-never-read', S([M('a', { x: 1 }), M('a', { x: 2 })], []), S([M('a', { x: 9 }), M('a', { x: 2 })], []));
run('syn/dup-ids-prev-dup-next-single', S([M('a', { x: 1 }), M('a', { x: 2 })], []), S([M('a', { x: 2 })], []));
run('syn/dup-ids-next-dup-prev-single', S([M('a', { x: 2 })], []), S([M('a', { x: 5 }), M('a', { x: 2 })], []));
run('syn/dup-ids-order-by-first-position', S([M('a', { x: 1 }), M('b', { x: 1 }), M('a', { x: 1 })], []), S([M('a', { x: 2 }), M('b', { x: 2 }), M('a', { x: 3 })], []));
run('syn/dup-ids-text-last-wins', S([M('a', {}, { text: 'p' }), M('a', {}, { text: 'q' })], []), S([M('a', {}, { text: 'zz' }), M('a', {}, { text: 'q' })], []));
run('syn/dup-ids-all-removed-by-one', S([M('a', {}), M('a', {}), M('b', {})], []), S([M('a', {}), M('b', {})], []));
run('syn/dup-param-keys-raw-last-wins', null, null, ['{"format":"chains.patch","modules":[{"id":"a","params":{"x":1,"y":2}}]}', '{"format":"chains.patch","modules":[{"id":"a","params":{"x":9,"y":2,"x":1}}]}']);
run('syn/dup-param-keys-raw-position-of-first', null, null, ['{"format":"chains.patch","modules":[{"id":"a","params":{"p":1,"q":1}}]}', '{"format":"chains.patch","modules":[{"id":"a","params":{"p":0,"q":0,"p":2}}]}']);
run('syn/dup-module-keys-raw-id-last-wins', null, null, ['{"format":"chains.patch","modules":[{"id":"zz","id":"a","params":{"x":1}}]}', '{"format":"chains.patch","modules":[{"id":"a","params":{"x":2}}]}']);
run('syn/dup-top-keys-raw', null, null, ['{"format":"chains.patch","modules":[],"modules":[{"id":"a","params":{}}]}', '{"format":"chains.patch","modules":[{"id":"a","params":{}}]}']);
run('syn/raw-infinity-value', null, null, ['{"format":"chains.patch","modules":[{"id":"a","params":{"x":1}}]}', '{"format":"chains.patch","modules":[{"id":"a","params":{"x":1e999}}]}']);
run('syn/raw-infinity-both-same', null, null, ['{"format":"chains.patch","modules":[{"id":"a","params":{"x":1e999}}]}', '{"format":"chains.patch","modules":[{"id":"a","params":{"x":1e999}}]}']);
run('syn/raw-minus-zero-vs-zero-param', null, null, ['{"format":"chains.patch","modules":[{"id":"a","params":{"x":0}}]}', '{"format":"chains.patch","modules":[{"id":"a","params":{"x":-0}}]}']);
run('syn/raw-number-spelling-same-value', null, null, ['{"format":"chains.patch","modules":[{"id":"a","params":{"x":1}}]}', '{"format":"chains.patch","modules":[{"id":"a","params":{"x":1.0e0}}]}']);
run('syn/raw-lone-surrogate-text', null, null, ['{"format":"chains.patch","modules":[{"id":"a","params":{},"text":"\\ud800"}]}', '{"format":"chains.patch","modules":[{"id":"a","params":{},"text":"\\ud801"}]}']);
run('syn/raw-text-split-surrogate-at-200', null, null, ['{"format":"chains.patch","modules":[{"id":"a","params":{},"text":"' + 'x'.repeat(199) + '\\ud83d\\ude00"}]}', '{"format":"chains.patch","modules":[{"id":"a","params":{},"text":"' + 'x'.repeat(199) + '\\ud83d\\ude01"}]}']);
run('syn/raw-text-pair-fully-inside-200', null, null, ['{"format":"chains.patch","modules":[{"id":"a","params":{},"text":"' + 'x'.repeat(198) + '\\ud83d\\ude00"}]}', '{"format":"chains.patch","modules":[{"id":"a","params":{},"text":"' + 'x'.repeat(198) + '\\ud83d\\ude01"}]}']);
run('syn/cables-missing-both', S([], undefined), S([], undefined));
run('syn/cables-null-and-false', { format: 'chains.patch', modules: [], cables: null }, { format: 'chains.patch', modules: [], cables: false });
run('syn/cables-not-array-in-next-throws', S0, ed(S0, q => { q.cables = {}; }));
run('syn/cables-string-in-prev-throws', ed(S0, q => { q.cables = 'ab'; }), S0);
run('syn/cable-null-throws', S0, ed(S0, q => { q.cables.push(null); }));
run('syn/cable-primitive-throws', S0, ed(S0, q => { q.cables.push(7); }));
run('syn/cable-array-throws', S0, ed(S0, q => { q.cables.push([['a', 'out'], ['b', 'in']]); }));
run('syn/cable-without-from-throws', S0, ed(S0, q => { q.cables.push({ to: ['b', 'in'] }); }));
run('syn/cable-without-to-throws', S0, ed(S0, q => { q.cables.push({ from: ['b', 'in'] }); }));
run('syn/cable-from-null-throws', S0, ed(S0, q => { q.cables.push({ from: null, to: ['b', 'in'] }); }));
run('syn/cable-without-from-in-prev-throws', ed(S0, q => { q.cables.push({ to: ['b', 'in'] }); }), S0);
run('syn/cable-from-short-array-same', S([M('a', {})], [C(['a'], ['b'])]), S([M('a', {})], [C(['a'], ['b'])]));
run('syn/cable-from-string-units', S([M('a', {})], [C('ab', 'cd')]), S([M('a', {})], [C('ab', 'cd')]));
run('syn/cable-from-string-vs-array-same-key', S([M('a', {})], [C('ab', 'cd')]), S([M('a', {})], [C(['a', 'b'], ['c', 'd'])]));
run('syn/cable-key-number-vs-string-same', S([M('a', {})], [C([1, 'out'], [2, 'in'])]), S([M('a', {})], [C(['1', 'out'], ['2', 'in'])]));
run('syn/cable-key-float-formats', S([M('a', {})], [C([1.5e21, 1e-7], [0.1, -0])]), S([M('a', {})], [C(['1.5e+21', '1e-7'], ['0.1', '0'])]));
run('syn/cable-key-null-and-missing-parts', S([M('a', {})], [C([null], [null, null])]), S([M('a', {})], [C([], [null, undefined])]));
run('syn/cable-key-bool-parts', S([M('a', {})], [C([true, false], [true, false])]), S([M('a', {})], [C(['true', 'false'], ['true', 'false'])]));
run('syn/cable-key-nested-array-parts', S([M('a', {})], [C([[1, [2, null]], 'x'], ['p', 'q'])]), S([M('a', {})], [C(['1,2,', 'x'], ['p', 'q'])]));
run('syn/cable-key-object-parts', S([M('a', {})], [C([{}, { a: 1 }], ['p', 'q'])]), S([M('a', {})], [C(['[object Object]', '[object Object]'], ['p', 'q'])]));
run('syn/cable-from-object-with-index-members', S([M('a', {})], [C({ 0: 'a', 1: 'b' }, ['c', 'd'])]), S([M('a', {})], [C(['a', 'b'], ['c', 'd'])]));
run('syn/cable-from-number-undefined-parts', S([M('a', {})], [C(5, 6)]), S([M('a', {})], [C(true, false)]));
run('syn/cable-key-collision-with-dots', S([M('a', {})], [C(['a.b', 'c'], ['d', 'e'])]), S([M('a', {})], [C(['a', 'b.c'], ['d', 'e'])]));
run('syn/cable-dup-keys-last-fb-wins', S([M('a', {})], [C(['a', 'o'], ['b', 'i'], { fb: true }), C(['a', 'o'], ['b', 'i'])]), S([M('a', {})], [C(['a', 'o'], ['b', 'i'])]));
run('syn/cable-dup-keys-last-fb-wins-2', S([M('a', {})], [C(['a', 'o'], ['b', 'i']), C(['a', 'o'], ['b', 'i'], { fb: true })]), S([M('a', {})], [C(['a', 'o'], ['b', 'i'], { fb: true })]));
run('syn/cable-dup-removed-one', S([M('a', {})], [C(['a', 'o'], ['b', 'i']), C(['a', 'o'], ['b', 'i'])]), S([M('a', {})], [C(['a', 'o'], ['b', 'i'])]));
run('syn/cable-fb-true-vs-fb-true', S([M('a', {})], [C(['a', 'o'], ['b', 'i'], { fb: true })]), S([M('a', {})], [C(['a', 'o'], ['b', 'i'], { fb: true })]));
run('syn/cable-extra-fields-ignored', S([M('a', {})], [C(['a', 'o'], ['b', 'i'], { bend: 1, color: 'x' })]), S([M('a', {})], [C(['a', 'o'], ['b', 'i'], { bend: 2 })]));
run('syn/cable-third-element-ignored', S([M('a', {})], [C(['a', 'o', 'z'], ['b', 'i', 'y'])]), S([M('a', {})], [C(['a', 'o', 'q'], ['b', 'i', 'w'])]));
run('syn/module-null-in-both-throws', ed(S0, q => { q.modules.push(null); }), ed(S0, q => { q.modules.push(null); }));
run('syn/cable-null-in-both-throws', ed(S0, q => { q.cables.push(null); }), ed(S0, q => { q.cables.push(null); }));
run('syn/cable-without-from-in-both-throws', ed(S0, q => { q.cables.push({ to: ['b', 'in'] }); }), ed(S0, q => { q.cables.push({ to: ['b', 'in'] }); }));
run('syn/cable-without-to-in-both-throws', ed(S0, q => { q.cables.push({ from: ['b', 'in'] }); }), ed(S0, q => { q.cables.push({ from: ['b', 'in'] }); }));
run('syn/cable-key-null-part-equals-string-null', S([M('a', {})], [C([null, 'o'], ['b', 'i'])]), S([M('a', {})], [C(['null', 'o'], ['b', 'i'])]));
run('syn/cable-key-missing-part-equals-string-undefined', S([M('a', {})], [C(['a'], ['b', 'i'])]), S([M('a', {})], [C(['a', 'undefined'], ['b', 'i'])]));
run('syn/param-order-index-keys-raw-unsorted', null, null, ['{"format":"chains.patch","modules":[{"id":"a","params":{"b":1,"2":1,"a":1,"1":1,"10":1}}]}', '{"format":"chains.patch","modules":[{"id":"a","params":{"b":2,"2":2,"a":2,"1":2,"10":2}}]}']);
run('syn/param-order-index-keys-raw-with-duplicates', null, null, ['{"format":"chains.patch","modules":[{"id":"a","params":{"b":1,"7":1,"3":1}}]}', '{"format":"chains.patch","modules":[{"id":"a","params":{"b":2,"7":2,"x":5,"3":2,"7":9}}]}']);
run('syn/param-and-cable-both-change-is-structural', S0, ed(S0, q => { q.modules[0].params.x = 5; q.cables.pop(); }));
run('syn/param-and-text-change-is-structural', S([M('a', { x: 1 }, { text: 'p' })], []), S([M('a', { x: 2 }, { text: 'q' })], []));
run('syn/param-then-non-numeric-param-is-structural', S([M('a', { x: 1, y: 1 })], []), S([M('a', { x: 2, y: 'no' })], []));
run('syn/params-in-three-modules-order', S([M('c', { p: 1 }), M('a', { p: 1, q: 1 }), M('b', { p: 1 })], []), S([M('c', { p: 2 }), M('a', { q: 3, p: 2 }), M('b', { p: 3 })], []));
run('syn/params-order-follows-next-key-order', S([M('a', { p: 1, q: 1, r: 1 })], []), S([M('a', { r: 5, p: 5, q: 5 })], []));
run('syn/module-order-differs-ops-follow-next', S([M('a', { x: 1 }), M('b', { x: 1 })], []), S([M('b', { x: 2 }), M('a', { x: 2 })], []));
{ const many = n => S(Array.from({ length: n }, (_, i) => M('m' + i, { x: i, y: 0 })), Array.from({ length: n - 1 }, (_, i) => C(['m' + i, 'o'], ['m' + (i + 1), 'i'])));
  run('syn/many-modules-one-param', many(600), ed(many(600), q => { q.modules[599].params.y = 1; q.modules[7].params.x = 99; }));
  run('syn/many-modules-identical', many(600), many(600));
  run('syn/many-modules-last-removed', many(600), ed(many(600), q => { q.modules.pop(); }));
  run('syn/many-cables-one-fb', many(600), ed(many(600), q => { q.cables[450].fb = true; })); }
{ const wide = o => S([M('a', o)], []);
  const mk = f => { const o = {}; for (let i = 0; i < 1000; i++) o['k' + i] = f(i); return o; };
  run('syn/many-params-two-change', wide(mk(i => i)), wide(mk(i => (i === 5 || i === 999 ? -1 : i))));
  run('syn/many-params-all-equal', wide(mk(i => i)), wide(mk(i => i))); }

// the page's own seeds the live host will meet: a Track In / Chain Out / Stereo mixer patch edited the way the editor does
{ const P = bases.electro;
  run('electro/knob-drag-sequence-1', P, ed(P, q => { mod(q, 'smixer').params.l1 = 0.81; }));
  run('electro/knob-drag-sequence-2', ed(P, q => { mod(q, 'smixer').params.l1 = 0.81; }), ed(P, q => { mod(q, 'smixer').params.l1 = 0.82; }));
  run('electro/bus-level-and-pre-toggle', P, ed(P, q => { mod(q, 'chainOut').params.level = 0.5; mod(q, 'trackIn').params.pre = 1; })); }

// ---- the deep-equal departure (lead's ruling): objects / arrays as params ----
{ const D = (extra) => S([M('a', Object.assign({ n: 1, o: { x: 1, y: [1, 2, { z: 3 }], w: { v: null, u: 'q', t: true } }, arr: [1, [2, 3], { k: [4] }] }, extra || {}))], [], { st2: 1, ti2: 1 });
  const nest = (n, leaf) => { let v = leaf; for (let i = 0; i < n; i++) v = [v]; return v; };
  const deepP = (n, leaf) => S([M('a', { d: nest(n, leaf) })], []);
  run('deep/identical-object-and-array-params', D(), D());
  run('deep/identical-plus-numeric-change', D(), D({ n: 2 }));
  run('deep/identical-plus-string-change', D(), D({ n: 'x' }));
  run('deep/object-leaf-differs', D(), D({ o: { x: 1, y: [1, 2, { z: 4 }], w: { v: null, u: 'q', t: true } } }));
  run('deep/array-leaf-differs', D(), D({ arr: [1, [2, 3], { k: [5] }] }));
  run('deep/array-longer', D(), D({ arr: [1, [2, 3], { k: [4] }, 0] }));
  run('deep/object-extra-key', D(), D({ o: { x: 1, y: [1, 2, { z: 3 }], w: { v: null, u: 'q', t: true }, extra: 1 } }));
  run('deep/object-key-missing', D(), D({ o: { x: 1, y: [1, 2, { z: 3 }], w: { v: null, u: 'q' } } }));
  run('deep/key-order-differs-same', D(), D({ o: { w: { t: true, u: 'q', v: null }, y: [1, 2, { z: 3 }], x: 1 } }));
  run('deep/array-order-differs', D(), D({ arr: [[2, 3], 1, { k: [4] }] }));
  run('deep/null-vs-empty-object', D({ o: null }), D({ o: {} }));
  run('deep/empty-object-vs-empty-array', D({ o: {} }), D({ o: [] }));
  run('deep/number-vs-string-leaf', D({ o: [1] }), D({ o: ['1'] }));
  run('deep/minus-zero-leaf-raw', null, null, ['{"format":"chains.patch","modules":[{"id":"a","params":{"o":[0]}}]}', '{"format":"chains.patch","modules":[{"id":"a","params":{"o":[-0]}}]}']);
  run('deep/duplicate-keys-last-wins-raw', null, null, ['{"format":"chains.patch","modules":[{"id":"a","params":{"o":{"a":1,"a":2}}}]}', '{"format":"chains.patch","modules":[{"id":"a","params":{"o":{"a":2}}}]}']);
  run('deep/duplicate-keys-last-wins-differs-raw', null, null, ['{"format":"chains.patch","modules":[{"id":"a","params":{"o":{"a":2,"a":1}}}]}', '{"format":"chains.patch","modules":[{"id":"a","params":{"o":{"a":2}}}]}']);
  run('deep/duplicate-keys-in-next-last-wins-raw', null, null, ['{"format":"chains.patch","modules":[{"id":"a","params":{"o":{"a":2}}}]}', '{"format":"chains.patch","modules":[{"id":"a","params":{"o":{"a":1,"a":2}}}]}']);
  run('deep/duplicate-keys-in-next-last-wins-differs-raw', null, null, ['{"format":"chains.patch","modules":[{"id":"a","params":{"o":{"a":1}}}]}', '{"format":"chains.patch","modules":[{"id":"a","params":{"o":{"a":1,"a":2}}}]}']);
  run('deep/object-in-prev-number-in-next', S([M('a', { o: { x: 1 } })], []), S([M('a', { o: 5 })], []));
  run('deep/number-in-prev-object-in-next', S([M('a', { o: 5 })], []), S([M('a', { o: { x: 1 } })], []));
  run('deep/array-in-prev-missing-in-next-ignored', S([M('a', { o: [1] })], []), S([M('a', {})], []));
  run('deep/array-new-in-next', S([M('a', {})], []), S([M('a', { o: [1] })], []));
  run('deep/object-in-prev-string-in-next', S([M('a', { o: { x: 1 } })], []), S([M('a', { o: 'x' })], []));
  run('deep/nest-60-equal', deepP(60, 1), deepP(60, 1));
  run('deep/nest-64-equal-at-limit', deepP(64, 1), deepP(64, 1));
  run('deep/nest-65-past-limit-not-equal', deepP(65, 1), deepP(65, 1));
  run('deep/nest-200-past-limit-not-equal', deepP(200, 1), deepP(200, 1));
  run('deep/nest-64-leaf-differs', deepP(64, 1), deepP(64, 2));
  run('deep/nest-objects-64-equal', S([M('a', { d: (() => { let v = 1; for (let i = 0; i < 64; i++) v = { k: v }; return v; })() })], []), S([M('a', { d: (() => { let v = 1; for (let i = 0; i < 64; i++) v = { k: v }; return v; })() })], []));
  run('deep/nest-objects-65-not-equal', S([M('a', { d: (() => { let v = 1; for (let i = 0; i < 65; i++) v = { k: v }; return v; })() })], []), S([M('a', { d: (() => { let v = 1; for (let i = 0; i < 65; i++) v = { k: v }; return v; })() })], []));
  run('deep/params-array-of-objects-equal', S([M('a', [{ x: 1 }, [2]])], []), S([M('a', [{ x: 1 }, [2]])], []));
  run('deep/params-array-of-objects-differs', S([M('a', [{ x: 1 }, [2]])], []), S([M('a', [{ x: 1 }, [3]])], [])); }

if (cases.length < 60) { console.error('chains-diff-dump: only ' + cases.length + ' cases'); process.exit(2); }
const names = new Set(); for (const c of cases) { if (names.has(c.name)) { console.error('duplicate case name ' + c.name); process.exit(2); } names.add(c.name); }

const doc = { header: { tool: 'native/tools/chains-diff-dump.mjs', note: 'GENERATED: the page\'s chnDiff (index.html) run on each pair; expectKind / expectParams are its ops classified (Same / Params / Structural). Do not edit: run the tool.', cases: cases.length }, cases };
const text = JSON.stringify(doc).replace(/\},\{"name"/g, '},\n{"name"') + '\n';
const kinds = {}; for (const c of cases) kinds[c.expectKind] = (kinds[c.expectKind] || 0) + 1;
const thr = cases.filter(c => c.throws).length;
if (process.argv.includes('--list')) { for (const c of cases) console.log(c.expectKind.padEnd(10), c.throws ? 'THROWS' : '      ', c.name, c.expectParams.length ? c.expectParams.map(p => p.id + '.' + p.name + '=' + p.v).join(' ') : ''); process.exit(0); }
if (process.argv.includes('--check')) {
  const cur = fs.existsSync(OUT) ? fs.readFileSync(OUT, 'utf8') : '';
  if (cur !== text) { console.error('chains-diff-dump: chains_diff_cases.json is not current (the page\'s chnDiff or this tool changed): run node native/tools/chains-diff-dump.mjs'); process.exit(1); }
  console.log('chains-diff-dump: chains_diff_cases.json is current (' + cases.length + ' cases, ' + JSON.stringify(kinds) + ', ' + thr + ' throw)'); process.exit(0);
}
fs.writeFileSync(OUT, text);
console.log('chains-diff-dump: wrote ' + cases.length + ' cases (' + JSON.stringify(kinds) + ', ' + thr + ' throw) to ' + path.relative(ROOT, OUT));
