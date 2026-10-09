// ...Thunder Plus offline support. The app page is fetched fresh when online (so updates show up)
// and served from the cache when offline.
const CACHE = 'thunderplus-v6';
const FILES = ['./', './index.html', './manifest.webmanifest?v=2', './icons/icon-192.png', './icons/icon-512.png', './icons/icon-maskable-512.png', './icons/apple-touch-icon.png',
  './fonts/Jost-400.woff', './fonts/Jost-500.woff', './fonts/Jost-600.woff', './fonts/Jost-700.woff', './fonts/JetBrainsMono-400.woff2', './fonts/JetBrainsMono-700.woff2',
  './waves/index.html', './waves/waves-worklet.js?v=3', './waves/dsp/thunder-dsp.wasm', './waves/dsp/tfx.wasm', './waves/fonts/Jost-400.woff', './waves/fonts/Jost-500.woff', './waves/fonts/Jost-600.woff', './waves/fonts/Jost-700.woff'];
self.addEventListener('install', e => { e.waitUntil(caches.open(CACHE).then(c => c.addAll(FILES))); self.skipWaiting(); });
self.addEventListener('activate', e => { e.waitUntil(caches.keys().then(keys => Promise.all(keys.filter(k => k !== CACHE && k.startsWith('thunderplus-')).map(k => caches.delete(k))))); self.clients.claim(); });
self.addEventListener('fetch', e => {
  const req = e.request;
  if (req.method !== 'GET') return;
  const url = new URL(req.url);
  const isPage = req.mode === 'navigate' || url.pathname.endsWith('/index.html');
  if (isPage){
    // no-cache: always ask the server (a quick check when nothing changed), so a reload never shows an old copy.
    e.respondWith(fetch(req.url, {cache:'no-cache', credentials:'same-origin'}).then(res => { const copy = res.clone(); caches.open(CACHE).then(c => c.put('./index.html', copy)); return res; })
      .catch(() => caches.match('./index.html')));
    return;
  }
  // Everything else (icons, fonts): cache first, then the network.
  e.respondWith(caches.match(req).then(hit => hit || fetch(req).then(res => {
    if (res.ok && (url.origin === location.origin || /fonts\.(googleapis|gstatic)\.com/.test(url.host))){ const copy = res.clone(); caches.open(CACHE).then(c => c.put(req, copy)); }
    return res;
  })));
});
