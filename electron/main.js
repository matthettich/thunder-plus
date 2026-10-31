// ...Thunder Plus as a desktop app: one window running the same index.html as the website.
const { app, BrowserWindow, session, shell, Menu, protocol, net, ipcMain, dialog } = require('electron');
const path = require('path');
const fs = require('fs'), fsp = fs.promises;
const { pathToFileURL } = require('url');
const { setupUpdates } = require('./updater');

// The app is served from app://thunder/ rather than opened as plain files. Plain files can't fetch each
// other, load audio worklets or talk across frames, which the …waves and …chains editors (frames inside
// the app) and the mastering and bass engines need.
const ROOT = path.join(__dirname, 'app');
protocol.registerSchemesAsPrivileged([{ scheme:'app', privileges:{ standard:true, secure:true, supportFetchAPI:true, corsEnabled:true, stream:true } }]);

// Audio can start without a click first (the tracker's play button still works the same).
app.commandLine.appendSwitch('autoplay-policy', 'no-user-gesture-required');
// PLAYBACK IN THE BACKGROUND (owner, 31 Oct: stutters and hangs when the app isn't in front): besides backgroundThrottling
// (below), Chromium lowers a hidden or covered window's renderer priority and slows its timers; these keep it running as
// in front, so the sequencer (main thread) keeps feeding the audio on time.
app.commandLine.appendSwitch('disable-renderer-backgrounding');
app.commandLine.appendSwitch('disable-background-timer-throttling');
app.commandLine.appendSwitch('disable-backgrounding-occluded-windows');

// Web MIDI, the save/open dialogs, the output device list and audio are allowed; nothing else is asked for.
const ALLOW = new Set(['midi', 'midiSysex', 'fileSystem', 'clipboard-sanitized-write', 'clipboard-read', 'speaker-selection']);   // no 'media': the app never uses the mic or camera

/* CONTENT SECURITY POLICY, report-only (so nothing can break): the console lists what it WOULD block, to read before it is ever
   enforced. Everything the app loads is its own (app://thunder/), so there are no outside origins to allow. What it can't do
   yet: the app is inline <script> blocks, so 'unsafe-inline' has to stay until they are moved out or given nonces; until then
   this stops pulling in a script, frame, image or connection from another site and locks object / base / form, not injected
   inline script. 'wasm-unsafe-eval': the effects and engines are WebAssembly; blob: is how worklets and workers are made.
   A <meta> tag can't carry Report-Only, which is why this is a header, set here for the pages the app:// handler serves. */
const CSP = [
  "default-src 'self'",
  "script-src 'self' 'unsafe-inline' 'wasm-unsafe-eval' blob:",
  "worker-src 'self' blob:",
  "style-src 'self' 'unsafe-inline'",
  "font-src 'self'",
  "img-src 'self' data: blob:",
  "media-src 'self' blob: data:",
  "connect-src 'self' blob:",
  "frame-src 'self'",
  "object-src 'none'",
  "base-uri 'none'",
  "form-action 'none'",
].join('; ');
const withCsp = res => { const h = new Headers(res.headers); h.set('Content-Security-Policy-Report-Only', CSP); return new Response(res.body, { status: res.status, statusText: res.statusText, headers: h }); };

// One copy at a time: opening it again brings the open window to the front (two copies would fight over
// the same saved songs and kit).
if (!app.requestSingleInstanceLock()){ app.quit(); return; }   // return: or whenReady below would still open a second window
let win = null;

/* FILE SAFETY (21 Oct): project files by path, a .thunder file opened from the desktop, and "Save changes?" on close.
   A .thunder file double-clicked (Windows / Linux: in argv; a second launch: second-instance; Mac: open-file) waits
   until the page says it's ready, then goes to it. Closing the window asks the page first (it answers at once, then
   shows its own Save / Don't save / Cancel when the song has unsaved changes); a page that doesn't answer in 3 s, or
   one that crashed, doesn't stop the window closing. */
