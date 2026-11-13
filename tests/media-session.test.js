// MS_ACTIVE (10 Nov): the BACKGROUND PLAY block is cut out of index.html and run against a fake <audio>, Media Session and clock.
// Checks: the clip is a real (non-zero, ~-60 dBFS, 8 s) 40 Hz sine; the session gets playing + metadata (song name, icons) on PLAY and paused on STOP;
// every start / stop / system pause is written to the playback report as "media session: ..."; kill switch false = old clip, no new lines.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');
const src = H.read('index.html');
const m = src.match(/var bgOn = [\s\S]*?\/\* ===== END BACKGROUND PLAY/); assert.ok(m, 'could not find the BACKGROUND PLAY block; this test needs updating');
assert.match(m[0], /const MS_ACTIVE = true,/, 'MS_ACTIVE kill switch present and on');

let proj = { label: '' };
function rig({ active = true } = {}) {
  proj = { label: '' };
  const log = [], blobs = [], els = [], ms = { handlers: {}, metadata: null, playbackState: 'none', setActionHandler(k, f) { this.handlers[k] = f; } };
  class FakeAudio {
    constructor() { this.paused = true; this.muted = false; this.volume = 1; this.duration = 8; this.L = {}; els.push(this); }
    addEventListener(t, f) { (this.L[t] = this.L[t] || []).push(f); }
    play() { this.paused = false; (this.L.playing || []).forEach(f => f()); return Promise.resolve(); }
    pause() { this.paused = true; (this.L.pause || []).forEach(f => f()); }
  }
  const tp = { on: true };
  const code = active ? m[0] : m[0].replace('const MS_ACTIVE = true', 'const MS_ACTIVE = false');
  const ctx = vm.createContext({ Math, Promise, console, performance: { now: () => 5000 }, Audio: FakeAudio, URL: Object.assign(function (p, base) { this.href = new globalThis.URL(p, base).href; }, { createObjectURL: b => (blobs.push(b), 'blob:x') }),
    Blob: function (parts) { this.parts = parts; }, ArrayBuffer, DataView, localStorage: { getItem: () => '1', setItem() {} },
    navigator: { userAgent: 'Android', mediaSession: ms }, MediaMetadata: function (o) { Object.assign(this, o); }, S: { name: 'My Song' }, tp, location: { href: 'https://x.example/beta/index.html' }, projLabel: () => proj.label,
    tStop: () => { tp.on = false; ctx.bgPlay(false); }, playCtx() {}, pbLog: s => log.push(s), pbState: () => 'state', setTimeout: () => 0,
    document: { hidden: false, addEventListener() {} } });
  vm.runInContext(code.replace(/\/\* ===== END BACKGROUND PLAY$/, ''), ctx);
  return { ctx, ms, log, blobs, els, tp, flush: () => new Promise(r => setImmediate(r)) };
}
const msLines = R => R.log.filter(s => /^media session: /.test(s));

test('the clip: 8 s, 40 Hz sine at about -60 dBFS, not all zeros, loops on a whole cycle', () => {
  const R = rig(); R.ctx.bgPlay(true);
  const d = R.blobs[0].parts[0], v = new DataView(d), n = (d.byteLength - 44) / 2;
  assert.equal(n, 8000 * 8); let peak = 0, nz = 0;
  for (let i = 0; i < n; i++) { const x = v.getInt16(44 + i * 2, true); peak = Math.max(peak, Math.abs(x)); if (x) nz++; }
  const db = 20 * Math.log10(peak / 32768);
  assert.ok(db > -64 && db < -56, 'peak ' + db.toFixed(1) + ' dBFS'); assert.ok(nz > n / 2, 'mostly non-zero');
  assert.equal(v.getInt16(44, true), 0); assert.equal(8000 * 8 * 40 % 8000, 0);
});

test('PLAY: session playing + metadata (song name, icons) + element start logged; STOP: paused + element stop logged', async () => {
  const R = rig(); R.ctx.bgPlay(true); await R.flush();
  assert.equal(R.ms.playbackState, 'playing'); assert.equal(R.ms.metadata.title, 'My Song');
  assert.ok(R.ms.metadata.artwork.some(a => /plus-icon-512/.test(a.src)));
  const t = msLines(R).join('\n');
  assert.match(t, /session set: playing, title "My Song"/); assert.match(t, /element playing/); assert.match(t, /element play\(\) ok · paused=false muted=false vol=1 dur=8s session=playing/);
  R.log.length = 0; R.tp.on = false; R.ctx.bgPlay(false); await R.flush();
  assert.equal(R.ms.playbackState, 'paused'); const u = msLines(R).join('\n');
  assert.match(u, /session set: paused/); assert.match(u, /element pause/);
});

test('a refused play() is logged with the reason', async () => {
  const R = rig(); R.ctx.bgPlay(true); await R.flush(); R.els[0].play = () => Promise.reject({ name: 'NotAllowedError' }); R.log.length = 0; R.ctx.bgPlay(true); await R.flush();
  assert.match(msLines(R).join('\n'), /refused: NotAllowedError/);
});

test('kill switch false: old +-1 bit clip and no "media session:" lines', async () => {
  const R = rig({ active: false }); R.ctx.bgPlay(true); await R.flush();
  const v = new DataView(R.blobs[0].parts[0]); let peak = 0; for (let i = 0; i < 64000; i++) peak = Math.max(peak, Math.abs(v.getInt16(44 + i * 2, true)));
  assert.equal(peak, 1); assert.equal(msLines(R).length, 0); assert.equal(R.ms.playbackState, 'playing');
});

test('notification title (12 Nov): the project file name, else the song name; set again on a rename / save as / open while playing, not on every call; artwork is absolute, 192 + 512 PNG', async () => {
  const R = rig(); R.ctx.bgPlay(true); await R.flush();
  assert.equal(R.ms.metadata.title, 'My Song', 'no file: the song name'); assert.equal(R.ms.metadata.artist, 'Thunder Plus');
  assert.equal(JSON.stringify(R.ms.metadata.artwork.map(a => [a.sizes, a.type, /^https:\/\/x\.example\/beta\/icons\/plus-icon-(192|512)\.png/.test(a.src)])), JSON.stringify([['192x192', 'image/png', true], ['512x512', 'image/png', true]]));
  proj.label = 'Night Drive.thunder'; R.log.length = 0; R.ctx.msMeta(); assert.equal(R.ms.metadata.title, 'Night Drive', 'opened / saved as a file: its name, without .thunder');
  assert.match(msLines(R).join('\n'), /title now "Night Drive"/);
  const o = R.ms.metadata; R.log.length = 0; R.ctx.msMeta(); assert.equal(R.ms.metadata, o, 'unchanged: no new metadata'); assert.equal(msLines(R).length, 0);
  proj.label = ''; R.ctx.S.name = 'Renamed'; R.ctx.msMeta(); assert.equal(R.ms.metadata.title, 'Renamed', 'a new song / rename: the song name');
  R.tp.on = false; R.ctx.S.name = 'Later'; R.ctx.msMeta(); assert.equal(R.ms.metadata.title, 'Renamed', 'not playing: nothing is touched');
});
