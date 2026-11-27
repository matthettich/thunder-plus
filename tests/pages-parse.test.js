// Every page's own script must parse. Build 619 to 638 shipped a raw U+2028 / U+2029 inside a regular expression literal in
// chains/index.html and waves/index.html: those two characters end a line inside a regex literal, so the whole script failed with
// "SyntaxError: Invalid regular expression: missing /", the editor never started and its canvas stayed empty (owner, 17 Nov).
'use strict';
const test = require('node:test'), assert = require('node:assert/strict');
const fs = require('fs'), path = require('path'), vm = require('vm');
const root = path.join(__dirname, '..');
for (const page of ['index.html', 'chains/index.html', 'waves/index.html']) {
  test(page + ': every inline script parses', () => {
    const html = fs.readFileSync(path.join(root, page), 'utf8');
    const re = /<script(?![^>]*\bsrc=)([^>]*)>([\s\S]*?)<\/script>/g; let m, n = 0;
    while ((m = re.exec(html))) {
      const type = (/type\s*=\s*["']?([\w/+-]+)/.exec(m[1]) || [, 'text/javascript'])[1];
      if (type !== 'text/javascript' && type !== 'module') continue;   // data blocks (json, templates)
      n++;
      // a module script is parsed as the body of an async function (same grammar, top-level await included)
      const src = type === 'module' ? '(async function(){' + m[2] + '\n})' : m[2];
      assert.doesNotThrow(() => new vm.Script(src, { filename: page + ' script ' + n }), page + ' script ' + n + ' has a syntax error');
    }
    assert.ok(n > 0);
  });
  test(page + ': no raw U+2028 / U+2029 in a script (write \\u2028 / \\u2029)', () => {
    assert.ok(!/[\u2028\u2029]/.test(fs.readFileSync(path.join(root, page), 'utf8')));
  });
}
