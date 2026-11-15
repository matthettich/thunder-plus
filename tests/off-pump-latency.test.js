// OFF PUMP SLOTS' LATENCY (build 603; owner, 14 Nov: "renders should sound like the playback"). A switched-OFF pump effect is the
// plugin's own bypass, which still delays the sound by the plugin's latency. The page counted only slots that are ON, so a render
// came out late by the OFF slots' latency (his song: CALM 2048 + TONE 2048 + MASTER 358 = 4454 samples) and LATENCY COMP lined
// tracks up without them. Two halves: (1) the real pump.wasm: bypassed, each look-ahead effect delays an impulse by exactly what
// pump_latency reports (the premise); (2) the page's count (cut out of index.html): ON or OFF counts, an OFF METER (no bypass of
// its own: the worklet skips it whole) does not, and pdcPlan sees a track's OFF slot.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm'), fs = require('fs'), path = require('path');
const H = require('./helpers');
const src = H.read('index.html');
const cut = (re, what) => { const m = src.match(re); assert.ok(m, `index.html: could not find ${what}; the code moved or changed shape, so this test needs updating`); return m[0]; };
const meta = vm.createContext({}); meta.globalThis = meta; vm.runInContext(H.read('pump/pump-meta.js'), meta);
const PM = meta.PUMP_META, SR = 48000;

function pump(){
  const z = () => 0, x = new WebAssembly.Instance(new WebAssembly.Module(fs.readFileSync(path.join(H.ROOT, 'pump/pump.wasm'))), {wasi_snapshot_preview1:{fd_close:z, fd_seek:z, fd_write:z, proc_exit:z}}).exports;
  if (x._initialize) x._initialize(); return x;
}
// One effect at its own defaults with Bypass on: where does an impulse come out?
function bypassDelay(x, type){
  const fx = PM.fx[type], pt = PM.types.indexOf(type), st = new Float32Array(x.memory.buffer, x.pump_stage(), 512);
  fx.params.forEach((p, i) => st[i] = p.s === 'bypass' ? 1 : p.def);
  const h = x.pump_new(pt, SR); assert.ok(h >= 0, type + ' starts');
  const lat = x.pump_latency(h); let at = -1, pos = 0;
  for (let b = 0; b < 80 && at < 0; b++, pos += 128){
    let io = [0, 1].map(c => new Float32Array(x.memory.buffer, x.pump_io(c), 128));
    io[0].fill(0); io[1].fill(0); if (b === 0){ io[0][0] = 0.5; io[1][0] = 0.5; }
    x.pump_process(h, 128, 0);
    io = [0, 1].map(c => new Float32Array(x.memory.buffer, x.pump_io(c), 128));
    for (let k = 0; k < 128; k++) if (Math.abs(io[0][k]) > 0.05){ at = pos + k; break; }
  }
  x.pump_free(h); return {lat, at};
}
test('pump.wasm: a bypassed look-ahead effect still delays by its reported latency', () => {
  const x = pump(); let late = 0;
  for (const type of ['calm', 'tone', 'limit', 'master']){
    const r = bypassDelay(x, type);
    assert.equal(r.at, r.lat, `${type}: bypassed, the impulse comes out at ${r.at}, pump_latency says ${r.lat}`);
    if (r.lat > 0) late++;
  }
  assert.ok(late >= 2, 'at least CALM and TONE have latency at their defaults (else this test proves nothing)');
});

function page(lat){
  const c = vm.createContext({Math, Map, Set, NT:32, aset:{pdc:true}, TFX:{pcalm:{pump:'calm'}, pmeter:{pump:'meter'}, sdelay:{}},
    pumpIdx:{calm:{bypass:9}, meter:{}}, pumpLatencyOf:f => lat[f.type] || 0});
  c.isPumpT = k => !!(c.TFX[k] && c.TFX[k].pump);
  vm.runInContext(cut(/const pumpSlotLat = [^\n]*\nconst pumpChainLatency = [^\n]*\n/, 'pumpSlotLat / pumpChainLatency') + '\nglobalThis.pumpChainLatency = pumpChainLatency;', c);
  return c;
}
test('the page counts a pump slot ON or OFF; an OFF METER (no bypass) and other effects add nothing', () => {
  const c = page({pcalm:2048, pmeter:64, sdelay:999}), L = A => c.pumpChainLatency(A, SR);
  assert.equal(L([{type:'pcalm', on:true}]), 2048, 'ON');
  assert.equal(L([{type:'pcalm', on:false}]), 2048, 'OFF: the bypass still delays, so it counts');
  assert.equal(L([{type:'pmeter', on:true}]), 64, 'METER on');
  assert.equal(L([{type:'pmeter', on:false}]), 0, 'METER off is skipped by the worklet: nothing');
  assert.equal(L([{type:'sdelay', on:true}, null, {type:'pcalm', on:false}, {type:'pcalm', on:true}]), 4096, 'a chain adds up; other effects and empty slots do not count');
  assert.equal(L(null), 0);
});
test('LATENCY COMP: a track whose pump effect is OFF is as late as one whose effect is ON', () => {
  const c = page({pcalm:2048});
  c.S = {tfx:Array.from({length:32}, () => [])}; c.S.tfx[3] = [{type:'pcalm', on:false}];
  c.busPlan = () => ({feeds:new Map(), pure:new Set()});
  vm.runInContext(cut(/function pdcPlan\(sr\)\{[\s\S]*?\n\}\n/, 'pdcPlan'), c);
  const p = c.pdcPlan(SR);
  assert.ok(Math.abs(p.T - 2048 / SR) < 1e-12, 'everything lines up to the OFF slot’s latency');
  assert.equal(p.d.get(3), 0, 'that track is not delayed further');
  assert.ok(Math.abs(p.d.get(0) - 2048 / SR) < 1e-12, 'the others wait for it');
});
