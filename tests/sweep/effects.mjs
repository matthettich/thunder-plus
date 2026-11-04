// SWEEP: every effect, in headless Chrome, through the app's own code.
//   - track / master effects (TFX: FXT_LIST, the …pump ones included): each made fresh (freshTfx) and rendered offline in
//     the app's own effect worklet (thunder-tfx, via tfxNode in an OfflineAudioContext, 32 at a time), checked for finite
//     output, not exploding; then every parameter at its min and its max (…pump: every plugin parameter by symbol), and
//     every factory preset (FX_FACTORY; …pump: the plugin's presets through PumpFX.applyPreset). The worklet quietly
//     resets an effect whose output goes bad (NaN or a runaway); the sweep's copy of the worklet reports each reset, and
//     a reset counts as a FAIL.
//   - pad effects (FX, the Synth page's Effects card): each on a test pad through renderVoice, min/max of every row.
//   - the master chain: the default …pump mastering (EQ → CALM → COMP → TONE → MASTER, MST_PUMP) and the older mastering
//     chain (S.mst: EQ, suppressor, compressor, limiter) through the song render's own pass().
//   - UI: every track effect shown on the FX screen and the FX bar, every …pump effect's pop-up window opened and closed,
//     the preset picker, every pad effect's card on the Synth page; page errors are collected after each.
//   node tests/sweep/effects.mjs [--only=track,pad,master,ui] [--fx=k1,k2]
import { openApp, table, live } from './lib.mjs';

const arg = n => (process.argv.find(a => a.startsWith('--' + n + '=')) || '').slice(n.length + 3).split(',').filter(Boolean);
const ONLY = arg('only'), FXONLY = arg('fx');
const want = p => !ONLY.length || ONLY.includes(p);
const rows = [];
const add = (name, res, info) => { const r = [name, res, info || '']; rows.push(r); live(r); };
let A = await openApp();

