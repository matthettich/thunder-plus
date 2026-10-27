# Gain staging: tracks, …chains and the master (audit, 27 Oct)

The owner asked whether to split the track volume (the meter's fader) from the Mixer's VOL field (as a pre-gain into
…chains), and to add a master fader bar on the Mixer page. Nothing is changed yet; this is the read of the code at
build 257. Owner (27 Oct): old songs changing how they sound is fine.

## Today

Per track (`makePlayer`, `routeTrack`; `applyMix`):

`source → buses[t].inp → [chains node] → [native track FX] → g (VOL + mute/solo) → p (pan) → out → master`

- **The Mixer's VOL field, the meter fader bar (`.mfbar`) and the bottom panel's track slider (`stVol`) are one value:**
  `S.mix[t].vol`, applied at `g`, *after* …chains and the track FX. The meter reads after the pan (post-fader).
- Note gain (phrase VOL column and commands, × the instrument's vol) is applied per voice before `inp`.
- **…chains inputs:** in0 the track's `inp` (pre-everything); in2 taps from `inp`; in3 Sidechain Out sends (pre-fader,
  mono, one block late); in4/5 bus feeds from each source's `p` (post-fader, post-pan, after the source's own chain).
- **A bus "send"** (Track In t into another track's Chain Out) is post-fader and post-pan, so VOL moves it; it also
  hears t *after* t's own chain.
- **Levels inside …chains:** Track In has no gain; Chain Out `level` 0–2 (+6 dB); Utility gain; Meter trim ±24 dB. The
  per-track node in Thunder has **no output limiter** (only the standalone editor's `soft()`); a hot chain reaches the
  master limiter unclipped (floating point).
- **CHN off in the Mixer** disposes the node and rewires `inp → fx → g`: a clean unity bypass, but a hard switch (tails
  cut, possible click, lookahead latency vanishes).
- **Master:** `rt.out` (gain `curve(S.master)`, the Mixer's MASTER value, FF = 0 dB, no boost) → master FX (…pump
  included) → mastering (EQ + limiter) → safety limiter (only when mastering's limiter is off) → the device output
  level (Settings, not saved). So MASTER is drive into the limiter, not an output fader. The master column's VOL
  automation drives `S.master` too. No fader bar today.
- Renders run the same `makePlayer` / `applyMix` in an OfflineAudioContext, so live and export match.

## Recommended split

`source → TRIM (inp.gain) → [taps, Sidechain Outs] → …chains → track FX → VOL fader + mute/solo (g) → pan (p) → bus feeds → master`

- **TRIM**: a new `S.mix[t].trim` in dB (about ±18 dB, 0 in the middle; the E0 = unity hex scale is too narrow for a
  trim), set on `inp.gain` in `applyMix`. The Mixer's VOL row becomes TRIM; the meter bar and the deck slider stay VOL.
  Show the Track In meter (`anIn` exists) next to it: a hot trim drives chains that have no limiter.
- **Bus sends**: keep post-fader as the default; an optional pre-fader send would tap just before `g` (still after the
  source's chain; it needs its own mute, as mute lives at `g`). Say in Help that a send hears the processed track.
- **Master**: keep `S.master` as MASTER IN (drive, and what the VOL automation moves); add a master OUT fader after the
  limiter, capped at 0 dB, default 0 dB, drawn as a bar like the tracks'.
- Phrase VOL stays note gain. CPU: none (`inp` is already a GainNode).

## Questions for the owner

1. Should the new master bar fade after the limiter (OUT), or simply edit MASTER (pre-limiter)? And the master
   column's VOL automation: which one?
2. Pre-fader bus sends wanted? Ignoring mute, as most consoles do?
3. Should a send branch hear the source dry (at TRIM) rather than after its own chain? (A pre-chain tap into the bus
   nodes: a bigger change.)
4. Does Thunder Perform read `mix.vol` itself? Then it needs `trim` too.
5. A short crossfade when CHN is switched on or off?
