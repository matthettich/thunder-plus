# Audio questions from the 10 Nov queue (items 5 and 6): findings, 11 Nov 2026

Investigation only: no app code was changed for this note. Every number below was measured on `fix/reset-rebuild` (build 562 plus the Reload offer, which touches none of this). Scripts were throwaway, kept in `/tmp`, built on `tests/helpers.js` (node, no browser) and on the engine-targets sweep's own page harness (headless Chrome, `tests/sweep/eng-targets.mjs` helpers). Nothing here needs the owner's ears.

## 1. Queued item 5: the "−58 dB high-band floor" on bass-heavy mixes

**Verdict: not from the master chain, so nothing in the limiter / clipper / mastering is a bug. The floor is the bass engine's own high band, and the sweep comment blaming the master chain is wrong.**

The claim (comment at `tests/sweep/eng-targets.mjs:213`): "the master chain limiter / clipper makes harmonics of a bass note that fill the 2 kHz+ band (a floor of ~1.3e-3 at any cutoff)". 1.3e-3 is −57.7 dBFS rms in the band above 2 kHz (the sweep's own 24 dB/oct high-pass at 2 kHz).

What was run, with the same 2 kHz high-pass (two RBJ biquads) and rms over steady stretches:

**a. The stages, offline in node (the real `master-worklet.js`, `MasterProcessor`, 128-frame blocks).** Input: clean (click-free) 55 Hz sine / bass notes with smooth attack and release, plus a sub kick, so the input's own band above 2 kHz is below −100 dB and everything above it is made by the processor.

| Stage (settings as shipped) | Limiting applied | Band above 2 kHz that the stage ADDED |
|---|---|---|
| Safety limiter (`SAFE_LIM`: ceiling −1, look 3 ms, rel 120 ms, true peak) | steady 55 Hz sine at 1.0 / 1.2 / 1.5 / 2.0 (1 to 7 dB) | −91 / −87 / −85 / −85 dBFS |
| same, bass notes on 16ths and 8ths, peaks 1.2 to 3.0 | up to 10 dB | −79 to −88 dBFS |
| same, kick + bass mix, input peak 1.5 / 2.4 | 3.5 / 7 dB | −94 / −82 dBFS |
| Fresh-song mastering (comp 2:1 at −14 + limiter; `freshMst`) | same | −94 / −85 dBFS |
| Compressor alone | peak 1.5, 2.4 | below the input's own (−102 / −99) |
| Resonance suppressor alone (old default, on) | peak 1.5, 2.4 | below the input's own |
| All three (the old default song) | | −94 / −85 dBFS |

So the loudest thing a limiter does to a clean bass is about −79 dBFS (7 to 10 dB of limiting), 20 dB under the quoted floor. When the signal sits under the ceiling (peak under 0.891) the limiter is a bit-exact pass-through (`tests/master.test.js` pins that).

**b. The stand-in (used until the master worklet has loaded, and on phones until another module loads).** `mkLimiter` (a `DynamicsCompressorNode`, ratio 20, threshold −2, attack 2 ms) then `softClip` (WaveShaper, tanh above 0.53, no oversampling), run in headless Chrome `OfflineAudioContext`: 1.5 amplitude 55 Hz: −89 dBFS; notes at 1.5 to 3.0: −81 to −86 dBFS. Also not −58.

**c. Live, in the real page (headless Chrome), a held bass note, taps on the bass node, `rt.out` and `master` (ScriptProcessor taps).** With the sweep's default bass (cut 2000) and with cut 300:
- The band above 2 kHz is already there at the bass node itself (−55 to −69 dBFS at cut 300, varying 20 dB from one 0.25 s slice to the next) and at `rt.out`, before any master stage. At the master it is the same or lower: master / `rt.out` band ratio 0.37 to 1.0, roughly the whole-band ratio the limiter's gain reduction gives (rms 0.91 to 0.49 at ×8 gain). It scales linearly with level (×8 gain: +18 dB at every tap) and does not change with mastering on or off or the safety limiter on or off. The master chain removes, not adds.
- Turning the master chain off (mastering off, safety limiter off) leaves the floor at the master (−65 to −72 dBFS), which a stage that creates harmonics could not do.

**d. Where it does come from.** `BassCore` alone (same params as the sweep's bass), 3 s held note, same measure:

| cut | rate 1 | rate 0.5 |
|---|---|---|
| 200 | −106.6 | −120.0 |
| 300 | −94.5 | −107.9 |
| 600 | −74.4 | −87.5 |
| 2000 | **−46.7** | **−58.2 (1.23e-3)** |
| 8000 | −43.0 | −57.5 |

The sweep's own bass at its default cut (2000), one octave under the pad's pitch, gives 1.23e-3: the quoted "1.3e-3" to two figures. It was the bass note's high partials passing a 2 kHz filter; the "at any cutoff" part holds only loosely (it falls fast below cut 600 in the core).

**What stays open.** In the live page the bass node's band at cut 300 (−55 to −69 dBFS) is 25 to 40 dB above what `BassCore` gives offline for the same knobs (−94 to −108 dBFS). The first half second of a capture has real steps (a click or the note's attack, sample jumps of about 0.2), and the later slices show no jumps over 0.05 but a band that jumps between 0.03e-3 and 0.5e-3. Possible causes: the live node's note-on (gate / trig automation), the page's `cut` reaching the node differently from the core's `set()`, or the ScriptProcessor capture itself on a busy 2-CPU machine. Not resolved in this session; it is unrelated to the master chain. If the owner hears a hiss or fizz on a held, nearly closed live bass, that is where to look.

**Suggested fix:** none for the master chain. Edit the comment at `tests/sweep/eng-targets.mjs:213` (the floor is the engine's own high band, not the limiter; the tap on `rt.out` stays because it is the cleaner measurement of the engine). Mastering defaults do not matter here: with mastering off (the new-song default: `mstPump` slots off) only the safety limiter (`S.limiter`, on by default) is in the path, and it is a pass-through below −1 dBFS.

(Side note, a test-setup trap: the sweep's page harness builds its song from the booted app state, where `S.mst.on` is true and sup / comp / lim are all on and the master pump slots retrig and flex are on: it is not a fresh new song. Anything measured at the master in that harness has the old default mastering in it unless `S.mst.on = false` and the master pump slots are switched off first.)

## 2. Queued item 6a: "Vox REL has no audible tail in a render"

**Verdict: a real bug in `VoxCore.enter()`, older than 555, the same in the live worklet and the render. The REL knob does nothing because the voice's source level is set to zero at the same instant the release envelope starts.**

Reproduced in node (the real `VoxCore`, `renderVox`, `voxParams` cut from `index.html`): `renderVox({vox:{rel}})` for the default lyric ("hello world, I am a robot") is 3.395 s at rel 20, 3.422 s at 140, 3.435 at 500, 3.441 at 1500 and 3.441 at 3000: the rel knob changes the length by 46 ms over a 150-fold range. One syllable "ah" (sylLen 320 ms, so the held part ends at 0.32 s): length 0.34 / 0.36 / 0.36 / 0.36 s for rel 20 / 140 / 1000 / 3000, and the rms from 0.5 to 1.0 s is exactly 0 for every rel.

**Cause** (`index.html`):
- `off()` (line 5232) plans the closing segments and ends them with `{ph:'', k:'end', d:0}`.
- `enter()` (line 5256) runs for each segment. Line 5263 sets `t.av = 0; t.an = 0; t.ah = 0;` for every segment, then the `switch` puts back what that segment wants. For `'end'` (line 5270) the case is `this.envT = 0; break;`: the amplitude envelope target goes to 0 (so `env` starts its release with time constant `rel / 4.6`, line 5297) but `t.av`, the voiced source level, was just zeroed at line 5263 and `K.av` follows it in about 4 ms (`ka`, line 5287). The envelope releases a source that is already silent.
- `renderVox` (line 5391) then spends `rel × 1.6` seconds rendering the tail, which is silence, and the "trim below 1e-4" at its end cuts it. The live `VoxProc` uses the same class text.

**Smallest fix** (one line): in `enter()`, `case 'end': this.envT = 0; t.av = this.k.av; break;` (keep the voiced source where it is; noise and aspiration still fade to 0). Measured in the same harness: "ah" lengths 0.35 / 0.56 / 1.95 / 5.15 s for rel 20 / 140 / 1000 / 3000; rms 0.5 to 1.0 s: 0 / 2.5e-4 / 3.5e-2 / 9.4e-2. A syllable that ends in a consonant ("yes") is unchanged (its last sounds carry no voicing, so there is nothing to ring). An alternative with the same effect is to return early from `enter()` at an `'end'` segment (the variant tried first: same lengths).

Consequences to plan for: it changes every vox render and live note, so the `engine|vox|…` entries of `tests/golden/engines.json` (`tools/engine-golden.js`) must be regenerated, and old songs with long REL on a vox pad (the default is 140 ms) get a tail they never had (about 0.2 s at the default; the sweep's REL check for vox, which carries a note saying it cannot pass, can then be made a real check). A What's new line is due.

## 3. Queued item 6b: "pump live synths are not repeatable render to render"

**Verdict: not a bug in the engine, the worklet or the wasm. The pump synths repeat bit for bit. What differs is the pad's Level, which the sweep's `build()` changes between its first and second call.**

Reproduced both ways:
1. **Node, `PsynProc` (`pump-synth-worklet.js`) on the real wasm:** for all 13 live kinds (`pmonol` … `pwobl`), the same held note played twice, (a) in two fresh worklet scopes (two wasm instances) and (b) as the first, second and third node in one scope (one shared wasm instance, as in the page): all identical to the last sample, 300 blocks each.
2. **Page, repeated offline renders of the same song (`__e2e.go`):** `pmono`: render 1 differs from render 2 by 1.23e-1, which is exactly its own peak (render 1 peaks at 0.1231, renders 2, 3, 4 at 0.2457: ratio 0.501 = −6.0 dB), and renders 2 to 7 are identical to the last sample (0 of 210000 samples differ). `ppoly` the same (0.114 peak diff). `bass` and `vox` showed 0 diff on all renders (they were run after the pump pad already held its second state).

**Cause** (a test-harness effect, not the app): `setEngine()` (`index.html:9180`) gives a pad's FIRST use of an engine the start level `ENGINE_LEVEL` = −6 dB (line 9200, `engFirst`: the engine's parameter object does not exist yet) and stores the pad's own level as `levelL`. The sweep's `build()` (`tests/sweep/eng-targets.mjs:54`) calls `setEngine('layers')` and then `setEngine(T.eng)` for each track on every call: the first call leaves the pad at −6 dB, and the second call's `setEngine('layers')` restores `levelL` (0 dB) and `setEngine(T.eng)` finds the parameter object already there (not first use any more), so the pad stays at 0 dB. Measured: `kit.voices[8].level` is −6 after the first `build()` and 0 after the second. Everything after the first render is identical, so render 1 is 6.02 dB quieter than the rest. "Free-running state" (HANDOVER 10 Nov) is not what is happening.

**Smallest fix** (test side only, nothing in the app): in `build()` after `setEngine(T.eng)`, set the level the test intends, for example `V().level = 0;`, or call `build()` twice before the first render. The sweep's A9 "pump live mono holding a note … equal to origin/beta" compares two branches that both have the −6 dB first render, so it passes as it is; its "this branch twice" figure (0.123) is this effect, not noise. (Not changed here, as the note says.)

A smaller real thing found on the way, not a bug: a pad that has used an engine before and is switched to it again keeps its level, and a fresh pad starts 6 dB down; that is by design (the "so a new synth isn't louder than the rest of the mix" comment at line 9187), but it is why two builds of "the same" song differ.

## What the owner may want to hear

- Item 5: nothing. If a held, nearly closed live bass sounds fizzy or hissy next to the same pad's render, say so: that is the open point in section 1d.
- Item 6a: after the one-line fix, a vox pad with REL at 1000 ms or more should ring on after the note ends.
