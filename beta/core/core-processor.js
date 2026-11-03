/* THUNDER CORE: the sequencer on the audio thread (native plan, docs/NATIVE-DESKTOP-PLAN.md, phase 2).
   This file is appended to the declarations tools/make-core.mjs copies out of index.html (makeSeq and what it reads), and
   the whole is core/seq-core.js, loaded with audioWorklet.addModule. Edit this file, then run node tools/make-core.mjs.

   Messages in (from the page, node.port):
     {op:'load', S, vars}        the song (S) and the few page settings the sequencer reads (vtrk, NSLOT, TFX: coreTfx())
     {op:'test', id, mode, max, opt}  run the sequencer at once (sim, seeded random) and post every row back: the parity
                                 check against the page's own makeSeq (tools/seq-golden.mjs --core)
     {op:'play', mode, row, loop, t} start: rows are stepped on the audio thread as audio time reaches them
     {op:'stop'}
   Messages out:
     {op:'ready'}  ·  {op:'test', id, rows}  ·  {op:'rows', rows}  (live: each row's events, booked LEAD ahead)  ·
     {op:'error', where, message} */

const CORE_V = 1;
const coreNum = v => Math.round(v * 1e6) / 1e6;
// A row's events as plain data (the same shape as the page's test hook writes, so the streams compare exactly)
const corePlain = e => { const o = {}; for (const k of Object.keys(e).sort()){ const v = e[k];
  if (typeof v === 'number') o[k] = coreNum(v); else if (typeof v === 'string' || typeof v === 'boolean' || v === null) o[k] = v;
  else if (Array.isArray(v) && v.every(x => typeof x === 'number')) o[k] = v.map(coreNum); } return o; };
// The test hook's seeded Math.random (same sequence as index.html's __seqTest.run)
function coreSeeded(f){
  const R = Math.random; let s = 0x2545F491;
  Math.random = () => { let x = s += 0x6D2B79F5; x = Math.imul(x ^ x >>> 15, x | 1); x ^= x + Math.imul(x ^ x >>> 7, x | 61); return ((x ^ x >>> 14) >>> 0) / 4294967296; };
  try { return f(); } finally { Math.random = R; }
}

class ThunderCore extends AudioWorkletProcessor {
  constructor(){
    super();
    this.seq = null; this.LEAD = 0.25;
    this.port.onmessage = e => { try { this.msg(e.data || {}); } catch(err){ this.port.postMessage({op:'error', where:(e.data && e.data.op) || '?', message:String(err && err.stack || err)}); } };
    this.port.postMessage({op:'ready', v:CORE_V});
  }
  msg(d){
    if (d.op === 'load'){ S = d.S; if (d.vars){ if ('vtrk' in d.vars) vtrk = d.vars.vtrk; if ('NSLOT' in d.vars) NSLOT = d.vars.NSLOT; if (d.vars.TFX) TFX = d.vars.TFX; } busPlanH = null; busPlanVal = null; return; }
    if (d.op === 'test'){
      const o = d.opt || {}, rows = coreSeeded(() => { const out = [];
        const sq = makeSeq(S, d.mode || 'song', {row:o.row || 0, loop:o.loop || null, once:!o.loop, sim:true});
        for (let n = 0; sq.alive() && n < (d.max || 20000); n++){ const r = sq.step(); out.push({t:coreNum(r.t), evs:r.evs.map(corePlain)}); }
        return out; });
      this.port.postMessage({op:'test', id:d.id, rows}); return;
    }
    if (d.op === 'play'){ this.seq = makeSeq(S, d.mode || 'song', {row:d.row || 0, loop:d.loop || null, once:!d.loop, sim:true}); this.seq.time = d.t; this.seq.base = null; return; }
    if (d.op === 'stop'){ this.seq = null; return; }
  }
  process(){
    const s = this.seq; if (!s) return true;
    const rows = []; let n = 0;
    while (s.alive() && s.time < currentTime + this.LEAD && n++ < 64){ const r = s.step(); rows.push({t:r.t, evs:r.evs, pos:r.pos, tks:r.tks, line:r.line, bpm:s.bpm}); }
    if (rows.length) this.port.postMessage({op:'rows', rows});
    if (!s.alive()) this.seq = null;
    return true;
  }
}
registerProcessor('thunder-core', ThunderCore);
