// SWEEP: every sound source (docs: tests/sweep/README in the sweep report). In headless Chrome, through the app's own code:
//   - every engine in the picker (ENGINE_GROUPS), on a drum pad and on a synth slot: setEngine, render (page thread and the
//     render Worker), checked for finite samples, not silent, peak <= 1;
//   - every factory preset of every engine card (XUI[k].pre, BASS_PRE, BLIP_PRE, VOX_PRE), the same checks;
//   - every parameter row of every engine at its min and at its max: no exception, no NaN / Infinity (silence or a peak
//     over 1 at an extreme is reported as NOTE, not FAIL: a Level row at its minimum is meant to be silent);
//   - live engines (bass, vox, acid, mach, …waves live, the …pump live twins) played through the offline mix (pass()),
//     beside their render: both must sound;
//   - the pad preset sheet's factory voices (fvp) and layer presets (flp), every Layers oscillator type;
//   - Table: the 30 AKWF starter tables and an imported WAV (single cycles and a longer sound); SoundFont: General MIDI
//     programs; Resynth with an analysed test sample; Freeze with its own sound.
//   node tests/sweep/engines.mjs [--quick] [--only=k1,k2]     (--quick: presets and min/max on a short render only)
import { openApp, table, live } from './lib.mjs';

const ONLY = (process.argv.find(a => a.startsWith('--only=')) || '').slice(7).split(',').filter(Boolean);
const FROM = (process.argv.find(a => a.startsWith('--from=')) || '').slice(7);   // start at this engine (resume a stopped run)
const TWINS = process.argv.includes('--twins');   // also presets and min/max for the …pump live twins (same render as their twin)
const rows = [];
const add = (name, res, info) => { const r = [name, res, info || '']; rows.push(r); live(r); };
let A = await openApp();

// page-side helpers, installed once per page
const HELPERS = `
globalThis.__swSt = d => { let pk = 0, nan = 0, s = 0; const n = d ? d.length : 0; for (let i = 0; i < n; i++){ const x = d[i]; if (!Number.isFinite(x)){ nan++; continue; } const a = Math.abs(x); if (a > pk) pk = a; s += x * x; } return {n, pk, rms:Math.sqrt(s / Math.max(1, n)), nan}; };
globalThis.__swVerdict = (st, strict) => { if (!st || !st.n) return ['FAIL', 'empty buffer'];
  if (st.nan) return ['FAIL', st.nan + ' non-finite samples'];
  const info = 'pk ' + st.pk.toFixed(3) + ' rms ' + st.rms.toFixed(4) + ' n ' + st.n;
  if (st.pk < 1e-4) return [strict ? 'FAIL' : 'NOTE', 'silent · ' + info];
  // over full scale: reported, not failed (a pad isn't the output: the mix, the master and its limiter follow, and a pad's
  // level is the owner's call, as the Tone drum's was on 25 Oct); 4 × full scale fails
  if (st.pk > 4) return ['FAIL', 'peak ' + st.pk.toFixed(2) + ' (exploding) · ' + info];
  if (st.pk > 1.0001) return ['NOTE', 'peak over 1 · ' + info];
  return ['PASS', info]; };
// the object an engine keeps its numbers in, and its row table
globalThis.__swObj = k => { const v = V(); return k === 'bass' ? v.bass : k === 'blip' ? v.blip : k === 'vox' ? v.vox : v[k]; };
globalThis.__swRows = k => k === 'bass' ? BU : k === 'blip' ? BLU : k === 'vox' ? VU : (XUI[k] ? XUI[k].rows : {});
globalThis.__swPre = k => k === 'bass' ? [BASS_PRE, bassPreset] : k === 'blip' ? [BLIP_PRE, blipPreset] : k === 'vox' ? [VOX_PRE, voxPreset] : [(XUI[k] && XUI[k].pre) || [], xPreset];
globalThis.__swRender = async (hold) => { const v = V(); if (v.engine === 'sf') await sfPreparePad(cur); return renderVoice(v, 48000, null, hold); };
return true;`;
await A.run(HELPERS);
await A.run(`setView('synth'); return view;`);
let engines = (await A.run(`return ENGINE_GROUPS.flatMap(g => g[1].map(e => e[0]));`)).filter(k => !ONLY.length || ONLY.includes(k));
if (FROM) engines = engines.slice(Math.max(0, engines.indexOf(FROM)));
const twinOf = await A.run(`const o = {}; for (const k in XENG) if (XENG[k].live && XENG[k].twin) o[k] = XENG[k].twin; return o;`);
const errs = tag => { const e = A.takeErrors(); if (e.length) add(tag + ' · page errors', 'FAIL', e.slice(0, 3).join(' | ').slice(0, 400)); };

