// CONTROL BAR LABELS (13 Nov, owner: "m.pat and lp.sel both get cutoff by their outlines… Cln down is tight, we could put the arrow below cln";
// "I don't think the command bar needs the 'commands' label"). M.PAT = MUTE / PAT, LP.SEL = LOOP / SEL, CLN↓ = CLN over ↓. Pure checks on the label tables and the CSS.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');
const src = H.read('index.html');
const cut = re => { const m = src.match(re); assert.ok(m, 'not found: ' + re); return m[0]; };
const tables = () => { const c = {}; vm.createContext(c);
  vm.runInContext(cut(/const CMD_PAD_SHORT = [^\n]*\n/) + cut(/const CMD_TWO = [^\n]*\n/) + cut(/const CMD_MINI_LAB = [^\n]*\n/) + cut(/const cmdLab = [^\n]*\n/) + 'this.T = {CMD_TWO, CMD_PAD_SHORT, CMD_MINI_LAB, cmdLab};', c); return c.T; };
const CMDS = () => { const m = cut(/const CMDS = \[\[[\s\S]*?\]\];/); const c = { MODK: 'Ctrl' }; vm.createContext(c); vm.runInContext(m.replace('const CMDS', 'this.C') , c); return c.C; };

test('MUTE / PAT, LOOP / SEL and CLN / ↓ are two-line labels with whole words, ids / titles unchanged', () => {
  const { CMD_TWO, cmdLab } = tables();
  assert.deepEqual([...CMD_TWO.mpat], ['MUTE', 'PAT']); assert.deepEqual([...CMD_TWO.lpsel], ['LOOP', 'SEL']); assert.deepEqual([...CMD_TWO.cldn], ['CLN', '↓']);
  assert.match(cmdLab('mpat', 'x'), /<i>MUTE<\/i><i>PAT<\/i>/); assert.match(cmdLab('cldn', 'x'), /<i>CLN<\/i><i>↓<\/i>/);
  const C = CMDS(), by = Object.fromEntries(C.map(c => [c[0], c])); for (const id of ['mpat', 'lpsel', 'cldn']) assert.ok(by[id] && by[id][2].length > 20, id + ' keeps its full title');
});
test('no visible label reads M.PAT or LP.SEL; the user-facing messages and menu entries use the words', () => {
  const { CMD_PAD_SHORT, CMD_MINI_LAB } = tables(); const C = CMDS();
  for (const t of [...C.map(c => c[1]), ...Object.values(CMD_PAD_SHORT), ...Object.values(CMD_MINI_LAB)]) assert.doesNotMatch(t, /M\.PAT|LP\.SEL/);
  assert.match(src, /toast\('MUTE PAT mutes blocks on Arrange\.'\)/); assert.match(src, /toast\('LOOP SEL loops the selected rows on Arrange\.'\)/); assert.match(src, /it\('lpsel', 'LOOP SEL: loop the selected rows/);
});
test('character budget: a big-bar label is at most 6 characters on a line (two-line labels 4), and the CSS shrinks any label to its button', () => {
  const { CMD_TWO, CMD_PAD_SHORT } = tables(), C = CMDS();
  for (const [id, t] of C.map(c => [c[0], c[1]])){ const lines = CMD_TWO[id] ? [...CMD_TWO[id]] : [CMD_PAD_SHORT[id] || t]; for (const l of lines) assert.ok([...l].length <= (CMD_TWO[id] ? 4 : 6), id + ' ' + l); }
  assert.match(src, /\.tcmd:not\(\.cmini\) button\{container-type:inline-size\}/); assert.match(src, /font-size:min\(1em, calc\(96cqw \/ \(\.74 \* var\(--n, 4\)\)\)\)/);
  assert.match(src, /\.tcmd:not\(\.cmini\) button \.cl\.two i\{display:block/);
});
test('the COMMANDS heading is gone; the bar is still named for screen readers; nothing was listening on the heading', () => {
  assert.doesNotMatch(src, /\.tcmd::before/); assert.doesNotMatch(src, /content:'COMMANDS'/);
  assert.match(src, /<div class="tcmd" id="tCmd" role="toolbar" aria-label="Commands" hidden><\/div>/);
  assert.doesNotMatch(cut(/\.tcmd\{background:color-mix[^\n]*/), /padding-top:18px/);
});
