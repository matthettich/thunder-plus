// 10 Nov retest fixes: a pad press holds Arrange's paging, a pad key never fires twice, the view switch keeps its words.
const test = require('node:test'), assert = require('node:assert'), fs = require('fs'), path = require('path');
const src = fs.readFileSync(path.join(__dirname, '..', 'index.html'), 'utf8');

test('a pad press sets arrActT (holds Arrange paging) as well as workT', () => {
  assert.match(src, /grid\.contains\(e\.target\)\)\) workT = performance\.now\(\); if \(q && e\.target\.closest\('#tPad'\)\) arrActT = workT;/);
  // the keyboard rule is unchanged and the grid / Arrange presses still do not set arrActT
  assert.match(src, /arrActT = workT = performance\.now\(\); \}, true\);/);
});
