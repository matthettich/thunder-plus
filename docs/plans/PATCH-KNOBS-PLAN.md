# Patch knobs: tracks control …chains / …waves (owner, 22 Nov 2026)

Owner, 22 Nov 23:00: "Start the work … push anything that is ready so I can test … Have CodeRabbit check the work, delegate
what you think is appropriate to cheap agents, but check their work. I want this to hit the web app beta, not the native app."

**Web app only, branch `beta`.** Each step is its own branch + PR into `beta` (CodeRabbit reviews; merge when checks pass).
Native agents: see "Native port: in-flight web work" in HANDOVER.md for what this touches.

## The idea
A patch exposes up to eight **knobs** (K1–K8). A **Knob** module (shared, so in both …chains and …waves) outputs a control
signal: its own Value slider, or, while the song has commanded it, the track's knob value, mapped into Min..Max. Patch that
output into any purple (control) jack. Then the track drives it from three places:

1. **Tracker commands `KN1`–`KN8`** (00–FF): set knob N on this track's patch from here on; STOP hands it back to the
   patch's own Value (like FXV / BYP). Appended to `FXC` (saved songs keep their numbers). RND / RNL / REP work on them.
2. **MOD targets** K1–K8 on tracks with a chain (LFOs sweep a knob).
3. **Macros / scenes** (MAC, XFD) reach the same ids.

## Steps
- [x] **S1 (build 663): Knob module + KN1–KN8 on …chains.** `k1`–`k8` AudioParams on `thunder-chains` (−1 = the patch's
  own), `engine.knobs`, shared `knob` module (DSP + editor), commands in index.html (FXC, FXB, groups, help, describer,
  sequencer `chc` events, STOP reset), `node tools/make-core.mjs`, `python3 tools/sync-shared.py`, tests, changelog, BUILD,
  sw.js cache. Audio-thread sequencer path is generic over `chc` param names (core-processor `tcParam`).
- [x] **S2 (build 664): …waves knobs.** The live waves engine (`thunder-wlive`) gets k1–k8 too, so `KN1`–`KN8` on a …waves instrument track
  reach its patch.
- [x] **S3 (build 665): MOD targets K1–K8** (tgtList) for tracks with a chain / waves instrument; macros + scenes follow from tgtList.
- [ ] **S4: knobs on the bottom panel**: a Knob module shows in the FX bar deck by default (moving it there moves the Value).
- [ ] Later ideas from the same chat (not started): chain wet/level + CHN on/off commands, send amounts, HP filter command,
  LFO restart / depth commands; modules: Note/Gate In for chains, crossover / band split, sample player + wavetable osc in
  waves, sequencer in chains, pump FX in waves. Help-text list is missing END JMP FLY ATK REL TCH SKP CHD.

## Progress notes
- 22 Nov 23:10: plan written, S1 started on branch `knobs-kn`.
- 22 Nov 23:40: S1 built and tested (npm test green, browser-check green), PR into `beta`.
