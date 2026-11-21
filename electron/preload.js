// The one bridge between the app page and the desktop shell (the page runs isolated and sandboxed). It offers project
// files by path and the close / open-file hand-off (FILE SAFETY), and the updater: what version this is and whether a newer one is out, a "check now", and "get it" (restart into a
// downloaded update, or open the download page). The website never has window.thunderDesktop.
const { contextBridge, ipcRenderer, webUtils } = require('electron');

contextBridge.exposeInMainWorld('thunderDesktop', {
  // FILE SAFETY (21 Oct): project files by path (open, read a recent one, save, save as), and the window's close / a
  // .thunder file opened from the desktop. Only .thunder and .json files can be read or written.
  files: {
    open: () => ipcRenderer.invoke('thunder:file-open'),                          // the Open dialog → {path, name, text} | null
    read: p => ipcRenderer.invoke('thunder:file-read', p),                         // {path, name, text} | {error}
    save: (p, text) => ipcRenderer.invoke('thunder:file-save', p, text),           // {ok, path} | {error}
    saveAs: (name, text, near) => ipcRenderer.invoke('thunder:file-saveas', name, text, near),  // the Save dialog → {ok, path} | null
    outPick: (name, near) => ipcRenderer.invoke('thunder:out-pick', name, near),   // RENDER TO: the Save dialog for a render → path | null
    outWrite: (p, data) => ipcRenderer.invoke('thunder:out-write', p, data),      // write it (bytes) → {ok, path} | {error}
    outShow: p => ipcRenderer.invoke('thunder:out-show', p),                       // show it in its folder
    pathOf: f => { try { return (webUtils && webUtils.getPathForFile(f)) || ''; } catch (e) { return ''; } }   // a dropped file's path, so Save goes back to it
  },
  app: {
    ready: () => ipcRenderer.send('thunder:page-ready'),                          // the page can take close-asks and opened files
    onCloseAsk: cb => { ipcRenderer.on('thunder:close-ask', () => cb()); },
    closeAck: () => ipcRenderer.send('thunder:close-ack'),                         // the page heard it (else the window closes after 3 s)
    closeReply: ok => ipcRenderer.send('thunder:close-reply', !!ok),              // true: close (and quit, if quitting); false: stay
    onOpenPath: cb => { ipcRenderer.on('thunder:open-path', (e, p) => cb(p)); },
    naturalScroll: () => ipcRenderer.invoke('thunder:natural-scroll'),          // macOS Natural scrolling on (the page's cursor / value wheels follow it)
    playing: on => ipcRenderer.send('thunder:playing', !!on),                     // APP NAP: the song plays / stopped (macOS keeps the app awake while it plays)
    reload: () => ipcRenderer.send('thunder:reload')                              // "Apply (reload)": the shell reloads this window (location.reload() is swallowed by main.js's will-navigate guard)
  },
  // ABLETON LINK (3 Nov): the session lives in the main process (link-session.js). available: false when this build has no
  // Link addon (or it didn't load here; why says what happened). Times are Link's clock in µs; the page maps them itself.
  link: (() => {
    let av = { available: false, why: '' }; try { av = ipcRenderer.sendSync('thunder:link-avail') || av; } catch (e) {}
    const on = ch => cb => { const f = (e, m) => cb(m); ipcRenderer.on(ch, f); return () => ipcRenderer.removeListener(ch, f); };
    return {
      available: !!av.available, why: String(av.why || ''),
      enable: (v, bpm) => ipcRenderer.invoke('thunder:link-enable', !!v, +bpm || 0),   // join / leave; bpm: ours, for a session of one
      sync: v => ipcRenderer.invoke('thunder:link-sync', !!v),                          // start / stop sync
      setTempo: bpm => ipcRenderer.invoke('thunder:link-tempo', +bpm),
      start: (at, sync) => ipcRenderer.invoke('thunder:link-start', +at || 0, !!sync), // we start at Link time `at` (µs)
      stop: () => ipcRenderer.invoke('thunder:link-stop'),
      now: () => ipcRenderer.invoke('thunder:link-now'),                                // Link's clock (µs), for the page's round-trip clock ping
      onState: on('thunder:link-state'),   // {available, enabled, sync, peers, tempo, playing, playAt}
      onTime: on('thunder:link-time')      // every 20 ms while on: {beat, phase, tempo, at, hostNow}
    };
  })(),
  // NATIVE I/O HELPER: start() forks the helper (first call) and hands the page a MessagePort, which arrives as a window
  // 'message' ({thunderIo:true}, ports[0]; the first thing on it is the helper's hello). available: this app has the helper path.
  io: { start: () => ipcRenderer.invoke('thunder:io-start'), mic: () => ipcRenderer.invoke('thunder:io-mic'), available: true },   // mic: asked for when an input opens (the helper also runs for MIDI alone)
  power: { on: cb => { const f = (e, why) => cb(why); ipcRenderer.on('thunder:power', f); return () => ipcRenderer.removeListener('thunder:power', f); } },   // 'resume' / 'unlock-screen': the computer woke (DEVICE RECOVERY)
  updates: {
    state: () => ipcRenderer.invoke('thunder:upd-state'),   // {version, avail, ready, notifyOnly, dl (0–1 while downloading), file}
    check: () => ipcRenderer.invoke('thunder:upd-check'),   // check now and say the result in a box
    act: () => ipcRenderer.invoke('thunder:upd-act'),       // restart into it, or (Mac, portable) download it and open it
    on: cb => { const f = (e, s) => cb(s); ipcRenderer.on('thunder:upd', f); return () => ipcRenderer.removeListener('thunder:upd', f); }
  }
});
// the port from main.js, forwarded to the page's own message listeners (the page can't reach ipcRenderer)
ipcRenderer.on('thunder:io-port', e => window.postMessage({ thunderIo: true }, '*', e.ports));
