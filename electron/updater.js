// Updates from GitHub Releases (electron-updater). A build checks a few seconds after it opens and every
// few hours after that; nothing happens when it's offline or when it isn't an installed build.
//  - Windows (installer) and Linux (AppImage): the new version downloads in the background, then a box
//    offers Restart now / Later. Later installs it the next time the app quits.
//  - Mac and the Windows portable .exe can't replace themselves while unsigned, so they say a new
//    version is out and, on Download, fetch the new .dmg / portable .exe straight into Downloads (with progress on the
//    page's Update button and the Dock / taskbar icon), then open it: the Mac's installer window (drag it to
//    Applications), or the portable .exe shown in its folder. If that fails, the download page opens instead.
// The page hears about it too (preload.js → window.thunderDesktop): an "Update" button in its top bar stays until it is
// used, and File ▾ has "Check for updates…". If electron-updater can't check (an unsigned Mac build, say), the newest
// release is read from GitHub's API instead, so a new build is still announced.
const { app, dialog, shell, BrowserWindow, net } = require('electron');
const fs = require('fs'), path = require('path'), crypto = require('crypto');

let au = null;
try { au = require('electron-updater').autoUpdater; } catch (e) { au = null; }

// '1.2.10' > '1.2.9'
function newer(a, b){
  const A = String(a).split('.').map(n => parseInt(n, 10) || 0), B = String(b).split('.').map(n => parseInt(n, 10) || 0);
  for (let i = 0; i < Math.max(A.length, B.length); i++){ const d = (A[i] || 0) - (B[i] || 0); if (d) return d > 0; }
  return false;
}