const isProj = p => typeof p === 'string' && /\.(thunder|json)$/i.test(p);
const argPath = argv => (argv || []).slice(1).find(a => /\.thunder$/i.test(a) && fs.existsSync(a)) || null;
let pendingPath = argPath(process.argv), pageReady = false, closeOk = false, quitting = false, ackT = null;
const sendPath = p => { if (pageReady && win) win.webContents.send('thunder:open-path', p); else pendingPath = p; };
app.on('open-file', (e, p) => { e.preventDefault(); sendPath(p); if (win){ if (win.isMinimized()) win.restore(); win.focus(); } });
app.on('before-quit', () => { quitting = true; });
async function readProj(p){
  if (!isProj(p)) return { error: 'that isn’t a .thunder or .json file' };
  try { return { path: p, name: path.basename(p), text: await fsp.readFile(p, 'utf8') }; }
  catch (e) { return { error: e.code === 'ENOENT' ? 'it isn’t there any more' : e.message }; }
}
// Written next to the file first, then renamed over it, so a crash mid-save never leaves half a project.
async function writeProj(p, text){
  if (!isProj(p) || typeof text !== 'string') return { error: 'not a project file' };
  const tmp = p + '.saving';
  try { await fsp.writeFile(tmp, text, 'utf8'); await fsp.rename(tmp, p); return { ok: true, path: p }; }
  catch (e) { try { await fsp.unlink(tmp); } catch (_) {} return { error: e.message }; }
}
const PROJ_FILTERS = [{ name: '...Thunder Plus project', extensions: ['thunder', 'json'] }];

function createWindow(){
  win = new BrowserWindow({
    width: 1400, height: 900, minWidth: 380, minHeight: 500,
    backgroundColor: '#f0e6f4', title: '...Thunder Plus',
    icon: path.join(ROOT, 'icons', 'plus-icon-512.png'),
    autoHideMenuBar: true,
    // preload.js: the page's only bridge to the shell (the updater: version, check now, get it).
    webPreferences: { contextIsolation: true, sandbox: true, backgroundThrottling: false, preload: path.join(__dirname, 'preload.js') }   // keep playing when the window is in the back
  });
  closeOk = false; pageReady = false;
  win.loadURL('app://thunder/index.html');
  win.webContents.on('did-start-loading', () => { pageReady = false; });   // a reload: wait for the page again
  win.webContents.on('render-process-gone', () => { pageReady = false; });
  win.on('close', e => {
    if (closeOk || !pageReady) return;
    e.preventDefault();
    win.webContents.send('thunder:close-ask');
    clearTimeout(ackT); ackT = setTimeout(() => { closeOk = true; if (win) win.close(); }, 3000);
  });
  // Links (help, licences) open in the normal browser rather than inside the app.
  win.webContents.setWindowOpenHandler(({ url }) => { if (/^https?:/.test(url)) shell.openExternal(url); return { action: 'deny' }; });
  // The window only ever shows the app: a dropped file (file://) or anything else must not navigate it away.
  win.webContents.on('will-navigate', (e, url) => { e.preventDefault(); if (/^https?:/.test(url)) shell.openExternal(url); });
  win.on('closed', () => { win = null; });
}

app.on('second-instance', (e, argv) => { if (win){ if (win.isMinimized()) win.restore(); win.focus(); } const p = argPath(argv); if (p) sendPath(p); });

