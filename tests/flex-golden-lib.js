// Shared by tests/flex-golden.test.js and tools/make-flex-golden.js: renders FlexCore (every pattern 1..511) and StepGateCore
// (patterns 0..15) from a source text (index.html, or a generated copy in chains/ or waves/) and hashes the output.
'use strict';
const crypto = require('node:crypto');
const H = require('./helpers');
const SR = 48000, BLOCKS = 22;
const SIG = H.testSignal(128 * 700);
function cores(src) {
  const cut = re => { const m = src.match(re); if (!m) throw new Error('cut ' + re); return m[0]; };
  return new Function(cut(/function clkSpb\(o, bp, n\)\{[\s\S]*?\n\}\n/) + cut(/class FlexCore \{[\s\S]*?\n\}\n/) + cut(/class StepGateCore \{[\s\S]*?\n\}\n/) + 'return {FlexCore, StepGateCore};')();
}
function render(C, kind, k) {
  const f = kind === 'flex' ? new C.FlexCore(SR) : new C.StepGateCore(SR), out = [];
  const dp = kind === 'flex' ? { pat: k, mix: 1, xf: 5, wait: false } : { pat: k, m1: 0xA5, m2: 0x5A, div: k % 5, len: .6, depth: 1, smooth: 3, wait: false };
  for (let b = 0; b < BLOCKS; b++) {
    const L = new Float32Array(128), R = new Float32Array(128); for (let i = 0; i < 128; i++) { L[i] = SIG[b * 128 + i]; R[i] = SIG[(b + 3) * 128 + i]; }
    const bp = Float32Array.from({ length: 128 }, (_, i) => ((b * 128 + i) * 0.009) % 16);
    f.process(L, R, 128, dp, { bp, flx: -1, gat: -1, sw: -1 }); out.push(L, R);
  }
  return out;
}
// sha256 of the exact float32 bytes (NaN payloads and -0 included), first 16 hex digits
const hash = arrs => { const h = crypto.createHash('sha256'); for (const a of arrs) h.update(new Uint8Array(a.buffer, a.byteOffset, a.byteLength)); return h.digest('hex').slice(0, 16); };
function hashes(src) {
  const C = cores(src), flex = {}, gate = {};
  for (let k = 1; k < 512; k++) flex[k] = hash(render(C, 'flex', k));
  for (let k = 0; k < 16; k++) gate[k] = hash(render(C, 'gate', k));
  return { flex, gate };
}
module.exports = { cores, render, hash, hashes, BLOCKS, SR };
