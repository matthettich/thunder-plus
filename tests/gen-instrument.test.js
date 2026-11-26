// GENERATORS TAKE AN INSTRUMENT (build 629, owner: "Make each generator allow you to set the instrument number"). Arp, Chords, Polymeter, Markov melody and Probability fill share genInsField
// (genScope's G.ins, default the instrument they used before); Mutate has its own choice for the notes it adds; Acid line, Euclidean fill and Drum pattern already had one. Source pins.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict');
const src = require('./helpers').read('index.html');
const fn = n => { const i = src.indexOf('function ' + n + '('); assert.ok(i > 0, n); return src.slice(i, src.indexOf('\n}\n', i)); };
test('the shared field: first entry is "Current", which restores the old default; picking sets G.ins and G.pick', () => {
  const m = src.match(/const genInsField = [^\n]*/)[0]; assert.match(m, /'Current \('/); assert.match(m, /G\.ins = v >= 0 \? v : G\.ins0/); assert.match(src, /return \{y0, y1, ph, spr, ins, ins0:ins, pick:false,/);
});
test('Arp, Chords, Polymeter, Markov melody and Probability fill show it; they still write G.ins', () => {
  for (const f of ['arpGen', 'chordGen', 'polyGen', 'markovGen', 'probFill']) assert.match(fn(f), /genInsField\(G, field\)/, f);
  for (const f of ['arpGen', 'chordGen', 'polyGen', 'markovGen']) assert.match(fn(f), /r\[IC\] = G\.ins/, f);
  assert.match(fn('probFill'), /si = G\.pick \? G\.ins : src\.i/);
});
test('Mutate: an instrument for the notes it adds, default "as the note before it"', () => {
  const m = fn('mutatePhrase'); assert.match(m, /let mutIns = -1/); assert.match(m, /mutIns >= 0 \? mutIns : prev\.i/); assert.match(m, /Instrument for added notes/);
});