// one evaluate per engine, with a time limit: a render that hangs the page fails the engine and the browser restarts
async function guarded(name, src, ms){
  let to; const t = new Promise((_, no) => { to = setTimeout(() => no(new Error('timeout ' + ms + ' ms (hang?)')), ms); });
  try { return await Promise.race([A.run(src), t]); }
  catch(e){ add(name, 'FAIL', String(e.message || e).slice(0, 300)); if (/timeout/.test(e.message)){ try { await A.close(); } catch(_){} A = await openApp(); await A.run(HELPERS); await A.run(`setView('synth');`); } return null; }
  finally { clearTimeout(to); }
}

for (const k of engines){
  const t0 = Date.now();
  // 1) default sound, drum pad (slot 0) and synth slot (NPAD), page thread and Worker
  const r1 = await guarded(k + ' · default', `
    const out = [];
    for (const slot of [0, NPAD]){
      cur = slot; setEngine('layers'); setEngine(${JSON.stringify(k)}); refreshAll(); const v = V(); v.len = 1;
      const hold = isSyn(cur) ? 1 : 0, where = isSyn(cur) ? 'synth slot' : 'drum pad';
      let st, e = null; try { st = __swSt(await __swRender(hold)); } catch(err){ e = String(err && err.stack || err); }
      out.push([where + ' render', ...(e ? ['FAIL', e.slice(0, 300)] : __swVerdict(st, true))]);
      if (v.engine !== 'sf'){ let w, e2 = null; try { w = __swSt((await renderAsync(v, 48000, hold, false)).data); } catch(err){ e2 = String(err && err.message || err); }
        out.push([where + ' Worker render', ...(e2 ? ['FAIL', e2] : __swVerdict(w, true))]);
        if (st && w && Math.abs(st.rms - w.rms) > 1e-3 * Math.max(st.rms, 1e-3) && !/^(bytebeat)$/.test(v.engine)) out.push([where + ' Worker = page', 'NOTE', 'rms ' + st.rms.toFixed(5) + ' vs ' + w.rms.toFixed(5)]); }
      try { presetSheet(${JSON.stringify(k)} === 'bass' ? 'bass' : ${JSON.stringify(k)} === 'blip' ? 'blip' : ${JSON.stringify(k)} === 'vox' ? 'vox' : ${JSON.stringify(k)} === 'layers' ? 'voice' : 'x'); const open = !!document.querySelector('.pbs'); if (pbOpen) pbOpen(); out.push([where + ' preset sheet opens', open ? 'PASS' : 'FAIL', '']); } catch(err){ out.push([where + ' preset sheet opens', 'FAIL', String(err).slice(0, 200)]); }
    }
    return out;`, 120000);
  if (r1) for (const [n, res, info] of r1) add(k + ' · ' + n, res, info);
  errs(k + ' · default');
  if (k === 'layers') continue;   // Layers: the factory voices and layer types below
  // a …pump live twin renders with its twin's code and the same preset list: its default sound is checked above, the
  // presets and rows once on the rendered twin (--twins does them again)
  if (twinOf[k] && !TWINS){ add(k + ' · presets and min/max', 'SKIP', 'same render and presets as ' + twinOf[k] + ' (--twins to run them)'); continue; }
  // 2) every factory preset (synth slot, 1 s hold; the drum engines on a drum pad)
  const r2 = await guarded(k + ' · presets', `
    const out = [], drum = ['tone','fmdrum','skin','breath','blip'].includes(${JSON.stringify(k)}) || (typeof LxrCore !== 'undefined' && LxrCore.isKind(${JSON.stringify(k)}));
    cur = drum ? 1 : NPAD + 1; setEngine('layers'); setEngine(${JSON.stringify(k)}); V().len = 1;
    const [L, fn] = __swPre(${JSON.stringify(k)});
    for (let i = 0; i < L.length; i++){
      let st, e = null; try { fn(i, true); refreshAll(); st = __swSt(await __swRender(isSyn(cur) ? 1 : 0)); } catch(err){ e = String(err && err.stack || err); }
      out.push(['preset ' + i + ' ' + L[i][0], ...(e ? ['FAIL', e.slice(0, 300)] : __swVerdict(st, true))]);
    }
    return out;`, 600000);
  if (r2) for (const [n, res, info] of r2) add(k + ' · ' + n, res, info);
  errs(k + ' · presets');
  // 3) every parameter row at its min and at its max (a short render; the rest of the sound at the defaults)
  const r3 = await guarded(k + ' · min/max', `
    const out = [], K = ${JSON.stringify(k)};
    cur = NPAD + 2; setEngine('layers'); setEngine(K); V().len = 0.4;
    const R = __swRows(K), o0 = JSON.parse(JSON.stringify(__swObj(K)));
    for (const key of Object.keys(R)){ const d = R[key];
      for (const end of ['min', 'max']){ const o = __swObj(K); Object.assign(o, o0); o[key] = d[end];
        let st, e = null; try { refreshAll(); st = __swSt(await __swRender(0.4)); } catch(err){ e = String(err && err.stack || err); }
        const vd = e ? ['FAIL', e.slice(0, 300)] : __swVerdict(st, false);
        if (vd[0] !== 'PASS') out.push([key + '=' + end + ' (' + d[end] + ')', ...vd]); else out.n = (out.n || 0) + 1; } }
    Object.assign(__swObj(K), o0);
    return {out, n:out.n || 0, rows:Object.keys(R).length};`, 900000);
  if (r3){ for (const [n, res, info] of r3.out) add(k + ' · ' + n, res, info); add(k + ' · min/max of ' + r3.rows + ' rows', 'PASS', r3.n + ' renders clean'); }
  errs(k + ' · min/max');
  console.error(`${k}: ${((Date.now() - t0) / 1000).toFixed(1)} s`);
}

