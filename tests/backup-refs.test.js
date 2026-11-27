// Backups (review P1-2): a backup holds sample ids, not base64 audio; smpTidy keeps the samples a backup (or the rescue copy) names;
// a backup that cannot be stored is logged and the user is told once. The code under test is cut out of index.html at run time.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');
const src = H.read('index.html');
function cut(re, what) { const m = src.match(re); assert.ok(m, `index.html: could not find ${what}; the code moved or changed shape, so this test needs updating`); return m[0]; }

const CODE = [
  cut(/function smpToJSON\(S\)\{[\s\S]*?\n\}\n/, 'smpToJSON'), cut(/function smpB64\(d\)\{[\s\S]*?\n\}\n/, 'smpB64'),
  cut(/function layerToJSON\([^\n]*\n/, 'layerToJSON'), cut(/function voiceToJSON\([^\n]*\n/, 'voiceToJSON'), cut(/function kitToJSON\(noSmp\)\{[^\n]*\n[^\n]*\n/, 'kitToJSON'),
  cut(/const projText = [^\n]*\n/, 'projText'), cut(/function smpTidy\(\)\{[\s\S]*?\n\}\n/, 'smpTidy'),
  cut(/const BK_N = 10, BK_MS = 180000;[^\n]*\n/, 'BK_N'), cut(/async function backupNow\(force\)\{[\s\S]*?\n\}\n/, 'backupNow'),
  cut(/async function kvSet\(k, v\)\{[^\n]*\n/, 'kvSet'),
].join('\n');

function world({ failSet = false } = {}) {
  const kv = new Map(), logs = [], toasts = [], smpDel = [], idbKeys = ['live', 'onlyBackup', 'inRescue', 'orphan'];
  const c = vm.createContext({ console, JSON, Math, Date, Set, Map, Error, String, Number, Array, Object, Promise, Int16Array, Uint8Array, Float32Array, btoa: s => Buffer.from(s, 'binary').toString('base64'),
    tpLog: (w, e) => logs.push(w + '|' + (e && e.message)), toastErr: m => toasts.push(m), BUILD: 1, kvDb: () => { throw new Error('no db'); },
    kit: { name: 'K', slots: 16, voices: [{ name: 'p', layers: [{ type: 'sine', sample: { id: 'live', sr: 48000, name: 'x', data: new Float32Array([0.5, -0.5, 0.25]) } }] }] },
    S: { name: 'Song', presets: { voices: [], layers: [] }, irs: [] }, SP: () => c.S.presets, songOut: s => ({ name: s.name }), fullPresets: () => ({ voices: [], layers: [] }), irsFull: () => [],
    sdbOk: true, smpStored: new Set(), smpHeld: new Map(), IRDATA: new Map(), smpId: () => 'newid', smbRecent: () => [], KEY: 'k', TK: 't', localStorage: { getItem: () => '' },
    flushS() {}, presetSmpFill: async () => {}, smpSaveGate: async () => true, smpPut: async s => { c.smpStored.add(s.id); }, bkDirty: false, rendering: false, otherTabs: async () => false,
    kvGet: async k => kv.get(k), kvDel: async k => { kv.delete(k); },
    smpDb: async () => ({ transaction: () => { const q = { }; const st = { getAllKeys: () => { setImmediate(() => { q.result = idbKeys.slice(); q.onsuccess(); }); return q; }, delete: k => smpDel.push(k) }; return { objectStore: () => st }; } }) });
  vm.runInContext(CODE.replace(/^async function kvSet[^\n]*\n/m, '') + '\nthis.kvSetReal = null;', c);
  c.kvSet = failSet ? async () => false : async (k, v) => { kv.set(k, v); return true; };
  vm.runInContext('this.backupNow = backupNow; this.smpTidy = smpTidy; this.getBad = () => bkBad;', c);
  return { c, kv, logs, toasts, smpDel };
}

test('backups: two backups hold sample ids and no base64 audio, and the project file still embeds the audio', async () => {
  const w = world(); w.c.bkDirty = true; await w.c.backupNow(false); await w.c.backupNow(true);
  const idx = w.kv.get('bkIndex'); assert.equal(idx.length, 2);
  for (const e of idx) { const t = w.kv.get(e.k); assert.ok(!/"b64/.test(t), 'no b64 key in a backup'); assert.match(t, /"ref":"live"/); assert.equal(JSON.stringify(e.refs), '["live"]'); }
  assert.ok(w.c.smpStored.has('live'), 'the audio is in the sample store before the backup names it');
  assert.match(vm.runInContext('projText()', w.c), /"b64"/, 'projText() with no argument (files, export) still carries the audio');
  assert.equal(w.logs.length, 0); assert.equal(w.toasts.length, 0);
});

test('smpTidy keeps a sample id that only a backup (or the rescue copy) references', async () => {
  const w = world(); w.kv.set('bkIndex', [{ k: 'bk1', refs: ['onlyBackup'] }]); w.kv.set('rescueIndex', [{ k: 'rescue1', refs: ['inRescue'] }, null]);
  w.c.smpTidy(); await new Promise(r => setTimeout(r, 50));
  assert.deepEqual(w.smpDel, ['orphan'], 'only the unreferenced sample is dropped: ' + w.smpDel);
});

test('a backup that cannot be stored is logged and the user is told once', async () => {
  const w = world({ failSet: true }); w.c.bkDirty = true; await w.c.backupNow(false); await w.c.backupNow(true);
  assert.ok(w.logs.some(l => /^backup failed\|/.test(l)), 'a tpLog entry: ' + w.logs);
  assert.equal(w.toasts.length, 1, 'one toast, not one per attempt'); assert.match(w.toasts[0], /export a project file/i);
  assert.equal(w.kv.has('bkIndex'), false); assert.equal(w.c.bkDirty, true, 'the next timer tick tries again');
});
