// STEREO PADS IN RENDERS (15 Nov). A pad with a STEREO sample must come out in stereo in the song render (renderOut) as it does live.
// Loads a 1.2 s stereo WAV (left a 440 Hz sine, right seeded noise) into pad 2 through the app's own loader, plays it from one note, and
// compares the left / right correlation of the RENDER (stereo: near 0; mono: 1) with the audio-thread sequencer and the page sequencer
// live (--live: about 25 s each). A mono control sample (KIND=sine) must render as before (identical channels).
//   node tests/sweep/render-stereo.mjs [--live]
import fs from 'fs';
import { open } from './seq-lib.mjs';
const LIVE = process.argv.includes('--live');
const A = await open(); const { page } = A; const run = src => page.evaluate(s => window.__seqTest.ev(s), `(async()=>{${src}\n})()`);
const PAD = 2; let KIND = 'stereo'; const AMPV = 0.5, LIMV = false, NOWARM = false;
const mkSetup = () => `
 const PAD = ${PAD}, II = S.insts.findIndex(I => I.pad === PAD);
 // a stereo sample: 1.2 s, L = 440 Hz sine, R = seeded noise; a real WAV file through the app's own loader
 const AMP = ${AMPV}, sr = 44100, n = Math.round(sr * 1.2), L = new Float32Array(n), R = new Float32Array(n); let s = 12345;
 for (let i = 0; i < n; i++){ const e = Math.min(1, i / 200, (n - i) / 2000); L[i] = AMP * Math.sin(2 * Math.PI * 440 * i / sr) * e; s = (s * 1103515245 + 12345) & 0x7fffffff; R[i] = AMP * ((s / 0x7fffffff) * 2 - 1) * e; }
 const KIND = '${KIND}', NC = KIND === 'stereo' ? 2 : 1; if (KIND === 'noise') L.set(R); const w = new Uint8Array(44 + n * 2 * NC), dv = new DataView(w.buffer); const ws = (o, t) => { for (let i = 0; i < t.length; i++) w[o + i] = t.charCodeAt(i); };
 ws(0, 'RIFF'); dv.setUint32(4, 36 + n * 2 * NC, true); ws(8, 'WAVEfmt '); dv.setUint32(16, 16, true); dv.setUint16(20, 1, true); dv.setUint16(22, NC, true); dv.setUint32(24, sr, true); dv.setUint32(28, sr * 2 * NC, true); dv.setUint16(32, 2 * NC, true); dv.setUint16(34, 16, true); ws(36, 'data'); dv.setUint32(40, n * 2 * NC, true);
 for (let i = 0; i < n; i++){ dv.setInt16(44 + i * 2 * NC, Math.round(L[i] * 32767), true); if (NC === 2) dv.setInt16(46 + i * 4, Math.round(R[i] * 32767), true); }
 await loadAudioInto(new File([w], 'stereo-test.wav', {type:'audio/wav'}), PAD);
 kit.voices[PAD].layers.forEach((L, i) => { if (i !== kit.voices[PAD].sel) L.on = false; }); changed(PAD);
 const smp = kit.voices[PAD].layers[kit.voices[PAD].sel].sample;
 // a song: instrument 0 (pad 0) plays four notes on track 0, nothing else
 for (let y = 0; y < 256; y++) S.song[y] = S.song[y].map(() => -1);
 S.song[0][0] = 0; S.chains[0] = Array.from({length:16}, (_, k) => k ? [-1, 0] : [0, 0]);
 S.phrases[0] = freshPhrase(); S.plen[0] = 32;
 for (const y of [0]){ S.phrases[0][y][0] = 48; S.phrases[0][y][IC] = II; }
 for (let t = 0; t < NT; t++) S.tfx[t] = S.tfx[t].map(() => null); S.mfx = S.mfx.map(() => null);
 S.mst.on = false; S.limiter = ${LIMV}; S.renderFrom = 0; S.bpm = 120; saveS();
 aset.vol = 100; if (typeof master !== 'undefined' && master) master.gain.value = 1; ex.norm = false; ex.bits = 24; ex.sr = ctx.sampleRate;
 return {PAD, II, stereo: !!smp.data2, n: smp.data.length, sr: smp.sr, appsr: ctx.sampleRate};`;

const stat = d => { const n = d.length / 2; let aL = 0, aR = 0, nA = 0, eL = 0, eR = 0, x = 0;
  for (let i = 0; i < n; i++){ const l = d[2*i], r = d[2*i+1]; if (Math.abs(l) > 1e-4 || Math.abs(r) > 1e-4){ aL += l*l; aR += r*r; nA++; } eL += l*l; eR += r*r; x += l*r; }
  return { rmsL: +Math.sqrt(aL / Math.max(1, nA)).toFixed(4), rmsR: +Math.sqrt(aR / Math.max(1, nA)).toFixed(4), corr: +(x / Math.sqrt(eL * eR + 1e-30)).toFixed(3), energy: eL + eR }; };
