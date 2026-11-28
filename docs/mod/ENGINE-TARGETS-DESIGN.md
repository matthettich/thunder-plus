# MOD and tracker commands on the live engines: design (lead, 10 Nov 2026)

Owner's choices (10 Nov): one generic target set on every live engine (cutoff, resonance, filter-envelope amount, attack,
decay, sustain, release), and the tracker commands CUT / RES / ATK / DEC / REL reach the live engines' own filter and
envelope in the same pass. Facts this is built on: [`ENGINE-TARGETS-FACTS.md`](ENGINE-TARGETS-FACTS.md) (read it first).
This file is the CONTRACT between three pieces of work: the engines (E2: bass, vox, acid, mach; E3: pump synths) and
the delivery (E1: sequencer events, page player, audio-thread core). Do not change a name or a formula here without the
lead.

## 1. What the user gets

- **Commands keep the meaning they have on sampled pads**, now applied to the synth's own filter and envelope while
  the note is held: `CUT xx` sets the synth's cutoff on the same 30 Hz – 18 kHz scale (`cutHz`), `FF` (or no command) =
  the pad's own knob; `RES xx` = resonance 00–FF across the engine's range (only once a RES command has been given on
  the track); `ATK` / `DEC` / `REL xx` = that envelope time on the `decSec` scale (8 ms – 4 s), `FF` = the pad's own.
  The post-engine biquad (`liveCut`) is no longer used for an engine that has its own cutoff.
