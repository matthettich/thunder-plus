// io/test/input.js: openInput / read / closeInput on miniaudio's null backend (no hardware needed; CI and this workspace).
//   node io/test/input.js        exits 1 on a failure
// Checks: it opens at the rate asked, reads arrive as contiguous runs (frame0 follows on), hostNs follows the frames at the
// stream's rate (within a few ms of the OS clock), close stops it, reopen works, and a bad id is refused, not a crash.
'use strict';
const path = require('path');
const I = require(path.join(__dirname, '..', '..', 'electron', 'io', 'thunder_io.node'));
const fail = m => { console.error('FAIL ' + m); try { I.closeInput(); } catch (e) {} process.exit(1); };
const sleep = ms => new Promise(r => setTimeout(r, ms));
(async () => {
  const o = I.openInput('', { rate: 48000, block: 256, backend: 'null' });
  if (!o.ok) fail('open: ' + o.why);
  console.log('opened', JSON.stringify(o));
  if (o.rate !== 48000 || !(o.channels >= 1)) fail('rate / channels');
  let next = -1, frames = 0, chunks = 0, worst = 0;
  const t0 = Date.now();
  while (Date.now() - t0 < 1500) {
    await sleep(5);
    for (let c; (c = I.read()); ) {
      if (next >= 0 && c.frame0 !== next) fail(`gap or overlap: frame0 ${c.frame0}, expected ${next}`);
      if (c.data.length !== c.frames * c.channels) fail('data length');
      // the run's last frame reached the computer before now, and not long before (the null device runs in real time)
      const lag = (I.now() - (c.hostNs + c.frames / c.rate * 1e9)) / 1e6; worst = Math.max(worst, lag);
      if (lag < -1) fail(`hostNs in the future by ${-lag} ms`);
      next = c.frame0 + c.frames; frames += c.frames; chunks++;
    }
  }
  I.closeInput(); await sleep(50);
  if (I.read() !== null) fail('read after close');
  const secs = frames / 48000;
  console.log(`${chunks} reads, ${frames} frames (${secs.toFixed(2)} s in ~1.5 s), worst lag ${worst.toFixed(1)} ms`);
  if (secs < 1.2 || secs > 1.6) fail('frame count is not real time');
  const again = I.openInput('', { rate: 44100, backend: 'null' });
  if (!again.ok || again.rate !== 44100) fail('reopen');
  I.closeInput(); I.closeInput();   // twice is fine
  const bad = I.openInput('in:zz', { backend: 'null' });
  if (bad.ok !== false) fail('a bad id should be refused');
  console.log('input ok');
})();