const renderIt = async () => { const R = await run(`
  const keep = wavN; window.__W = null; wavN = function(chs, sr, bits){ window.__W = {chs: chs.map(c => Float32Array.from(c)), sr}; return new Uint8Array(1); };
  pickOut = async () => ({dl:true}); writeOut = async () => {};
  try { await renderOut(false); } finally { wavN = keep; }
  if (!window.__W) return null; const f = window.__W; const n = f.chs[0].length, o = new Float32Array(n * 2); for (let i = 0; i < n; i++){ o[2*i] = f.chs[0][i]; o[2*i+1] = f.chs[1][i]; }
  let s = '', u = new Uint8Array(o.buffer); for (let i = 0; i < u.length; i += 32768) s += String.fromCharCode.apply(null, u.subarray(i, i + 32768)); return {sr: f.sr, b: btoa(s)};`);
  return R ? new Float32Array(Buffer.from(R.b, 'base64').buffer.slice(0)) : null; };
const liveIt = async mode => { const ok = await run(`
    const ok = await __seqTest.coreOn(${mode === 'core'}); if (!ok) return 'core did not load';
    rtInit();
    const src = 'class R extends AudioWorkletProcessor{constructor(){super();this.b=[];this.on=false;this.port.onmessage=e=>{if(e.data==="get"){this.port.postMessage({b:this.b});this.b=[];}else if(e.data==="on"){this.on=true;this.b=[];}}}process(i){const x=i[0];if(x&&x.length&&this.on){const l=x[0],r=x[1]||x[0];const o=new Float32Array(l.length*2);for(let k=0;k<l.length;k++){o[2*k]=l[k];o[2*k+1]=r[k];}this.b.push(o);}return true;}}registerProcessor("rec2",R);';
    const url = URL.createObjectURL(new Blob([src], {type:'text/javascript'}));
    if (!window.__recNode){ await ctx.audioWorklet.addModule(url); const n = new AudioWorkletNode(ctx, 'rec2', {numberOfInputs:1, numberOfOutputs:1, outputChannelCount:[2]}); const z = ctx.createGain(); z.gain.value = 0; master.connect(n); n.connect(z); z.connect(ctx.destination); window.__recNode = n; n.port.onmessage = e => { window.__recGot = e.data; }; }
    tStart('song', {row:0}); await new Promise(r => setTimeout(r, 2500)); tStop(); await new Promise(r => setTimeout(r, 2500));
    __recNode.port.postMessage('on'); tStart('song', {row:0}); await new Promise(r => setTimeout(r, 7000)); tStop(); await new Promise(r => setTimeout(r, 500));
    window.__recGot = null; __recNode.port.postMessage('get'); for (let i = 0; i < 200 && !window.__recGot; i++) await new Promise(r => setTimeout(r, 50));
    const g = window.__recGot; let tot = 0; for (const b of g.b) tot += b.length; const o = new Float32Array(tot); let p = 0; for (const b of g.b){ o.set(b, p); p += b.length; }
    let s = '', u = new Uint8Array(o.buffer); for (let i = 0; i < u.length; i += 32768) s += String.fromCharCode.apply(null, u.subarray(i, i + 32768));
    return {b: btoa(s)};`);
  return typeof ok === 'string' ? ok : new Float32Array(Buffer.from(ok.b, 'base64').buffer.slice(0)); };

const rows = [], tab = {};
for (KIND of ['stereo', 'sine']) {
  const su = await run(mkSetup()); const d = await renderIt();
  if (!d) { rows.push([`${KIND}: render`, 'FAIL', 'no render came out']); continue; }
  const st = stat(d); tab[`${KIND} render`] = st;
  if (KIND === 'stereo') {
    rows.push(['stereo sample loaded', su.stereo ? 'PASS' : 'FAIL', JSON.stringify(su)]);
    rows.push(['render is stereo', st.corr < 0.5 ? 'PASS' : 'FAIL', `L/R correlation ${st.corr} (mono would be 1); rms L ${st.rmsL} R ${st.rmsR}`]);
    rows.push(['render has both channels', st.rmsL > 0.05 && st.rmsR > 0.05 ? 'PASS' : 'FAIL', '']);
    if (LIVE) for (const mode of ['core', 'page']) { const l = await liveIt(mode); if (typeof l === 'string') { rows.push([`live ${mode}`, 'FAIL', l]); continue; } const ls = stat(l); tab['live ' + mode] = ls;
      rows.push([`live ${mode} is stereo`, ls.corr < 0.5 ? 'PASS' : 'FAIL', `L/R correlation ${ls.corr}`]); }
  } else rows.push(['mono control renders as before', st.corr > 0.999 ? 'PASS' : 'FAIL', `L/R correlation ${st.corr}`]);
}
console.log('\nL/R table (render / live):'); for (const [k, v] of Object.entries(tab)) console.log('  ' + k.padEnd(14) + `rms L ${v.rmsL}  rms R ${v.rmsR}  correlation ${v.corr}`);
const w = 32; console.log('\n' + rows.map(r => r[0].padEnd(w) + r[1].padEnd(6) + r[2]).join('\n'));
const bad = rows.filter(r => r[1] === 'FAIL').length; console.log(`\n${rows.length - bad} pass, ${bad} fail`); console.log('page errors:', A.errors.length ? A.errors : 'none');
await A.close(); process.exit(bad ? 1 : 0);