const HELPERS = `
// the effect worklet's source, with a message when it resets an effect that went bad (NaN / runaway), and when a slot
// with a JS / module / …pump effect does
if (!globalThis.__swTfx){ const src = TFX_SRC.split('if (bad){ try {').join('if (bad){ this.port.postMessage({swBad:s.key}); try {');
  tfxURL = URL.createObjectURL(new Blob([rvMix.toString() + '\\n' + DegradeCore.toString() + '\\n' + VSampCore.toString() + '\\n' + ResonCore.toString() + '\\n' + PitchFxCore.toString() + '\\n' + clkSpb.toString() + '\\n' + FlexCore.toString() + '\\n' + StepGateCore.toString() + '\\n' + RetrigCore.toString() + '\\n' + beatLock.toString() + '\\n' + bkHash.toString() + '\\n' + GrainsCore.toString() + '\\n' + ScrambleCore.toString() + '\\n' + GlitchCore.toString() + '\\n' + src], {type:'application/javascript'}));
  globalThis.__swTfx = true; }
// configs: [{id, f}] (f: a slot). Renders each through its own effect node; returns [{id, pk, rms, nan, bad, failed}]
globalThis.__swBatch = async (configs, secs, level) => {
  const sr = 48000, N = Math.round(sr * secs), oc = new OfflineAudioContext(32, N, sr);
  oc.destination.channelCount = 32; oc.destination.channelCountMode = 'explicit'; oc.destination.channelInterpretation = 'discrete';
  if (!(await tfxReady(oc))) throw new Error('effect worklet did not load');
  const b = oc.createBuffer(2, N, sr), on = Math.min(N, Math.round(sr * secs * 0.7));
  for (let c = 0; c < 2; c++){ const d = b.getChannelData(c); let s = 12345 + c;
    for (let i = 0; i < on; i++){ const t = i / sr; s = (s * 1103515245 + 12345) >>> 0;
      d[i] = (Math.sin(2 * Math.PI * 110 * t) * 0.3 + Math.sin(2 * Math.PI * (c ? 663 : 660) * t) * 0.1 + ((s / 4294967296) * 2 - 1) * 0.25 * Math.exp(-(t % 0.125) * 40)) * Math.min(1, t * 200) * Math.min(1, (on / sr - t) * 30) * (level || 1); } }
  const src = oc.createBufferSource(); src.buffer = b; const mg = oc.createChannelMerger(32); mg.connect(oc.destination);
  const res = configs.map(c => ({id:c.id, bad:0, failed:0}));
  configs.forEach((c, i) => {
    const node = tfxNode(oc, tfxChain([c.f], sr, 120)), h = node.port.onmessage;
    node.onprocessorerror = () => { res[i].perr = 1; };
    node.port.onmessage = e => { const d = e.data; if (d && d.swBad) res[i].bad++; if (d && d.failed) res[i].failed++; if (!(d && d.failed)) try { h(e); } catch(_){} };
    const mono = oc.createGain(); mono.channelCount = 1; mono.channelCountMode = 'explicit'; mono.channelInterpretation = 'speakers';
    src.connect(node); node.connect(mono); mono.connect(mg, 0, i); });
  src.start(0);
  const out = await oc.startRendering();
  await new Promise(r => setTimeout(r, 50));   // the last messages from the worklet
  configs.forEach((c, i) => { const d = out.getChannelData(i); let pk = 0, nan = 0, s = 0; for (const x of d){ if (!Number.isFinite(x)) nan++; else { const a = Math.abs(x); if (a > pk) pk = a; s += x * x; } }
    Object.assign(res[i], {pk, rms:Math.sqrt(s / d.length), nan}); });
  return res;
};
globalThis.__swV = r => r.perr ? ['FAIL', 'the effect worklet threw (processorerror)'] : r.nan ? ['FAIL', r.nan + ' non-finite samples'] : r.failed ? ['FAIL', 'the effect crashed in the worklet (bypassed)'] : r.bad ? ['FAIL', 'output went bad (NaN, or over ±8 / ±1e4 for wasm): the worklet reset it ' + r.bad + '×']
  : r.pk > 4 ? ['FAIL', 'peak ' + r.pk.toFixed(2) + ' (exploding)'] : r.pk > 1.0001 ? ['NOTE', 'peak over 1: ' + r.pk.toFixed(3)] : r.pk < 1e-4 ? ['NOTE', 'silent'] : ['PASS', 'pk ' + r.pk.toFixed(3) + ' rms ' + r.rms.toFixed(4)];
return true;`;
await A.run(HELPERS);
const errs = tag => { const e = A.takeErrors(); if (e.length) add(tag + ' · page errors', 'FAIL', [...new Set(e)].slice(0, 3).join(' | ').slice(0, 400)); };
async function guarded(name, src, ms){
  let to; const t = new Promise((_, no) => { to = setTimeout(() => no(new Error('timeout ' + ms + ' ms (hang?)')), ms); });
  try { return await Promise.race([A.run(src), t]); }
  catch(e){ add(name, 'FAIL', String(e.message || e).slice(0, 300)); if (/timeout|closed|crash/i.test(e.message)){ try { await A.close(); } catch(_){} A = await openApp(); await A.run(HELPERS); } return null; }
  finally { clearTimeout(to); }
}
// configurations → batches of 32 → rows; quiet: only the non-PASS ones get a row each, plus one summary row per group
async function runConfigs(group, configs, secs, quiet){
  let pass = 0; const t0 = Date.now();
  for (let i = 0; i < configs.length; i += 32){
    // Each effect node makes its own wasm instance, and Chrome doesn't give their memory back when an offline render ends:
    // after ~100 in one page, new nodes fail ("Out of memory: Cannot allocate Wasm memory") and play silence (see the sweep
    // report). A fresh page for every batch keeps the sweep measuring the effects, not that limit.
    if (i){ await A.close(); A = await openApp(); await A.run(HELPERS); }
    const part = configs.slice(i, i + 32);
    const r = await guarded(group + ' batch ' + (i / 32 + 1), `return await __swBatch(${JSON.stringify(part)}, ${secs});`, 600000);
    if (!r) continue;
    const again = [];
    for (const x of r){ const [v, info] = await A.run(`return __swV(${JSON.stringify(x)});`); if (v === 'PASS'){ pass++; if (!quiet) add(x.id, v, info); } else if (v === 'FAIL' && (x.bad || x.pk > 4) && !x.nan && !x.failed && !x.perr) again.push([x, info]); else add(x.id, v, info); }
    // reset for going over the worklet's guard: the same setting again at -40 dB. Still bad there: a real runaway (FAIL);
    // clean there: just a lot of gain on a loud test signal (NOTE)
    if (again.length){ const q = await guarded(group + ' quiet re-check', `return await __swBatch(${JSON.stringify(part.filter(c => again.some(([x]) => x.id === c.id)))}, ${secs}, 0.01);`, 600000);
      for (const [x, info] of again){ const y = q && q.find(z => z.id === x.id); if (y && !y.bad && !y.nan && !y.failed && y.pk < Math.max(0.05, x.pk * 0.05)) add(x.id, 'NOTE', info + '; clean at -40 dB (gain on a loud signal, not a runaway)'); else add(x.id, 'FAIL', info + (y ? '; bad at -40 dB too' : '')); } }
    errs(group + ' batch ' + (i / 32 + 1));
  }
  if (quiet) add(group + ' (' + configs.length + ' renders)', 'PASS', pass + ' clean · ' + ((Date.now() - t0) / 1000).toFixed(0) + ' s');
}