function setupUpdates({ name, page, repo }){
  const state = { version: app.getVersion(), avail: null, ready: false, notifyOnly: true, dl: null, file: null };   // dl: 0–1 while downloading, 1 done
  const win = () => BrowserWindow.getFocusedWindow() || BrowserWindow.getAllWindows()[0] || null;
  const box = o => { const w = win(); return w ? dialog.showMessageBox(w, o) : dialog.showMessageBox(o); };
  const send = () => { for (const w of BrowserWindow.getAllWindows()) try { w.webContents.send('thunder:upd', { ...state }); } catch (e) {} };
  const told = new Set();
  const GH = { 'Accept': 'application/vnd.github+json', 'User-Agent': 'thunder-plus-desktop' };
  const bar = f => { for (const w of BrowserWindow.getAllWindows()) try { w.setProgressBar(f); } catch (e) {} };

  /* DIRECT DOWNLOAD (owner, 19 Oct): Mac and the portable .exe fetch the new build themselves. The file is the release's
     .dmg (Mac) or -portable.exe, found through GitHub's API; it's written to Downloads as name.part and renamed when
     complete, so a half-finished file never looks like the real one. */
  const dl = { busy: false, file: null, version: null };
  const wantAsset = n => process.platform === 'darwin' ? /\.dmg$/i.test(n) : /-portable\.exe$/i.test(n);
  const assetOf = async version => {
    const r = await net.fetch('https://api.github.com/repos/' + repo + '/releases/tags/v' + version, { headers: GH });
    if (!r.ok) return null;
    const a = ((await r.json()).assets || []).find(x => x && wantAsset(String(x.name)));
    const m = a && /^sha256:([0-9a-f]{64})$/i.exec(String(a.digest || ''));   // GitHub's own SHA-256 of the asset (absent on old releases)
    return a ? { url: a.browser_download_url, name: path.basename(String(a.name)), size: a.size || 0, sha256: m ? m[1].toLowerCase() : null } : null;
  };
  // The downloaded file: the Mac mounts the .dmg and shows its window (drag the app to Applications, after quitting
  // this one, since the Finder won't replace an app that's open); the portable .exe is shown in its folder.
  const openIt = () => {
    const mac = process.platform === 'darwin';
    if (mac) shell.openPath(dl.file); else shell.showItemInFolder(dl.file);
    box({ type: 'info', buttons: ['Quit ' + name, 'Later'], defaultId: 0, cancelId: 1, title: name + ' update',
      message: name + ' ' + dl.version + ' is downloaded.',
      detail: mac ? 'Its installer window is open. Quit this copy, then drag ' + name + ' into Applications and choose Replace. Your songs and kit stay.'
        : 'It’s in your Downloads folder (shown now). Quit this copy and open the new one. Your songs and kit stay.' })
      .then(r => { if (r.response === 0) setImmediate(() => app.quit()); });
  };
  const download = async version => {
    if (dl.busy){ box({ type: 'info', buttons: ['OK'], title: name + ' update', message: 'Downloading ' + name + ' ' + version + '… ' + Math.round((state.dl || 0) * 100) + '%' }); return; }
    if (dl.file && dl.version === version && fs.existsSync(dl.file)){ openIt(); return; }
    dl.busy = true; state.dl = 0; send(); bar(0.01);
    let part = null;
    try {
      const a = repo && await assetOf(version); if (!a) throw new Error('no file');
      const dir = app.getPath('downloads'), file = path.join(dir, a.name); part = file + '.part';
      const r = await net.fetch(a.url); if (!r.ok || !r.body) throw new Error('HTTP ' + r.status);
      const total = +r.headers.get('content-length') || a.size || 0, out = fs.createWriteStream(part), rd = r.body.getReader();
      const hash = crypto.createHash('sha256');   // hashed as it arrives: no second pass over a 130–250 MB file
      let got = 0, shown = 0, werr = null; out.on('error', e => { werr = e; });   // a disk error must not take the app down
      try {
        for (;;){
          const { done, value } = await rd.read(); if (done) break;
          if (werr) throw werr;
          got += value.length; hash.update(value);
          if (!out.write(Buffer.from(value))) await new Promise(res => { const go = () => { out.off('drain', go); out.off('error', go); res(); }; out.on('drain', go); out.on('error', go); });
          if (total){ const f = Math.min(.99, got / total); if (f - shown >= .01){ shown = f; state.dl = f; send(); bar(f); } }
        }
      } finally { await new Promise(res => { if (werr) return res(); out.once('error', res); out.end(res); }); }
      if (werr) throw werr;
      if (total && got < total) throw new Error('cut short');
      // The file must be the one GitHub lists. A mismatch is a corrupt or swapped download: it is deleted and never opened.
      // (No digest on the release = an older release: carry on as before.)
      if (a.sha256 && hash.digest('hex') !== a.sha256) throw new Error('checksum mismatch');
      try { fs.rmSync(file, { force: true }); } catch (e) {}
      fs.renameSync(part, file); part = null;
      dl.file = file; dl.version = version; state.dl = 1; state.file = a.name; send(); bar(-1);
      openIt();
    } catch (e) {
      if (part) try { fs.rmSync(part, { force: true }); } catch (e2) {}
      state.dl = null; send(); bar(-1);
      box({ type: 'warning', buttons: ['Open download page', 'OK'], defaultId: 0, cancelId: 1, title: name + ' update',
        message: 'Couldn’t download ' + name + ' ' + version + '.', detail: 'You may be offline. The download page has it too.' })
        .then(r => { if (r.response === 0) shell.openExternal(page); });
    } finally { dl.busy = false; }
  };

  // A newer version is out and can't install itself here: a box (once per version unless asked) and the page's button.
  const announce = (version, byHand) => {
    state.avail = version; send();
    if (told.has(version) && !byHand) return;
    told.add(version);
    box({ type: 'info', buttons: ['Download', 'Later'], defaultId: 0, cancelId: 1, title: name + ' update',
      message: name + ' ' + version + ' is out.', detail: 'You have ' + app.getVersion() + '. Download the new version and install it over this one.' })
      .then(r => { if (r.response === 0) download(version); });
  };

  // The fallback: GitHub's newest release, when electron-updater can't check.
  const ghCheck = async (byHand) => {
    if (!repo) return false;
    try {
      const r = await net.fetch('https://api.github.com/repos/' + repo + '/releases/latest', { headers: GH });
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
    if (state.avail){ download(state.avail); return true; }   // Mac / portable: fetch it straight into Downloads
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
