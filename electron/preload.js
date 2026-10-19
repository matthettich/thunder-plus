// The one bridge between the app page and the desktop shell (the page runs isolated and sandboxed). It only offers
// the updater: what version this is and whether a newer one is out, a "check now", and "get it" (restart into a
// downloaded update, or open the download page). The website never has window.thunderDesktop.
const { contextBridge, ipcRenderer } = require('electron');

contextBridge.exposeInMainWorld('thunderDesktop', {
  updates: {
    state: () => ipcRenderer.invoke('thunder:upd-state'),   // {version, avail, ready, notifyOnly, dl (0–1 while downloading), file}
    check: () => ipcRenderer.invoke('thunder:upd-check'),   // check now and say the result in a box
    act: () => ipcRenderer.invoke('thunder:upd-act'),       // restart into it, or (Mac, portable) download it and open it
    on: cb => { const f = (e, s) => cb(s); ipcRenderer.on('thunder:upd', f); return () => ipcRenderer.removeListener('thunder:upd', f); }
  }
});
