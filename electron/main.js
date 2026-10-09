// ...Thunder Plus as a desktop app: one window running the same index.html as the website.
const { app, BrowserWindow, session, shell, Menu, protocol, net } = require('electron');
const path = require('path');
const { pathToFileURL } = require('url');

// The app is served from app://thunder/ rather than opened as plain files. Plain files can't fetch each
// other, load audio worklets or talk across frames, which the …waves editor (a frame inside the app) needs.
const ROOT = path.join(__dirname, 'app');
protocol.registerSchemesAsPrivileged([{ scheme:'app', privileges:{ standard:true, secure:true, supportFetchAPI:true, corsEnabled:true, stream:true } }]);

// Audio can start without a click first (the tracker's play button still works the same).
app.commandLine.appendSwitch('autoplay-policy', 'no-user-gesture-required');

// Web MIDI, the save/open dialogs and audio are allowed; nothing else is asked for.
const ALLOW = new Set(['midi', 'midiSysex', 'fileSystem', 'clipboard-sanitized-write', 'clipboard-read', 'media', 'speaker-selection']);

function createWindow(){
  const win = new BrowserWindow({
    width: 1280, height: 860, minWidth: 380, minHeight: 500,
    backgroundColor: '#0e0e10', title: '...Thunder Plus',
    icon: path.join(__dirname, 'app', 'icons', 'icon-512.png'),
    autoHideMenuBar: true,
    webPreferences: { contextIsolation: true, sandbox: true, backgroundThrottling: false }   // keep playing when the window is in the back
  });
  win.loadURL('app://thunder/index.html');
  // Links (help, licences) open in the normal browser rather than inside the app.
  win.webContents.setWindowOpenHandler(({ url }) => { if (/^https?:/.test(url)) shell.openExternal(url); return { action: 'deny' }; });
  win.webContents.on('will-navigate', (e, url) => { if (/^https?:/.test(url)){ e.preventDefault(); shell.openExternal(url); } });
}

app.whenReady().then(() => {
  protocol.handle('app', req => {
    let p = decodeURIComponent(new URL(req.url).pathname);
    if (p.endsWith('/')) p += 'index.html';
    const file = path.normalize(path.join(ROOT, p));
    if (!file.startsWith(ROOT + path.sep)) return new Response('Not found', { status:404 });
    return net.fetch(pathToFileURL(file).toString());
  });
  session.defaultSession.setPermissionRequestHandler((wc, perm, cb) => cb(ALLOW.has(perm)));
  session.defaultSession.setPermissionCheckHandler((wc, perm) => ALLOW.has(perm));
  if (process.platform !== 'darwin') Menu.setApplicationMenu(null);   // the Mac keeps its menu so ⌘C / ⌘V / ⌘Q work
  createWindow();
  app.on('activate', () => { if (!BrowserWindow.getAllWindows().length) createWindow(); });
});
app.on('window-all-closed', () => { if (process.platform !== 'darwin') app.quit(); });