if (!ONLY.length || ONLY.includes('live')){
  // 4) live engines: the offline mix (the worklets, as an export plays them) beside the pad's render
  const r4 = await guarded('live engines', `
    const out = [], sr = 48000, ks = ['bass', 'vox', ...Object.keys(XENG).filter(k => XENG[k].live)];
    for (const k of ks){
      cur = NPAD + 3; setEngine('layers'); setEngine(k); const v = V(); v.len = 1; refreshAll();
      try {
        const d = renderVoice(v, sr, null, 1), getB = () => mkBuf(d, sr);
        const ev = {type:'note', t:0.05, track:0, poly:false, pad:cur, rate:1, gain:1, pan:128, ofs:0, rev:false, cut:null, slide:null, rel:0.65, mn:60, vel:1, tick:0.02, vc:-1};
        const [Lc] = await pass([ev], new Set([0]), sr * 2, sr, getB, false, false), a = __swSt(d), b = __swSt(Lc);
        const vr = __swVerdict(a, true), vl = __swVerdict(b, true);
        out.push([k + ' · live (offline mix) and render both sound', vr[0] === 'PASS' && vl[0] === 'PASS' && isLivePad(cur) ? 'PASS' : 'FAIL', 'live: ' + vl[1] + ' / render: ' + vr[1] + (isLivePad(cur) ? '' : ' (pad not live?)')]);
      } catch(err){ out.push([k + ' · live', 'FAIL', String(err && err.stack || err).slice(0, 300)]); }
    }
    return out;`, 600000);
  if (r4) for (const [n, res, info] of r4) add(n, res, info);
  errs('live engines');
}

