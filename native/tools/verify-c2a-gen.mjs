// verify-c2a: independent differential generator. usage: node verify-c2a-gen.mjs <repo root> <seed> <count> <out.jsonl> <expect.jsonl>
import fs from 'node:fs'; import vm from 'node:vm';
const [root, seedS, countS, outF, expF] = process.argv.slice(2);
const html = fs.readFileSync(root + '/index.html', 'utf8').replace(/\r\n/g, '\n');
const cloneSrc = /^const chnClone = [^\n]*;$/m.exec(html)[0];
const diffSrc = /^function chnDiff\(prev, next\)\{\n[\s\S]*?\n\}$/m.exec(html)[0];
const ctx = vm.createContext({}); vm.runInContext(cloneSrc + '\n' + diffSrc + '\nthis.chnDiff = chnDiff;', ctx);
const base = JSON.parse(fs.readFileSync(root + '/sounds/ELECTRO.thunder', 'utf8')).song.chainPatch;
let s = (+seedS) >>> 0; const rnd = () => { s |= 0; s = s + 0x6D2B79F5 | 0; let t = Math.imul(s ^ s >>> 15, 1 | s); t = t + Math.imul(t ^ t >>> 7, 61 | t) ^ t; return ((t ^ t >>> 14) >>> 0) / 4294967296; };
const ri = n => Math.floor(rnd() * n), pick = a => a[ri(a.length)];
// independent deep equality: canonical string with sorted keys, depth-limited
function canon(v, lvl) { if (v === null || typeof v !== 'object') return typeof v === 'number' ? 'n' + (v === 0 ? 0 : v) : typeof v === 'string' ? 's' + JSON.stringify(v) : 'x' + v;
  if (lvl > 64) return null; const parts = []; if (Array.isArray(v)) { for (const e of v) { const c = canon(e, lvl + 1); if (c === null) return null; parts.push(c); } return '[' + parts.join(',') + ']'; }
  for (const k of Object.keys(v).sort()) { const c = canon(v[k], lvl + 1); if (c === null) return null; parts.push(JSON.stringify(k) + ':' + c); } return '{' + parts.join(',') + '}'; }