const list = (await A.run(`return FXT_LIST;`)).filter(k => !FXONLY.length || FXONLY.includes(k));
if (want('track')){
  // 1) fresh
  const fresh = await A.run(`return ${JSON.stringify(list)}.map(k => ({id:'track fx ' + k + ' (' + TFX[k].n + ') fresh', f:freshTfx(k)}));`);
  await runConfigs('fresh', fresh, 0.6, false);
  // 2) min / max of every parameter
  const mm = await A.run(`const out = [];
    for (const k of ${JSON.stringify(list)}){
      // (…pump EQ / BAND repeat the same parameters for every band: band 1 stands for them)
      if (isPumpT(k)){ const type = TFX[k].pump; for (const p of pumpFx(type).params){ if (pumpSkip(p) || /^b([2-9]|\\d\\d)_/.test(p.s)) continue;
          for (const end of ['min', 'max']){ const f = freshTfx(k); f.pump[p.s] = p[end]; pumpBytes(f); out.push({id:'track fx ' + k + ' · ' + p.s + '=' + end + ' (' + p[end] + ')', f}); } } continue; }
      for (const q of TFX[k].ps) for (const end of ['min', 'max']){ const f = freshTfx(k); f[q.k] = q[end]; out.push({id:'track fx ' + k + ' · ' + q.k + '=' + end + ' (' + q[end] + ')', f}); } }
    return out;`);
  await runConfigs('track fx min/max', mm, 1, true);   // 1 s: grain, reverse and delay effects at full wet are silent for their first few hundred ms
  // 3) every factory preset
  const pr = await A.run(`const out = [];
    S.tfx[0] = S.tfx[0] || freshChainFx();
    for (const k of ${JSON.stringify(list)}){
      if (isPumpT(k)){ const L = PumpFX.presets(TFX[k].pump); S.mfx[0] = freshTfx(k);
        for (const p of L){ if (!PumpFX.applyPreset(NT, 0, p.index)){ out.push({id:'track fx ' + k + ' · preset ' + p.name + ' (applyPreset refused)', f:freshTfx(k), refused:true}); continue; } out.push({id:'track fx ' + k + ' · preset ' + p.index + ' ' + p.name, f:JSON.parse(JSON.stringify(S.mfx[0]))}); }
        continue; }
      for (const [name, vals] of FX_FACTORY[k] || []){ const f = freshTfx(k); for (const q of TFX[k].ps) if (q.k in vals || q.k !== 'sck') f[q.k] = clamp(Math.round(q.k in vals ? vals[q.k] : q.d), q.min, q.max);
        const extra = Object.keys(vals).filter(x => !TFX[k].ps.some(q => q.k === x)); out.push({id:'track fx ' + k + ' · preset ' + name + (extra.length ? ' [unknown keys: ' + extra.join(',') + ']' : ''), f, extra:extra.length}); } }
    S.mfx[0] = null; return out;`);
  for (const c of pr) if (c.extra) add(c.id, 'FAIL', 'the preset names parameters the effect does not have (ignored by fxPreApply)');
  await runConfigs('track fx factory presets', pr, 1, true);
}

