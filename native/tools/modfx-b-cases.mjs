// U20: the direct cases of the MODFX modules of unit B (Spectral 'spectral', Extreme stretch 'stretch', Convolution reverb 'convolve'), for tools/modfx-dump.mjs.
// These three differ from the modules of U19 in how long they must run to show what they do: Convolve builds a 6 s impulse response over about a hundred blocks and crossfades to a new one
// after a change; Extreme stretch records 12 s and only jumps to the newest sound when its read head has fallen about 10 s behind (so the jump cases run at 8 kHz, where that takes 1500
// blocks); Spectral's frames are 2048 samples. So each module gets its own list, still driven by the shared runDirect().
// THE SONG'S IMPULSE RESPONSES (globalThis.__thunderIR = [{ L, R | null, sr, id } x4]): made here from an integer xorshift and basic arithmetic only (no Math.*), so the same IR is made by the
// C++ test (tests/test_modfx.cpp, irMake) on every platform; a case lists them as specs { n, sr, st (stereo), seed, nan } per slot (null: an empty slot) and may swap them at a block.
export const MODS_B = ['spectral', 'stretch', 'convolve'];

export function irMake(sp) {
  const mk = ch => {
    const a = new Float32Array(sp.n); let s = (Math.imul(sp.seed, 2654435761) + ch * 40503) >>> 0 || 1, env = 1; const d = 1 - 9 / sp.n;
    for (let i = 0; i < sp.n; i++) {
      s ^= s << 13; s >>>= 0; s ^= s >>> 17; s ^= s << 5; s >>>= 0; const noise = s / 4294967296 - .5;
      a[i] = Math.fround(i === 0 ? 1 : noise * env * (i % 50 === 0 ? 3 : 1)); env *= d;
    }
    if (sp.nan) a[5] = NaN;
    return a;
  };
  return { L: mk(0), R: sp.st ? mk(1) : null, sr: sp.sr, id: 'ir' + sp.seed };
}
// a new table (new objects) from four specs
export const irTable = set => set.map(sp => (sp ? irMake(sp) : null));

