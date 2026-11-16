// U14: shared by fx-table.mjs and fx-dump.mjs. Cuts the page's track-effect command layer (TFX, normTfx, tfxChain, ...) and the real
// ThunderFx worklet source (TFX_SRC) out of index.html with acorn, runs them in Node's vm. Reads index.html with \r\n -> \n. Needs acorn
// (ACORN env, /opt/npm-tools/node_modules, <repo>/node_modules, /usr/lib/node_modules, /usr/local/lib/node_modules); exit 77 (skipped) when missing, exit 1 when the environment variable CI is set (GitHub Actions sets it: a missing acorn there must fail, not skip).
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';

export const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
export const SR = 48000;
const rd = f => fs.readFileSync(path.join(ROOT, f), 'utf8').replace(/\r\n/g, '\n');

function findAcorn() {
  for (const d of [process.env.ACORN, '/opt/npm-tools/node_modules', path.join(ROOT, 'node_modules'), '/usr/lib/node_modules', '/usr/local/lib/node_modules'])
    if (d && fs.existsSync(path.join(d, 'acorn'))) return createRequire(path.join(d, 'x.js'))(path.join(d, 'acorn'));
  console.error('fx-src: acorn not found (set ACORN to the folder that holds it)' + (process.env.CI ? '; CI: this is a failure, the workflow must install acorn' : '; skipped')); process.exit(process.env.CI ? 1 : 77);
}

let CACHE = null;
export function load() {
  if (CACHE) return CACHE;
  const acorn = findAcorn(), h = rd('index.html');
  const i0 = h.indexOf('<script>\n/* ===ENGINE START=== */'), s0 = i0 + 9, src = h.slice(s0, h.indexOf('\n</script>', s0));
  const ast = acorn.parse(src, { ecmaVersion: 'latest', sourceType: 'script', allowReturnOutsideFunction: true }), body = [...ast.body];
  for (const st of ast.body) { const c = st.type === 'ExpressionStatement' && st.expression.type === 'CallExpression' && st.expression.callee; if (c && c.body && c.body.body && c.end - c.start > 200000) body.push(...c.body.body); }
  const by = new Map(), pos = new Map();
  for (const st of body) {
    if (st.type === 'VariableDeclaration') for (const d of st.declarations) { if (d.id.name && !by.has(d.id.name)) { by.set(d.id.name, src.slice(st.start, st.end)); pos.set(d.id.name, st.start); } }
    else if ((st.type === 'FunctionDeclaration' || st.type === 'ClassDeclaration') && !by.has(st.id.name)) { by.set(st.id.name, src.slice(st.start, st.end)); pos.set(st.id.name, st.start); }
  }
  // the command layer: TFX, normTfx, tfxChain (their helpers are found by the ReferenceErrors they raise)
  const need = ['clamp', 'u8', 'sdb', 'pc', 'khz', 'P_', 'dlySec', 'int', 'EN', 'FlexCore', 'StepGateCore', 'GrainsCore', 'RetrigCore', 'VSampCore', 'SPACE_SIZE_MAX', 'fxRestartN', 'freshTfx', 'normTfx', 'tfxChain', 'TFX'], stub = ['pumpFresh', 'pumpNorm1', 'pumpSync', 'pumpFx', 'pumpIdx', 'PUMP_CREATIVE', 'PUMP_STATE_UI', 'PUMP_STATE_OUT', 'isPumpT'];
  let ctx, ok = false;
  for (let tries = 0; tries < 80 && !ok; tries++) {
    ctx = vm.createContext({ S: { bpm: 120 }, Math, Number, Array, Object, String, JSON, Map, Set, Float32Array });
    for (const n of stub) vm.runInContext(`globalThis.${n} = function(){ throw new Error('pump: not in this tool'); };`, ctx);
    const pre = [...new Set(need.filter(n => !stub.includes(n) && by.has(n)).sort((x, y) => pos.get(x) - pos.get(y)).map(n => by.get(n)))].join('\n').replace(/^let fxRestartN/m, 'var fxRestartN');
    try { vm.runInContext(pre + '\nglobalThis.__T = { TFX, normTfx, tfxChain, freshTfx };', ctx); ok = true; }
    catch (e) { const m = /^(\w+) is not defined/.exec(e.message) || /^Cannot access '(\w+)' before/.exec(e.message); if (!m) throw e; if (!by.has(m[1]) || need.includes(m[1])) throw e; need.push(m[1]); }
  }
  const T = ctx.__T;
  // the worklet: the cores it names (WK), then TFX_SRC as the page builds it
  const WK = ['rvMix', 'DegradeCore', 'VSampCore', 'ResonCore', 'PitchFxCore', 'clkSpb', 'FlexCore', 'StepGateCore', 'RetrigCore', 'beatLock', 'bkHash', 'GrainsCore', 'ScrambleCore', 'GlitchCore'];
  const a = h.indexOf('const TFX_SRC = `') + 'const TFX_SRC = `'.length, b = h.indexOf('`;\nlet tfxBytes', a);
  const workletSrc = WK.map(n => by.get(n)).join('\n') + '\n' + new Function('return `' + h.slice(a, b) + '`')();
  const tfxWasm = fs.readFileSync(path.join(ROOT, 'dsp/tfx/../../chains/dsp/tfx.wasm'));
  CACHE = { ctx, TFX: T.TFX, normTfx: T.normTfx, tfxChain: T.tfxChain, freshTfx: T.freshTfx, workletSrc, tfxWasm, by, setRestartN: v => { ctx.fxRestartN = v; } };
  return CACHE;
}
// the 34 wasm effects in id order: [{key, id, ps:[{k,d,min,max}], mapSrc}]
export function effects() {
  const { TFX } = load(), out = [];
  for (const key of Object.keys(TFX)) { const D = TFX[key]; if (D.id === undefined || D.id > 33 || D.pump || D.mod) continue; out.push({ key, id: D.id, ps: D.ps.map(p => ({ k: p.k, d: p.d, min: p.min, max: p.max })), mapSrc: String(D.map) }); }
  out.sort((x, y) => x.id - y.id); return out;
}
