import fs from 'node:fs';
const mk = (nm, nc, tweak) => { const mods = [], cab = []; for (let i = 0; i < nm; i++) mods.push({ id: 'm' + i, type: 'gain', params: { a: i, b: 1 }, text: 'x' }); for (let i = 0; i < nc; i++) cab.push({ from: ['m' + i, 'out'], to: ['m' + ((i + 1) % nm), 'in'] }); return { format: 'chains.patch', modules: mods, cables: cab }; };
const A = mk(20000, 20000); const B = mk(20000, 20000); B.modules[19999].params.a = 7;
const w = (n, o) => fs.writeFileSync(n, JSON.stringify(o));
const L = [];
L.push(JSON.stringify({ p: JSON.stringify(A), n: JSON.stringify(A) }));
L.push(JSON.stringify({ p: JSON.stringify(A), n: JSON.stringify(B) }));
// all cables dup keys
const C = mk(20000, 20000); for (const c of C.cables) { c.from = ['x', 'y']; c.to = ['z', 'w']; }
L.push(JSON.stringify({ p: JSON.stringify(C), n: JSON.stringify(C) }));
// all same module id
const D = mk(20000, 0); for (const m of D.modules) m.id = 'same';
L.push(JSON.stringify({ p: JSON.stringify(D), n: JSON.stringify(D) }));
// one module with 100k params
const E = { format: 'chains.patch', modules: [{ id: 'a', params: Object.fromEntries(Array.from({ length: 100000 }, (_, i) => ['k' + i, i])) }], cables: [] };
const E2 = JSON.parse(JSON.stringify(E)); E2.modules[0].params.k99999 = -1;
L.push(JSON.stringify({ p: JSON.stringify(E), n: JSON.stringify(E2) }));
// integer keys 100k
const F = { format: 'chains.patch', modules: [{ id: 'a', params: Object.fromEntries(Array.from({ length: 100000 }, (_, i) => [String(99999 - i), i])) }], cables: [] };
L.push(JSON.stringify({ p: JSON.stringify(F), n: JSON.stringify(F) }));
// huge string params; param string of 1MiB; text 1MiB
const G = { format: 'chains.patch', modules: [{ id: 'a', params: 'x'.repeat(900000), text: 'y'.repeat(900000) }], cables: [] };
L.push(JSON.stringify({ p: JSON.stringify(G), n: JSON.stringify(G) }));
// deep nesting 250 in params, equal and not, and arrays
const nest = d => { let s = '1'; for (let i = 0; i < d; i++) s = '[' + s + ']'; return s; };
const nestO = d => { let s = '1'; for (let i = 0; i < d; i++) s = '{"a":' + s + '}'; return s; };
for (const d of [60, 64, 65, 100, 250]) for (const f of [nest, nestO]) { const t = '{"format":"chains.patch","modules":[{"id":"a","params":{"k":' + f(d) + '}}],"cables":[]}'; L.push(JSON.stringify({ p: t, n: t })); }
// deep from arrays in cables
const dd = '{"format":"chains.patch","modules":[],"cables":[{"from":' + nest(250) + ',"to":' + nest(250) + '}]}';
L.push(JSON.stringify({ p: dd, n: dd }));
// 300 deep -> parse error expected
L.push(JSON.stringify({ p: '{"format":"chains.patch","modules":[{"id":"a","params":{"k":' + nest(300) + '}}],"cables":[]}', n: null }));
// many big arrays in cable from (quadratic toString?)
const H = { format: 'chains.patch', modules: [], cables: Array.from({ length: 200 }, (_, i) => ({ from: [Array.from({ length: 2000 }, (_, j) => j), i], to: ['a', 'b'] })) };
L.push(JSON.stringify({ p: JSON.stringify(H), n: JSON.stringify(H) }));
// deep-equal of big objects: 20000 modules each with object param
const I = { format: 'chains.patch', modules: Array.from({ length: 20000 }, (_, i) => ({ id: 'm' + i, params: { o: { a: [1, 2, { b: 3 }], c: 'x' } } })), cables: [] };
L.push(JSON.stringify({ p: JSON.stringify(I), n: JSON.stringify(I) }));
// one param, object with 200k keys
const K = { format: 'chains.patch', modules: [{ id: 'a', params: { o: Object.fromEntries(Array.from({ length: 150000 }, (_, i) => ['k' + i, i])) } }], cables: [] };
L.push(JSON.stringify({ p: JSON.stringify(K), n: JSON.stringify(K) }));
// huge flat array param
const M = { format: 'chains.patch', modules: [{ id: 'a', params: { o: Array.from({ length: 300000 }, (_, i) => i) } }], cables: [] };
L.push(JSON.stringify({ p: JSON.stringify(M), n: JSON.stringify(M) }));
fs.writeFileSync('big.jsonl', L.join('\n') + '\n');
