// ...Thunder Plus offline support. The app page is fetched fresh when online (so updates show up)
// and served from the cache when offline.
const CACHE = 'thunderplus-v250';
const FILES = ['./', './index.html', './manifest.webmanifest?v=8', './master-worklet.js?v=4', './pump/pump-meta.js?v=1', './pump/pump.wasm?v=1', './pump/ui/kit.js?v=3', './pump/ui/eq.js?v=1', './pump/ui/comp.js?v=1', './pump/ui/limit.js?v=1', './pump/ui/meter.js?v=1', './pump/ui/multiband.js?v=1', './pump/ui/calm.js?v=1', './pump/ui/tone.js?v=1', './pump/ui/band.js?v=1', './pump/ui/wide.js?v=1', './pump/ui/heat.js?v=1', './icons/plus-icon-192.png?v=2', './icons/plus-icon-512.png?v=2', './icons/plus-icon-maskable-512.png?v=2', './icons/plus-apple-touch-icon.png?v=2',
  './fonts/Jost-400.woff', './fonts/Jost-500.woff', './fonts/Jost-600.woff', './fonts/Jost-700.woff', './fonts/JetBrainsMono-400.woff2', './fonts/JetBrainsMono-700.woff2', './fonts/ShareTechMono-400.woff',
  './waves/index.html', './waves/waves-worklet.js?v=31', './waves/dsp/thunder-dsp.wasm', './waves/dsp/tfx.wasm', './waves/fonts/Jost-400.woff', './waves/fonts/Jost-500.woff', './waves/fonts/Jost-600.woff', './waves/fonts/Jost-700.woff',
  './chains/index.html', './chains/chains-worklet.js?v=38', './chains/dsp/tfx.wasm', './chains/fonts/Jost-400.woff', './chains/fonts/Jost-500.woff', './chains/fonts/Jost-600.woff', './chains/fonts/Jost-700.woff'];
// LXR ENGINES: lxr/ is optional (GPL code that can be left out), so its files are cached when they are there and a
// missing folder never stops the rest from installing.
const OPTIONAL = ['./lxr/lxr-engine.js?v=2', './lxr/lxr.wasm?v=1'];
// cache:'reload': a new cache fills from the server, not from the HTTP cache (GitHub Pages serves 10-minute max-age), so a
// rebuilt wasm or worklet can't be precached stale next to a new page.
self.addEventListener('install', e => { e.waitUntil(caches.open(CACHE).then(c => c.addAll(FILES.map(f => new Request(f, {cache:'reload'})))
  .then(() => Promise.all(OPTIONAL.map(f => c.add(new Request(f, {cache:'reload'})).catch(() => {})))))); self.skipWaiting(); });
self.addEventListener('activate', e => { e.waitUntil(caches.keys().then(keys => Promise.all(keys.filter(k => k !== CACHE && k.startsWith('thunderplus-')).map(k => caches.delete(k))))); self.clients.claim(); });
self.addEventListener('fetch', e => {
  const req = e.request;
  const url = new URL(req.url);
  // SHARE TARGET: another app (Gmail, Files) shares a project with this app. Leave the file where the page
  // can pick it up, then send the app back to itself. The cache name does NOT start with "thunderplus-",
  // so the activate clean-up below never deletes a project that is waiting to be opened.
  if (req.method === 'POST' && /\/share-target\/?$/.test(url.pathname)){
    e.respondWith((async () => {
      let got = false;
      try {
        const form = await req.formData();
        const f = form.getAll('project').find(x => x && typeof x !== 'string' && x.size > 0);
        if (f){
          const c = await caches.open('thunder-share');
          await c.put(new URL('__thunder-share-project', self.registration.scope).href,
            new Response(f, {headers:{'content-type': f.type || 'application/octet-stream', 'x-name': encodeURIComponent(f.name)}}));
          got = true;
        }
      } catch(err){}
      return Response.redirect(new URL(got ? './?shared=1' : './?shared=0', self.registration.scope).href, 303);
    })());
    return;
  }
  if (req.method !== 'GET') return;
  const isPage = req.mode === 'navigate' || url.pathname.endsWith('/index.html');
  if (isPage){
    // no-cache: always ask the server (a quick check when nothing changed), so a reload never shows an old copy.
    e.respondWith(fetch(req.url, {cache:'no-cache', credentials:'same-origin'}).then(res => { if (res.ok){ const copy = res.clone(); caches.open(CACHE).then(c => c.put(req, copy)); } return res; })
      .catch(() => caches.match(req)));
    return;
  }
  // Everything else (icons, fonts): cache first, then the network.
  e.respondWith(caches.match(req).then(hit => hit || fetch(req).then(res => {
    // (SoundFonts in sounds/ are kept in IndexedDB by the app, so they aren't copied into this cache as well.)
    if (res.ok && !url.pathname.includes('/sounds/') && (url.origin === location.origin || /fonts\.(googleapis|gstatic)\.com/.test(url.host))){ const copy = res.clone(); caches.open(CACHE).then(c => c.put(req, copy)); }
    return res;
  })));
});