if (!ONLY.length || ONLY.includes('extras')){
  // 5) Layers: every factory voice preset (pad Presets…), every layer preset, every oscillator type
  const r5 = await guarded('factory voices', `
    const out = []; let n = 0; cur = 2; setEngine('layers');
    const F = fvp();
    for (const [g, list] of F) for (const it of list){ let st, e = null; const slot = /Bass|Pads|Chords|Acid|Machine|Modal|Swarm|Piano|piano|Organ|Table|Breath|Strings|Wind|Phase|Partials|Chip|Vector|Scanned|Resynth|Bytebeat|pump|Leads|Drift|Hack|Owls/.test(g) ? NPAD + 4 : 2;
      try { const v = facVoice(it); if (v.engine === 'sf') { out.push(['voice preset ' + g + ' / ' + it[0], 'SKIP', 'SoundFont']); continue; } kit.voices[slot] = v; cur = slot; const hold = isSyn(slot) ? Math.min(v.len || 1, 2) : 0; st = __swSt(renderVoice(v, 48000, null, hold)); } catch(err){ e = String(err && err.stack || err); }
      const vd = e ? ['FAIL', e.slice(0, 300)] : __swVerdict(st, true); if (vd[0] !== 'PASS') out.push(['voice preset ' + g + ' / ' + it[0], ...vd]); else n++; }
    out.push(['factory voice presets (' + F.reduce((a, g) => a + g[1].length, 0) + ')', 'PASS', n + ' clean']);
    return out;`, 1500000);
  if (r5) for (const [n, res, info] of r5) add(n, res, info);
  errs('factory voices');
  const r6 = await guarded('layers', `
    const out = []; cur = 3; kit.voices[3] = mkVoice('T', []); setEngine('layers');
    for (const [g, list] of flp()) for (const it of list){ let st, e = null;
      try { const v = mkVoice('T', [Object.assign({}, it[1], {on:true, level:facLayerLevel(it[1])})]); st = __swSt(renderVoice(v, 48000, null, 0)); } catch(err){ e = String(err && err.stack || err); }
      out.push(['layer preset ' + g + ' / ' + it[0], ...(e ? ['FAIL', e.slice(0, 300)] : __swVerdict(st, true))]); }
    for (const T of TYPES){ let st, e = null;
      try { const L = {type:T.id, on:true, a:0.5, b:0.3, level:0.8};
        if (T.id === 'sample' || T.id === 'grains' || T.id === 'pwm'){   // (grains: silent until a sample is loaded, as its card says) const d = new Float32Array(24000); for (let i = 0; i < d.length; i++) d[i] = Math.sin(i * 0.05) * Math.exp(-i / 8000) * 0.8; L.sample = {data:d, sr:48000, name:'test'}; }
        const v = mkVoice('T', [L]); st = __swSt(renderVoice(v, 48000, null, 0)); } catch(err){ e = String(err && err.stack || err); }
      const vd = e ? ['FAIL', e.slice(0, 300)] : __swVerdict(st, true);
      out.push(['layer type ' + T.id + ' (' + T.name + ')', ...(vd[0] === 'FAIL' && /silent/.test(vd[1]) && T.id === 'waves' ? ['NOTE', vd[1] + ' (…waves layer needs a patch Bus)'] : vd)]); }
    return out;`, 600000);
  if (r6) for (const [n, res, info] of r6) add(n, res, info);
  errs('layers');
  // 6) Table: the starter tables, and imported WAVs
  const r7 = await guarded('wavetables', `
    const out = []; cur = NPAD + 5; setEngine('layers'); setEngine('table'); const v = V(); v.len = 0.5; refreshAll();
    for (const [id, nm] of WT_STARTER){ let st, e = null;
      try { v.wtData = await wtStarter(id); v.wtData.from = 'starter'; v.table.table = 4; refreshAll(); st = __swSt(renderVoice(v, 48000, null, 0.5)); } catch(err){ e = String(err && err.stack || err); }
      out.push(['starter table ' + id, ...(e ? ['FAIL', e.slice(0, 300)] : __swVerdict(st, true))]); }
    // WAVs made here: 3 single cycles (600 samples, as AKWF), a 64 x 256 table, and a 1.5 s sound
    const wav = (x, sr) => { const b = new ArrayBuffer(44 + x.length * 2), dv = new DataView(b), w = (o, s) => { for (let i = 0; i < s.length; i++) dv.setUint8(o + i, s.charCodeAt(i)); };
      w(0, 'RIFF'); dv.setUint32(4, 36 + x.length * 2, true); w(8, 'WAVE'); w(12, 'fmt '); dv.setUint32(16, 16, true); dv.setUint16(20, 1, true); dv.setUint16(22, 1, true); dv.setUint32(24, sr, true); dv.setUint32(28, sr * 2, true); dv.setUint16(32, 2, true); dv.setUint16(34, 16, true); w(36, 'data'); dv.setUint32(40, x.length * 2, true);
      for (let i = 0; i < x.length; i++) dv.setInt16(44 + i * 2, Math.max(-32767, Math.min(32767, Math.round(x[i] * 32767))), true); return new File([b], 'w.wav', {type:'audio/wav'}); };
    const cyc = (n, f) => Float32Array.from({length:n}, (_, i) => f(i / n));
    const cases = [['3 single cycles', [wav(cyc(600, p => Math.sin(2 * Math.PI * p)), 44100), wav(cyc(600, p => 2 * p - 1), 44100), wav(cyc(600, p => p < 0.5 ? 0.8 : -0.8), 44100)]],
      ['64 x 256 table', [wav(Float32Array.from({length:64 * 256}, (_, i) => Math.sin(2 * Math.PI * (i % 256) / 256 * (1 + Math.floor(i / 256) / 16)) * 0.7), 48000)]],
      ['a 1.5 s sound', [wav(Float32Array.from({length:72000}, (_, i) => (Math.sin(2 * Math.PI * 220 * i / 48000) + 0.3 * Math.sin(2 * Math.PI * 660 * i / 48000)) * Math.exp(-i / 40000) * 0.6), 48000)]]];
    for (const [nm, files] of cases){ let st, e = null, wt = null;
      try { wt = await wtImport(files); if (!wt) throw new Error('no table made'); v.wtData = wt; v.table.table = 4; refreshAll(); st = __swSt(renderVoice(v, 48000, null, 0.5)); } catch(err){ e = String(err && err.stack || err); }
      out.push(['wavetable import: ' + nm, ...(e ? ['FAIL', e.slice(0, 300)] : __swVerdict(st, true).map((x, j) => j ? x + ' · ' + wt.nf + ' frames, ' + wt.from : x))]); }
    return out;`, 300000);
  if (r7) for (const [n, res, info] of r7) add(n, res, info);
  errs('wavetables');
  // 7) SoundFont: General MIDI programs (pad render, and through the offline mix as the tracker plays it)
  const r8 = await guarded('soundfont', `
    const out = [], sr = 48000; cur = NPAD + 6; setEngine('layers'); setEngine('sf'); const v = V(); v.len = 1;
    for (const prog of [0, 4, 19, 24, 33, 40, 48, 56, 65, 73, 80, 88, 118]){ let st, e = null, m = null;
      try { v.sf.prog = prog; v.sf.bank = 0; await sfPreparePad(cur); st = __swSt(renderVoice(v, sr, null, 1));
        const d = renderVoice(v, sr, null, 1), ev = {type:'note', t:0.05, track:0, poly:false, pad:cur, rate:1, gain:1, pan:128, ofs:0, rev:false, cut:null, slide:null, rel:0.8, mn:60, vel:1, tick:0.02, vc:-1};
        m = __swSt((await pass([ev], new Set([0]), sr * 2, sr, () => mkBuf(d, sr), false, false))[0]); } catch(err){ e = String(err && err.stack || err); }
      out.push(['SoundFont GM program ' + prog + ' (' + sfPresetLabel(prog) + ') render', ...(e ? ['FAIL', e.slice(0, 300)] : __swVerdict(st, true))]);
      if (m) out.push(['SoundFont GM program ' + prog + ' in the offline mix', ...__swVerdict(m, true)]); }
    for (const [i] of XUI.sf.pre.entries()){ let st, e = null; try { xPreset(i, true); await sfPreparePad(cur); st = __swSt(renderVoice(V(), sr, null, 1)); } catch(err){ e = String(err && err.stack || err); }
      out.push(['SoundFont preset ' + i + ' ' + XUI.sf.pre[i][0], ...(e ? ['FAIL', e.slice(0, 300)] : __swVerdict(st, true))]); }
    return out;`, 600000);
  if (r8) for (const [n, res, info] of r8) add(n, res, info);
  errs('soundfont');
  // 8) Resynth with an analysed test sample; Freeze with a sound of its own
  const r9 = await guarded('sample engines', `
    const out = [], sr = 48000, x = new Float32Array(sr * 2);
    for (let i = 0; i < x.length; i++){ const t = i / sr; x[i] = (Math.sin(2 * Math.PI * 196 * t) * 0.5 + Math.sin(2 * Math.PI * 392 * t) * 0.25 + Math.sin(2 * Math.PI * 588 * t) * 0.12) * Math.exp(-t * 1.5); }
    cur = NPAD + 7; setEngine('layers'); setEngine('resynth'); let v = V(); v.len = 1;
    try { v.rsData = ResynthCore.analyse(x, sr, {name:'test'}); refreshAll(); out.push(['Resynth: analysed test sample', ...__swVerdict(__swSt(renderVoice(v, sr, null, 1)), true)]); } catch(err){ out.push(['Resynth: analysed test sample', 'FAIL', String(err && err.stack || err).slice(0, 300)]); }
    try { v.rsData = ResynthCore.analyse(new Float32Array(sr), sr, {name:'silence'}); refreshAll(); const st = __swSt(renderVoice(v, sr, null, 1)); out.push(['Resynth: silent sample (no crash)', st.nan ? 'FAIL' : 'PASS', 'pk ' + st.pk]); } catch(err){ out.push(['Resynth: silent sample (no crash)', 'FAIL', String(err && err.stack || err).slice(0, 300)]); }
    for (const k of ['pfreeze', 'pfreezel']){ cur = NPAD + 8; setEngine('layers'); setEngine(k); v = V(); v.len = 1;
      try { v.frzL = 7; const L = freshLayer({on:false, type:'sample'}); L.sample = {data:x, sr, name:'test'}; v.layers[7] = L; XEV().source = 0; refreshAll(); out.push([k + ': its own sound (SOURCE File)', ...__swVerdict(__swSt(renderVoice(v, sr, null, 1)), true)]); }
      catch(err){ out.push([k + ': its own sound', 'FAIL', String(err && err.stack || err).slice(0, 300)]); } }
    return out;`, 300000);
  if (r9) for (const [n, res, info] of r9) add(n, res, info);
  errs('sample engines');
}

await A.close();
console.log('\nENGINES SWEEP\n');
process.exit(table(rows) ? 1 : 0);
