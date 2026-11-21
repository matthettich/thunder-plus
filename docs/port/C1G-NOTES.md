# C1G-NOTES: the song-level golden of the chains host (unit C1g)

Branch `n1/chains-c1g` (from `native`). Tool `native/tools/chains-song-dump.mjs` (its top comment is the README: how to run, page code vs glue, what it does not prove),
goldens `native/tests/golden/chains_song_cases.json` (21 cases) and `chains_song_beta_fragment.json` (BETA-TESTING-ELECTRO's song fragment, made by `--extract-beta`).
`index.html:N` means beta's index.html at `native/tests/web-ref/BETA-COMMIT` (cdd94394); `native/tests/web-ref/chains-wiring-excerpt.js` holds those lines with the same numbers in `// ==== index.html:A-B ====` headers.

## What was added
- `native/tests/web-ref/chains-worklet.js` (verbatim beta; the repository root's copy is OLDER: it lacks the out meters and Send/Receive; audio of Track In / Chain Out / Stereo mixer is the same), `mix-excerpt.js` (executed page text), `chains-wiring-excerpt.js` (read-only), made by `native/tools/web-pin-chains.mjs`.
- `mix-dump.mjs`: 4 added lines (`useSources`): its model can run on the pinned beta sources. Default unchanged (`mix-dump.mjs --check` still passes). Needed because the root snapshot's `busPlan` has no bus into bus and its `audible` no solo closure.
- the tool, the goldens.

## Cases
a/electro, b/beta-testing-electro, c/bus-into-bus (2 nodes, PRE send, levels/pans), d/mono-into-bus, e/mute-source, e/solo-source, e/mute-bus, e/solo-bus, f/byp-mid-quantum, f/byp-on-quantum-start, f/byp-one-frame-before,
f/byp-one-frame-after, f/byp-fuzz-quantum-start, g/tempo-change, h/stem-bus, h/stem-plain-track, h/stem-source-only, i/chain-off-bus-track, i/no-chain-out, j/patch-transport-vs-song, k/twenty-sources (second merger).
Modules used: trackIn, chainOut, smixer, vca, lfo only (none of the 48 library types). Every case also carries `expect` (heard tracks, nodes in creation order, bus maps, PRE sources, routed sources, event quanta): C1 should assert its own plan equals these before comparing hashes.
Negative controls in each case (`controls`): events one quantum early / late, and for j the missing initial transport, all give a DIFFERENT hash (the tool fails if not), so the cases do pin what they say.
Determinism: `--golden` run three times gave the same sha256 (9c3067cf...b56b3); `--check` passes.

## The t == t0 edge (D3)
Rule (index.html:23229 `q = 128 / ac.sampleRate, at = Math.ceil(ev.t / q) * q`, tempo; chc is `setValueAtTime(ev.v, max(ev.t, ac.currentTime))` at :23272 on a k-rate parameter, chains-worklet.js:6235 descriptor): an event is seen from quantum k = ceil(t / q).
For t exactly the first frame of quantum k, t / q = k, so the event takes effect IN quantum k (the same block, "t == t0 lands in the block that starts at t0"): a suspend(at) at a quantum start runs before that quantum, and a k-rate parameter reads the value in force at the quantum's first frame, which includes an event at that time.
One frame earlier is also quantum k; one frame later is k+1 (cases f/byp-one-frame-*). Pinned by f/byp-on-quantum-start (equal hash to one-frame-before, different from one-frame-after).
**Trap:** in double arithmetic `Math.ceil((k * 128 / 48000) / (128 / 48000))` is k+1 for about 18 % of k (k = 5, 7, 10, 14, ... 190). The page's expression then puts an event that is exactly on a quantum start into the NEXT quantum (case f/byp-fuzz-quantum-start, k = 190 -> 191). C1 must evaluate ceil(t / q) in double with q = 128.0 / 48000.0 (not integer frames). For chc a real Chrome k-rate read may differ from this expression in those cases (Blink compares in frames): INFERRED, not measured; for a real song t is never exactly a quantum start, so only synthetic times hit it.
Also found: in an offline render a MUTED bus has no node (pass() routes only `keep` tracks, index.html:25859-25862), so its pure sources are NOT taken off the master: they play straight (e/mute-bus). A tempo event whose ceil(t/q)*q is outside (0, length) is sent at once (before quantum 0).

## How C1 reproduces a case
1. Build the song from `case.song` (bpm, lpb, ticks, master, mout, mix[32], chainOn[32], chainPatch; the mixer fields are the song's own). Heard set: `case.stem` null -> tracks with an input that are audible, plus buses added by busStemPlan, then busClosure; stem -> {stem} then busClosure. `expect.heard` is the answer.
2. Inputs: `case.inputs` lists tracks and `mono`; make each track's samples with the generator in the header (below) for quanta*128 samples; mono = one channel (L only, R is not used). Feed them to the `Mixer` as the strips' inputs.
3. Hook a `ChainsNode` (zero latency, D1) on each heard track with chainsFeed(t), in `expect.nodes` order, patch = case.song.chainPatch, busMap = `expect.busMaps[t]` (ascending, max 32), tapMap empty, node transport = {tempo bpm, lpb, ticks, playing false}; then apply {tempo bpm, lpb, ticks, playing true} to every node before quantum 0 (this beats the patch's saved transport). Feed j of a bus is pair 2j, 2j+1 of merger j>>4 (inputs 4 / 5): after pan, or from `pre` for PRE sources. Pure sources of a bus that has a node leave the master (`expect.routed`).
4. Events: apply at the start of quantum ceil(t/q) (double, q = 128.0/48000.0, `expect.eventQuanta`): chc -> bypass switch b<p> of that track's node = v; tempo -> transport tempo / ticks on every node.
5. Output: master = sum x MASTER x OUT per the Mixer (no effects, mastering, limiter, PDC). Hash FNV-1a 32 over L(128) then R(128) of each quantum; compare `hash`; on a mismatch compare `qh` (per quantum) then `head`. `--dump DIR` writes the reference samples (L then R, float32) for a local diff.
Input generator (exact; compile with -ffp-contract=off): seeds sL = ((uint32)(t+1) * 2654435761u) ^ 0x2545F491u, sR = sL ^ 0xA5A5A5A5u; per sample advance both (s ^= s<<13; s ^= s>>17; s ^= s<<5); uL = sL/4294967296.0; inc = 25769803 + 3231531*t; ph = (uint32)(i*inc); phR = ph + 0x2AAAAAAB; wave(p): x = p/4294967296.0; w = x<0.5 ? x : x-1.0; a = |w|; y = (16.0*w)*(0.5-a); burst = (((i>>11)+t)&3)==0; X = 0.25*wave(p) + (burst ? 0.2*(2.0*u-1.0) : 0.0) in double, one conversion to float. L uses (ph, uL), R (phR, uR).

## Glue (each piece in the tool's README names its page lines)
G1 want (applyMix filter = keep, :23495-23500), G2 plan (:23547, :21137-21160, :22924-22962, :25859-25862), G3 block (mix-dump Model.block with the node in place of the 1:1 stand-in; :23523-23529, :22947-22952, :22929/22961), G4 the node call (inputs [pair, [], [], [], merger0, merger1], k-rate b1..b15 as 1-sample arrays), G5 processorOptions (:21137-21160), G6 initial transport (:25862), G7 event delivery (:23222-23236, :23272), G8 keep rule (:25909-25913, :25946-25952, busClosure :25829), G9 inputs.

## What could not be done / not proven
- Not Chrome: k-rate parameter sampling and port-message delivery time in an OfflineAudioContext are inferred (above). The wet + dry(0) sum is taken as the wet path.
- No taps, sends, sidechain, Resample, patch edits, quality shedding, track effects, master effects: not in these cases.
- Chrome extension (task 6): see the PR body / report for what was found. `song-ref.mjs` and `render-compare.sh` read the repository root's OLD index.html, not the pin, so a `--chains` reference from them would be of the old page (no bus into bus): left unchanged.