const deq = (a, b) => { if (Array.isArray(a) !== Array.isArray(b)) return false; const x = canon(a, 1), y = canon(b, 1); return x !== null && y !== null && x === y; };
function expect(pT, nT) {
  const prev = pT === null ? undefined : JSON.parse(pT), next = nT === null ? undefined : JSON.parse(nT);
  let ops; try { ops = ctx.chnDiff(prev, next); } catch (e) { return ['Structural']; }
  // prev value for deep compare: last module with that id (Map), params||{}
  const kept = ops.filter(o => { if (o.op !== 'param' || o.value === null || typeof o.value !== 'object') return true; let q; for (const m of prev.modules) if (m.id === o.id) q = m; const pv = (q.params || {})[o.name]; return !(pv !== null && typeof pv === 'object' && deq(pv, o.value)); });
  if (!kept.length) return ['Same'];
  if (kept.every(o => o.op === 'param' && typeof o.value === 'number' && isFinite(o.value) && typeof o.id === 'string')) return ['Params', ...kept.map(o => o.id + '\x01' + o.name + '\x01' + (Object.is(o.value, -0) ? '-0' : String(o.value)))];
  return ['Structural'];
}
const J = x => JSON.parse(JSON.stringify(x));
const lits = ['@NULL', true, false, 0, 1, -1, 1.5, '"1"', '', '0', [], {}, [1, 2], { a: 1 }, { a: { b: [1] } }, 'x', 1e21, 1e-7, 123456789012345680000, 0.1, 9007199254740993, '@-0', '@1e999', '@-1e999', '@1.0', '@1E2', '@5e-324', '@1e-400', '\ud83d', '😀', 'é', [null], [[1],[2]], { '1': 1, '0': 2 }, ' 1'];
const lit = () => { const l = pick(lits); return l === '@NULL' ? null : J(l); };
const nameKeys = ['01', '-1', '4294967295', '4294967294', '1e3', ' 1', '0', '1', '2', '10', '9', 'a', 'b', 'constructor', 'toString', '__proto__', 'length', 'hasOwnProperty', '', '1.5', '+1', '9007199254740991', '123456789012', '00'];
function allNodes(o, path = [], out = []) { out.push(path); if (o && typeof o === 'object') for (const k of Object.keys(o)) allNodes(o[k], path.concat(k), out); return out; }
function getp(o, p) { for (const k of p) o = o[k]; return o; }
function mutate(doc) {
  const kind = rnd() < .55 ? pick([1,1,1,2,2,3,15,5,4,13,0]) : ri(16);
  const nodes = allNodes(doc);
  const mods = () => doc.modules && Array.isArray(doc.modules) ? doc.modules : [];
  const m = () => pick(mods());
  try {
    switch (kind) {
      case 0: { const p = pick(nodes); if (!p.length) break; getp(doc, p.slice(0, -1))[p[p.length - 1]] = lit(); break; }
      case 1: { const mm = m(); if (mm && mm.params) { const ks = Object.keys(mm.params); if (ks.length) mm.params[pick(ks)] = rnd() < .5 ? ri(100) : rnd() * 100; } break; }
      case 2: { const mm = m(); if (mm) { if (!mm.params || typeof mm.params !== 'object') mm.params = {}; mm.params[pick(nameKeys)] = lit(); } break; }
      case 3: { const mm = m(); if (mm && mm.params) { const ks = Object.keys(mm.params); if (ks.length) delete mm.params[pick(ks)]; } break; }
      case 4: { const mm = m(); if (mm) mm.text = pick(['', 'a*k1', 'x'.repeat(199) + '😀' + 'y', 'x'.repeat(198) + '😀😀', 'x'.repeat(250), 'é'.repeat(201), '\ud83d' + 'x'.repeat(199), 'x'.repeat(200) + 'zzz', 5, null]); break; }
      case 5: { const c = pick(doc.cables || []); if (c) c.fb = pick([true, false, 1, 'true', null, undefined]); break; }
      case 6: { doc.modules = (doc.modules || []).slice(); doc.modules.splice(ri(doc.modules.length + 1), 0, J(pick(doc.modules))); break; }
      case 7: { if (mods().length) doc.modules.splice(ri(doc.modules.length), 1); break; }
      case 8: { const c = pick(doc.cables || []); if (c) { const w = pick(['from', 'to']); c[w] = J(pick([['a', 'b'], 'ab', [1, 2], [null, 1], [[1, 2], [null]], { 0: 'q', 1: 'r' }, 5, null, [], ['x'], [true, false], [{}, []], '😀', J(c[w])])); } break; }
      case 9: { if (doc.cables && doc.cables.length) doc.cables.splice(ri(doc.cables.length), 1); break; }
      case 10: { doc.cables = (doc.cables || []).concat([J(pick(doc.cables || [{ from: ['a', 'b'], to: ['c', 'd'] }]))]); break; }
      case 11: { const mm = m(); if (mm) mm.id = pick([1, '1', 1.0, null, true, [], {}, 'dup', '', undefined, mods()[0] && mods()[0].id]); break; }
      case 12: { const mm = m(); if (mm) mm.params = J(pick(['abc', [1, 2, 3], 5, null, true, false, 0, '', [], {}, 'a😀b', { '2': 1, '1': 2, 'z': 3 }])); break; }
      case 13: { doc[pick(['st2', 'ti2'])] = pick([0, 1, '', '0', null, [], {}, false, true, undefined, -0]); break; }
      case 14: { doc[pick(['modules', 'cables', 'format'])] = pick([undefined, null, 'x', 5, {}, [], 'chains.patch', false, [null], [5], ['s'], [[]]]); break; }
      case 15: { const mm = m(); if (mm && mm.params) { const ks = Object.keys(mm.params); const k = pick(ks.length ? ks : ['q']); mm.params[k] = J(pick([{ a: 1, b: [1, { c: 2 }] }, [1, [2, 3]], { b: 1, a: 1 }])); } break; }
    }
  } catch (e) { }
}
function ser(o) { let t; try { t = JSON.stringify(o); } catch (e) { t = '{}'; } if (t === undefined) return null;
  return t.replace(/"@(-0|1e999|-1e999|1\.0|1E2|5e-324|1e-400)"/g, '$1'); }
