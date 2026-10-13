// ...Thunder Plus as a desktop app: one window running the same index.html as the website.
const { app, BrowserWindow, session, shell, Menu, protocol, net } = require('electron');
const path = require('path');
const { pathToFileURL } = require('url');
const { setupUpdates } = require('./updater');

// The app is served from app://thunder/ rather than opened as plain files. Plain files can't fetch each
// other, load audio worklets or talk across frames, which the …waves and …chains editors (frames inside
// the app) and the mastering and bass engines need.
const ROOT = path.join(__dirname, 'app');
protocol.registerSchemesAsPrivileged([{ scheme:'app', privileges:{ standard:true, secure:true, supportFetchAPI:true, corsEnabled:true, stream:true } }]);

// Audio can start without a click first (the tracker's play button still works the same).
app.commandLine.appendSwitch('autoplay-policy', 'no-user-gesture-required');

// Web MIDI, the save/open dialogs, the output device list and audio are allowed; nothing else is asked for.
const ALLOW = new Set(['midi', 'midiSysex', 'fileSystem', 'clipboard-sanitized-write', 'clipboard-read', 'media', 'speaker-selection']);

// One copy at a time: opening it again brings the open window to the front (two copies would fight over
// the same saved songs and kit).
if (!app.requestSingleInstanceLock()) app.quit();
let win = null;

function createWindow(){
  win = new BrowserWindow({
    width: 1400, height: 900, minWidth: 380, minHeight: 500,
    backgroundColor: '#f0e6f4', title: '...Thunder Plus',
    icon: path.join(ROOT, 'icons', 'plus-icon-512.png'),
    autoHideMenuBar: true,
    webPreferences: { contextIsolation: true, sandbox: true, backgroundThrottling: false }   // keep playing when the window is in the back
  });
  win.loadURL('app://thunder/index.html');
  // Links (help, licences) open in the normal browser rather than inside the app.
  win.webContents.setWindowOpenHandler(({ url }) => { if (/^https?:/.test(url)) shell.openExternal(url); return { action: 'deny' }; });
  win.webContents.on('will-navigate', (e, url) => { if (/^https?:/.test(url)){ e.preventDefault(); shell.openExternal(url); } });
  win.on('closed', () => { win = null; });
}

app.on('second-instance', () => { if (win){ if (win.isMinimized()) win.restore(); win.focus(); } });

app.whenReady().then(() => {
  protocol.handle('app', req => {
    let p = decodeURIComponent(new URL(req.url).pathname);
    if (p === '' || p.endsWith('/')) p += 'index.html';
    const file = path.normalize(path.join(ROOT, p));
    if (!file.startsWith(ROOT + path.sep)) return new Response('Not found', { status:404 });
    return net.fetch(pathToFileURL(file).toString());
  });
  session.defaultSession.setPermissionRequestHandler((wc, perm, cb) => cb(ALLOW.has(perm)));
  session.defaultSession.setPermissionCheckHandler((wc, perm) => ALLOW.has(perm));
  if (process.platform !== 'darwin') Menu.setApplicationMenu(null);   // the Mac keeps its menu so ⌘C / ⌘V / ⌘Q work
  createWindow();
  // New versions come from the GitHub Releases page (see updater.js).
  setupUpdates({ name: '...Thunder Plus', page: 'https://github.com/matthettich/thunder-plus/releases/latest' });
  app.on('activate', () => { if (!BrowserWindow.getAllWindows().length) createWindow(); });
});
app.on('window-all-closed', () => { if (process.platform !== 'darwin') app.quit(); });
