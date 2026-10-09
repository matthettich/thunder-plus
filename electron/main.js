// ...Thunder as a desktop app: one window running the same index.html as the website.
const { app, BrowserWindow, session, shell, Menu } = require('electron');
const path = require('path');

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
  win.loadFile(path.join(__dirname, 'app', 'index.html'));
  // Links (help, licences) open in the normal browser rather than inside the app.
  win.webContents.setWindowOpenHandler(({ url }) => { if (/^https?:/.test(url)) shell.openExternal(url); return { action: 'deny' }; });
  win.webContents.on('will-navigate', (e, url) => { if (/^https?:/.test(url)){ e.preventDefault(); shell.openExternal(url); } });
}

app.whenReady().then(() => {
  session.defaultSession.setPermissionRequestHandler((wc, perm, cb) => cb(ALLOW.has(perm)));
  session.defaultSession.setPermissionCheckHandler((wc, perm) => ALLOW.has(perm));
  if (process.platform !== 'darwin') Menu.setApplicationMenu(null);   // the Mac keeps its menu so ⌘C / ⌘V / ⌘Q work
  createWindow();
  app.on('activate', () => { if (!BrowserWindow.getAllWindows().length) createWindow(); });
});
app.on('window-all-closed', () => { if (process.platform !== 'darwin') app.quit(); });
