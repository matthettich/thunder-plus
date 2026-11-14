// EXPRESSION, editor side (14 Nov 2026, module queue): one shared module in both editors (engine scope 'poly'; the shared entries are 'mono' like Math's).
// A module whose own data is TEXT: the formula lives on the module as `text` (a string, at most 200 characters), and has to travel everywhere the module does.
// Source guards (definition, palette, Help, the field's attributes and keys), the twelve Examples compiled with the engine's own parser, node runs of the pure helpers
// (the error sentence for every code, the text cleaners), node runs of the host's patchOp and chnDiff with text, and an opt-in browser run (TP_BROWSER_TESTS=1).
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm'), fs = require('fs'), path = require('path');
const H = require('./helpers');
const ED = { chains: H.read('chains/index.html'), waves: H.read('waves/index.html') };
const WK = { chains: H.read('chains/chains-worklet.js'), waves: H.read('waves/waves-worklet.js') };
const HOST = H.read('index.html');
const block = (src, tag) => { const m = new RegExp('/\\* ===' + tag + ' START===[\\s\\S]*?/\\* ===' + tag + ' END=== \\*/').exec(src); assert.ok(m, tag); return m[0]; };
function literal(src, i) {
  const open = src[i], close = open === '{' ? '}' : open === '[' ? ']' : ')'; let d = 0, q = null;
  for (let j = i; j < src.length; j++) {
    const c = src[j];
    if (q) { if (c === '\\') j++; else if (c === q) q = null; continue; }
    if (c === "'" || c === '"' || c === '`') q = c; else if (c === open) d++; else if (c === close && --d === 0) return src.slice(i, j + 1);
  }
  throw new Error('unbalanced');
}
const fnSrc = (src, name) => { const i = src.indexOf('function ' + name + '('); assert.ok(i >= 0, name + ' not found'); const l = src.indexOf('\n', i); if (/\}\s*(\/\/.*)?$/.test(src.slice(i, l)) && src.slice(i, l).trim().endsWith('}') || /\}\s*\/\/[^\n]*$/.test(src.slice(i, l)) && !/\{\s*(\/\/.*)?$/.test(src.slice(i, l))) return src.slice(i, l);   // (a one-line function)
  const e = src.indexOf('\n}\n', i); assert.ok(e > i, name + ' end'); return src.slice(i, e + 2); };   // (a function that ends with a closing brace in column 0)
const constSrc = (src, name) => { const m = new RegExp('\\nconst ' + name + ' = ').exec(src); assert.ok(m, name + ' not found'); const i = m.index + 1; let j = i, d = 0, q = null; for (; j < src.length; j++) { const c = src[j]; if (q) { if (c === '\\') j++; else if (c === q) q = null; continue; } if (c === "'" || c === '"' || c === '`') q = c; else if ('{[('.includes(c)) d++; else if ('}])'.includes(c)) d--; else if (c === ';' && d === 0) break; } return src.slice(i, j + 1); };
const ev = (s, ctx = {}) => vm.runInNewContext('(' + s + ')', ctx);

const SH = block(ED.chains, 'SHARED EDITOR');
const EXAMPLES = ev(literal(SH, SH.indexOf('[', SH.indexOf('const EXPR_EXAMPLES = ')))).map(a => [...a]);
// the editor's pure helpers, run in a clean context (what they need is the text of the shared block itself)
const helperCtx = () => {
  const ctx = { MODULES: { expr: { text: true }, onepole: {} } }; vm.createContext(ctx);
  assert.ok(SH.includes("const EXPR_MAX = 200, EXPR_DEFAULT = 'a * k1', EXPR_HOLD = 450;"));
  vm.runInContext("var EXPR_MAX = 200, EXPR_DEFAULT = 'a * k1', EXPR_HOLD = 450;", ctx);
  vm.runInContext(['exprText', 'exprClean'].map(n => constSrc(SH, n).replace(/^const /, 'var ')).join('\n') + '\n' + fnSrc(SH, 'exprShort') + '\n', ctx);
  vm.runInContext(constSrc(SH, 'EXPR_WORDS').replace(/^const /, 'var ') + '\n' + fnSrc(SH, 'exprMessage') + '\n' + fnSrc(SH, 'textFix') + '\n', ctx);
  return ctx;
};

