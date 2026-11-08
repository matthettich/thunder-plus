// NATIVE I/O HELPER (plan: docs/NATIVE-IO-HELPER-PLAN.md): runs as an Electron utility process ("thunder-io"), started
// lazily by main.js on the page's first thunder:io-start. It loads the native addon (electron/io/thunder_io.node) in a try
// and talks to the page over a MessagePort that main.js hands over. The addon's path arrives as the one process.argv entry
// ending in .node (main.js: utilityProcess.fork(file, [addonPath])). Messages from main: {op:'port'} with ports[0]; a second
// one (the page reloaded) replaces the first. The logic is a factory with no Electron in it, so tests/io.test.js can run it.
'use strict';

// loadAddon(): the addon, or throws. post(msg, transfer): to the page's port. Returns {hello, handle}: hello is what the
// wiring posts on every new port; when hello.available is false the wiring exits the process after posting it.
// timers: {setInterval, clearInterval}, injectable so tests tick the input poll by hand. shutdown() closes any open input.
const POLL_MS = 5, MAX_READS_PER_TICK = 8, MIDI_POLL_MS = 2;
function createIoHelper({ loadAddon, post, timers }) {
  const T = timers || { setInterval: (f, ms) => setInterval(f, ms), clearInterval: h => clearInterval(h) };
  let addon = null, hello, poll = null, inputOpen = false, midiPoll = null;
  const midiPorts = new Set(), midiIns = new Set();   // every MIDI port this page opened (closed with it: a reload, shutdown); the inputs among them
  try { addon = loadAddon(); hello = { op: 'hello', available: true, version: String(addon && addon.version ? addon.version() : ''), midi: !!(addon && typeof addon.midiPorts === 'function') }; }   // (midi: phase B; an older addon has none)
  catch (e) { addon = null; hello = { op: 'hello', available: false, why: String((e && e.message) || e) }; }
  const err = (id, e) => post({ op: 'error', id, why: typeof e === 'string' ? e : String((e && e.message) || e) });
  function stopPoll() { if (poll !== null) { T.clearInterval(poll); poll = null; } }
  // MIDI IN (phase B): the addon stamps each message with the OS time it came; they're handed on in batches every 2 ms
  function midiTick() {
    let list; try { list = addon.midiRead(); } catch (e) { return; }
    if (list && list.length) post({ op: 'midiIn', list });
  }
  function midiPollSet(on) {
    if (on && midiPoll === null) midiPoll = T.setInterval(midiTick, MIDI_POLL_MS);
    else if (!on && midiPoll !== null) { T.clearInterval(midiPoll); midiPoll = null; }
  }
  function closeMidi() {
    if (!addon || typeof addon.midiClose !== 'function') return;
    try { addon.midiClear(0); } catch (_) {}
    for (const p of midiPorts) { try { addon.midiClose(p); } catch (_) {} }
    midiPorts.clear(); midiIns.clear(); midiPollSet(false);
  }
  const hasMidi = () => !!addon && typeof addon.midiPorts === 'function';
  function closeInput() {
    stopPoll();
    if (!inputOpen) return;
    inputOpen = false;
    try { addon.closeInput(); } catch (_) {}
  }
  function tick() {
    // UNPLUGGED (A2): the addon says the device stopped by itself: what arrived is drained first, then the page is told
    const lost = typeof addon.inputLost === 'function' && addon.inputLost();
    for (let i = 0; i < MAX_READS_PER_TICK; i++) {
      let c;
      try { c = addon.read(); }
      catch (e) {
        post({ op: 'error', why: String((e && e.message) || e) });
        closeInput();
        post({ op: 'closed', why: String((e && e.message) || e) });
        return;
      }
      if (!c) { if (lost) { closeInput(); post({ op: 'closed', why: 'the interface went away (unplugged or switched off)' }); } return; }
      post({ op: 'in', frame0: c.frame0, hostNs: c.hostNs, channels: c.channels, frames: c.frames, data: c.data, overruns: c.overruns },
        c.data && c.data.buffer ? [c.data.buffer] : []);
    }
  }
  function handle(msg) {
    const id = msg && msg.id, op = msg && msg.op;
    if (op === 'open') {
      if (!addon) return err(id, hello.why || 'not available');
      closeInput();
      let r;
      try { r = addon.openInput(msg.dev, { rate: msg.rate, block: msg.block }); } catch (e) { return err(id, e); }
      post({ op: 'opened', id, ...r });
      if (r && r.ok) { inputOpen = true; poll = T.setInterval(tick, POLL_MS); }
    } else if (op === 'close') {
      closeInput();
      post({ op: 'closed', id });
    } else if (op === 'now') {   // the page lines up its clock with the addon's (hostNs) by a few of these round trips
      if (!addon) return err(id, hello.why || 'not available');
      try { post({ op: 'now', id, ns: addon.now() }); } catch (e) { err(id, e); }
    } else if (op === 'devices') {
      if (!addon) return err(id, hello.why || 'not available');
      try { post({ op: 'devices', id, devices: addon.devices() }); } catch (e) { err(id, e); }
    // MIDI (phase B). midiSend / midiSendMany / midiClear get no answer (they come in streams: notes, clock).
    } else if (op === 'midiSend') {
      if (hasMidi()) { try { addon.midiSend(msg.port, msg.bytes, msg.at || 0); } catch (_) {} }
    } else if (op === 'midiSendMany') {   // [[port, bytes, at], …]: one port message for a whole booking pass
      if (hasMidi() && Array.isArray(msg.list)) for (const m of msg.list) { try { addon.midiSend(m[0], m[1], m[2] || 0); } catch (_) {} }
    } else if (op === 'midiClear') {
      if (hasMidi()) { try { addon.midiClear(msg.port || 0); } catch (_) {} }
    } else if (op === 'midiPorts') {
      if (!hasMidi()) return err(id, hello.why || 'no MIDI in this helper');
      try { const r = addon.midiPorts(); post({ op: 'midiPorts', id, outs: r.outs, ins: r.ins }); } catch (e) { err(id, e); }
    } else if (op === 'midiOpenOut' || op === 'midiOpenIn') {
      if (!hasMidi()) return err(id, hello.why || 'no MIDI in this helper');
      let r; try { r = op === 'midiOpenOut' ? addon.midiOpenOut(String(msg.name || '')) : addon.midiOpenIn(String(msg.name || '')); } catch (e) { return err(id, e); }
      if (r && r.ok) { midiPorts.add(r.port); if (op === 'midiOpenIn') { midiIns.add(r.port); midiPollSet(true); } }
      post({ op: 'midiOpened', id, ...r });
    } else if (op === 'midiClose') {
      if (hasMidi()) { try { addon.midiClose(msg.port); } catch (_) {} midiPorts.delete(msg.port); if (midiIns.delete(msg.port) && !midiIns.size) midiPollSet(false); }   // (the last input closed: no more polling)
      if (id != null) post({ op: 'midiClosed', id });
    } else err(id, 'unknown op');
  }
  const shutdown = () => { closeInput(); closeMidi(); };
  return { hello, handle, shutdown };
}