export function casesB(type, def, add, H) {
  if (!MODS_B.includes(type)) return false;
  const { defaults, stressP, switchesP, randParams, lcg, signal } = H;
  const P = (o) => Object.assign(defaults(def), o);
  // inputs with the things the modules must shrug off: NaN, infinity, samples of 8 and more
  const mkBad = (k, total, kind) => { const a = signal(kind, 'mix', total, 4242 + k * 7, ''); for (let i = 0; i < total; i += 331) a[i] = NaN; for (let i = 17; i < total; i += 1511) a[i] = Infinity; for (let i = 29; i < total; i += 1801) a[i] = (i & 1) ? 9.5 : -8; return a; };
  const kinds = ['mix', 'loud', 'quiet', 'impulse', 'burst', 'longsleep'];
  if (type === 'spectral') {
    const nb = 220;
    add('def', { params: [defaults(def)], nblocks: 120 });
    add('stress', { params: [stressP(def)], sigKind: 'loud', nblocks: nb });
    add('sw', { params: [switchesP(def)], sigKind: 'impulse', nblocks: nb });
    add('min', { params: [randParams(def, null, 'min')], nblocks: 120 });
    add('max', { params: [randParams(def, null, 'max')], sigKind: 'quiet', nblocks: 120 });
    for (let mode = 0; mode <= 5; mode++) {
      // each mode, Freeze on from block 60 to 140 (the catch, the held sound, the release), Amount and Shift away from their defaults
      add(`mode${mode}`, { params: [P({ mode, amt: .7, shift: .4, tone: .3, freeze: 0, mix: 1 }), P({ mode, amt: .7, shift: .4, tone: .3, freeze: 1, mix: 1 }), P({ mode, amt: .35, shift: -.6, tone: -.5, freeze: 0, mix: .8 })], pb: [0, 60, 140], nblocks: nb, sigKind: mode & 1 ? 'mix' : 'burst' });
    }
    add('blur1', { params: [P({ mode: 0, amt: 1, mix: 1 })], sigKind: 'burst', nblocks: 300 });
    add('shiftfb', { params: [P({ mode: 5, amt: 1, shift: 1, mix: 1 }), P({ mode: 5, amt: .6, shift: -.15, mix: 1 })], pb: [0, 100], nblocks: 200, sigKind: 'mix' });
    add('robot0', { params: [P({ mode: 3, amt: 0, mix: 1 })], nblocks: 100 });
    for (let i = 0; i < 4; i++) { const rr = lcg(77 + 13 * i + 5); add(`rand${i}`, { params: [randParams(def, rr)], sigKind: kinds[(i * 2 + 1) % 5], linkMode: ['all', 'first', 'all', 'half'][i], rr: i, nblocks: 140 }); }
    for (let i = 0; i < 2; i++) add(`mod${i}`, { params: [], changeEvery: i ? 7 : 25, nblocks: 160, rr: 10 + i });
    add('first', { params: [defaults(def)], linkMode: 'first', nblocks: 100 });
    add('none', { params: [stressP(def)], linkMode: 'none', nblocks: 60 });
    add('bad', { params: [P({ mode: 4, amt: .5, freeze: 1, mix: 1 })], nblocks: 120, mkSig: mkBad });
    add('odd', { params: [switchesP(def)], sizes: [1, 37, 128, 64, 3, 127, 100, 5], nblocks: 200, sigKind: 'mix' });
    add('sr44', { params: [stressP(def)], sr: 44100, nblocks: 120 });
    add('sr96', { params: [switchesP(def)], sr: 96000, sigKind: 'loud', nblocks: 120 });
    return true;
  }
  if (type === 'stretch') {
    add('def', { params: [defaults(def)], nblocks: 200 });
    add('stress', { params: [stressP(def)], sigKind: 'loud', nblocks: 200 });
    add('sw', { params: [switchesP(def)], sigKind: 'impulse', nblocks: 200 });
    add('min', { params: [randParams(def, null, 'min')], nblocks: 200 });
    add('max', { params: [randParams(def, null, 'max')], sigKind: 'quiet', nblocks: 200 });
    for (let win = 0; win <= 4; win++) add(`win${win}`, { params: [P({ amt: .3, win, tone: .4, mix: 1, freeze: 0 }), P({ amt: .3, win, tone: .4, mix: 1, freeze: 1 }), P({ amt: .8, win, tone: -.3, mix: .7, freeze: 0 })], pb: [0, 150, 260], nblocks: win > 2 ? 340 : 280, sigKind: win & 1 ? 'mix' : 'longsleep' });
    // the read head falls behind and jumps (about 10 s behind: reached in 1500 blocks at 8 kHz), with a crossfade; a restart of the song also jumps
    add('jump0', { params: [P({ amt: 0, win: 2, mix: 1 })], sr: 8000, nblocks: 1500, sigKind: 'mix' });
    add('jump1', { params: [P({ amt: .05, win: 1, mix: .8, tone: .5 }), P({ amt: .05, win: 3, mix: 1, freeze: 1 }), P({ amt: .1, win: 0, mix: 1, freeze: 0 })], pb: [0, 600, 900], sr: 8000, nblocks: 1500, sigKind: 'mix', transport: [{ b: 300, startN: 1 }, { b: 1200, startN: 2 }] });
    for (let i = 0; i < 4; i++) { const rr = lcg(77 + 13 * i + 7); add(`rand${i}`, { params: [randParams(def, rr)], sigKind: kinds[(i * 2 + 2) % 5], linkMode: ['all', 'first', 'all', 'half'][i], rr: i, nblocks: 240 }); }
    for (let i = 0; i < 2; i++) add(`mod${i}`, { params: [], changeEvery: i ? 30 : 55, nblocks: 300, rr: 10 + i });
    add('none', { params: [stressP(def)], linkMode: 'none', nblocks: 60 });
    add('bad', { params: [P({ amt: .2, win: 0, mix: 1 })], nblocks: 200, mkSig: mkBad });
    add('odd', { params: [switchesP(def)], sizes: [1, 37, 128, 64, 3, 127, 100, 5], nblocks: 260, sigKind: 'mix', transport: [{ b: 100, startN: 1 }] });
    add('sr44', { params: [stressP(def)], sr: 44100, nblocks: 200 });
    add('sr96', { params: [switchesP(def)], sr: 96000, sigKind: 'loud', nblocks: 200 });
    return true;
  }
  // ---- convolve ----
  const IRS = [{ n: 48000, sr: 48000, st: true, seed: 11 }, { n: 20000, sr: 44100, st: false, seed: 12 }, { n: 90000, sr: 96000, st: true, seed: 13 }, { n: 5000, sr: 22050, st: true, seed: 14 }];   // 96 kHz: the anti-alias path
  const IRS2 = [{ n: 400000, sr: 48000, st: false, seed: 21 }, null, { n: 3000, sr: 48000, st: true, seed: 23, nan: true }, { n: 30000, sr: 8000, st: true, seed: 24 }];   // longer than 6 s (cut); an empty slot; a NaN; a very low rate
  const irs = set => [{ b: 0, set }];
  const base = { size: .55, decay: 1.6, pre: .02, tone: .4, early: .6, width: 1.2, mix: .6 };
  const NAMES = ['room', 'hall', 'chamber', 'plate', 'cath', 'spring', 'gated', 'reverse', 'cab', 'phone', 'song1', 'song2', 'song3', 'song4'];
  for (let t = 0; t <= 13; t++) add(`t${t}_${NAMES[t]}`, { params: [P(Object.assign({ type: t }, base, t === 4 ? { decay: 1.1 } : {}))], nblocks: t === 4 ? 260 : 200, irs: irs(IRS), sigKind: t % 3 === 0 ? 'mix' : t % 3 === 1 ? 'burst' : 'impulse', mix: 1 });
  add('def', { params: [defaults(def)], nblocks: 120, irs: irs(IRS) });
  add('stress', { params: [stressP(def)], sigKind: 'loud', nblocks: 200, irs: irs(IRS) });
  add('sw', { params: [switchesP(def)], sigKind: 'impulse', nblocks: 200, irs: irs(IRS) });
  add('min', { params: [randParams(def, null, 'min')], nblocks: 200, irs: irs(IRS) });
  add('max', { params: [randParams(def, null, 'max')], sigKind: 'quiet', nblocks: 200, irs: irs(IRS) });
  // early / tone / size / decay corners of the IR makers
  for (const [t, o] of [[0, { early: 0, tone: 0, size: 0, decay: .1 }], [0, { early: 1, tone: 1, size: 1, decay: 4 }], [1, { early: 0, tone: 1, size: 1, decay: 3 }], [6, { early: .2, decay: 1.5 }], [5, { early: 1, tone: 1, size: 1 }], [5, { early: 0, tone: 0, size: 0, decay: 5 }], [8, { early: 0 }], [9, { early: 1, tone: 1, size: 0 }], [10, { early: 0, tone: 0, size: 0 }], [10, { early: 1, tone: 1, size: 1 }], [11, { early: .25, tone: .8, size: .2 }], [13, { early: .9, tone: .2, size: .9, decay: 10 }]])
    add(`c${t}_${Object.values(o).join('_')}`, { params: [P(Object.assign({ type: t, mix: 1 }, o))], nblocks: 170, irs: irs(IRS2), sigKind: 'burst' });
  // changes while it plays: a new IR once the controls rest (150 ms), crossfaded in; every kind of IR made while another one plays
  add('chg0', { params: [P({ type: 1, mix: 1 }), P({ type: 0, size: .3, decay: .8, mix: 1 }), P({ type: 5, decay: 2.5, mix: 1 }), P({ type: 10, mix: 1 }), P({ type: 3, mix: 1 })], pb: [0, 280, 580, 880, 1180], nblocks: 1450, irs: irs(IRS), sigKind: 'mix' });
  add('chg1', { params: [P({ type: 9, mix: .8 }), P({ type: 8, tone: .8, mix: .8 }), P({ type: 7, decay: 1, mix: .8 }), P({ type: 13, mix: .8 })], pb: [0, 230, 500, 760], nblocks: 1000, irs: irs(IRS2), sigKind: 'longsleep' });
  add('irswap', { params: [P({ type: 10, mix: 1 }), P({ type: 10, mix: 1, size: .5 })], pb: [0, 520], nblocks: 1000, irs: [{ b: 0, set: IRS }, { b: 300, set: IRS2 }, { b: 520, set: IRS }, { b: 800, set: [null, null, null, null] }], sigKind: 'mix' });
  add('noirs', { params: [P({ type: 12, mix: 1 })], nblocks: 120 });
  add('mod0', { params: [], changeEvery: 90, nblocks: 600, rr: 10, irs: irs(IRS) });
  add('mod1', { params: [], changeEvery: 300, nblocks: 800, rr: 11, irs: irs(IRS2) });
  add('moving', { params: [], changeEvery: 8, nblocks: 500, rr: 12, irs: irs(IRS) });   // "or every second while they keep moving"
  add('first', { params: [defaults(def)], linkMode: 'first', nblocks: 160, irs: irs(IRS) });
  add('none', { params: [stressP(def)], linkMode: 'none', nblocks: 60, irs: irs(IRS) });
  add('half', { params: [P({ type: 2, mix: 1 })], linkMode: 'half', nblocks: 160, irs: irs(IRS) });
  add('bad', { params: [P({ type: 4, decay: 1, mix: 1 })], nblocks: 200, mkSig: mkBad, irs: irs(IRS) });
  add('odd', { params: [P({ type: 0, mix: .7 })], sizes: [1, 37, 128, 64, 3, 127, 100, 5], nblocks: 260, irs: irs(IRS), sigKind: 'mix' });
  add('odd2', { params: [P({ type: 11, mix: .7 })], sizes: [64, 64, 128, 128, 100, 28], nblocks: 260, irs: irs(IRS), sigKind: 'burst' });
  add('sr44', { params: [stressP(def)], sr: 44100, nblocks: 200, irs: irs(IRS) });
  add('sr96', { params: [P({ type: 10, mix: 1 })], sr: 96000, sigKind: 'loud', nblocks: 200, irs: irs(IRS) });
  add('sr8', { params: [P({ type: 12, mix: 1, size: 0 })], sr: 8000, nblocks: 200, irs: irs(IRS2) });
  for (let i = 0; i < 3; i++) { const rr = lcg(77 + 13 * i + 9); add(`rand${i}`, { params: [randParams(def, rr)], sigKind: kinds[(i * 2 + 1) % 5], linkMode: ['all', 'first', 'all'][i], rr: i, nblocks: 200, irs: irs(i & 1 ? IRS2 : IRS) }); }
  return true;
}
