// Play buttons and keys (9 Nov): the pad's PLAY and the Space bar share one rule, and the control bar's Play stays reachable on a phone.
const test = require('node:test'), assert = require('node:assert'), fs = require('fs'), path = require('path');
const src = fs.readFileSync(path.join(__dirname, '..', 'index.html'), 'utf8');

test('pad PLAY and Space both go through playKey', () => {
  assert.match(src, /if \(mod\('shift'\)\)\{ used\.shift = true; takeLatch\(\); playKey\(true\); \} else playKey\(false\); return; \}/);
  assert.match(src, /if \(k === ' '\)\{ if \(!e\.repeat\) playKey\(e\.shiftKey\); return true; \}/);
});
test('the top bar Play plays the whole song on every screen (12 Nov)', () => {
  assert.match(src, /\$\('tSong'\)\.addEventListener\('click', \(\) => \{ apRun\(\(\) => \{ if \(tp\.on\) tStopUser\(\); else tStart\('song', \{row:songRow\(\)\}\); \}, \[\]\); songBtn\(\); \}\);/);   // (12 Nov, owner: the top bar's Play plays the whole song)
});
test('the control bar Play is not hidden on a folded phone', () => {
  assert.doesNotMatch(src, /body\.mob \.top #tSong\{display:none\}/);
});
test('PAD mode on a small screen drops only the COMMANDS row play button', () => {
  assert.match(src, /@media \(max-width:699px\)\{ \.tcmd\.cpad:not\(\.cmini\) button\[data-c=song\]\{display:none\} \}/);
});