if (want('pad')){
  const r = await guarded('pad effects', `
    const out = [], sr = 48000, st = d => { let pk = 0, nan = 0; for (const x of d){ if (!Number.isFinite(x)) nan++; else pk = Math.max(pk, Math.abs(x)); } return {pk, nan, n:d.length}; };
    const base = (lv = 1) => mkVoice('FX test', [{type:'saw', on:true, level:0.7 * lv, mode:'AHD', atk:1, hold:150, dec:300}, {type:'noise', on:true, level:0.3 * lv, mode:'AHD', atk:0.5, hold:5, dec:80}]);
    const V1 = (f, info) => { if (f.nan) return ['FAIL', f.nan + ' non-finite samples']; if (f.pk > 4) return ['FAIL', 'peak ' + f.pk.toFixed(2)]; if (f.pk > 1.0001) return ['NOTE', 'peak over 1: ' + f.pk.toFixed(3)]; if (f.pk < 1e-4) return ['NOTE', 'silent']; return ['PASS', 'pk ' + f.pk.toFixed(3)]; };
    for (const k of Object.keys(FX)){
      let e = null, s = null; const v = base(); v.fx = [freshFx(k)];
      try { if (!fxReady(v.fx[0])) throw new Error('not ready (wasm or module missing)'); s = st(renderVoice(v, sr, null, 0)); } catch(err){ e = String(err && err.stack || err); }
      out.push(['pad fx ' + k + ' (' + FX[k].name + ') fresh', ...(e ? ['FAIL', e.slice(0, 300)] : V1(s))]);
      let n = 0;
      for (const [key, d] of Object.entries(FX[k].rows || {})) for (const end of ['min', 'max']){
        const w = base(); w.fx = [Object.assign(freshFx(k), {[key]:d[end]})]; let e2 = null, s2 = null;
        try { s2 = st(renderVoice(w, sr, null, 0)); } catch(err){ e2 = String(err && err.stack || err); }
        let vd = e2 ? ['FAIL', e2.slice(0, 300)] : V1(s2);
        // over 4× full scale: the same setting at -20 dB. Scaling down with it (≤ 0.4): gain on a loud signal (NOTE), not a runaway
        if (vd[0] === 'FAIL' && s2 && !s2.nan){ const q = base(0.1); q.fx = [Object.assign(freshFx(k), {[key]:d[end]})]; const s3 = st(renderVoice(q, sr, null, 0)); if (!s3.nan && s3.pk <= 0.4 * Math.max(1, s2.pk / 4)) vd = ['NOTE', vd[1] + '; scales with the input (' + s3.pk.toFixed(3) + ' at -20 dB): gain, not a runaway']; }
        if (vd[0] === 'PASS') n++; else out.push(['pad fx ' + k + ' · ' + key + '=' + end + ' (' + d[end] + ')', ...vd]); }
      out.push(['pad fx ' + k + ' · min/max of ' + Object.keys(FX[k].rows || {}).length + ' rows', 'PASS', n + ' clean']);
    }
    return out;`, 1800000);
  if (r) for (const [n, res, info] of r) add(n, res, info);
  errs('pad effects');
}

