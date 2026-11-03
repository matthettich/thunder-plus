// THUNDER CORE (plan phase 2): core/seq-core.js is generated from index.html by tools/make-core.mjs. If makeSeq or
// anything it reads changes, the bundle must be regenerated, or the audio-thread sequencer plays old code.
const { test } = require('node:test');
const assert = require('node:assert');
const { execFileSync } = require('node:child_process');
const path = require('node:path');
const fs = require('node:fs');
const ROOT = path.join(__dirname, '..');
const haveAcorn = [process.env.ACORN, '/opt/npm-tools/node_modules', path.join(ROOT, 'node_modules'), '/usr/lib/node_modules', '/usr/local/lib/node_modules'].some(d => d && fs.existsSync(path.join(d, 'acorn')));
test('core/seq-core.js is up to date with index.html', { skip: !haveAcorn && 'acorn not installed' }, () => {
  execFileSync(process.execPath, [path.join(ROOT, 'tools/make-core.mjs'), '--check'], { stdio: 'pipe' });
});
test('core/seq-core.js registers the thunder-core processor', () => {
  const src = fs.readFileSync(path.join(ROOT, 'core/seq-core.js'), 'utf8');
  assert.match(src, /registerProcessor\('thunder-core', ThunderCore\)/);
  assert.match(src, /function makeSeq\(S, mode, o\)/);
});
