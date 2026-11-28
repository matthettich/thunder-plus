// FEEDBACK, the editor side (audit finding M2; the engine is tests/feedback.test.js). Both editors now ALLOW a loop: the cable that would close one is
// marked fb:true (saved on the cable in the patch), drawn as a long dash and a dot with a "1 blk" tag, and kept true after every change to the cables.
// Source guards for both editors and the host page, plus logic tests that run the real fbReach / fbFix / patchOp / chnDiff text in node, and one
// engine round trip (an old patch with an unmarked loop is marked by fbFix and then runs as a marked loop does). The browser run is by script (see the lead's notes).
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');
const ED = { chains: H.read('chains/index.html'), waves: H.read('waves/index.html') };
const HOST = H.read('index.html');
const block = (src, a, b) => { const i = src.indexOf(a), j = src.indexOf(b, i); assert.ok(i >= 0 && j > i, a + ' … ' + b + ' not found'); return src.slice(i, j); };

/* the real functions, from the page text */
function editorFns(src, doc) {
  const code = block(src, 'const fbOk = ', '// Tell the engine a cable\'s mark') + 'function reaches(a, b){ return fbReach(doc.cables, a, b); }\n';
  const ctx = { doc, out: null }; vm.createContext(ctx);
  vm.runInContext(code + '\nout = { fbReach, fbFix, fbCyclic, reaches };', ctx);
  return ctx.out;
}
const cab = (a, b, fb, ap = 'out', bp = 'in') => { const c = { from: [a, ap], to: [b, bp], bend: 0 }; if (fb) c.fb = true; return c; };
const marks = cables => cables.map(c => c.fb === true ? 1 : 0).join('');

