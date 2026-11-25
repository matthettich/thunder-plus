import fs from 'node:fs';
const [expF, gotF, inF] = process.argv.slice(2);
const E = fs.readFileSync(expF, 'utf8').split('\n'), G = fs.readFileSync(gotF, 'utf8').split('\n'), I = fs.readFileSync(inF, 'utf8').split('\n');
let bad = 0; const n = E.length;
const norm = l => { l = l.replace(/\uFFFD+/g, '?'); const p = l.split('\t'); return [p[0], ...p.slice(1).map(x => { const q = x.split('\x01'); return q[0] + '|' + q[1] + '|' + Object.is(parseFloat(q[2]), -0) + parseFloat(q[2]); })].join('\t'); };
let kinds = { Same: 0, Params: 0, Structural: 0 };
for (let i = 0; i < n; i++) { if (E[i] === '' && G[i] === undefined) continue; kinds[E[i].split('\t')[0]]++; if (norm(E[i]) !== norm(G[i] || '')) { if (bad++ < 8) console.log('MISMATCH line', i + 1, '\n expect', JSON.stringify(E[i]), '\n got   ', JSON.stringify(G[i]), '\n input ', I[i].slice(0, 1500)); } }
console.log('cases', n - 1, 'mismatches', bad, JSON.stringify(kinds));
