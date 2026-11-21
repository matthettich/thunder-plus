// Send / Receive in both editors (15 Nov): the module entries exist and are identical (shared block), the label keys match the worklet's, the status lines, and the cross-chain mark over a pair.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');
const ED = { chains: H.read('chains/index.html'), waves: H.read('waves/index.html') };
const mod = (id, type, params, text) => ({ id, type, params: params || {}, ...(text != null ? { text } : {}) });
const cab = (a, pa, b, pb) => ({ from: [a, pa], to: [b, pb] });
function fns(src, doc){
  const pick = (re, what) => { const m = src.match(re); assert.ok(m, what + ' not found'); return m[0]; };
  const code = [pick(/const SR_LAB = [^\n]*\n/, 'SR_LAB'), pick(/function srKeyOf\(m\)\{[\s\S]*?\n\}\n/, 'srKeyOf'), pick(/const srName = [^\n]*\n/, 'srName'), pick(/function srStatus\(m\)\{[\s\S]*?\n\}\n/, 'srStatus'),
    'const fbOk = c => !!(c && Array.isArray(c.from) && Array.isArray(c.to));', pick(/function xcCables\(\)\{[\s\S]*?\n\}\n(?=function xcTip)/, 'xcCables')].join('\n');
  const c = vm.createContext({ Set, Map, Array, Math, doc }); vm.runInContext(code, c); return c;
}
for (const [ed, src] of Object.entries(ED)){
  test(ed + ': Send and Receive are defined with the worklet\'s ports and parameter, take text, and sit in the palette', () => {
    assert.match(src, /send:\{ label:'Send', scope:'mono', display:'srlab', text:true, inputs:\{ in:'audio', inR:'audio' \}, outputs:\{\}/);
    assert.match(src, /recv:\{ label:'Receive', scope:'mono', display:'srlab', text:true, inputs:\{\}, outputs:\{ out:'audio', outR:'audio' \}/);
    assert.match(src, /SHARED_PALETTE\.push\(\['Utilities', \[\['send', 'Send'\], \['recv', 'Receive'\]\]/);
    assert.match(src, /NOTES\.send = NOTES\.recv = /);
  });
  test(ed + ': the label key is A-H or trimmed lower-case text, and the status line says what is missing', () => {
    const doc = { modules: [mod('r', 'recv', { lab: 0 }, ' Bus 9 ')], cables: [] }, c = fns(src, doc);
    assert.equal(c.srKeyOf(doc.modules[0]), 't:bus 9'); assert.equal(c.srKeyOf(mod('x', 'send', { lab: 3 })), 'C'); assert.equal(c.srKeyOf(mod('x', 'send', { lab: 0 })), '');
    assert.equal(c.srStatus(doc.modules[0]), 'no Send named Bus 9');
    doc.modules.push(mod('s', 'send', { lab: 0 }, 'BUS 9')); assert.equal(c.srStatus(doc.modules[0]), ''); assert.equal(c.srStatus(doc.modules[1]), '');
    doc.modules.push(mod('s2', 'send', { lab: 5 })); assert.equal(c.srStatus(doc.modules[2]), 'No Receive named E yet.');
  });
  test(ed + ': a pair that joins two tracks\' chains marks the cable into the Send and the cable out of the Receive; a pair inside one chain marks nothing', () => {
    const base = () => ({ modules: [mod('i1', 'trackIn', { track: 1 }), mod('o9', 'chainOut', { track: 9 }), mod('i15', 'trackIn', { track: 15 }), mod('sc', 'sccomp'), mod('o15', 'chainOut', { track: 15 }), mod('s', 'send', { lab: 1 }), mod('r', 'recv', { lab: 1 })],
      cables: [cab('i1', 'out', 'o9', 'in'), cab('i15', 'out', 'sc', 'in'), cab('sc', 'out', 'o15', 'in'), cab('i1', 'out', 's', 'in'), cab('r', 'out', 'sc', 'sc')] });
    const d = base(), c = fns(src, d), X = c.xcCables();
    assert.equal(X.size, 2); assert.deepEqual([...X.keys()].map(k => k.to[0] + '.' + k.to[1]).sort(), ['s.in', 'sc.sc']);
    const e = base(); e.modules.push(mod('i1b', 'trackIn', { track: 15 })); e.cables = [cab('i15', 'out', 's', 'in'), cab('r', 'out', 'sc', 'sc'), cab('i15', 'out', 'sc', 'in'), cab('sc', 'out', 'o15', 'in')];
    assert.equal(fns(src, e).xcCables().size, 0, 'inside track 15\'s chain');
    const f = base(); f.modules[6].params.lab = 2; assert.equal(fns(src, f).xcCables().size, 0, 'a Receive with no Send crosses nothing');
  });
}
test('the Send and Receive entries are the same in both editors (shared block)', () => {
  const g = s => s.match(/const SR_LAB = [\s\S]*?\nfunction srStatus[\s\S]*?\n\}\n/)[0];
  assert.equal(g(ED.chains), g(ED.waves));
});

for (const [ed, src] of Object.entries(ED)){
  test(ed + ': the editor picks ports as the worklet does (Out takes In only; In R falls back per Send), follows pairs in series, and a custom-text rename refreshes the marks', () => {
    const d = { modules: [mod('i1', 'trackIn', { track: 1 }), mod('i2', 'trackIn', { track: 2 }), mod('o1', 'chainOut', { track: 1 }), mod('s', 'send', { lab: 1 }), mod('r', 'recv', { lab: 1 })],
      cables: [cab('i1', 'out', 's', 'in'), cab('i2', 'out', 's', 'inR'), cab('r', 'out', 'o1', 'in')] };
    assert.equal(fns(src, d).xcCables().size, 0, 'track 2 is on In R, which Out does not carry');
    const e = { modules: [mod('i1', 'trackIn', { track: 1 }), mod('i2', 'trackIn', { track: 2 }), mod('o1', 'chainOut', { track: 1 }), mod('sa', 'send', { lab: 1 }), mod('ra', 'recv', { lab: 1 }), mod('sb', 'send', { lab: 2 }), mod('rb', 'recv', { lab: 2 })],
      cables: [cab('i2', 'out', 'sa', 'in'), cab('ra', 'out', 'sb', 'in'), cab('rb', 'out', 'o1', 'in'), cab('i1', 'out', 'o1', 'in')] };
    assert.equal(fns(src, e).xcCables().size, 3, 'the whole series is marked');
    assert.match(src, /paintSr\(\); try \{ paintXc\(\); \} catch \(e\) \{\} if \(m\.collapsed\) paintSum\(n\); store\.touch\(\);/);
  });
}

for (const [ed, src] of Object.entries(ED)){
  test(ed + ': 12 pairs in series, each Send fed by two Receives, mark in linear time (every cable once)', () => {
    const N = 12, modules = [mod('a', 'trackIn', { track: 1 }), mod('b', 'trackIn', { track: 2 }), mod('o1', 'chainOut', { track: 1 }), mod('o2', 'chainOut', { track: 2 })], cables = [cab('a', 'out', 'o1', 'in'), cab('b', 'out', 'o2', 'in')];
    for (let k = 1; k <= N; k++) modules.push(mod('s' + k, 'send', { lab: 0 }, 'L' + k), mod('x' + k, 'recv', { lab: 0 }, 'L' + k), mod('y' + k, 'recv', { lab: 0 }, 'L' + k));
    cables.push(cab('a', 'out', 's1', 'in'), cab('b', 'out', 's1', 'in'));
    for (let k = 2; k <= N; k++) cables.push(cab('x' + (k - 1), 'out', 's' + k, 'in'), cab('y' + (k - 1), 'out', 's' + k, 'in'));
    cables.push(cab('x' + N, 'out', 'o1', 'in'), cab('y' + N, 'out', 'o1', 'in'));
    const t0 = Date.now(), X = fns(src, { modules, cables }).xcCables(); assert.ok(Date.now() - t0 < 1000);
    assert.ok(X.size >= 2 + 2 * (N - 1) + 2 - 1 && X.size <= cables.length, 'marked cables: ' + X.size);
    assert.ok([...X.keys()].some(c => c.from[0] === 'x' + N));
  });
}