for (const ed of ['chains', 'waves']) {
  const src = ED[ed];
  test(ed + ': Expression is defined with the engine\'s params and ports, takes text, and has an about text in plain words', () => {
    const i = src.indexOf('\n  expr:{ label:'); assert.ok(i > 0, 'editor entry'); const e = literal(src, i + 1 + '  expr:'.length);
    const params = ev(literal(e, e.indexOf('params:') + 7)), wi = WK[ed].indexOf("type:'expr', scope:'poly'"); assert.ok(wi > 0, 'engine def');
    const wparams = ev(literal(WK[ed], WK[ed].indexOf('{', WK[ed].indexOf('params:', wi))));
    assert.equal(JSON.stringify(params), JSON.stringify(wparams), 'params match the worklet'); assert.equal(JSON.stringify(params), '{"k1":[1,-4,4,0.001],"k2":[0,-4,4,0.001]}');
    assert.equal(JSON.stringify(ev(literal(e, e.indexOf('inputs:') + 7))), '{"a":"audio","b":"audio","c":"cv","d":"cv"}'); assert.equal(JSON.stringify(ev(literal(e, e.indexOf('outputs:') + 8))), '{"out":"audio"}');
    assert.match(e, /label:'Expression', scope:'mono', display:'expr', text:true/); assert.match(e, /plabels:\{ a:'A', b:'B', c:'C', d:'D' \}/);
    const ui = ev(literal(e, e.indexOf('ui:') + 3), {}); assert.deepEqual(Object.fromEntries(Object.entries(ui).map(([k, v]) => [k, v.l])), { k1: 'K1', k2: 'K2' }); assert.equal(ui.k1.f(1), '1.000'); assert.equal(ui.k2.f(-.25), '-0.250');
    const about = /about:'((?:[^'\\]|\\.)*)'/.exec(e)[1];
    for (const w of ['line of maths', 'a, b, c, d, k1, k2', 't (seconds', 'y (this module', 'one sample ago', 'filter or a counter', 'rnd', 'pi and sr', 'tanh', 'clip(x, low, high)', 'mix(a, b, amount)', 'tri saw sq', 'between −4 and 4', 'only ever read as maths', 'never run as a program', 'says where', '200 characters', 'Enter', 'Escape', 'Examples'])
      assert.ok(about.includes(w), 'about mentions ' + w);
    assert.doesNotMatch(about, /\bCV\b|control voltage/);
  });
  test(ed + ': Expression sits after Crossfade in Control utilities, with a hint, a use line, a width of about 300 and a Compact line that is the formula', () => {
    assert.match(src, /\['xfade','Crossfade'\],\['expr','Expression'\]/);
    assert.match(/const PAL_LAYOUT = \[[\s\S]*?\n\];/.exec(src)[0], /\['Control utilities', '#[0-9a-f]{6}', \[[^\]]*'xfade', 'expr', 'slew'/);
    assert.match(/const PAL_HINT = \{[^}]*\}/.exec(src)[0], /\bexpr:'type a formula'/);
    assert.match(src, /\n  expr:'[^'\n]*tanh\(a \* \(1 \+ k1 \* 4\)\)[^'\n]*'/, 'MODULE_USE line');
    assert.match(/const MODULE_WIDTH = \{[\s\S]*?\n\};/.exec(src)[0], /\bexpr:300\b/);
    assert.match(/const SUMMARY_KEYS = \{[\s\S]*?\n\};/.exec(src)[0], /\bexpr:\['\$text'\]/);
    assert.match(src, /\bexpr:'util'/);
    assert.match(src, /const ok = k => \(k === '\$text' && def\.text === true\) \|\|/, 'the pseudo key is only for a module that takes text');
  });
  test(ed + ': the Help paragraph Expression names the names, operators, functions, the limits and the safety', () => {
    const p = /\['Expression', '(Expression \(in Control utilities[^']*)'\]/.exec(src); assert.ok(p, 'paragraph'); const t = p[1];
    for (const w of ['a and b', 'c and d', 'k1 and k2', 'the seconds since the song started', 'y this module’s own output one sample ago', 'filter or a counter', 'rnd', 'pi and sr', '+ − * / % ^', '&& || !', 'a ? b : c', 'sin cos tan tanh', 'clip(x, low, high)', 'mix(a, b, amount)', 'tri saw sq', 'between −4 and 4', 'only ever read as maths, never run as a program', '200 characters', 'Enter', 'Escape', 'Examples'])
      assert.ok(t.includes(w), 'Help mentions ' + w);
    assert.doesNotMatch(t, /\bCV\b/);
  });
  test(ed + ': the field: a textarea that keeps a phone from rewriting it, applies on Enter or blur, Escape puts back, and the text goes into value / textContent only', () => {
    const b = fnSrc(SH, 'buildExpr');
    for (const a of [/h\('textarea'/, /maxlength:String\(EXPR_MAX\)/, /spellcheck:'false'/, /autocapitalize:'off'/, /autocorrect:'off'/, /autocomplete:'off'/, /'aria-invalid':'false'/, /rows:'2'/]) assert.match(b, a);
    assert.match(b, /e\.key === 'Enter'\)\{ e\.preventDefault\(\); apply\(\); ta\.blur\(\); \}/); assert.match(b, /e\.key === 'Escape'\)\{ e\.preventDefault\(\); ta\.value = exprText\(m\); ta\.blur\(\); \}/);
    assert.match(b, /ta\.addEventListener\('blur', apply\)/); assert.match(b, /if \(!\(e\.ctrlKey \|\| e\.metaKey\)\) e\.stopPropagation\(\)/, 'the field keeps its plain keys to itself');
    assert.doesNotMatch(b, /ta\.addEventListener\('input', [^)]*snapshot/, 'typing is not an undo step');
    assert.match(b, /store\.snapshot\(\); m\.text = v; exprSend\(m, v\);/, 'apply: one undo step, the module, the engine');
    assert.match(b, /doc\.modules\.includes\(m\)/, 'a field of a module an undo replaced writes nothing');
    assert.match(b, /ta\.value = exprText\(m\)/); assert.match(b, /err\.textContent = /);
    for (const t of [b, fnSrc(SH, 'exprPaint'), fnSrc(SH, 'exprMessage'), fnSrc(SH, 'exprSend')]) { assert.doesNotMatch(t, /innerHTML|insertAdjacentHTML|outerHTML|document\.write|\beval\b|new Function|Function\(/); }
    assert.match(fnSrc(SH, 'exprSend'), /bridge\.setText\(m\.id, v\)/); assert.match(fnSrc(SH, 'exprSend'), /TW\.mod\(\{ op:'text', id:m\.id, value:v \}\)/);
    assert.match(src, /const typing = e => e\.target && e\.target\.closest && e\.target\.closest\('input:not\(\[type=range\]\):not\(\[type=checkbox\]\), select, textarea, \[contenteditable="true"\]'\);/, 'the editors\' own typing guard names a textarea');
    assert.match(b, /ta\.readOnly = !!playMode/, 'Play locks it');
    const css = SH.split('\n').filter(l => /\.exbox|\.cs-x/.test(l)).join('\n'); assert.match(css, /monospace/); assert.match(css, /\[aria-invalid="true"\]\{outline:2px solid var\(--rec\)/); assert.doesNotMatch(css, /#[0-9a-fA-F]{3,8}\b|\brgba?\(/, 'colours from the skin only');
  });
  test(ed + ': the error line shows only a report that arrives after the hold, and clears on apply', () => {
    const p = fnSrc(SH, 'exprPaint'); assert.match(p, /performance\.now\(\) < S\.hold\) return/); assert.match(p, /S\.err\.hidden = !msg/); assert.match(p, /setAttribute\('aria-invalid', code \? 'true' : 'false'\)/);
    assert.match(SH, /if \(S\.kind === 'expr'\)\{ exprPaint\(n, v\); continue; \}/); assert.match(SH, /if \(kind === 'expr'\) return buildExpr\(n\);/);
    assert.match(fnSrc(SH, 'buildExpr'), /S\.key = ''; S\.hold = performance\.now\(\) \+ EXPR_HOLD; err\.textContent = ''; err\.hidden = true;/);
  });
  test(ed + ': the formula travels: bridge add and text, new module, load, paste, Alt-drag, copy, custom module (capture and insert)', () => {
    assert.match(src, /addModule\(id, type, o\)\{ o = o \|\| \{\}; const msg = \{ op:'add', id, type, params:o\.params, pos:o\.pos \}; if \(typeof o\.text === 'string'\) msg\.text = o\.text; this\._send\(msg\); \}/);
    assert.match(src, /setText\(id, value\)\{ this\._send\(\{ op:'text', id, value \}\); \}/);
    assert.match(src, /if \(def\.text === true\) m\.text = EXPR_DEFAULT;/, 'a new module says its formula');
    assert.match(src, /bridge\.addModule\(m\.id, type, \{ params:m\.params, pos:m\.pos, text:m\.text \}\)/);
    assert.equal((src.match(/bridge\.addModule\(m\.id, m\.type, \{ params:m\.params, pos:m\.pos, text:m\.text \}\)/g) || []).length, 1, 'paste and custom insert (pasteGraph)');
    assert.match(src, /for \(const m of d\.modules\) textFix\(m\);/); assert.match(src, /for \(const c of d\.custom\) if \(c && c\.graph && Array\.isArray\(c\.graph\.modules\)\) for \(const m of c\.graph\.modules\) textFix\(m\);/);
    assert.equal((src.match(/params:Object\.assign\(\{\}, m\.params\), \.\.\.\(typeof m\.text === 'string' \? \{ text:m\.text \} : \{\}\) \}\)\)/g) || []).length, 2, 'copySel and captureCustom copy the text');
    assert.match(src, /textFix\(m\);   \/\/ TEXT: the copy keeps the formula/, 'pasteGraph');
    assert.match(src, /function dupForDrag\(group\)\{[\s\S]*?JSON\.parse\(JSON\.stringify\(m\)\)[\s\S]*?pasteGraph\(graph/, 'Alt-drag clones the whole module object, then pastes');
    assert.match(src, /if \(def\.display\)\{ const sh = buildShow\(n\); if \(sh\) \{ if \(def\.display === 'expr'\) kids\.unshift\(sh\);/);
    assert.match(src, /if \(it\.text\)\{ const t = exprShort\(n\.m\.text\);/, 'Compact paints the formula');
  });
}
test('the SHARED EDITOR block is identical in …chains and …waves', () => { assert.equal(block(ED.chains, 'SHARED EDITOR'), block(ED.waves, 'SHARED EDITOR')); });

test('the twelve Examples: the names and formulas asked for, 2 to 4 words each, every one compiled by each engine\'s own parser', () => {
  assert.deepEqual(JSON.parse(JSON.stringify(EXAMPLES)), [['Pass A (K1 = level)', 'a * k1'], ['Ring modulate A by B', 'a * b'], ['Soft clip', 'tanh(a * (1 + k1 * 4))'], ['Fold', 'tri(a * (1 + k1 * 4) / 4 + 0.25)'], ['Rectify', 'abs(a)'], ['Crossfade by C', 'mix(a, b, clip(c, 0, 1))'],
    ['Sine at K1 kHz', 'sin(2 * pi * 1000 * k1 * t)'], ['One-pole lowpass (K1)', 'y + (a - y) * clip(k1, 0, 1)'], ['Sample and hold on C', 'c > 0.5 ? a : y'], ['Noise', 'rnd * k1'], ['Bit crush (K1)', 'round(a * (2 + k1 * 30)) / (2 + k1 * 30)'], ['Gate A when B is loud', 'abs(b) > k1 ? a : 0']]);
  for (const [nm, f] of EXAMPLES) { const w = nm.split(/\s+/).length; assert.ok(w >= 1 && w <= 6, nm); assert.ok(f.length <= 200, nm); }
  for (const [nm] of EXAMPLES.filter(e => e[0] !== 'Fold' && e[0] !== 'Noise' && e[0] !== 'Rectify')) assert.ok(nm.split(/\s+/).length >= 2, nm);
  const CC = H.freezeClock(H.workletScope()); H.load(CC, 'chains/chains-worklet.js'); const WC = H.freezeClock(H.pageScope()); H.load(WC, 'waves/waves-worklet.js');
  for (const [name, D] of [['chains', CC.ChainsDSP], ['waves', WC.WavesDSP]]) for (const [nm, f] of EXAMPLES) { const P = D.MathExpr.compile(f); assert.equal(P.err, 0, name + ': ' + nm + ' compiles (error ' + P.err + ' at ' + P.pos + ')'); assert.ok(P.n > 0 && P.n <= 128); }
});

test('exprMessage: one plain sentence for every code, with the position (said from 1) for the ones that have one, and nothing for 0', () => {
  const C = helperCtx(), seen = new Set();
  assert.equal(C.exprMessage(0, 0), '');
  assert.equal(C.exprMessage(6, 8), 'At character 9: a bracket is not closed.'); assert.equal(C.exprMessage(8, 4), 'At character 5: a value is expected here.');
  assert.equal(C.exprMessage(1, 0), 'The formula is empty.'); assert.equal(C.exprMessage(2, 200), 'The formula is too long: 200 characters at most.'); assert.equal(C.exprMessage(10, 0), 'The formula is not text.');
  for (let c = 1; c <= 10; c++) {
    const m = C.exprMessage(c, 3); assert.ok(m.length > 10 && m.length < 140, c + ': ' + m); assert.ok(/[.]$/.test(m), 'a sentence'); assert.ok(!seen.has(m), 'one message per code: ' + m); seen.add(m);
    assert.doesNotMatch(m, /[<>&"]/.test(m) ? /^$/ : /\bCV\b|control voltage/, 'no jargon'); assert.doesNotMatch(m, /\bparser|\bcode \d|error \d|NaN|undefined|null/i);
    if (![1, 2, 10].includes(c)) assert.match(m, /^At character 4: /, c + ' says where');
  }
  assert.match(C.exprMessage(5, 0), /a b c d k1 k2 t y rnd pi sr/, 'an unknown name lists the names'); assert.equal(C.exprMessage(99, 0), 'At character 1: the formula cannot be read.');
  assert.equal(C.exprMessage(4, -5), 'At character 1: a character that is not used in this maths.'); assert.equal(C.exprMessage(4, 9999), 'At character 201: a character that is not used in this maths.');
  // the positions the engine really reports, through the engine's own parser
  const CC = H.freezeClock(H.workletScope()); H.load(CC, 'chains/chains-worklet.js'); const M = CC.ChainsDSP.MathExpr;
  for (const [f, want] of [['sin(', 'At character 5: a value is expected here.'], ['(1+2', 'At character 5: a bracket is not closed.'], ['foo', 'At character 1: a name that is not known (the names are a b c d k1 k2 t y rnd pi sr).'], ['sin(1,2)', 'At character 1: a function has the wrong number of values.'], ['1 2', 'At character 3: something is left over after the formula.'], ['2 $ 3', 'At character 3: a character that is not used in this maths.']]) {
    const r = M.compile(f); assert.equal(C.exprMessage(r.err, r.pos), want, f);
  }
});
test('the text helpers: default, cut to 200, line breaks to spaces, the Compact line, and textFix drops text from a module that takes none', () => {
  const C = helperCtx();
  assert.equal(C.exprText({}), 'a * k1'); assert.equal(C.exprText({ text: 'abs(a)' }), 'abs(a)'); assert.equal(C.exprText({ text: '' }), ''); assert.equal(C.exprText({ text: 5 }), 'a * k1'); assert.equal(C.exprText({ text: 'x'.repeat(300) }).length, 200); assert.equal(C.exprText(null), 'a * k1');
  assert.equal(C.exprClean('a\n*\r\nb\t+ 1'), 'a * b + 1'); assert.equal(C.exprClean('x'.repeat(250)).length, 200); assert.equal(C.exprClean(null), ''); assert.equal(C.exprClean(12), '12');
  assert.equal(C.exprShort('abs(a)'), 'abs(a)'); assert.equal(C.exprShort('  a   *\n b '), 'a * b'); assert.equal(C.exprShort('x'.repeat(100)), 'x'.repeat(39) + '…'); assert.equal(C.exprShort(undefined), 'a * k1');
  const a = { type: 'expr', text: 'y'.repeat(300) }; C.textFix(a); assert.equal(a.text.length, 200);
  const b = { type: 'expr', text: 42 }; C.textFix(b); assert.ok(!('text' in b)); const c = { type: 'onepole', text: 'a * k1' }; C.textFix(c); assert.ok(!('text' in c)); const d = { type: 'expr' }; C.textFix(d); assert.ok(!('text' in d)); C.textFix(null); C.textFix('x'); C.textFix({ type: 'nope', text: 'x' });
  const e = { type: 'expr', text: '__proto__' }; C.textFix(e); assert.equal(e.text, '__proto__', 'it is data: kept as a string, never used as a key');
});

// the host's document plumbing, run as it is in index.html
const hostCtx = () => { const ctx = {}; vm.createContext(ctx); vm.runInContext(['const chnClone = o => o == null ? o : JSON.parse(JSON.stringify(o));', fnSrc(HOST, 'patchOp'), fnSrc(HOST, 'chnDiff'), 'this.patchOp = patchOp; this.chnDiff = chnDiff;'].join('\n'), ctx); return ctx; };
const plain = o => JSON.parse(JSON.stringify(o));
test('host patchOp: an add op carries text (cut to 200, a non-string left out), a text op sets it on the module, and a bad value changes nothing', () => {
  const C = hostCtx(), d = { modules: [], cables: [] };
  C.patchOp(d, { op: 'add', id: 'e1', type: 'expr', params: { k1: 1 }, pos: [0, 0], text: 'abs(a)' }); assert.deepEqual(plain(d.modules[0]), { id: 'e1', type: 'expr', params: { k1: 1 }, pos: [0, 0], text: 'abs(a)' });
  C.patchOp(d, { op: 'add', id: 'e2', type: 'expr', params: {}, pos: [0, 0], text: 'x'.repeat(300) }); assert.equal(d.modules[1].text.length, 200);
  C.patchOp(d, { op: 'add', id: 'e3', type: 'expr', params: {}, pos: [0, 0], text: 42 }); assert.ok(!('text' in d.modules[2])); C.patchOp(d, { op: 'add', id: 'e4', type: 'onepole', params: {}, pos: [0, 0] }); assert.ok(!('text' in d.modules[3]));
  C.patchOp(d, { op: 'text', id: 'e1', value: 'a * b' }); assert.equal(d.modules[0].text, 'a * b'); C.patchOp(d, { op: 'text', id: 'e1', value: 'z'.repeat(250) }); assert.equal(d.modules[0].text.length, 200);
  C.patchOp(d, { op: 'text', id: 'e1', value: 7 }); assert.equal(d.modules[0].text.length, 200, 'a number is ignored'); C.patchOp(d, { op: 'text', id: 'e1', value: null }); C.patchOp(d, { op: 'text', id: 'e1' }); assert.equal(d.modules[0].text.length, 200);
  C.patchOp(d, { op: 'text', id: 'nobody', value: 'a' }); assert.equal(d.modules.length, 4); C.patchOp(d, { op: 'text', id: '__proto__', value: 'a' }); assert.equal(({}).text, undefined, 'no prototype touched');
  C.patchOp(null, { op: 'text', id: 'e1', value: 'a' }); C.patchOp({}, { op: 'text', id: 'e1', value: 'a' });   // (no throw)
});
test('host chnDiff: a new module\'s add op carries its text; a changed text is one text op; an equal text is no op; a missing one falls back to the engine\'s default', () => {
  const C = hostCtx(), M = (id, text, extra = {}) => Object.assign({ id, type: 'expr', params: { k1: 1, k2: 0 }, pos: [0, 0] }, text === undefined ? {} : { text }, extra);
  const base = mods => ({ format: 'chains.patch', version: 1, st2: 1, ti2: 1, modules: mods, cables: [] });
  assert.deepEqual(plain(C.chnDiff(base([M('a', 'a * k1')]), base([M('a', 'a * k1')]))), [], 'equal: no op');
  assert.deepEqual(plain(C.chnDiff(base([]), base([M('a', 'abs(a)')]))), [{ op: 'add', id: 'a', type: 'expr', params: { k1: 1, k2: 0 }, pos: [0, 0], text: 'abs(a)' }]);
  assert.deepEqual(plain(C.chnDiff(base([]), base([M('a')]))), [{ op: 'add', id: 'a', type: 'expr', params: { k1: 1, k2: 0 }, pos: [0, 0] }], 'no text: none sent (the engine\'s default stands)');
  assert.deepEqual(plain(C.chnDiff(base([M('a', 'a * k1')]), base([M('a', 'a * b')]))), [{ op: 'text', id: 'a', value: 'a * b' }], 'a change is one text op');
  assert.deepEqual(plain(C.chnDiff(base([M('a', 'a * k1')]), base([M('a', 'a * b', { params: { k1: 2, k2: 0 } })]))).map(o => o.op), ['param', 'text'], 'beside a param change');
  assert.deepEqual(plain(C.chnDiff(base([M('a', 'a')]), base([M('a', 'x'.repeat(300))]))), [{ op: 'text', id: 'a', value: 'x'.repeat(200) }], 'cut to 200 as the engine does');
  assert.deepEqual(plain(C.chnDiff(base([M('a', 'x'.repeat(300))]), base([M('a', 'x'.repeat(250))]))), [], 'two over-long texts are the same 200');
  assert.deepEqual(plain(C.chnDiff(base([M('a', 'abs(a)')]), base([M('a')]))), [{ op: 'text', id: 'a', value: 'a * k1' }], 'text gone: the default');
  assert.deepEqual(plain(C.chnDiff(base([M('a')]), base([M('a', 'abs(a)')]))), [{ op: 'text', id: 'a', value: 'abs(a)' }]);
  assert.deepEqual(plain(C.chnDiff(base([M('a')]), base([M('a', 99)]))), [], 'a non-string is no text at all: nothing sent');
  assert.deepEqual(plain(C.chnDiff(base([M('a', 'abs(a)')]), base([M('a', 12)]))), [{ op: 'text', id: 'a', value: 'a * k1' }], 'a text turned into a number falls back to the default');
  // replaying the ops on the prev document gives the next one's text (the song's copy and the engines' record stay in step)
  const prev = base([M('a', 'a * k1'), M('b', 'abs(a)')]), next = base([M('a', 'sin(t)'), M('c', 'rnd'), M('b', 'abs(a)')]), cp = plain(prev); for (const o of C.chnDiff(prev, next)) C.patchOp(cp, o);
  assert.deepEqual(Object.fromEntries(cp.modules.map(m => [m.id, m.text])), { a: 'sin(t)', b: 'abs(a)', c: 'rnd' });
});

/* THE REAL PAGE (opt-in: TP_BROWSER_TESTS=1; boots headless Chrome, about 30 s). In each editor: add an Expression from the palette, type `sin(` and the error shows with its
   position, type `a * 0.5` and it goes, reload and the text is still there, copy / paste gives a copy with the text, undo / redo restore the text, and typing "a" in the field
   does not open the add-module search (…waves: nor play a note). */
const PW = process.env.PLAYWRIGHT || ['/opt/npm-tools/node_modules/playwright/index.mjs', '/opt/node-tools/node_modules/playwright/index.mjs'].find(f => fs.existsSync(f));
const P = (id, type, x, y, params = {}) => ({ id, type, scope: 'mono', pos: [x, y], collapsed: false, params });
const C2 = (a, ap, b, bp) => ({ from: [a, ap], to: [b, bp], bend: 0 });
const SPEC = {
  chains: { key: 'chains.patch', sk: 'chains.settings', mods: [P('i', 'trackIn', 0, 0, { track: 1 }), P('o', 'chainOut', 300, 0, { track: 1 }), { ...P('x', 'expr', 0, 300), text: 'a * k1' }], cab: [C2('i', 'out', 'o', 'in'), C2('i', 'out', 'x', 'a')] },
  waves: { key: 'waves.patch', sk: 'waves.settings', mods: [P('h', 'hostIn', 0, 0), P('v', 'vco', 300, 0, { wave: 1 }), P('o', 'out', 600, 0, { slot: 1, level: 1 }), { ...P('x', 'expr', 0, 300), text: 'a * k1' }], cab: [C2('h', 'pitch', 'v', 'pitch'), C2('v', 'out', 'o', 'in'), C2('v', 'out', 'x', 'a')] },
};
test('the Expression field in a real page: errors with their position, applies, survives a reload, copies, undoes, and keeps its keys (both editors)', { skip: !process.env.TP_BROWSER_TESTS ? 'opt-in: TP_BROWSER_TESTS=1 (boots Chrome, about 40 s)' : !PW && 'no Playwright here', timeout: 240000 }, async () => {
  process.env.PLAYWRIGHT = PW;
  const { boot } = await import('./sweep/ui-lib.mjs');
  const B = await boot({ width: 1440, height: 900 }); const { page } = B, base = 'http://127.0.0.1:' + B.port;
  try {
    for (const ed of ['chains', 'waves']) {
      const S = SPEC[ed], doc = { meta: { name: 'expr test' }, view: { x: 20, y: 20, zoom: 1, ui: 2, nw: 1 }, modules: S.mods, cables: S.cab, groups: [], custom: [], transport: { tempo: 120 } };
      const url = base + '/' + ed + '/index.html'; await page.goto(url);
      await page.evaluate(([k, sk, d]) => { localStorage.clear(); localStorage.setItem(k, JSON.stringify(d)); localStorage.setItem(sk, JSON.stringify({ tips: false })); }, [S.key, S.sk, doc]);
      await page.goto(url); await page.waitForSelector('.node[data-id="x"] textarea.extext', { timeout: 20000 });
      if (ed === 'waves') { await page.mouse.click(700, 760); await page.keyboard.press('z'); await page.waitForTimeout(300); }   // (…waves' engine runs the module only while a voice does)
      const ta = page.locator('.node[data-id="x"] textarea.extext'), err = page.locator('.node[data-id="x"] .exerr');
      const saved = () => page.evaluate(([k]) => { const d = JSON.parse(localStorage.getItem(k)); return d.modules.filter(m => m.type === 'expr').map(m => m.text); }, [S.key]);
      assert.equal(await ta.inputValue(), 'a * k1'); assert.equal(await ta.getAttribute('maxlength'), '200'); assert.equal(await ta.getAttribute('spellcheck'), 'false');
      // typing "a" in the field: no add-module search, no note, the letter lands in the field
      await ta.click(); await page.keyboard.press('Control+a'); await page.keyboard.type('a'); assert.equal(await ta.inputValue(), 'a');
      assert.equal(await page.locator('.qadd:visible, #qa:visible, .quickadd:visible').count(), 0, ed + ': no search opened');
      for (const k of ['n', 'f', 'Delete', 'Backspace', 'ArrowLeft']) await page.keyboard.press(k);
      assert.equal(await page.locator('.node[data-id="x"]').count(), 1, ed + ': the module is still there');
      // `sin(` → the error shows with its position
      await page.keyboard.press('Control+a'); await page.keyboard.type('sin('); await page.keyboard.press('Enter');
      await page.waitForFunction(() => { const e = document.querySelector('.node[data-id="x"] .exerr'); return e && !e.hidden && /At character 5: a value is expected here\./.test(e.textContent); }, null, { timeout: 8000 });
      assert.equal(await ta.getAttribute('aria-invalid'), 'true'); assert.deepEqual(await saved().then(a => a.length), 1);
      // `a * 0.5` → no error; the error line goes
      await ta.click(); await page.keyboard.press('Control+a'); await page.keyboard.type('a * 0.5'); await page.keyboard.press('Enter');
      await page.waitForFunction(() => { const e = document.querySelector('.node[data-id="x"] .exerr'); return e && e.hidden; }, null, { timeout: 8000 }); await page.waitForTimeout(900);
      assert.equal(await err.isHidden(), true, ed + ': still no error'); assert.equal(await ta.getAttribute('aria-invalid'), 'false');
      // Escape puts back the last applied text
      await ta.click(); await page.keyboard.type(' + junk'); await page.keyboard.press('Escape'); assert.equal(await ta.inputValue(), 'a * 0.5');
      // reload: the text is still there (autosave runs 1.2 s after the edit)
      await page.waitForTimeout(1600); assert.deepEqual(await saved(), ['a * 0.5']);
      await page.goto(url); await page.waitForSelector('.node[data-id="x"] textarea.extext'); assert.equal(await ta.inputValue(), 'a * 0.5', ed + ': after a reload');
      // copy / paste: the copy has the text
      await page.locator('.node[data-id="x"] header').click(); await page.keyboard.press('Control+c'); await page.keyboard.press('Control+v'); await page.waitForTimeout(400);
      const copies = await page.evaluate(() => [...document.querySelectorAll('.node textarea.extext')].map(t => t.value)); assert.deepEqual(copies, ['a * 0.5', 'a * 0.5'], ed + ': paste');
      await page.keyboard.press('Delete'); await page.waitForTimeout(300); assert.equal(await page.locator('.node textarea.extext').count(), 1, ed + ': the pasted copy is removed again (it lay over the original)');
      // undo / redo restore the text
      await page.locator('.node[data-id="x"] textarea.extext').click(); await page.keyboard.press('Control+a'); await page.keyboard.type('abs(a)'); await page.keyboard.press('Enter');
      assert.equal(await ta.inputValue(), 'abs(a)'); await page.locator('.ws, #ws').first().click({ position: { x: 700, y: 700 } }).catch(() => {});
      await page.keyboard.press('Control+z'); await page.waitForTimeout(300); assert.equal(await page.locator('.node[data-id="x"] textarea.extext').inputValue(), 'a * 0.5', ed + ': undo');
      await page.keyboard.press('Control+Shift+z'); await page.waitForTimeout(300); assert.equal(await page.locator('.node[data-id="x"] textarea.extext').inputValue(), 'abs(a)', ed + ': redo');
      // the Examples menu fills the field
      await page.locator('.node[data-id="x"] .row.pre button.presel').click(); await page.getByRole('menuitem', { name: 'Soft clip' }).click();
      assert.equal(await page.locator('.node[data-id="x"] textarea.extext').inputValue(), 'tanh(a * (1 + k1 * 4))');
    }
  } finally { await B.close(); }
});
