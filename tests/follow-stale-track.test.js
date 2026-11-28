// Build 703 (owner, 27 Nov: "I cannot get the song mode to follow the playhead … an errant empty pattern on an unused track?").
// A track that played one stray block and then went quiet keeps its last position in tp.pos. FOLLOW and Arrange's paging must go
// by the tracks still playing, not that frozen row. followPlay + arrFollowPage are cut out of index.html and run in a vm.
const test = require('node:test'), assert = require('node:assert'), vm = require('node:vm'), H = require('./helpers');
const src = H.read('index.html');
const cut = (re, what) => { const m = src.match(re); assert.ok(m, 'could not find ' + what + ': the code moved, this test needs updating'); return m[0]; };
const code = [cut(/const handsOn = [^\n]*\n/, 'handsOn'), cut(/let navT = -1e9;\n/, 'navT'), cut(/const ARR_AHEADS = \[[\s\S]*?\nfunction arrPageTop\([^\n]*\)\{[\s\S]*?\n\}\n/, 'arrPageTop'),
  cut(/function arrFollowPage\(own\)\{[\s\S]*?\n\}\n/, 'arrFollowPage'), cut(/const posLive = [^\n]*\n/, 'posLive'),
  cut(/function followPlay\(\)\{[\s\S]*?\n\}\n\/\/ ARRANGE PAGES/, 'followPlay').replace(/\n\/\/ ARRANGE PAGES$/, '')].join('\n');
const clamp = (v, a, b) => Math.max(a, Math.min(b, v));
function rig(cursorTrack){
  let now = 1e6;
  const c = { performance: { now: () => now }, localStorage: { getItem: () => null, setItem(){} }, clamp, Math,
    tv: 'song', view: 'tracker', cTrack: cursorTrack, cPhrase: 0, cSect: 0, MT: 16, followMode: 2, followOn: true, apF: '', apSel: null, selA: null, sdrag: false, drag: false, apReady: false,
    cs: { song: { x: cursorTrack, y: 0, top: 0, pg: false }, phrase: { y: 0, top: 0, pg: false }, sect: { y: 0 } },
    tp: { on: true, mode: 'song', pos: new Array(17).fill(null), posAt: new Array(17).fill(-1e9), st: 0, line: 0.125, loopV: '' },
    fpHome: null, fpMem: null, workT: -1e9, arrHandT: -1e9, arrActT: -1e9, arrTouchT: -1e9, FOLLOW_HOLD: 4000, ARR_IDLE: 60000,
    S: { song: Array.from({ length: 256 }, (_, y) => { const r = new Array(17).fill(-1); if (y < 40) r[0] = 0; if (y === 0) r[5] = 1; return r; }), chains: [[[0, 0]], [[1, 0]]] },
    asongOn: () => true, apPanel: () => false, arrWrapRow: () => 255, pvFollow(){}, draw(){}, console, arrRows: () => 16, songRows: () => 16, songViewRows: () => 16, pvActive: () => false, plen: () => 32 };
  vm.createContext(c); vm.runInContext(code.replace(/^let /gm, 'var ') + '\nthis.followPlay = followPlay;', c);
  return { c, at: s => { now = 1e6 + s * 1000; c.tp.st = s; } };
}
test('a stray block played early on another track does not freeze FOLLOW: Arrange pages with track 1', () => {
  const { c, at } = rig(5);   // the cursor sits on the track with the stray block (row 0 only)
  at(0); c.tp.pos[5] = { row: 0, cr: 0, step: 0, sect: 1, phrase: 1 }; c.tp.posAt[5] = 0;
  for (let row = 0; row < 30; row++){ at(row * 4); c.tp.pos[0] = { row, cr: 0, step: 0, sect: 0, phrase: 0 }; c.tp.posAt[0] = row * 4; c.followPlay(); }
  assert.ok(c.cs.song.top > 0, 'Arrange turned its page with the playing track (top ' + c.cs.song.top + ')');
});
test('a stray block on a track BEFORE the playing ones does not freeze FOLLOW when the cursor track is empty', () => {
  const { c, at } = rig(9);   // cursor on an empty track; the first track with a position is the stale one
  c.S.song[0][0] = -1; c.S.song[0][2] = 1; for (let y = 0; y < 40; y++) c.S.song[y][6] = 0;
  at(0); c.tp.pos[2] = { row: 0, cr: 0, step: 0, sect: 1, phrase: 1 }; c.tp.posAt[2] = 0;
  for (let row = 0; row < 30; row++){ at(row * 4); c.tp.pos[6] = { row, cr: 0, step: 0, sect: 0, phrase: 0 }; c.tp.posAt[6] = row * 4; c.followPlay(); }
  assert.ok(c.cs.song.top > 0, 'Arrange turned its page (top ' + c.cs.song.top + ')');
});