const hand = [ // raw texts
  ['{"format":"chains.patch","modules":[{"id":"a","params":{"x":1,"x":2}}],"cables":[]}', '{"format":"chains.patch","modules":[{"id":"a","params":{"x":1,"x":3}}],"cables":[]}'],
  ['{"format":"chains.patch","modules":[{"id":"a","params":{"x":1}}],"cables":[]}', '{"format":"chains.patch","modules":[{"id":"a","params":{"x":1}},{"id":"a","params":{"x":9}}],"cables":[]}'],
  ['{"format":"chains.patch","modules":[{"id":"a","text":"\\ud83d"}],"cables":[]}', '{"format":"chains.patch","modules":[{"id":"a","text":"\\ud83d\\ude00"}],"cables":[]}'],
  ['{"format":"chains.patch","modules":[{"id":"\\ud83d\\ude00"}],"cables":[]}', '{"format":"chains.patch","modules":[{"id":"😀"}],"cables":[]}'],
  ['{"format":"chains.patch","modules":[{"id":"a","params":{"k":0}}],"cables":[]}', '{"format":"chains.patch","modules":[{"id":"a","params":{"k":-0}}],"cables":[]}'],
  ['{"format":"chains.patch","modules":[{"id":"a","params":{"k":1}}],"cables":[]}', '{"format":"chains.patch","modules":[{"id":"a","params":{"k":1.0}}],"cables":[]}'],
  ['{"format":"chains.patch","modules":[{"id":"a","params":{"k":9007199254740993}}],"cables":[]}', '{"format":"chains.patch","modules":[{"id":"a","params":{"k":9007199254740992}}],"cables":[]}'],
  ['{"format":"chains.patch","modules":[{"id":"a","params":{"k":0.1}}],"cables":[]}', '{"format":"chains.patch","modules":[{"id":"a","params":{"k":0.10000000000000001}}],"cables":[]}'],
  ['{"format":"chains.patch","modules":[{"id":"a","params":{"k":1e-400}}],"cables":[]}', '{"format":"chains.patch","modules":[{"id":"a","params":{"k":0}}],"cables":[]}'],
  ['{"format":"chains.patch","modules":[{"id":"a","params":{"k":{"x":1,"x":2,"y":[1]}}}],"cables":[]}', '{"format":"chains.patch","modules":[{"id":"a","params":{"k":{"y":[1],"x":2}}}],"cables":[]}'],
  ['{"format":"chains.patch","modules":[{"id":"a","params":{"k":{"x":2,"x":1}}}],"cables":[]}', '{"format":"chains.patch","modules":[{"id":"a","params":{"k":{"x":1}}}],"cables":[]}'],
  ['{"format":"chains.patch","modules":[{"id":"a","params":{"k":[]}}],"cables":[]}', '{"format":"chains.patch","modules":[{"id":"a","params":{"k":{}}}],"cables":[]}'],
  ['{"format":"chains.patch","modules":[{"id":"a","params":{"k":{"a":1}}}],"cables":[]}', '{"format":"chains.patch","modules":[{"id":"a","params":{"k":5}}],"cables":[]}'],
  ['{"format":"chains.patch","modules":[{"id":"a","params":{"k":5}}],"cables":[]}', '{"format":"chains.patch","modules":[{"id":"a","params":{"k":{"a":1}}}],"cables":[]}'],
  ['{"format":"chains.patch","modules":[{"id":"a","params":{"b":1,"1":1,"a":1,"0":1,"4294967295":1,"4294967294":1}}],"cables":[]}', '{"format":"chains.patch","modules":[{"id":"a","params":{"b":2,"1":2,"a":2,"0":2,"4294967295":2,"4294967294":2,"9007199254740991":2,"01":2,"-1":2,"1e3":2," 1":2}}],"cables":[]}'],
  ['{"format":"chains.patch","modules":[{"id":"a","params":{}}],"cables":[{"from":"ab","to":"cd"}]}', '{"format":"chains.patch","modules":[{"id":"a","params":{}}],"cables":[{"from":["a","b"],"to":["c","d"]}]}'],
  ['{"format":"chains.patch","modules":[],"cables":[{"from":[1e21,1e-7],"to":[0.1,-0]}]}', '{"format":"chains.patch","modules":[],"cables":[{"from":["1e+21","1e-7"],"to":["0.1","0"]}]}'],
  ['{"format":"chains.patch","modules":[],"cables":[{"from":[[1,null,[2]],{"a":1}],"to":[true,null]}]}', '{"format":"chains.patch","modules":[],"cables":[{"from":["1,,2","[object Object]"],"to":["true","null"]}]}'],
  ['{"format":"chains.patch","modules":[],"cables":[{"from":[1,2],"to":[3,4]}]}', '{"format":"chains.patch","modules":[],"cables":[{"from":["1","2"],"to":["3","4"],"fb":true}]}'],
  [null, null], ['{"format":"chains.patch","modules":[]}', null], [null, '{"format":"chains.patch","modules":[]}'], ['{"format":"chains.patch","modules":[]}', '0'], ['{"format":"chains.patch","modules":[]}', '5'], ['{"format":"chains.patch","modules":[]}', '"abc"'], ['{"format":"chains.patch","modules":[]}', '[]'], ['{"format":"chains.patch","modules":[]}', '{}'],
  ['{"format":"chains.patch","modules":[],"st2":"0"}', '{"format":"chains.patch","modules":[],"st2":[]}'], ['{"format":"chains.patch","modules":[],"st2":0}', '{"format":"chains.patch","modules":[],"st2":-0}'], ['{"format":"chains.patch","modules":[],"ti2":""}', '{"format":"chains.patch","modules":[],"ti2":null}'],
];

