# Gain staging: tracks, …chains and the master (audit, 27 Oct)

The owner asked whether to split the track volume (the meter's fader) from the Mixer's VOL field (as a pre-gain into
…chains), and to add a master fader bar on the Mixer page. "Today" and "Recommended split" below are the read of the code
at build 257. Owner (27 Oct): old songs changing how they sound is fine. **Built in build 264: see "Built" at the end.**

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

## Questions for the owner (answered 27 Oct: see "Owner's calls")

1. Should the new master bar fade after the limiter (OUT), or simply edit MASTER (pre-limiter)? And the master
   column's VOL automation: which one?
2. Pre-fader bus sends wanted? Ignoring mute, as most consoles do?
3. Should a send branch hear the source dry (at TRIM) rather than after its own chain? (A pre-chain tap into the bus
   nodes: a bigger change.)
4. Does Thunder Perform read `mix.vol` itself? Then it needs `trim` too.
5. A short crossfade when CHN is switched on or off?

## Owner's calls (27 Oct)

1. **The master bar is a master OUT fader after the limiter**; MASTER stays the drive into it, and the master phrase's
   VOL keeps moving MASTER.
2. **PRE-fader bus sends: yes**, as an option per send, tapping after the source's chain and FX, before VOL and pan;
   **mute and solo still silence them**.
3. A DRY (pre-chain) send tap: no.
4. Thunder Perform: answered from its code. It reads nothing of the mix itself; it runs Thunder Plus in frames from its
   pinned submodule, this repo's `deck` branch (13 Oct, far behind `main`), so it gets all of this only once `main` is
   merged into `deck` and the submodule moved on. `deck` sends `master` to the host's deck input, after which OUT sits.
5. **CHN on / off crossfades** (about 20 ms).

## Built (build 264)

Each part has a `GAIN STAGING`, `PRE SENDS` or `CHN SWITCH` comment at the code; `tests/gain-staging.test.js` covers it.

- **Per track** (`makePlayer`): `voices → inp (TRIM) → dry ─┬→ [track FX] → ins → g (VOL, mute, solo) → p (pan) → out / its bus`,
  with `inp → …chains → wet` joining at the same point, and `ins → pre` (1 or 0 with mute / solo) for PRE sends. `inp`
  also feeds the Sidechain In taps, the chain's Track In meter (`anIn`) and `ai` (the Mixer's TRIM light). `routeTrack`
  no longer pulls `inp` off everything to rewire; it rewires only what changed (a removed track-FX node is now
  disconnected too: it used to stay wired to the fader, silent).
- **TRIM**: `S.mix[t].trim`, 0–255, 80 = 0 dB, 00 −18 dB, FF +18 dB, even dB steps (`trimDb`, `trimGain`, `trimS`). The
  Mixer's first row is `TRM` (EDIT+arrows, typed hex, Delete resets, mouse drag / wheel, Ctrl/⌘+D copies it). Its light:
  an underline in the meter's hot colour above −6 dB and clip colour above −1 dB, from the input peak (held 0.6 s), read
  only for the tracks on screen. The info line gives the input peak. **VOL** is the bar on the meter (unchanged) and the
  bottom panel's slider.
- **Master OUT**: `S.mout` (0–255, `curve()`, FF = 0 dB), `rt.mo` between the limiter (mastering's or the safety one, or
  none) and the device's level. Live it glides (`moApply`, no `rtRoute` rewire); renders put it last (stems too, as
  MASTER is). The master row reads MASTER → FX → MASTERING → OUT (`c.mx` 0, 1, 2); OUT's bar stands on the L / R meter
  (`mixOutBar`: drag sideways, Shift fine, left of the meter is off, a double-click / double-tap is 0 dB). The bottom
  panel's master strip and its meter are OUT now (they were MASTER, before the limiter). Project → MASTER OUT.
- **PRE sends**: a Track In's `pre` param (…chains editor: "Bus send: After its fader / Before its fader"; also in the
  engine's def, ignored there). `busPlan().pre` lists the sources it applies to; `busSync` feeds them from `pre`
  (`splP`) instead of `p` (`spl`). The editor's switch calls `busSync` at once. A track has one Track In, so the switch
  is per source track (all the buses it feeds). A pure source (heard only through its bus) on PRE: its own fader then
  changes nothing, as on a console.
- **CHN switch**: `chnIn` / `chnOut` crossfade `dry` and `wet` over `XF` = 20 ms (`ramp`, anchored at now: a linear
  ramp starts at the previous event, and the first try came in at about 60% at once). A new chain fades in only when its
  engine posts `ready` (`n._onReady`); until then the track stays dry (it used to drop out). The old node is disposed
  after its fade. Renders switch at once. Not crossfaded: a bus's sources coming back to the speakers when the bus goes
  (they switch), and adding or removing a track's first native FX (a rewire, as before).
- Headless Chromium checks (not in `npm test`): offline renders gave TRIM +5.95 dB for 42 steps and exactly ±18 dB at
  00 / FF; a PRE send kept its level with VOL at 0, ignored pan, followed TRIM and went silent on mute, while a post send
  followed VOL, pan and mute; live, CHN on / off recorded on the audio thread showed no level dip or bump and no sample
  step bigger than the test sine's own; the OUT bar sat exactly on the meter's dB scale at 1400, 882 and 384 px wide.