// main.js's "restart once" rule, pure so it can be tested: the first death is answered by a fresh start on the next
// request; after the second the helper is given up on (the page falls back to getUserMedia / Web MIDI).
function createRestartGate(maxDeaths = 2) {
  let deaths = 0;
  return { died: () => { deaths++; }, canStart: () => deaths < maxDeaths, deaths: () => deaths };
}

module.exports = { createIoHelper, createRestartGate };

// Only inside an Electron utility process (parentPort exists there): plain require() from Node or the tests starts nothing.
if (process.parentPort) {
  const parent = process.parentPort, addonPath = process.argv.find(a => /\.node$/i.test(a)) || '';
  let port = null;
  // NO TRANSFER LIST (CodeRabbit, PR #14; the owner's beta.2 failures): Electron's MessagePortMain only transfers ports, so an
  // ArrayBuffer in the list threw on the first audio chunk and the helper died (twice, then the page fell back to inputs 1–2).
  // The chunk is copied instead (structured clone): ~5 MB/s for 28 channels at 48 kHz, nothing for this process.
  const helper = createIoHelper({ loadAddon: () => require(addonPath), post: m => { if (port) port.postMessage(m); } });
  process.on('exit', () => helper.shutdown());
  parent.on('message', e => {
    if (!(e.data && e.data.op === 'port') || !e.ports || !e.ports[0]) return;
    helper.shutdown();                                  // the page reloaded: its input is gone with it
    if (port) { try { port.close(); } catch (_) {} }   // the old port is dead
    port = e.ports[0];
    port.on('message', ev => helper.handle(ev.data));
    port.start();
    port.postMessage(helper.hello);
    if (!helper.hello.available) setTimeout(() => process.exit(0), 20);   // nothing to run; main reports "helper stopped" after a second try
  });
}
