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
    onOpenPath: cb => { ipcRenderer.on('thunder:open-path', (e, p) => cb(p)); }
  },
  updates: {
    state: () => ipcRenderer.invoke('thunder:upd-state'),   // {version, avail, ready, notifyOnly, dl (0–1 while downloading), file}
    check: () => ipcRenderer.invoke('thunder:upd-check'),   // check now and say the result in a box
    act: () => ipcRenderer.invoke('thunder:upd-act'),       // restart into it, or (Mac, portable) download it and open it
    on: cb => { const f = (e, s) => cb(s); ipcRenderer.on('thunder:upd', f); return () => ipcRenderer.removeListener('thunder:upd', f); }
  }
});