const P = (mods, extra) => '{"format":"chains.patch","modules":' + mods + (extra || ',"cables":[]') + '}';
const M1 = (params, extra) => P('[{"id":"a","params":' + params + (extra || '') + '}]');
const hand2 = [
 [P('[]', ',"st2":-1,"cables":[]'), P('[]')], [P('[]', ',"st2":-1,"cables":[]'), P('[]', ',"st2":-5,"cables":[]')], [P('[]', ',"ti2":-0.5,"cables":[]'), P('[]')], [P('[{"id":true}]'), P('[{"id":false}]')], [P('[{"id":null}]'), P('[{"id":false}]')], [P('[{"id":0}]'), P('[{"id":false}]')], [P('[{"id":""}]'), P('[{"id":null}]')], [P('[{"id":"null"}]'), P('[{"id":null}]')], [P('[{"id":"1e21"}]'), P('[{"id":1e21}]')], [P('[{"id":"0.0000001"}]'), P('[{"id":1e-7}]')],
 [M1('"abc"'), M1('"abd"')], [M1('"abc"'), M1('"abc"')], [M1('"a😀"'), M1('"a😁"')], [M1('"ab"'), M1('"abc"')], [M1('[1,2,3]'), M1('[1,2,4]')], [M1('[1,2,3]'), M1('[1,2,3,4]')],
 [M1('[1,2,3]'), M1('{"0":1,"1":2,"2":9}')], [M1('{"0":1,"1":2}'), M1('[1,2]')], [M1('{"length":3}'), M1('[1,2,3]')], [M1('[1,2,3]'), M1('{"length":3}')], [M1('{"length":3}'), M1('{"length":4}')],
 [M1('5'), M1('{"a":1}')], [M1('{"a":1}'), M1('5')], [M1('true'), M1('{"a":1}')], [M1('{"a":1}'), M1('true')], [M1('null'), M1('{"a":1}')], [M1('{"a":1}'), M1('null')], [M1('0'), M1('{"a":1}')], [M1('{"a":1}'), M1('""')],
 [M1('{"a":null}'), M1('{"a":null}')], [M1('{}'), M1('{"a":null}')], [M1('{"a":null}'), M1('{"a":0}')], [M1('{"a":0}'), M1('{"a":null}')], [M1('{"a":false}'), M1('{"a":false}')], [M1('{"a":false}'), M1('{"a":true}')], [M1('{"a":"x"}'), M1('{"a":"x"}')], [M1('{"a":"x"}'), M1('{"a":"y"}')],
 [M1('{"constructor":1}'), M1('{"constructor":1}')], [M1('{}'), M1('{"constructor":1}')], [M1('{"constructor":1}'), M1('{"constructor":2}')], [M1('{}'), M1('{"__proto__":1}')], [M1('{"__proto__":1}'), M1('{"__proto__":1}')], [M1('{"__proto__":{"a":1}}'), M1('{"__proto__":{"a":1}}')], [M1('{"hasOwnProperty":{}}'), M1('{"hasOwnProperty":{}}')],
 [M1('"abc"'), M1('{"0":"a","1":"b","2":"c"}')], [M1('{"0":"a"}'), M1('"abc"')], [M1('"abc"'), M1('"abc"', ',"text":"t"')], [M1('{}'), M1('{"a":1e5}')], [M1('{"a":1e5}'), M1('{"a":100000}')], [M1('{"a":1}'), M1('{"a":1e999}')], [M1('{"a":1}'), M1('{"a":-1e999}')],
 [M1('{"a":[1,2]}'), M1('{"a":[1,2]}')], [M1('{"a":[1,2]}'), M1('{"a":[2,1]}')], [M1('{"a":{"x":1,"y":2}}'), M1('{"a":{"y":2,"x":1}}')], [M1('{"a":{"x":1}}'), M1('{"a":{"x":1,"y":undefined}}'.replace(',"y":undefined',''))],
 [M1('{"a":{"x":null}}'), M1('{"a":{}}')], [M1('{"a":{"x":0}}'), M1('{"a":{"x":-0}}')], [M1('{"a":[0]}'), M1('{"a":[-0]}')], [M1('{"a":[1e999]}'), M1('{"a":[1e999]}')], [M1('{"a":["a"]}'), M1('{"a":{"0":"a"}}')], [M1('{"a":[[]]}'), M1('{"a":[{}]}')],
 [P('[1]'), P('[1]')], [P('[1]'), P('[2]')], [P('["s"]'), P('["s"]')], [P('[[]]'), P('[[]]')], [P('[{"id":1,"params":{"a":1}}]'), P('[{"id":1,"params":{"a":2}}]')], [P('[{"id":1,"params":{"a":1}}]'), P('[{"id":"1","params":{"a":2}}]')], [P('[{"id":true,"params":{"a":1}}]'), P('[{"id":true,"params":{"a":2}}]')], [P('[{"params":{"a":1}}]'), P('[{"params":{"a":2}}]')], [P('[{"id":null,"params":{"a":1}}]'), P('[{"id":null,"params":{"a":2}}]')], [P('[{"id":[1],"params":{"a":1}}]'), P('[{"id":[1],"params":{"a":2}}]')], [P('[{"id":1.0,"params":{"a":1}}]'), P('[{"id":1,"params":{"a":2}}]')], [P('[{"id":-0}]'), P('[{"id":0}]')], [P('[{"id":"a"},null]'), P('[{"id":"a"},null]')],
 [P('[{"id":"a"}]'), P('{"0":{"id":"a"}}')], [P('[{"id":"a"}]'), P('"x"')], [P('[{"id":"a"}]'), P('5')], [P('[]'), P('0')], [P('[]'), P('[]').replace('"modules":[]','"modules":0')], [P('[]'), P('[]').replace('"modules":[]','"modules":""')], [P('[]'), P('[]').replace('"modules":[]','"modules":null')], [P('[]'), P('[]').replace('"modules":[]','"modules":false')], [P('[]'), P('[]').replace('"modules":[]','"modules":"x"')],
 [P('[]'), P('[]', ',"cables":null')], [P('[]'), P('[]', ',"cables":"x"')], [P('[]'), P('[]', ',"cables":{}')], [P('[]', ',"cables":null'), P('[]', ',"cables":0')], [P('[]', ',"cables":[null]'), P('[]', ',"cables":[null]')], [P('[]', ',"cables":[5]'), P('[]', ',"cables":[5]')], [P('[]', ',"cables":[[]]'), P('[]', ',"cables":[[]]')], [P('[]', ',"cables":[{}]'), P('[]', ',"cables":[{}]')], [P('[]', ',"cables":[{"from":null,"to":[]}]'), P('[]', ',"cables":[{"from":null,"to":[]}]')], [P('[]', ',"cables":[{"from":0,"to":false}]'), P('[]', ',"cables":[{"from":0,"to":false}]')], [P('[]', ',"cables":[{"from":"","to":""}]'), P('[]', ',"cables":[{"from":"","to":""}]')], [P('[]', ',"cables":[{"from":[],"to":[]}]'), P('[]', ',"cables":[{"from":[null,null],"to":[]}]')], [P('[]', ',"cables":[{"from":[],"to":[]}]'), P('[]', ',"cables":[{"from":[[],[]],"to":[]}]')],
 [P('[]', ',"cables":[{"from":[1],"to":[2],"fb":true}]'), P('[]', ',"cables":[{"from":[1],"to":[2]}]')], [P('[]', ',"cables":[{"from":[1],"to":[2],"fb":1}]'), P('[]', ',"cables":[{"from":[1],"to":[2],"fb":true}]')], [P('[]', ',"cables":[{"from":[1],"to":[2],"fb":"a"}]'), P('[]', ',"cables":[{"from":[1],"to":[2],"fb":false}]')],
 [P('[]', ',"cables":[{"from":[1,2],"to":[3,4]},{"from":["1","2"],"to":["3","4"],"fb":true}]'), P('[]', ',"cables":[{"from":["1","2"],"to":["3","4"],"fb":true}]')],
 [P('[{"id":"a","text":"' + 'x'.repeat(200) + 'AAA"}]'), P('[{"id":"a","text":"' + 'x'.repeat(200) + 'BBB"}]')], [P('[{"id":"a","text":"' + 'x'.repeat(199) + '😀"}]'), P('[{"id":"a","text":"' + 'x'.repeat(199) + '😁"}]')], [P('[{"id":"a","text":"' + 'x'.repeat(198) + '😀"}]'), P('[{"id":"a","text":"' + 'x'.repeat(198) + '😁"}]')], [P('[{"id":"a","text":"' + 'é'.repeat(200) + 'Z"}]'), P('[{"id":"a","text":"' + 'é'.repeat(200) + 'Y"}]')], [P('[{"id":"a","text":"' + '€'.repeat(201) + '"}]'), P('[{"id":"a","text":"' + '€'.repeat(200) + '"}]')], [P('[{"id":"a","text":""}]'), P('[{"id":"a"}]')], [P('[{"id":"a","text":5}]'), P('[{"id":"a"}]')], [P('[{"id":"a","text":null}]'), P('[{"id":"a","text":5}]')], [P('[{"id":"a","text":["x"]}]'), P('[{"id":"a"}]')],
 ['{"format":"chains.patch","modules":[],"cables":[],"st2":1}', '{"format":"chains.patch","modules":[],"cables":[],"st2":2}'], ['{"format":"chains.patch","modules":[],"cables":[],"st2":{}}', '{"format":"chains.patch","modules":[],"cables":[],"st2":"x"}'], ['{"format":"chains.patch","modules":[],"cables":[],"ti2":0}', '{"format":"chains.patch","modules":[],"cables":[],"ti2":true}'],
 ['{"format":"chains.patch ","modules":[]}', '{"format":"chains.patch ","modules":[]}'], ['{"format":["chains.patch"],"modules":[]}', '{"modules":[]}'], ['{"modules":[]}', '{"modules":[]}'], ['{"format":"chains.patch","modules":{}}', '{"format":"chains.patch","modules":{}}'],
];
for (const h of hand2) hand.push(h);
const lines = [], exps = [];
const add = (p, n) => { lines.push(JSON.stringify({ p, n })); exps.push(expect(p, n).join('\t')); };
for (const [p, n] of hand) add(p, n);
const N = +countS;
for (let i = 0; i < N; i++) {
  const a = J(base); const b = rnd() < .5 ? J(a) : J(base);
  const ma = rnd() < .3 ? 0 : 1 + ri(2), mb = 1 + ri(3);
  for (let j = 0; j < ma; j++) mutate(a); for (let j = 0; j < mb; j++) mutate(b);
  add(ser(a), ser(b));
}
fs.writeFileSync(outF, lines.join('\n') + '\n'); fs.writeFileSync(expF, exps.join('\n') + '\n');
console.log('cases', lines.length);