if (want('master')){
  const r = await guarded('master chain', `
    const out = [], sr = 48000, N = Math.round(sr * 2), d = new Float32Array(Math.round(sr * 1.5));
    for (let i = 0; i < d.length; i++){ const t = i / sr; d[i] = (Math.sin(2 * Math.PI * 55 * t) * 0.5 + Math.sin(2 * Math.PI * 2500 * t) * 0.15 * Math.exp(-(t % 0.25) * 20)) * Math.min(1, t * 200) * Math.min(1, (1.5 - t) * 20); }
    const buf = mkBuf(d, sr), getB = () => buf;
    const evs = [{type:'beat', t:0, b:0}, {type:'note', t:0.02, track:0, poly:false, pad:0, rate:1, gain:1, pan:128, ofs:0, rev:false, cut:null, slide:null, rel:null, mn:60, vel:1, tick:0.02, vc:-1}];
    const st = a => { let pk = 0, nan = 0, s = 0; for (const x of a){ if (!Number.isFinite(x)) nan++; else { pk = Math.max(pk, Math.abs(x)); s += x * x; } } return {pk, nan, rms:Math.sqrt(s / a.length)}; };
    const V1 = s => s.nan ? ['FAIL', s.nan + ' non-finite'] : s.pk > 1.0001 ? ['FAIL', 'peak ' + s.pk.toFixed(3) + ' over full scale'] : s.pk < 1e-4 ? ['FAIL', 'silent'] : ['PASS', 'pk ' + s.pk.toFixed(3) + ' rms ' + s.rms.toFixed(4)];
    const keep = {mfx:S.mfx, mstPump:S.mstPump, mst:S.mst, tfx0:S.tfx[0], lim:S.limiter};
    try {
      S.tfx[0] = freshChainFx();
      { const [L] = await pass(evs, new Set([0]), N, sr, getB, false, false); out.push(['master: dry render (no master effects)', ...V1(st(L))]); }
      // a new song's master: …pump EQ → CALM → COMP → TONE → MASTER in slots 4–8, on
      S.mfx = mstPumpFresh(); S.mstPump = true; S.mst = freshMst();
      out.push(['master: new song default chain', JSON.stringify(S.mfx.map(f => f && f.type)) === JSON.stringify([null, null, null, 'peq', 'pcalm', 'pcomp', 'ptone', 'pmaster']) && S.mfx.every(f => !f || f.on) ? 'PASS' : 'FAIL', S.mfx.map(f => f ? f.type + (f.on ? '' : '(off)') : '-').join(' ')]);
      out.push(['master: mstOn() with the default chain', mstOn() ? 'PASS' : 'FAIL', '']);
      { const [L] = await pass(evs, new Set([0]), N, sr, getB, true, true); out.push(['master: default …pump chain + safety limiter', ...V1(st(L))]); }
      { const [L] = await pass(evs, new Set([0]), N, sr, getB, false, true); out.push(['master: default …pump chain, no safety limiter', ...V1(st(L)).map((x, j) => j === 0 && x === 'FAIL' && /over full/.test(V1(st(L))[1]) ? 'NOTE' : x)]); }
      for (const k of MST_PUMP){ const one = freshChainFx(); one[3] = freshTfx(k); S.mfx = one;
        const [L] = await pass(evs, new Set([0]), N, sr, getB, false, true); out.push(['master: ' + k + ' alone', ...V1(st(L)).map((x, j) => j === 0 && x === 'FAIL' && /over full/.test(V1(st(L))[1]) ? 'NOTE' : x)]); }
      mstSetOn(false); out.push(['master: mstSetOn(false) switches the chain off', !mstOn() ? 'PASS' : 'FAIL', '']); mstSetOn(true);
      // an older song's mastering chain (S.mst): every stage on, the suppressor too
      S.mfx = freshChainFx(); S.mstPump = false; S.mst = freshMst(); S.mst.on = true; S.mst.sup.on = true;
      { const [L] = await pass(evs, new Set([0]), N, sr, getB, false, true); out.push(['master: older mastering chain (EQ, suppressor, comp, limiter)', ...V1(st(L))]); }
      for (const [stg, rng] of Object.entries(MST_RANGES)) for (const [key, [lo, hi]] of Object.entries(rng)) for (const v of [lo, hi]){
        S.mst = freshMst(); S.mst.on = true; S.mst.sup.on = true; S.mst[stg][key] = v; const [L] = await pass(evs, new Set([0]), Math.round(sr * 0.8), sr, getB, false, true); const s = st(L);
        if (s.nan || s.pk > 1.0001) out.push(['master: older chain ' + stg + '.' + key + '=' + v, s.nan ? 'FAIL' : 'NOTE', s.nan ? s.nan + ' non-finite' : 'peak ' + s.pk.toFixed(3)]); }
      out.push(['master: older chain, every stage value at min and max', 'PASS', 'see rows above for any non-finite / over-1 result']);
    } finally { Object.assign(S, {mfx:keep.mfx, mstPump:keep.mstPump, mst:keep.mst, limiter:keep.lim}); S.tfx[0] = keep.tfx0; }
    return out;`, 1800000);
  if (r) for (const [n, res, info] of r) add(n, res, info);
  errs('master chain');
}

if (want('ui')){
  // the FX screen and FX bar for every track effect (pump ones on the master), each …pump window, the preset picker
  const r = await guarded('fx ui', `
    const out = []; rtInit(); go('fx');
    for (const k of ${JSON.stringify(list)}){
      const t = isPumpT(k) && !pumpAnyTrack(k) ? NT : 0, A = t >= NT ? S.mfx : S.tfx[t]; const keepS = A[0];
      try {
        A[0] = freshTfx(k); fxChanged(t); cFxT = t; cFxS = 0; cs.fx.y = FXSLOTS + 1; draw();
        const txt = document.body.innerText.includes(TFX[k].n);
        deck.fxbar = true; deckFx(); const bar = !!document.querySelector('#dkFxBar .dkfc'); deck.fxbar = false; deckFx(); const rowsN = document.querySelectorAll('#dkFxRows .row').length; deck.fxbar = true; deckFx();
        let win = 'no window';
        if (isPumpT(k)){ const before = document.querySelectorAll('.pumpwin').length; PumpFX.open(t, 0); await new Promise(r => setTimeout(r, 150)); const n = document.querySelectorAll('.pumpwin').length; win = n > before ? 'window opened' : 'no window (toast)'; if (window.PumpUI) PumpUI.closeAll(); }
        else { fxPresetOpen(t, 0); const n = fxpN(); if (n > 1) fxpChoose(1); else fxpClose(); win = 'preset picker ' + n + ' items'; }
        out.push(['ui ' + k + ': FX screen, FX bar, panel', txt && bar && rowsN === TFX[k].ps.length ? 'PASS' : 'FAIL', 'name shown ' + txt + ', FX bar column ' + bar + ', panel rows ' + rowsN + '/' + TFX[k].ps.length + ', ' + win]);
      } catch(err){ out.push(['ui ' + k, 'FAIL', String(err && err.stack || err).slice(0, 300)]); }
      A[0] = keepS; fxChanged(t);
    }
    return out;`, 1200000);
  if (r) for (const [n, res, info] of r) { add(n, res, info); errs(n); }
  errs('fx ui');
  const r2 = await guarded('pad fx ui', `
    const out = []; setView('synth'); cur = 4; setEngine('layers'); const v = V(); const sel = $('fxAdd');
    for (const k of Object.keys(FX)){ v.fx = []; renderFx();
      try { sel.value = k; sel.dispatchEvent(new Event('change')); const n = $('fxList').children.length; out.push(['ui pad fx ' + k + ' card', n === 1 && v.fx.length === 1 && v.fx[0].type === k ? 'PASS' : 'FAIL', 'cards ' + n]); }
      catch(err){ out.push(['ui pad fx ' + k, 'FAIL', String(err && err.stack || err).slice(0, 300)]); } }
    v.fx = []; renderFx(); return out;`, 300000);
  if (r2) for (const [n, res, info] of r2) { add(n, res, info); }
  errs('pad fx ui');
}

