// Updates from GitHub Releases (electron-updater). A build checks a few seconds after it opens and every
// few hours after that; nothing happens when it's offline or when it isn't an installed build.
//  - Windows (installer) and Linux (AppImage): the new version downloads in the background, then a box
//    offers Restart now / Later. Later installs it the next time the app quits.
//  - Mac and the Windows portable .exe can't replace themselves while unsigned, so they say a new
//    version is out and open the download page.
// The page hears about it too (preload.js → window.thunderDesktop): an "Update" button in its top bar stays until it is
// used, and File ▾ has "Check for updates…". If electron-updater can't check (an unsigned Mac build, say), the newest
// release is read from GitHub's API instead, so a new build is still announced.
const { app, dialog, shell, BrowserWindow, net } = require('electron');

let au = null;
try { au = require('electron-updater').autoUpdater; } catch (e) { au = null; }

// '1.2.10' > '1.2.9'
function newer(a, b){
  const A = String(a).split('.').map(n => parseInt(n, 10) || 0), B = String(b).split('.').map(n => parseInt(n, 10) || 0);
  for (let i = 0; i < Math.max(A.length, B.length); i++){ const d = (A[i] || 0) - (B[i] || 0); if (d) return d > 0; }
  return false;
}

function setupUpdates({ name, page, repo }){
  const state = { version: app.getVersion(), avail: null, ready: false, notifyOnly: true };
  const win = () => BrowserWindow.getFocusedWindow() || BrowserWindow.getAllWindows()[0] || null;
  const box = o => { const w = win(); return w ? dialog.showMessageBox(w, o) : dialog.showMessageBox(o); };
  const send = () => { for (const w of BrowserWindow.getAllWindows()) try { w.webContents.send('thunder:upd', { ...state }); } catch (e) {} };
  const told = new Set();

  // A newer version is out and can't install itself here: a box (once per version unless asked) and the page's button.
  const announce = (version, byHand) => {
    state.avail = version; send();
    if (told.has(version) && !byHand) return;
    told.add(version);
    box({ type: 'info', buttons: ['Download', 'Later'], defaultId: 0, cancelId: 1, title: name + ' update',
      message: name + ' ' + version + ' is out.', detail: 'You have ' + app.getVersion() + '. Download the new version and install it over this one.' })
      .then(r => { if (r.response === 0) shell.openExternal(page); });
  };

  // The fallback: GitHub's newest release, when electron-updater can't check.
  const ghCheck = async (byHand) => {
    if (!repo) return false;
    try {
      const r = await net.fetch('https://api.github.com/repos/' + repo + '/releases/latest', { headers: { 'Accept': 'application/vnd.github+json', 'User-Agent': 'thunder-plus-desktop' } });
      if (!r.ok) return false;
      const v = String((await r.json()).tag_name || '').replace(/^v/, '');
      if (v && newer(v, app.getVersion())){ announce(v, byHand); return true; }
      if (byHand) box({ type: 'info', buttons: ['OK'], title: name, message: 'You have the newest ' + name + ' (' + app.getVersion() + ').' });
      return true;
    } catch (e) { return false; }
  };
  const failBox = () => box({ type: 'warning', buttons: ['Open download page', 'OK'], defaultId: 1, cancelId: 1, title: name,
    message: 'Couldn’t check for updates.', detail: 'You may be offline. The newest version is always on the download page.' })
    .then(r => { if (r.response === 0) shell.openExternal(page); });

  const act = async () => {
    if (state.ready && au){ setImmediate(() => au.quitAndInstall()); return true; }
    if (state.avail && !state.notifyOnly){ box({ type: 'info', buttons: ['OK'], title: name + ' update', message: 'Downloading ' + name + ' ' + state.avail + '…', detail: 'You’ll be asked to restart when it’s ready.' }); return true; }
    shell.openExternal(page); return true;
  };

  if (!au || !app.isPackaged){
    // A development run (or no updater): only the GitHub check, by hand.
    return { check: async () => { if (!(await ghCheck(true))) failBox(); return true; }, state: () => ({ ...state }), act };
  }

  const notifyOnly = process.platform === 'darwin' || !!process.env.PORTABLE_EXECUTABLE_DIR;
  state.notifyOnly = notifyOnly;
  au.autoDownload = !notifyOnly;
  au.autoInstallOnAppQuit = true;
  au.logger = null;
  let manual = false;

  au.on('update-available', info => {
    const byHand = manual; manual = false;
    if (!notifyOnly){ state.avail = info.version; send(); if (byHand) box({ type: 'info', buttons: ['OK'], title: name + ' update', message: 'Downloading ' + name + ' ' + info.version + '…', detail: 'You’ll be asked to restart when it’s ready.' }); return; }
    announce(info.version, byHand);
  });
  au.on('update-downloaded', info => {
    state.avail = info.version; state.ready = true; send();
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
    // electron-updater couldn't check (offline, or a Mac build it can't handle): ask GitHub directly.
    const byHand = manual; manual = false;
    ghCheck(byHand).then(ok => { if (!ok && byHand) failBox(); });
  });

  const check = async (byHand) => { manual = !!byHand; try { await au.checkForUpdates(); return true; } catch (e) { return false; } };
  setTimeout(() => check(false), 8000);
  setInterval(() => check(false), 4 * 3600 * 1000);
  return { check: () => check(true), state: () => ({ ...state }), act };
}

module.exports = { setupUpdates, newer };
