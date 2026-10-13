// Updates from GitHub Releases (electron-updater). A build checks a few seconds after it opens and every
// few hours after that; nothing happens when it's offline or when it isn't an installed build.
//  - Windows (installer) and Linux (AppImage): the new version downloads in the background, then a box
//    offers Restart now / Later. Later installs it the next time the app quits.
//  - Mac and the Windows portable .exe can't replace themselves while unsigned, so they say a new
//    version is out and open the download page.
const { app, dialog, shell, BrowserWindow } = require('electron');

let au = null;
try { au = require('electron-updater').autoUpdater; } catch (e) { au = null; }

function setupUpdates({ name, page }){
  if (!au || !app.isPackaged) return { check: async () => false };
  const notifyOnly = process.platform === 'darwin' || !!process.env.PORTABLE_EXECUTABLE_DIR;
  au.autoDownload = !notifyOnly;
  au.autoInstallOnAppQuit = true;
  au.logger = null;
  const told = new Set();
  let manual = false;
  const win = () => BrowserWindow.getFocusedWindow() || BrowserWindow.getAllWindows()[0] || null;
  const box = o => { const w = win(); return w ? dialog.showMessageBox(w, o) : dialog.showMessageBox(o); };

  au.on('update-available', info => {
    const byHand = manual; manual = false;
    if (!notifyOnly){ if (byHand) box({ type: 'info', buttons: ['OK'], title: name + ' update', message: 'Downloading ' + name + ' ' + info.version + '…', detail: 'You’ll be asked to restart when it’s ready.' }); return; }
    if (told.has(info.version) && !byHand) return;
    told.add(info.version);
    box({ type: 'info', buttons: ['Download', 'Later'], defaultId: 0, cancelId: 1, title: name + ' update',
      message: name + ' ' + info.version + ' is out.', detail: 'You have ' + app.getVersion() + '. Download the new version and install it over this one.' })
      .then(r => { if (r.response === 0) shell.openExternal(page); });
  });
  au.on('update-downloaded', info => {
    if (told.has('dl' + info.version)) return;
    told.add('dl' + info.version);
    box({ type: 'info', buttons: ['Restart now', 'Later'], defaultId: 0, cancelId: 1, title: name + ' update',
      message: name + ' ' + info.version + ' is ready.', detail: 'Restart to use it, or keep working: it installs the next time you quit.' })
      .then(r => { if (r.response === 0) setImmediate(() => au.quitAndInstall()); });
  });
  au.on('update-not-available', () => {
    if (manual){ manual = false; box({ type: 'info', buttons: ['OK'], title: name, message: 'You have the newest ' + name + ' (' + app.getVersion() + ').' }); }
  });
  au.on('error', () => {
    if (manual){ manual = false; box({ type: 'warning', buttons: ['Open download page', 'OK'], defaultId: 1, cancelId: 1, title: name,
      message: 'Couldn’t check for updates.', detail: 'You may be offline. The newest version is always on the download page.' })
      .then(r => { if (r.response === 0) shell.openExternal(page); }); }
  });

  const check = async (byHand) => { manual = !!byHand; try { await au.checkForUpdates(); return true; } catch (e) { return false; } };
  setTimeout(() => check(false), 8000);
  setInterval(() => check(false), 4 * 3600 * 1000);
  return { check };
}

module.exports = { setupUpdates };
