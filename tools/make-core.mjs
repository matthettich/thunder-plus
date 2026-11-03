// CORE BUNDLE GENERATOR (native plan, phase 2: the sequencer on the audio thread).
//
// The app has no build step and keeps one copy of its code in index.html. The audio-thread core (core/seq-core.js, an
// AudioWorklet module) needs the sequencer, makeSeq, and everything it reads: this tool finds those declarations in
// index.html (scope-aware, with acorn) and copies their exact source into core/seq-core.js, then appends the hand-written
// processor from core/core-processor.js. So the sequencer is still edited in one place, index.html.
//
//   node tools/make-core.mjs           write core/seq-core.js
//   node tools/make-core.mjs --check   exit 1 if core/seq-core.js is out of date (tests/core.test.js runs this)
//
// Needs acorn and acorn-walk (npm i -g acorn acorn-walk, or ACORN=/path/to/node_modules).
import fs from 'fs'; import path from 'path'; import { fileURLToPath, pathToFileURL } from 'url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const NM = [process.env.ACORN, '/opt/npm-tools/node_modules', path.join(ROOT, 'node_modules'), '/usr/lib/node_modules', '/usr/local/lib/node_modules'].find(d => d && fs.existsSync(path.join(d, 'acorn')));
if (!NM){ console.error('acorn not found: npm i -g acorn acorn-walk, or set ACORN=/path/to/node_modules'); process.exit(2); }
const acorn = await import(pathToFileURL(path.join(NM, 'acorn/dist/acorn.mjs')).href);
const walk = await import(pathToFileURL(path.join(NM, 'acorn-walk/dist/walk.mjs')).href);

// What the core starts from, and what it must not take: the render helpers (the main thread prepares buffers) and the
// live song / kit, which the core keeps its own copies of (set by the processor's messages).
const ROOTS = ['makeSeq', 'trackGain', 'curve', 'shapeData', 'sfPick', 'sfVoice'];   // (trackGain, curve: the core's mix outputs, TVA and the master VOL; shapeData: CUT / RES / DEC)
// TFX (the effect table) holds DSP and display code from other scripts; the core only reads each effect's pump flag and
// its controls' k / min / max, which the page sends (coreTfx in index.html).
const STOP = new Set(['ensurePlay', 'ensure', 'varWant', 'applyChop', 'S', 'kit', 'TFX']);

