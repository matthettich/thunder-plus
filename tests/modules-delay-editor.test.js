// DELAY LINE, editor side (14 Nov 2026, module queue): one shared module in both editors (engine scope 'poly'; the editors' shared entries are 'mono' like Math's).
// Source guards in the style of modules-blocks-editor: the engine's params, ui labels and formatters, about / use / hint text, a width, a Compact summary, the palette
// place after Allpass, the Time jack's own name, the Help sentence, and the SHARED EDITOR block identical in both pages.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');
const ED = { chains: H.read('chains/index.html'), waves: H.read('waves/index.html') };
const WK = { chains: H.read('chains/chains-worklet.js'), waves: H.read('waves/waves-worklet.js') };
const block = (src, tag) => { const m = new RegExp('/\\* ===' + tag + ' START===[\\s\\S]*?/\\* ===' + tag + ' END=== \\*/').exec(src); assert.ok(m, tag); return m[0]; };
function literal(src, i) {
  const open = src[i], close = open === '{' ? '}' : ']'; let d = 0, q = null;
  for (let j = i; j < src.length; j++) {
    const c = src[j];
    if (q) { if (c === '\\') j++; else if (c === q) q = null; continue; }
    if (c === "'" || c === '"' || c === '`') q = c; else if (c === open) d++; else if (c === close && --d === 0) return src.slice(i, j + 1);
  }
  throw new Error('unbalanced');
}
const spct = v => (v > 0 ? '+' : '') + Math.round(v * 100) + '%', pct = v => Math.round(v * 100) + '%', khz = v => v >= 1000 ? (v / 1000).toFixed(1) + ' kHz' : Math.round(v) + ' Hz';
const ctx = { opts: names => names.map((t, i) => [i, t]), pct, spct, khz };
const ev = s => vm.runInNewContext('(' + s + ')', ctx);
ctx.dlTime = v => v >= 1000 ? (v / 1000).toFixed(2) + ' s' : v >= 100 ? Math.round(v) + ' ms' : v < 20 && v >= .1 ? v.toFixed(1) + ' ms (' + khz(1000 / v) + ')' : v.toFixed(1) + ' ms';   // placeholder, replaced per editor below
const entry = src => { const m = /\n  dline:\{ label:/.exec(src); assert.ok(m, 'editor entry'); return literal(src, m.index + 1 + '  dline:'.length); };
const engineParams = src => { const i = src.indexOf("type:'dline', scope:'"); assert.ok(i > 0); const j = src.indexOf('params:', i); return ev(literal(src, src.indexOf('{', j))); };
const dlTime = src => { const m = /const dlTime = (v => .*);\n/.exec(src); assert.ok(m, 'dlTime'); return ev(m[1]); };

for (const ed of ['chains', 'waves']) {
  const src = ED[ed];
  test(ed + ': Delay line is defined with the engine\'s params, ports, labels and an about text in a musician\'s words', () => {
    ctx.dlTime = dlTime(src); const e = entry(src), params = ev(literal(e, e.indexOf('params:') + 7));
    assert.equal(JSON.stringify(params), JSON.stringify(engineParams(WK[ed])), 'params match the worklet');
    const ui = ev(literal(e, e.indexOf('ui:') + 3));
    assert.deepEqual(Object.fromEntries(Object.entries(ui).map(([k, v]) => [k, v.l])), { time: 'Time', fine: 'Fine', fb: 'Feedback', damp: 'Damp', depth: 'Mod depth' });
    assert.equal(JSON.stringify(ev(literal(e, e.indexOf('inputs:') + 7))), '{"in":"audio","tmod":"cv"}'); assert.equal(JSON.stringify(ev(literal(e, e.indexOf('outputs:') + 8))), '{"out":"audio"}');
    assert.match(e, /plabels:\{ tmod:'Time' \}/, 'the cv jack is named Time');
    assert.match(e, /label:'Delay line', scope:'mono'/);
    const about = /about:'([^']*)'/.exec(e)[1];
    for (const w of ['one sample to two seconds', 'only the delayed sound', 'Crossfade', 'Mixer', '1000 ÷ Time', '2 ms rings at 500 Hz', 'Fine', 'plucked string', 'Noise', 'Damp', 'LFO', 'Mod depth', 'chorus', 'flanger', '99%', 'run away', 'feedback cable', 'one block', 'exact'])
      assert.ok(about.includes(w), 'about mentions ' + w);
    assert.ok(about.length > 600);
  });
  test(ed + ': the readouts: Time in ms, whole ms, seconds, and the pitch under 20 ms; Fine in three decimals; Feedback signed', () => {
    ctx.dlTime = dlTime(src); const e = entry(src), ui = ev(literal(e, e.indexOf('ui:') + 3)), f = ctx.dlTime;
    assert.equal(f(250), '250 ms'); assert.equal(f(100), '100 ms'); assert.equal(f(1500), '1.50 s'); assert.equal(f(2000), '2.00 s'); assert.equal(f(50), '50.0 ms'); assert.equal(f(20), '20.0 ms');
    assert.equal(f(2), '2.0 ms (500 Hz)'); assert.equal(f(4.5), '4.5 ms (222 Hz)'); assert.equal(f(.1), '0.1 ms (10.0 kHz)'); assert.equal(f(0), '0.0 ms'); assert.equal(f(19.9), '19.9 ms (50 Hz)');
    assert.equal(ui.time.f(2), '2.0 ms (500 Hz)', 'the slider uses it');
    assert.equal(ui.fine.f(.25), '+0.250 ms'); assert.equal(ui.fine.f(-.001), '-0.001 ms'); assert.equal(ui.fine.f(0), '0.000 ms');
    assert.equal(ui.fb.f(.95), '+95%'); assert.equal(ui.fb.f(-.5), '-50%'); assert.equal(ui.fb.f(0), '0%'); assert.equal(ui.damp.f(.2), '20%'); assert.equal(ui.depth.f(3), '3.0 ms');
  });
  test(ed + ': Delay line has a palette entry after Allpass, a use line, a hint, a width, a family and a Compact summary (time, fb)', () => {
    assert.match(src, /SHARED_PALETTE\.push\(\['Building blocks', \[\['onepole','One-pole filter'\],\['allpass','Allpass'\],\['dline','Delay line'\],\['dcblock','DC blocker'\],\['midside','Mid \/ side'\]\], 'audio'\]\)/);
    assert.match(/const PAL_LAYOUT = \[[\s\S]*?\n\];/.exec(src)[0], /\['Building blocks', '#[0-9a-f]{6}', \['onepole', 'allpass', 'dline', 'dcblock', 'midside'\]\]/);
    assert.match(src, /\n  dline:'[^'\n]*plucked string[^'\n]*echo[^'\n]*'/, 'MODULE_USE line');
    assert.match(/const PAL_HINT = \{[^}]*\}/.exec(src)[0], /\bdline:'comb, echo'/);
    assert.match(/const MODULE_WIDTH = \{[\s\S]*?\n\};/.exec(src)[0], /\bdline:2[1-5]\d\b/, 'width 216-250');
    assert.match(/const SUMMARY_KEYS = \{[\s\S]*?\n\};/.exec(src)[0], /\bdline:\['time', 'fb'\]/);
    assert.match(src, /\bdline:'afx'/);
  });
  test(ed + ': the Help paragraph Building blocks names the Delay line', () => {
    const p = /\['Building blocks', '(Building blocks in the Modules list[^']*)'\]/.exec(src)[1];
    assert.match(p, /Delay line is a bare delay[^.]*two seconds[^.]*feedback[^.]*ring[^.]*echo[^.]*dry sound yourself\./);
    assert.ok(p.indexOf('Allpass') < p.indexOf('Delay line') && p.indexOf('Delay line') < p.indexOf('DC blocker'), 'in palette order');
  });
}
test('the SHARED EDITOR block (and the engine\'s shared block) is identical in …chains and …waves, and the Time jack is named by the shared portName', () => {
  assert.equal(block(ED.chains, 'SHARED EDITOR'), block(ED.waves, 'SHARED EDITOR'));
  assert.equal(block(WK.chains, 'SHARED MODULES'), block(WK.waves, 'SHARED MODULES'));
  for (const ed of ['chains', 'waves']) assert.match(ED[ed], /if \(d && d\.plabels && d\.plabels\[port\]\) return d\.plabels\[port\];/);
});
