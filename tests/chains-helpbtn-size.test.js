// The ? button on a module's title bar is exactly the size of the x button (15 Nov): expanded, collapsed, and on touch screens.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict');
const H = require('./helpers');
for (const ed of ['chains', 'waves']){
  const src = H.read(ed + '/index.html');
  const size = sel => { const m = src.match(new RegExp('\\n\\s*' + sel.replace(/[.*+?^${}()|[\]\\]/g, '\\$&') + '\\{[^}]*?width:(\\d+)px;\\s*height:(\\d+)px')); assert.ok(m, ed + ': rule ' + sel); return m[1] + 'x' + m[2]; };
  test(ed + ': ? and x are the same size, expanded, collapsed and on touch', () => {
    assert.equal(size('.node > header button.mq'), size('.node > header .x'));
    assert.equal(size('.node.collapsed > header button.mq'), size('.node.collapsed > header .x'));
    const touch = src.match(/@media \(pointer: coarse\)\{\n[\s\S]*?\n\}\n/g).find(b => /button\.mq/.test(b)); assert.ok(touch);
    const t = re => { const m = touch.match(re); assert.ok(m, String(re)); return m[1] + 'x' + m[2]; };
    assert.equal(t(/header button\.mq, \.node\.collapsed > header button\.mq\{width:(\d+)px; height:(\d+)px/), t(/header button\.x, \.node\.collapsed > header button\.x\{width:(\d+)px; height:(\d+)px/));
  });
}
