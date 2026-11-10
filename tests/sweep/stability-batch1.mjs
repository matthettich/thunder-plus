// STABILITY BATCH 1 (10 Nov): the fixes F1-F7, each through the app in headless Chrome (F8 is a Node test, tests/stability-batch1.test.js).
//   node tests/sweep/stability-batch1.mjs [F1,F2,...]      each check prints PASS / FAIL; exit code 1 if any FAIL
import { openApp, table, live } from './lib.mjs';
const only = (process.argv[2] || '').split(',').filter(Boolean), want = k => !only.length || only.includes(k);
const rows = [], add = (n, ok, d) => { const r = [n, ok ? 'PASS' : 'FAIL', d || '']; rows.push(r); live(r); };
// a first PLAY whose live worklets are not ready yet: the warm-up waits 400 ms for them
const SLOW = `bassOK.delete(ctx); bassLoad.set(ctx, new Promise(r => setTimeout(() => r(true), 400)));`;
// a loaded machine can crash the page: run a section again (up to 3 times) when the browser went away
async function retry(f){ for (let i = 0; ; i++){ try { return await f(); } catch(e){ if (i >= 2 || !/closed|crash/i.test(e.message)) throw e; } } }
async function fresh(opts, init){ const A = await openApp(opts); if (init) await A.run(init); return A; }

if (want('F1')){   // PLAY then STOP inside the first-PLAY warm-up must not start the song
  const A = await fresh();
  const r = await A.run(SLOW + `const warm = !bassOK.has(ctx || {}) && kit.voices.some((v, i) => v && isLivePad(i)); tStart('song', {row:0}); const sync = tp.on;
    await new Promise(r => setTimeout(r, 30)); stopKey(); await new Promise(r => setTimeout(r, 2000)); const later = tp.on; tStop(true); return {warm, sync, later};`);
  add('F1 STOP inside the warm-up: the song does not start', r.warm && !r.sync && !r.later, JSON.stringify(r));
  await A.close();
  const B = await fresh();
  const r2 = await B.run(SLOW + `tStart('song', {row:0}); tStart('song', {row:0}); await new Promise(r => setTimeout(r, 1500)); const on = tp.on; tStop(true); return on;`).catch(e => 'err ' + e.message);
  add('F1 PLAY alone (pressed twice) still starts after the warm-up', r2 === true, String(r2));
  await B.close();
}

if (want('F2')){   // one event that throws is skipped; a flood still stops the song
  const A = await fresh();
  const r = await A.run(`aset.core = 'page'; let n = 0, ok = 0, threw = 0; const o = rt.pl.play; rt.pl.play = function(e){ if (e.type === 'note' && ++n % 3 === 0){ threw++; throw new Error('bad event'); } if (e.type === 'note') ok++; return o.apply(this, arguments); };
    tStart('song', {row:0}); await new Promise(r => setTimeout(r, 3000)); const on = tp.on, usedCore = !!tp.core; tStop(true); rt.pl.play = o; return {on, usedCore, ok, threw};`);
  add('F2 page sequencer, every third note throws: the song keeps playing, the others play', r.on && !r.usedCore && r.threw >= 2 && r.ok >= 3, JSON.stringify(r));
  const r2 = await A.run(`let threw = 0; const o = rt.pl.play; rt.pl.play = function(e){ threw++; throw new Error('broken engine'); };
    tStart('song', {row:0}); await new Promise(r => setTimeout(r, 3000)); const on = tp.on; tStop(true); rt.pl.play = o; return {on, threw};`);
  add('F2 page sequencer, every event throws: the song stops as before', r2.on === false, JSON.stringify(r2));
  // the audio-thread sequencer's batch: a bad event in the middle of a row must not drop the rest of the batch
  const r3 = await A.run(`aset.core = 'core'; rtInit(); coreLoad(); for (let i = 0; i < 60 && !core.ready; i++) await new Promise(r => setTimeout(r, 100));
    tStart('song', {row:0}); await new Promise(r => setTimeout(r, 500)); const usedCore = !!tp.core, got = []; const o = rt.pl.play;
    rt.pl.play = function(e){ if (e.mark === 'bad') throw new Error('bad event'); if (e.mark) { got.push(e.mark); return; } return o.apply(this, arguments); };
    const t = ctx.currentTime + 0.3, ev = m => ({type:'off', track:0, t, mark:m}); let err = null;
    try { coreMsg({op:'rows', gen:core.gen, time:t, bpm:S.bpm, tks:6, base:0, btT:0, alive:true, rows:[{t, pos:[], bpm:S.bpm, tks:6, line:0, evs:[ev('a'), ev('bad'), ev('b')]}, {t:t + 0.1, pos:[], bpm:S.bpm, tks:6, line:1, evs:[ev('c')]}]}); } catch(e){ err = e.message; }
    const on = tp.on; tStop(true); rt.pl.play = o; return {usedCore, on, got, err};`);
  add('F2 audio-thread sequencer batch: the bad event is skipped, the rest of the batch plays', r3.usedCore && !r3.err && r3.got.join() === 'a,b,c', JSON.stringify(r3));
  await A.close();
}
if (want('F3')){   // a context that reports 'interrupted' is resumed, and PLAY on one that stays stopped gives up after 2.5 s
  const A = await fresh();
  const r = await A.run(`audio(); let res = 0; const o = ctx.resume.bind(ctx); ctx.resume = function(){ res++; return o(); };
    Object.defineProperty(ctx, 'state', {configurable:true, get: () => 'interrupted'});
    audio(); const afterAudio = res; const msgs = [], ot = toast; toast = function(m){ msgs.push(String(m)); return ot.apply(this, arguments); };
    tStart('song', {row:0}); await new Promise(r => setTimeout(r, 1000)); const onAt1 = tp.on; await new Promise(r => setTimeout(r, 2500)); const onAt35 = tp.on; toast = ot; tStop(true);
    return {afterAudio, resumes:res, onAt1, onAt35, msgs};`);
  add('F3 audio() resumes an interrupted context', r.afterAudio === 1, JSON.stringify(r));
  add('F3 PLAY with the state stuck: still on at 1 s, stopped by 3.5 s with the toast', r.onAt1 && !r.onAt35 && r.msgs.some(m => /interrupted/i.test(m)), JSON.stringify(r));
  await A.close();
}
// a Pixel 7 as the app sees one (the hand-over is for phones)
const PIXEL = { userAgent: 'Mozilla/5.0 (Linux; Android 14; Pixel 7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/126.0.0.0 Mobile Safari/537.36', isMobile: true, hasTouch: true, viewport: { width: 412, height: 915 } };
// a MIDI output that records what is sent; every instrument plays out of it
const MIDIRIG = `const log = []; midi.access = {outputs:new Map([['o1', {send:b => log.push(Array.from(b)), native:false}]]), inputs:new Map()}; midi.outIds[0] = 'o1';
  S.insts.forEach(I => { if (I){ I.mo = 1; I.mch = 0; I.mpt = 0; } }); window.__midiLog = log;`;