app.whenReady().then(() => {
  protocol.handle('app', req => {
    let p = decodeURIComponent(new URL(req.url).pathname);
    if (p === '' || p.endsWith('/')) p += 'index.html';
    const file = path.normalize(path.join(ROOT, p));
    if (!file.startsWith(ROOT + path.sep)) return new Response('Not found', { status:404 });
    return net.fetch(pathToFileURL(file).toString()).then(res => /\.html?$/i.test(file) ? withCsp(res) : res);
  });
  session.defaultSession.setPermissionRequestHandler((wc, perm, cb) => cb(ALLOW.has(perm)));
  session.defaultSession.setPermissionCheckHandler((wc, perm) => ALLOW.has(perm));
  if (process.platform !== 'darwin') Menu.setApplicationMenu(null);   // the Mac keeps its menu so ⌘C / ⌘V / ⌘Q work
  createWindow();
  // New versions come from the GitHub Releases page (see updater.js). The page asks through preload.js.
  const updates = setupUpdates({ name: '...Thunder Plus', page: 'https://github.com/matthettich/thunder-plus/releases/latest', repo: 'matthettich/thunder-plus' });
  ipcMain.handle('thunder:upd-state', () => updates.state());
  ipcMain.handle('thunder:upd-check', () => updates.check());
  ipcMain.handle('thunder:upd-act', () => updates.act());
  // FILE SAFETY: the page's project files and the close / open hand-off.
  ipcMain.handle('thunder:file-read', (e, p) => readProj(p));
  ipcMain.handle('thunder:file-save', (e, p, text) => writeProj(p, text));
  ipcMain.handle('thunder:file-open', async () => {
    const r = await dialog.showOpenDialog(win, { properties: ['openFile'], filters: PROJ_FILTERS });
    return r.canceled || !r.filePaths[0] ? null : readProj(r.filePaths[0]);
  });
  ipcMain.handle('thunder:file-saveas', async (e, name, text, near) => {
    const base = String(name || 'Song.thunder').replace(/[\\/:*?"<>|]/g, '_');
    const dir = isProj(near) && fs.existsSync(path.dirname(near)) ? path.dirname(near) : app.getPath('documents');
    const r = await dialog.showSaveDialog(win, { defaultPath: path.join(dir, base), filters: [{ name: '...Thunder Plus project', extensions: ['thunder'] }] });
    if (r.canceled || !r.filePath) return null;
    return writeProj(isProj(r.filePath) ? r.filePath : r.filePath + '.thunder', text);
  });
  // RENDER TO (owner, 31 Oct: "I don't have any clue where render renders a file to"): renders and exports ask where to go
  // first, with the system Save dialog (starting where the last one went, else the project's folder, else Music), then the
  // page writes the file there and can show it in its folder. Only .wav, .zip and .mid files are written this way.
  let lastOut = '';
  const OUT_EXT = { wav: 'WAV audio', zip: 'Zip archive', mid: 'MIDI file' };
  ipcMain.handle('thunder:out-pick', async (e, name, near) => {
    const base = String(name || 'Render.wav').replace(/[\\/:*?"<>|]/g, '_'), ext = (base.match(/\.(\w+)$/) || [])[1] || 'wav';
    if (!OUT_EXT[ext.toLowerCase()]) return null;
    const dir = lastOut && fs.existsSync(lastOut) ? lastOut : isProj(near) && fs.existsSync(path.dirname(near)) ? path.dirname(near) : app.getPath('music');
    const r = await dialog.showSaveDialog(win, { defaultPath: path.join(dir, base), filters: [{ name: OUT_EXT[ext.toLowerCase()], extensions: [ext.toLowerCase()] }] });
    if (r.canceled || !r.filePath) return null;
    let f = r.filePath; if (!f.toLowerCase().endsWith('.' + ext.toLowerCase())) f += '.' + ext.toLowerCase();
    lastOut = path.dirname(f); return f;
  });
  ipcMain.handle('thunder:out-write', async (e, f, data) => {
    try {
      if (typeof f !== 'string' || !/\.(wav|zip|mid)$/i.test(f)) return { error: 'not a render file' };
      await fs.promises.writeFile(f, Buffer.from(data instanceof ArrayBuffer ? new Uint8Array(data) : data));
      return { ok: true, path: f };
    } catch (err) { return { error: String((err && err.message) || err) }; }
  });
  ipcMain.handle('thunder:out-show', (e, f) => { if (typeof f === 'string' && fs.existsSync(f)) shell.showItemInFolder(f); return true; });
  ipcMain.on('thunder:page-ready', () => { pageReady = true; if (pendingPath && win){ const p = pendingPath; pendingPath = null; win.webContents.send('thunder:open-path', p); } });
  ipcMain.on('thunder:close-ack', () => clearTimeout(ackT));
  ipcMain.on('thunder:close-reply', (e, ok) => { clearTimeout(ackT); if (!ok){ quitting = false; return; } closeOk = true; if (win) win.close(); if (quitting) app.quit(); });
  app.on('activate', () => { if (!BrowserWindow.getAllWindows().length) createWindow(); });
});
app.on('window-all-closed', () => { if (process.platform !== 'darwin') app.quit(); });
