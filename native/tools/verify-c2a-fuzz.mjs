import fs from 'node:fs'; import vm from 'node:vm';
const root = '/home/claude/wt/verify-c2a-src';
const html = fs.readFileSync(root + '/index.html', 'utf8');
const ctx = vm.createContext({}); vm.runInContext(/^const chnClone = [^\n]*;$/m.exec(html)[0] + '\n' + /^function chnDiff\(prev, next\)\{\n[\s\S]*?\n\}$/m.exec(html)[0] + '\nthis.chnDiff = chnDiff;', ctx);
const base = JSON.stringify(JSON.parse(fs.readFileSync(root + '/sounds/ELECTRO.thunder', 'utf8')).song.chainPatch);
let s = 12345; const rnd = () => { s |= 0; s = s + 0x6D2B79F5 | 0; let t = Math.imul(s ^ s >>> 15, 1 | s); t = t + Math.imul(t ^ t >>> 7, 61 | t) ^ t; return ((t ^ t >>> 14) >>> 0) / 4294967296; };
const toks = [...base.matchAll(/"(?:[^"\\]|\\.)*"|-?\d+(?:\.\d+)?(?:e[+-]?\d+)?|true|false|null|[\[\]{}:,]/g)].map(m => [m.index, m[0].length]);
const rep = ['null', 'true', '0', '-0', '1e999', '[]', '{}', '"x"', '[1]', '{"a":1}', '""', '"\\ud83d"', '5', '[[]]', '{"1":2,"0":3}', ',', '', '{', ']', '"constructor"', '"length"', '-1', '1e-400', '[null]'];
const ri = n => Math.floor(rnd() * n);
function mut() { let t = base; const k = 1 + ri(3); for (let i = 0; i < k; i++) { const [ix, len] = toks[ri(toks.length)]; const r = rep[ri(rep.length)]; t = t.slice(0, ix) + r + t.slice(ix + len); } return t; }
const L = [], E = []; let ok = 0;
while (ok < 20000) { const p = rnd() < .4 ? base : mut(), n = mut(); let pp, nn; try { pp = JSON.parse(p); nn = JSON.parse(n); } catch (e) { continue; } ok++; L.push(JSON.stringify({ p, n }));
  let ops; try { ops = ctx.chnDiff(pp, nn); } catch (e) { E.push('Structural'); continue; }
  if (!ops.length) E.push('Same'); else if (ops.every(o => o.op === 'param' && typeof o.value === 'number' && isFinite(o.value) && typeof o.id === 'string')) E.push(['Params', ...ops.map(o => o.id + '\x01' + o.name + '\x01' + (Object.is(o.value, -0) ? '-0' : String(o.value)))].join('\t')); else E.push('Structural'); }
fs.writeFileSync('inF.jsonl', L.join('\n') + '\n'); fs.writeFileSync('expF.jsonl', E.join('\n') + '\n');