const nOff = `window.__midiLog.filter(m => (m[0] & 0xF0) === 0xB0 && m[1] === 123).length`;
if (want('F4')){   // the hand-over to the page sequencer must not cut the MIDI notes that are sounding
  let A, r;
  for (let attempt = 0; attempt < 4; attempt++){   // (a loaded machine can make the core's own watchdog let go of the song: try again)
    A = await fresh({ page: PIXEL });
    r = await A.run(MIDIRIG + `aset.core = 'core'; rtInit(); coreLoad(); for (let i = 0; i < 60 && !core.ready; i++) await new Promise(r => setTimeout(r, 100));
    tStart('song', {row:0}); for (let i = 0; i < 50 && !tp.core; i++) await new Promise(r => setTimeout(r, 100)); await new Promise(r => setTimeout(r, 1500)); const core0 = !!tp.core, bad = core.bad, on0 = tp.on, held0 = mx.held.reduce((a, H) => a + (H ? H.length : 0), 0), notes = log.filter(m => (m[0] & 0xF0) === 0x90).length;
    log.length = 0; let rel = 0, stp = 0; const oR = moRelease, oS = moStop; moRelease = function(){ rel++; return oR.apply(this, arguments); }; moStop = function(){ stp++; return oS.apply(this, arguments); };
    Object.defineProperty(document, 'hidden', {configurable:true, get: () => true}); document.dispatchEvent(new Event('visibilitychange'));
    const handed = !tp.core && tp.on, anoOff = ${nOff}, relCalls = rel, stopCalls = stp, pending = log.length; await new Promise(r => setTimeout(r, 800)); const stillOn = tp.on;
    log.length = 0; Object.defineProperty(document, 'hidden', {configurable:true, get: () => false}); moRelease = oR; moStop = oS; tStop();
    const stopOff = ${nOff}; return {core0, bad, on0, held0, notes, handed, anoOff, relCalls, stopCalls, pending, stillOn, stopOff};`);
    if (r.core0 && !r.bad && r.handed && r.held0 > 0){ break; } await A.close();
  }
  add('F4 hand-over: moStop / moRelease are not run, no All Notes Off goes out', r.core0 && r.handed && r.held0 > 0 && r.anoOff === 0 && r.relCalls === 0 && r.stopCalls === 0 && r.stillOn, JSON.stringify(r));
  add('F4 a normal STOP still sends All Notes Off', r.stopOff > 0, JSON.stringify(r));
  await A.close();
}
if (want('F5')){   // an effect change booked ahead must not land after STOP's restore
  const A = await fresh();
  const r = await A.run(`const t = 0; S.tfx[t] = [Object.assign({type:'eq', on:true}, ...TFX.eq.ps.map(q => ({[q.k]:q.d != null ? q.d : q.min})))]; rtInit(); for (let i = 0; i < 100 && !rt.fxOk; i++) await new Promise(r => setTimeout(r, 100)); rtFx(t); await new Promise(r => setTimeout(r, 500));
    const got = [], o = MessagePort.prototype.postMessage; MessagePort.prototype.postMessage = function(m){ if (m && m.pset) got.push(JSON.stringify(m.pset)); return o.apply(this, arguments); };
    rt.pl.play({type:'fxp', t:ctx.currentTime + 0.2, track:t, slot:0, pi:0, v:255});   // the song's value, booked 0.2 s ahead
    rt.pl.stopAll(ctx.currentTime); await new Promise(r => setTimeout(r, 500)); MessagePort.prototype.postMessage = o;
    return {got, ps: TFX.eq.ps.length};`);
  const restored = r.got.length >= 1 && r.got[r.got.length - 1] === r.got[0];
  add('F5 FXV booked 0.2 s ahead, then STOP: the last message to the effect is the restore', restored && r.got.length === 1, JSON.stringify(r));
  await A.close();
}
if (want('F6')){   // tStop is exception-safe: the state flips and the background stream is released whatever throws
  const A = await fresh();
  const r = await A.run(`const o = rcdSongStopped; rcdSongStopped = function(){ throw new Error('rcd broke'); };
    tStart('song', {row:0}); for (let i = 0; i < 120 && !tp.on; i++) await new Promise(r => setTimeout(r, 100)); const on0 = tp.on; await new Promise(r => setTimeout(r, 300));
    RCD.sync = true; let thrown = null; try { tStop(); } catch(e){ thrown = e.message; }
    const off = !tp.on, noTimer = !tp.timer; rcdSongStopped = o; RCD.sync = false;
    tStart('song', {row:0}); await new Promise(r => setTimeout(r, 800)); const again = tp.on; tStop(true); return {on0, thrown, off, noTimer, again};`);
  add('F6 rcdSongStopped throws: tp.on is false after STOP and PLAY works again', r.on0 && r.off && r.noTimer && r.again, JSON.stringify(r));
  await A.close();
  const r2 = await retry(async () => { const B = await fresh({ page: PIXEL }); try { return await B.run(`const o = rt.pl.stopAll; rt.pl.stopAll = function(){ throw new Error('stopAll broke'); };
    tStart('song', {row:0}); for (let i = 0; i < 120 && !tp.on; i++) await new Promise(r => setTimeout(r, 100)); await new Promise(r => setTimeout(r, 300));
    const playing = !!bgEl && !bgEl.paused, bgOn0 = bgOn, android = bgAndroid; try { tStop(); } catch(e){} await new Promise(r => setTimeout(r, 100));
    const r = {bgOn0, android, playing, paused:!bgEl || bgEl.paused, off:!tp.on}; rt.pl.stopAll = o; return r;`); } finally { await B.close().catch(() => {}); } });
  add('F6 stopAll throws on Android: the background stream is paused after STOP', r2.android && r2.playing && r2.paused && r2.off, JSON.stringify(r2));
}
if (want('F7')){   // the deck's spectrum analyser follows the master after Reset audio engine
  // a tone into the master (not the song: a quiet bar would pass either way); the level at that tone's own bins, so what the analyser
  // heard before the reset can't stand in for it
  const r = await retry(async () => { const A = await fresh(); try { return await A.run(`rtInit();
    const tone = async f => { const o = ctx.createOscillator(), g = ctx.createGain(); o.frequency.value = f; g.gain.value = 0.3; o.connect(g); g.connect(master); const a = specAnalyser(); o.start();
      await new Promise(r => setTimeout(r, 500)); const d = new Float32Array(a.frequencyBinCount); a.getFloatFrequencyData(d); o.stop(); g.disconnect();
      const k = Math.round(f / (ctx.sampleRate / a.fftSize)); let m = -Infinity; for (let b = k - 2; b <= k + 2; b++) if (d[b] > m) m = d[b]; return Math.round(m); };
    const before = await tone(1000); resetAudio(true); const after = await tone(3000); return {before, after};`); } finally { await A.close().catch(() => {}); } });
  add('F7 a tone into the master shows on the spectrum before and after Reset audio engine', r.before > -60 && r.after > -60, JSON.stringify(r));
}
process.exit(table(rows) ? 1 : 0);