export function build(){
  const html = fs.readFileSync(path.join(ROOT, 'index.html'), 'utf8');
  // the main script: the <script> holding the ENGINE block
  const i0 = html.indexOf('<script>\n/* ===ENGINE START=== */'); if (i0 < 0) throw new Error('main script not found');
  const s0 = i0 + '<script>\n'.length, s1 = html.indexOf('\n</script>', s0), src = html.slice(s0, s1);
  const ast = acorn.parse(src, {ecmaVersion:'latest', sourceType:'script', allowReturnOutsideFunction:true});
  // top-level statements, and those of the big IIFE that wraps most of the app
  const body = [...ast.body];
  for (const st of ast.body){ const c = st.type === 'ExpressionStatement' && st.expression.type === 'CallExpression' && st.expression.callee;
    if (c && c.body && c.body.body && c.end - c.start > 200000) body.push(...c.body.body); }
  const collect = (p, out) => { if (!p) return; if (p.type === 'Identifier') out.push(p.name); else if (p.type === 'ObjectPattern') p.properties.forEach(q => collect(q.value || q.argument, out));
    else if (p.type === 'ArrayPattern') p.elements.forEach(e => collect(e, out)); else if (p.type === 'RestElement') collect(p.argument, out); else if (p.type === 'AssignmentPattern') collect(p.left, out); };
  const decl = new Map();
  for (const st of body){
    if (st.type === 'FunctionDeclaration') decl.set(st.id.name, {node:st, text:src.slice(st.start, st.end)});
    else if (st.type === 'VariableDeclaration') for (const d of st.declarations){ const ns = []; collect(d.id, ns);
      for (const n of ns) if (!decl.has(n)) decl.set(n, {node:d, text:st.kind + ' ' + src.slice(d.start, d.end) + ';'}); }
  }
  // free identifiers of a declaration (property names and its own locals left out)
  function free(node){
    const locals = new Set(), used = new Set();
    walk.fullAncestor(node, (n, st, anc) => {
      if (n.type === 'VariableDeclarator'){ const o = []; collect(n.id, o); o.forEach(x => locals.add(x)); }
      if (/Function/.test(n.type)){ if (n.id && n !== node) locals.add(n.id.name); n.params.forEach(p => { const o = []; collect(p, o); o.forEach(x => locals.add(x)); }); }
      if (n.type === 'CatchClause' && n.param){ const o = []; collect(n.param, o); o.forEach(x => locals.add(x)); }
      if (n.type === 'Identifier'){ const p = anc[anc.length - 2];
        if (p && p.type === 'MemberExpression' && p.property === n && !p.computed) return;
        if (p && (p.type === 'Property' || p.type === 'MethodDefinition') && p.key === n && !p.computed && !p.shorthand) return;
        if (p && p.type === 'VariableDeclarator' && p.id === n && p === node) return;
        used.add(n.name); }
    });
    return [...used].filter(x => !locals.has(x) || x === (node.id && node.id.name));
  }
  // FXV ON THE AUDIO THREAD (3 Nov): each effect's map() (its controls → the numbers its DSP takes), copied out of TFX's
  // entries one by one, with whatever top-level helpers they read (u8, clamp, dlySec…). An entry whose map reads anything
  // else (a closure, page state) is left out: its FXV stays on the page.
  const mapSrc = [], mapNeed = [];
  { const T = decl.get('TFX'), lit = T && T.node.init;
    if (lit && lit.type === 'ObjectExpression') for (const pr of lit.properties){
      if (pr.type !== 'Property' || pr.computed || !pr.value || pr.value.type !== 'ObjectExpression') continue;
      const key = pr.key.type === 'Identifier' ? pr.key.name : pr.key.value;
      const m = pr.value.properties.find(x => x.type === 'Property' && !x.computed && (x.key.name || x.key.value) === 'map');
      if (!m || !/Function/.test(m.value.type)) continue;
      const need = free(m.value), bad = need.filter(x => !decl.has(x) && !(x in globalThis) || STOP.has(x));
      if (bad.length) continue;
      mapSrc.push(JSON.stringify(key) + ':' + src.slice(m.value.start, m.value.end)); mapNeed.push(...need.filter(x => decl.has(x))); } }
  const seen = new Map(), q = [...ROOTS, ...mapNeed];
  while (q.length){ const n = q.pop(); if (seen.has(n) || STOP.has(n)) continue; const d = decl.get(n); if (!d) continue; seen.set(n, d);
    for (const x of free(d.node)) if (decl.has(x) && !seen.has(x) && !STOP.has(x)) q.push(x); }
  // one copy of each declaration (a line like `const A = 1, B = 2` gives two), in source order
  const parts = [...new Map([...seen.values()].map(d => [d.node.start, d])).values()].sort((x, y) => x.node.start - y.node.start);
  const proc = fs.readFileSync(path.join(ROOT, 'core/core-processor.js'), 'utf8');
  return '// GENERATED by tools/make-core.mjs from index.html (' + parts.length + ' declarations): do not edit here.\n'
    + '// Edit index.html (or core/core-processor.js) and run: node tools/make-core.mjs\n'
    + '/* eslint-disable */\n'
    + '// The render helpers stay on the main thread: the core is told what they would return.\n'
    + 'function ensurePlay(){ return null; } function ensure(){ return null; } function varWant(){} function applyChop(){}\n'
    + 'let S = null, kit = {voices:[]}, TFX = {};\n'
    + 'const localStorage = {getItem(){ return null; }, setItem(){}};\n\n'
    + parts.map(d => d.text).join('\n') + '\n\n'
    + '// FXV: the effects\' map() functions, from TFX in index.html (' + mapSrc.length + ' of them)\n'
    + 'const TFX_MAP = {\n  ' + mapSrc.join(',\n  ') + '\n};\n\n'
    + '/* ===== core/core-processor.js ===== */\n' + proc;
}

// The bundle's hash goes into index.html (CORE_REV), so its URL changes with it and no cache keeps an old one.
const REV_RE = /const CORE_REV = '[^']*';/;
const revOf = out => { let h = 0x811c9dc5; for (let i = 0; i < out.length; i++){ h ^= out.charCodeAt(i); h = Math.imul(h, 0x01000193); } return (h >>> 0).toString(36); };
if (import.meta.url === pathToFileURL(process.argv[1]).href){
  const out = build(), f = path.join(ROOT, 'core/seq-core.js'), hf = path.join(ROOT, 'index.html'), html = fs.readFileSync(hf, 'utf8');
  const want = "const CORE_REV = '" + revOf(out) + "';";
  if (!REV_RE.test(html)){ console.error('index.html has no CORE_REV line'); process.exit(1); }
  if (process.argv.includes('--check')){
    const have = fs.existsSync(f) ? fs.readFileSync(f, 'utf8') : '';
    if (have !== out){ console.error('core/seq-core.js is out of date: run node tools/make-core.mjs'); process.exit(1); }
    if (!html.includes(want)){ console.error('index.html CORE_REV is out of date: run node tools/make-core.mjs'); process.exit(1); }
    console.log('core/seq-core.js and CORE_REV are up to date');
  } else { fs.writeFileSync(f, out); if (!html.includes(want)) fs.writeFileSync(hf, html.replace(REV_RE, want)); console.log('wrote core/seq-core.js (' + out.length + ' chars), ' + want); }
}