if (want('frames')){
  // …chains and …waves: the screen opens its frame; every module in the frame's list is added (a click on its palette
  // button, as a person does) and removed again (it is selected when added; Delete), with page errors checked after each
  for (const [kind, view, sel] of [['chains', 'chains', '#chView iframe'], ['waves', 'waves', '#wvView iframe']]){
    const ok = await guarded(kind + ' frame', `setView(${JSON.stringify(view)}); for (let i = 0; i < 100; i++){ const f = document.querySelector(${JSON.stringify(sel)}); if (f && f.contentDocument && f.contentDocument.querySelectorAll('.pitem[data-type]').length) return f.contentDocument.querySelectorAll('.pitem[data-type]').length; await new Promise(r => setTimeout(r, 200)); } return 0;`, 60000);
    add(kind + ' page loads in its frame', ok ? 'PASS' : 'FAIL', ok ? ok + ' modules in its list' : 'no module list after 20 s');
    errs(kind + ' frame load');
    if (!ok) continue;
    const fr = A.page.frames().find(f => f.url().includes('/' + kind + '/index.html'));
    const types = await fr.evaluate(() => [...new Set([...document.querySelectorAll('.pitem[data-type]')].map(b => b.dataset.type))].filter(t => !t.startsWith('c:')));
    let pass = 0;
    for (const type of types){
      try {
        const r = await fr.evaluate(async type => {
          const n0 = document.querySelectorAll('.node').length, b = document.querySelector('.pitem[data-type="' + type + '"]'); if (!b) return 'no button';
          const o = {bubbles:true, button:0, pointerId:1, pointerType:'mouse', clientX:5, clientY:5, isPrimary:true};
          b.dispatchEvent(new PointerEvent('pointerdown', o)); window.dispatchEvent(new PointerEvent('pointerup', o));
          await new Promise(r => setTimeout(r, 30));
          const n1 = document.querySelectorAll('.node').length;
          document.body.focus(); window.dispatchEvent(new KeyboardEvent('keydown', {key:'Delete', bubbles:true}));
          await new Promise(r => setTimeout(r, 20));
          const n2 = document.querySelectorAll('.node').length;
          return n1 === n0 + 1 && n2 === n0 ? 'ok' : 'nodes ' + n0 + ' → ' + n1 + ' → ' + n2;
        }, type);
        const e = A.takeErrors();
        if (r === 'ok' && !e.length) pass++; else add(kind + ' module ' + type + ' add/remove', 'FAIL', (r === 'ok' ? '' : r + ' ') + [...new Set(e)].slice(0, 2).join(' | ').slice(0, 300));
      } catch(err){ add(kind + ' module ' + type, 'FAIL', String(err.message || err).slice(0, 300)); }
    }
    add(kind + ': every module added and removed (' + types.length + ')', pass === types.length ? 'PASS' : 'FAIL', pass + ' clean');
    await A.run(`setView('tracker'); return true;`); errs(kind + ' leave');
  }
}

await A.close();
console.log('\nEFFECTS SWEEP\n');
process.exit(table(rows) ? 1 : 0);
