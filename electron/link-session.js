/* ABLETON LINK, the main process's side (3 Nov; desktop only, off by default). The session lives here, in link/'s native
   addon (GPL-2.0-or-later, optional: main.js loads it in a try, and a build without it says "not available in this build").
   This file is ours (no Link code in it): it owns enable / tempo / start / stop and tells the page what Link is doing:
     thunder:link-state  on any change of peers, tempo, playing (and when) or start/stop sync
     thunder:link-time   every 20 ms while enabled: {beat, phase, tempo, at, hostNow}: beat and phase (quantum 4) at Link time
                         `at` (µs, Link's own clock); hostNow is this process's performance clock (ms since the epoch), for
                         checking the IPC delay. The page maps `at` to its audio clock with an offset it measures on every
                         message (the smallest of the last second's: the least IPC delay), as clkIn does with MIDI clock.
   Nothing is sent while Link is off; the addon then has no sockets open. */
'use strict';
const Q = 4;   // quantum: a bar of 4 beats (start on the next bar line, phase over the bar)
const now = () => performance.timeOrigin + performance.now();

function linkSession(addon, send){
  let on = false, timer = null, last = null;
  const stateOf = s => ({available:true, enabled:on, sync:!!s.sync, peers:s.peers | 0, tempo:s.tempo, playing:!!s.playing, playAt:s.playAt});
  function tick(){
    let s; try { s = addon.snapshot(Q); } catch (e){ return; }
    send('thunder:link-time', {beat:s.beat, phase:s.phase, tempo:s.tempo, at:s.now, hostNow:now()});
    if (!last || s.peers !== last.peers || s.playing !== last.playing || s.playAt !== last.playAt || s.sync !== last.sync || Math.abs(s.tempo - last.tempo) > 1e-6){
      last = s; send('thunder:link-state', stateOf(s)); }
  }
  const api = {
    available: true,
    // enable(on, bpm): our tempo goes in first, so a session of one starts at the song's tempo; joining a session that
    // already runs, Link takes the session's tempo (the page then follows it).
    enable(v, bpm){
      on = !!v; clearInterval(timer); timer = null; last = null;
      if (on && bpm >= 20) addon.setTempo(+bpm);
      addon.enable(on);
      if (on){ timer = setInterval(tick, 20); tick(); }
      else send('thunder:link-state', {available:true, enabled:false, sync:false, peers:0, tempo:0, playing:false, playAt:0});
      return on;
    },
    sync(v){ addon.startStopSync(!!v); if (on) tick(); return !!v; },
    setTempo(bpm){ return Number.isFinite(+bpm) ? addon.setTempo(+bpm) : false; },
    // start(at, sync): we start at Link time `at` (µs; the page picked the bar). Default: 100 ms from now.
    start(at, sync){ const t = Number.isFinite(at) && at > 0 ? at : addon.now() + 100000; addon.start(t, Q, !!sync); if (on) tick(); return t; },
    stop(){ addon.stop(); if (on) tick(); return true; },
    off(){ if (on) api.enable(false); },
    get enabled(){ return on; }
  };
  return api;
}
module.exports = { linkSession, QUANTUM: Q };