- **LFOs sweep both ways around whatever is in force** (the pad's own knob, or the commanded value): an LFO never
  clamps at "open" as `cut:t` does today on sampled pads.
- New MOD targets on a track: `env:t` (filter-envelope amount), `atk:t`, `sus:t`, `rel:t`, beside the existing
  `cut:t`, `res:t`, `dec:t`. No new tracker commands (no ENV or SUS command): those two are MOD-only for now.
- Sampled (rendered) pads behave exactly as today.
- An engine without one of the parameters ignores that target (section 4).

## 2. The carrier: twelve k-rate AudioParams on every live engine node

Declared in each live processor's `parameterDescriptors` with `automationRate: 'k-rate'`:

| Name | Default | Range | Meaning |
|---|---|---|---|
| `cCut`, `cRes`, `cDec`, `cAtk`, `cRel` | −1 | −1 … 255 | A COMMANDED value as the tracker byte; −1 = none (the pad's own value) |
| `oCut`, `oRes`, `oEnv`, `oAtk`, `oDec`, `oSus`, `oRel` | 0 | −255 … 255 | A MODULATION OFFSET in byte units, signed; 0 = none |

Why AudioParams: every live node already has them, the page drives them with ordinary automation (also in an
`OfflineAudioContext`, so renders match), and the audio-thread core drives them through its existing `__tcOv` /
`Auto` overrides by name with no new protocol. In the C++ port they become per-voice-node control inputs.

A processor reads each once per render quantum as `P.name[0]` (when a non-constant override arrives as a 128-sample
array, the first sample is used: quantum resolution, about 2.7 ms at 48 kHz).

**When all twelve are at their defaults the processor must take exactly the code path it takes today**, so output is
bit-identical (the engine goldens and existing tests prove it). Test `any = cCut >= 0 || … || oRel !== 0` first.

## 3. Effective values (computed in the processor, each quantum, only when `any`)

`own` = the pad's stored parameter (what the knob shows). **Never write an effective value into the stored parameter
object** (`this.c.p`, `this.p`): the knob value, `{set}` merges and saved songs must not change. Apply on a per-quantum
copy, or swap the fields in and restore them after the engine's block (whichever is smaller for that engine; no
allocation per quantum: reuse one scratch object).

With `K = 18000 / 30` and `D = 4 / 0.008`, engine limits `lo`, `hi` taken from the engine's own parameter range:

- **Cutoff (Hz engines):** `base = cCut < 0 ? own : 30 * K ** (cCut / 255)`; `eff = clamp(base * K ** (oCut / 255), lo, hi)`.
- **Cutoff (0–1 engines, e.g. vox `bright`):** `base = cCut < 0 ? own01 : cCut / 255`; `eff01 = clamp(base + oCut / 255, 0, 1)`.
- **Resonance:** `base = cRes < 0 ? own01 : cRes / 255`; `eff01 = clamp(base + oRes / 255, 0, 1)`, then to the engine's unit
  (`own01` is the engine's resonance normalised over its range; where the engine squares or scales it internally, that
  stays inside the engine).
- **Filter-envelope amount:** `eff = clamp(own + (oEnv / 255) * (hi - lo), lo, hi)` (signed ranges keep their sign).
- **Attack / decay / release (seconds):** `base = c < 0 ? own : 0.008 * D ** (min(c, 254) / 254)`;
  `eff = clamp(base * D ** (o / 254), lo, hi)`. Engines store ms: convert at the edges.
- **Sustain:** `eff01 = clamp(own01 + oSus / 255, 0, 1)`.

An envelope time changed during a stage affects the stage that is running from then on (the engines read times per
block): that is intended.

## 4. The per-engine map (the engine's own parameter names; "–" = ignored)

| Target | bass | mach | acid | vox | pump live synths |
|---|---|---|---|---|---|
| cut | `cut` (Hz) | `lp` (Hz) | `cut` (Hz) | `bright` (0–1) | the type's cutoff symbol (`cutoff`…); types without one: – (they keep `liveCut`) |
| res | `res` | `res` | `res` | – | `res` / `reso` |
| env | `fenv` | `fenv` | `envAmt` | – | `filter_env` / `env_cut` |
| atk | `atk` | `atk` | – (fixed 2 ms) | `atk` | `a_a` / `attack` |
| dec | `dec` | `dec` | `ampDec` | – | `a_d` / `decay` |
| sus | `sus` | `sus` | `sus` | – | `a_s` / `sustain` |
| rel | `rel` | `rel` | `rel` | `rel` | `a_r` / `release` |

Exact names, units, ranges and curves per engine and per pump type are in the facts file (sections 1–5); where a row
here disagrees with the code, the code wins and the builder reports it. `dec` is the AMP decay (as DEC is on sampled
pads); the filter-envelope decay (`fdec`, acid's `dec`) is not a target. Pump kit types, fold, coil, tract, freeze and
…waves live have no cutoff / envelope: they ignore everything and keep the post-engine `liveCut` biquad for CUT / RES.

## 5. Delivery (E1; after the engines exist)

- `makeSeq`: for a track whose notes go to a live engine, each step emits an `eng` event when anything changed:
  `{type:'eng', t: track, at, c:{cut,res,dec,atk,rel} (byte or −1), o:{cut,res,env,atk,dec,sus,rel} (signed bytes)}`.
  `c` comes from the track's sticky command state (`T.cut`, `T.res` once a RES command was given, `T.dec`, `T.cAtk`,
  `T.cRel`); `o` from the LFOs / scenes / macros on `cut|res|env|atk|dec|sus|rel:t`, as `v_unclamped − centre`, with
  4 points a step live and 2 in renders (ramps between points), not 1.
  `PF.pno` keeps being written exactly as today for sampled pads (their behaviour does not change).
- Page player: on `eng`, for every live node of that track (`bassN` entries with that track), set the twelve params
  (`setValueAtTime` for `c*`, `linearRampToValueAtTime` between points for `o*`). Also at note start, so a node
  created later starts from the current state. `liveCut` is skipped for kinds whose map has a `cut` row.
- Core: the same event sets `ov.p[name]` `Auto` timelines on every registered live node of that track.
- `tgtOk` / `tgtList` / `tgtLabel` / `tgtOwn`: add `env`, `atk`, `sus`, `rel`; the MOD screen lists the seven on a
  track that plays a live engine.
- Tests: a held bass note's cutoff moves under an LFO (render through the page path offline and through the core);
  CUT / ATK / REL commands change a held and a new live note; sampled pads unchanged (existing goldens); songs with no
  commands or LFOs: identical event streams and bit-identical audio.