for (const [ed, src] of Object.entries(ED)) {
  test(ed + ': a loop is allowed: no refusal toasts, the cable is marked, the engine is told, the first toast says what happened', () => {
    assert.doesNotMatch(src, /That cable would make a loop/); assert.doesNotMatch(src, /A module can’t feed itself/);
    const c = block(src, 'function connect(from, to, pts, snap){', 'function disconnect(c, quiet){');
    assert.match(c, /const loop = from\[0\] === to\[0\] \|\| reaches\(to\[0\], from\[0\]\);/);
    assert.match(c, /if \(loop\) c\.fb = true;/); assert.match(c, /bridge\.connect\(from, to, loop\)/); assert.match(c, /if \(loop\) fbToast\(\);/);
    assert.match(src, /toast\('Feedback cable: it arrives one block late \(about 3 ms\)\.'/);
    assert.match(src, /connect\(a, b, fb\)\{ this\._send\(fb === true \? \{ op:'connect', a, b, fb:true \} : \{ op:'connect', a, b \}\); \}/);
  });
  test(ed + ': the marks are made true after every change: load, undo, redo, import, delete, module delete, paste (normalize, disconnect, removeModule, pasteGraph)', () => {
    assert.match(block(src, 'function normalize(d){', '/* ═══════════════ the patch'), /fbFix\(d\.cables\);[^\n]*\n  return d;\n\}/);
    assert.match(block(src, 'function disconnect(c, quiet){', '/* ----- patching by dragging'), /normFeedback\(\);/);
    assert.match(block(src, 'function removeModule(id){', 'syncCableEls(); store.touch()'), /normFeedback\(\);/);
    const p = block(src, 'function pasteGraph(graph, pos){', 'function dupForDrag');
    assert.match(p, /if \(cc\.fb === true\) c\.fb = true;/); assert.match(p, /const fbChg = fbFix\(doc\.cables\);/); assert.match(p, /bridge\.connect\(c\.from, c\.to, c\.fb === true\)/);
    assert.match(src, /store\.onRestore = \(\) => \{ normalize\(doc\);/, 'undo, redo, restore and import go through normalize, then the whole patch is sent to the engine');
    assert.match(block(src, 'function normFeedback(){', 'let fbToldOnce'), /fbFix\(doc\.cables\)[^\n]*fbSend\(c\)/);
  });
  test(ed + ': the mark travels: copy, duplicate (Alt-drag), custom-module capture and paste carry fb; Tidy up ignores a marked cable', () => {
    assert.equal((src.match(/\.\.\.\(c\.fb === true \? \{ fb:true \} : \{\}\)/g) || []).length, 3, 'copySel, dupForDrag, captureCustom');
    assert.match(src, /if \(a !== b && c\.fb !== true && ids\.has\(a\) && ids\.has\(b\)\)\{ pred\.get\(b\)\.push\(a\);/);
  });
  test(ed + ': the look: a third dash pattern, a tag by the target end, the variables of the skin only, the readout and tooltip say so', () => {
    assert.match(src, /svg\.cables \.cln\.fb\{stroke-linecap:round; stroke-dasharray:14 4 \.1 4\}/);
    const css = block(src, 'svg.cables .fbtag{', '/* CABLE STATES');
    assert.doesNotMatch(css, /#[0-9a-f]{3,6}\b/i, 'no colour of its own: the line keeps its kind’s stroke, the tag reads --ink on --panel');
    assert.match(css, /fill:var\(--panel\); stroke:var\(--ink\)/); assert.match(css, /fill:var\(--ink\)/); assert.match(css, /font-size:11px/);
    assert.match(src, /t\.textContent = '1 blk'/);
    assert.match(src, /feedback, one block late/); assert.match(src, /Feedback cable: one block late\. Click to select/);
    assert.match(src, /el\.line\.classList\.toggle\('fb', on\)/);
    assert.match(src, /if \(o\.fb !== \(c\.fb === true\)\) paintFb\(c, o\);/, 'a mark that changes (undo, delete) is drawn without rebuilding the cable');
    assert.match(src, /hover|g:hover > \.cln/, 'hover and selected rules are untouched');
    assert.match(src, /svg\.cables g\.sel > \.hit\{stroke:var\(--selc\); stroke-opacity:\.32\}/);
  });
  test(ed + ': Help says loops are allowed, one block late, limited when loud; no manual toggle in the cable menu', () => {
    assert.match(src, /\['Feedback loops', 'A cable that would close a loop is allowed:/);
    const t = src.match(/\['Feedback loops', '([^']*(?:\\'[^']*)*)'\]/)[1];
    assert.match(t, /one block late/); assert.match(t, /128 samples/); assert.match(t, /limited if it gets very loud/); assert.match(t, /Turn the gain round the loop up carefully/);
    assert.doesNotMatch(block(src, "const its = [['Remove cable'", 'if (o.touch)'), /[Ff]eedback/, 'no toggle');
  });
}

test('the two editors keep identical SHARED EDITOR blocks', () => {
  const sh = s => block(s, '/* ===SHARED EDITOR START===', '/* ===SHARED EDITOR END=== */');
  assert.equal(sh(ED.chains), sh(ED.waves));
  assert.match(sh(ED.chains), /feedback, one block late/, 'the cable readout is in the shared block');
});
test('the cable code outside the shared block is the same text in both editors', () => {
  for (const [a, b] of [['const fbOk = ', '// Tell the engine a cable\'s mark'], ['function placeCable(', 'function drawCables']]) {
    const x = block(ED.chains, a, b), y = block(ED.waves, a, b);
    assert.equal(x.replace(/\s+/g, ' '), y.replace(/\s+/g, ' '), a);
  }
});

/* ---------- logic: reaches / fbFix on small graphs, with the real text ---------- */
for (const [ed, src] of Object.entries(ED)) {
  test(ed + ' logic: reaches ignores marked cables; a module reaches itself; a simple loop is detected and then marked', () => {
    const doc = { cables: [cab('a', 'b'), cab('b', 'c')] }, f = editorFns(src, doc);
    assert.equal(f.reaches('a', 'c'), true); assert.equal(f.reaches('c', 'a'), false); assert.equal(f.reaches('a', 'a'), true);
    doc.cables.push(cab('c', 'a'));   // the editor's connect(): reaches('a', 'c') says a loop, so the new cable is marked
    doc.cables[2].fb = true;
    assert.equal(f.reaches('a', 'c'), true, 'a → b → c still reaches, over the unmarked cables');
    assert.equal(f.reaches('c', 'a'), false, 'a loop already closed by a marked cable is not a loop for ordering');
    assert.equal(JSON.stringify(f.fbFix(doc.cables)), '[]'); assert.equal(marks(doc.cables), '001');
  });
  test(ed + ' logic: a module patched into itself keeps its mark', () => {
    const doc = { cables: [cab('a', 'a', true), cab('a', 'b')] }, f = editorFns(src, doc);
    assert.equal(JSON.stringify(f.fbFix(doc.cables)), '[]'); assert.equal(marks(doc.cables), '10');
    const old = { cables: [cab('a', 'a')] }; assert.equal(editorFns(src, old).fbFix(old.cables).length, 1); assert.equal(marks(old.cables), '1', 'an old self-patch is marked');
  });
  test(ed + ' logic: two nested loops are each closed by one marked cable (the latest cable on a cycle), until no cycle is left', () => {
    // a → b → c → d, d → c (inner), d → a (outer); neither marked (an old patch)
    const doc = { cables: [cab('a', 'b'), cab('b', 'c'), cab('c', 'd'), cab('d', 'c'), cab('d', 'a')] }, f = editorFns(src, doc);
    const ch = f.fbFix(doc.cables);
    assert.equal(ch.length, 2); assert.equal(marks(doc.cables), '00011', 'both late cables are marked');
    assert.equal(f.fbCyclic(doc.cables), false);
    assert.equal(JSON.stringify(f.fbFix(doc.cables)), '[]', 'a second pass changes nothing');
    // two loops that share a cable need only one mark: a → b → a and a → b → c → a: b → a and c → a are marked, a → b stays
    const d2 = { cables: [cab('a', 'b'), cab('b', 'a'), cab('b', 'c'), cab('c', 'a')] }, g = editorFns(src, d2);
    g.fbFix(d2.cables); assert.equal(g.fbCyclic(d2.cables), false); assert.equal(marks(d2.cables), '0101');
  });
  test(ed + ' logic: a stale mark goes when the loop is cut; a mark that still closes a loop stays', () => {
    const doc = { cables: [cab('a', 'b'), cab('b', 'c'), cab('c', 'a', true)] }, f = editorFns(src, doc);
    assert.equal(JSON.stringify(f.fbFix(doc.cables)), '[]');
    const cut = doc.cables.filter(c => c.from[0] !== 'b');   // delete b → c
    const ch = f.fbFix(cut); assert.equal(ch.length, 1); assert.equal(ch[0].from[0], 'c'); assert.equal('fb' in cut[1], false, 'the key is gone, not false');
    const two = { cables: [cab('a', 'b'), cab('b', 'a', true), cab('b', 'a', true, 'outR', 'inR')] }, g = editorFns(src, two);
    assert.equal(JSON.stringify(g.fbFix(two.cables)), '[]', 'a stereo pair of loop cables both stay marked');
  });
  test(ed + ' logic: an old loop with no marks is marked, the last cable of the list that lies on the cycle', () => {
    const doc = { cables: [cab('in', 'x'), cab('x', 'y'), cab('y', 'x'), cab('x', 'out')] }, f = editorFns(src, doc);
    const ch = f.fbFix(doc.cables); assert.equal(ch.length, 1); assert.equal(marks(doc.cables), '0010');
  });
  test(ed + ' logic: deterministic: the same marks for any order of the cables that are not in the loop; the loop cables keep their relative order', () => {
    const loop = [cab('x', 'y'), cab('y', 'z'), cab('z', 'x')], others = [cab('in', 'x'), cab('z', 'out'), cab('q', 'r'), cab('r', 's')];
    const run = list => { const d = { cables: list.slice() }; editorFns(src, d).fbFix(d.cables); return d.cables.filter(c => c.fb).map(c => c.from[0] + '>' + c.to[0]).join(); };
    const base = run([...others, ...loop]);
    assert.equal(base, 'z>x');
    assert.equal(run([...loop, ...others]), base); assert.equal(run([others[2], loop[0], others[0], loop[1], others[3], loop[2], others[1]]), base);
    assert.equal(run([...others].reverse().concat(loop)), base);
  });
  test(ed + ' logic: a patch with no loop is left as it was, byte for byte (no key added, nothing returned)', () => {
    const cables = [cab('m1', 'm2'), cab('m1', 'm2', false, 'outR', 'inR'), cab('m2', 'm3'), cab('m3', 'm4'), cab('m5', 'm2', false, 'out', 'p:time')], d = { cables }, before = JSON.stringify(cables);
    assert.equal(JSON.stringify(editorFns(src, d).fbFix(cables)), '[]'); assert.equal(JSON.stringify(cables), before);
    // a marked cable is saved with the key, an unmarked one without it
    assert.ok(!/"fb"/.test(before)); const m = [cab('a', 'b'), cab('b', 'a', true)]; assert.ok(/"fb":true/.test(JSON.stringify(m)));
    // malformed cables do not throw
    assert.equal(JSON.stringify(editorFns(src, { cables: [] }).fbFix([null, {}, { from: 1 }, cab('a', 'b')])), '[]');
  });
  test(ed + ' logic: many cables stay quick (a 60-module chain with 40 back cables)', () => {
    const cables = []; for (let i = 0; i < 59; i++) cables.push(cab('m' + i, 'm' + (i + 1)));
    for (let i = 0; i < 40; i++) cables.push(cab('m' + (59 - i), 'm' + i));
    const d = { cables }, f = editorFns(src, d), t = Date.now(); f.fbFix(cables); assert.ok(Date.now() - t < 1500, 'took ' + (Date.now() - t) + ' ms'); assert.equal(f.fbCyclic(cables), false);
  });
}

/* ---------- the host page: the song's copy of the patch and the live engines' ---------- */
const D = (x, y, m) => assert.equal(JSON.stringify(x), JSON.stringify(y), m);
function hostFns() {
  const po = block(HOST, 'function patchOp(d, op){', '/* A track that gains a Chain Out'), cd = block(HOST, 'function chnDiff(prev, next){', 'function chainsNode(');
  const ctx = { chnClone: x => JSON.parse(JSON.stringify(x)), out: null }; vm.createContext(ctx);
  vm.runInContext(po + '\n' + cd + '\nout = { patchOp, chnDiff };', ctx); return ctx.out;
}
test('host patchOp: a connect op carries fb:true onto the cable; connecting again with another mark only changes the mark; no key when unmarked', () => {
  const { patchOp } = hostFns(), d = { modules: [], cables: [] };
  patchOp(d, { op: 'connect', a: ['a', 'out'], b: ['b', 'in'] }); assert.equal(JSON.stringify(d.cables), '[{"from":["a","out"],"to":["b","in"]}]');
  patchOp(d, { op: 'connect', a: ['b', 'out'], b: ['a', 'in'], fb: true }); assert.equal(d.cables[1].fb, true);
  patchOp(d, { op: 'connect', a: ['b', 'out'], b: ['a', 'in'], fb: true }); assert.equal(d.cables.length, 2, 'the same cable again is not a second cable');
  patchOp(d, { op: 'connect', a: ['b', 'out'], b: ['a', 'in'] }); assert.equal(d.cables.length, 2); assert.equal('fb' in d.cables[1], false, 'the mark is taken off');
  patchOp(d, { op: 'connect', a: ['a', 'out'], b: ['b', 'in'], fb: true }); assert.equal(d.cables[0].fb, true);
  patchOp(d, { op: 'connect', a: ['a', 'out'], b: ['b', 'in'], fb: false }); assert.equal('fb' in d.cables[0], false);
});
test('host chnDiff: a new marked cable is connected with fb:true; a cable whose mark changed is connected again with the new mark; no mark, no key', () => {
  const { chnDiff } = hostFns(), mod = id => ({ id, type: 'atten', params: {}, pos: [0, 0] });
  const base = { format: 'chains.patch', modules: [mod('a'), mod('b')], cables: [cab('a', 'b')] };
  const clone = x => JSON.parse(JSON.stringify(x));
  let next = clone(base); next.cables.push(cab('b', 'a', true));
  D(chnDiff(base, next), [{ op: 'connect', a: ['b', 'out'], b: ['a', 'in'], fb: true }]);
  next = clone(base); next.cables.push(cab('b', 'a'));
  D(chnDiff(base, next), [{ op: 'connect', a: ['b', 'out'], b: ['a', 'in'] }], 'an unmarked cable has no fb key');
  const prev = clone(base); prev.cables.push(cab('b', 'a'));
  next = clone(prev); next.cables[1].fb = true;
  D(chnDiff(prev, next), [{ op: 'connect', a: ['b', 'out'], b: ['a', 'in'], fb: true }], 'marked later');
  D(chnDiff(next, prev), [{ op: 'connect', a: ['b', 'out'], b: ['a', 'in'], fb: false }], 'mark taken off');
  D(chnDiff(next, clone(next)), [], 'nothing changed, nothing sent');
  D(chnDiff(base, clone(base)), []);
  const cut = clone(next); cut.cables.pop(); D(chnDiff(next, cut), [{ op: 'disconnect', a: ['b', 'out'], b: ['a', 'in'] }]);
  assert.equal(chnDiff(null, next)[0].op, 'patch', 'a whole patch carries its cables (and their marks) as it is');
});
test('host: the bottom-panel rail orders modules without the marked cable; the track-routing plan (plan.loop) is untouched', () => {
  assert.match(HOST, /by\.has\(c\.from\[0\]\) && by\.has\(c\.to\[0\]\) && c\.from\[0\] !== c\.to\[0\] && c\.fb !== true\)\{/);
  const plan = block(HOST, 'for (const c of d.cables || []) if (c && c.from && c.to){ ms(c.from[0]); mx(59);', 'if (busPlanVal && busPlanH === h)');
  assert.doesNotMatch(plan, /\bfb\b/);
});

/* ---------- engine round trip (chains): an old patch with an unmarked loop, marked by fbFix, runs as a marked loop does ---------- */
test('chains engine: fbFix on an old unmarked loop gives the same audio as the marked loop, whatever the module order', () => {
  const N = 128, mod = (id, type, params) => ({ id, type, scope: 'mono', params });
  const mk = () => { const ctx = H.freezeClock(H.workletScope()); H.load(ctx, 'chains/chains-worklet.js'); return new ctx.ChainsDSP.ChainsEngine(48000, { focus: 0, quality: 'full', transport: { tempo: 120, lpb: 4, ticks: 6, playing: true } }); };
  const all = { a: mod('a', 'trackIn', { track: 1 }), x: mod('x', 'atten', { amount: 1, offset: 0 }), y: mod('y', 'atten', { amount: .5, offset: 0 }), c: mod('c', 'chainOut', { track: 1, level: 1 }) };
  const cables = () => [cab('a', 'x'), cab('x', 'c'), cab('x', 'y'), cab('y', 'x')];
  const render = patch => { const E = mk(); E.loadPatch(patch); const imp = new Float32Array(N * 6); imp[5] = 1; const out = new Float32Array(N * 6);
    for (let b = 0; b < 6; b++){ const s = imp.subarray(b * N, (b + 1) * N); E.setTrackInputs(s, s); E.process(N); out.set(E.bus(1), b * N); } return out; };
  const doc = src => ({ modules: ['y', 'c', 'x', 'a'].map(k => all[k]), cables: src });
  const fixed = cables(); const ch = editorFns(ED.chains, { cables: fixed }).fbFix(fixed); assert.equal(ch.length, 1); assert.equal(marks(fixed), '0001');
  const ref = cables(); ref[3].fb = true;
  const A = render(doc(fixed)), B = render(doc(ref));
  assert.deepEqual(A, B);
  for (let k = 0; k < 5; k++) assert.equal(A[5 + k * N], Math.fround(Math.pow(.5, k)), 'pass ' + k);
});
