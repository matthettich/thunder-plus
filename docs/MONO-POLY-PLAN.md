# Plan: separate MONO TRACKS and POLY TRACKS counts (not built: waiting for the owner's go-ahead)

Owner's ask of 14 Nov ("nice to have: plan it and show me before building"). Planned by an agent on build 602, checked by the lead.

## What you would see

- Settings gets two rows, **MONO TRACKS** and **POLY TRACKS**, in place of TRACKS (for example 4 mono and 12 poly). Today the first half of the tracks are mono and the second half poly.
- Every existing song opens exactly as before: 16 tracks read as 8 mono + 8 poly.
- Changing either count renumbers the tracks after it. Song rows, mixer, effects, buses, side-chain sources, chains, Perform (LFOs, locks, scenes, macros, parts) and a voice per track all follow, with the code that moves them for TRACKS today. Making a count smaller hides tracks and keeps their notes, as today.
- A song saved with an uneven split and opened in an OLDER build keeps all its notes, but that build treats it as half and half, so some tracks show as the wrong kind there.

## How it works today

- The total is `S.ntr` (in the song, 8 to 24 with a voice per track, else up to 32). The split is one rule: `polyFrom() = ceil(ntr / 2)`, `isPolyT(t) = t >= polyFrom()`. Nothing else is stored.
- `ntrPerm(n0, n1)` builds the renumbering (mono tracks stay, poly tracks move past new mono ones, removed tracks are parked past the count so growing brings them back); `ntrRemap` applies it to the song, mixer, effects, chains, side-chain picks and voices, and `perfRemap` to the Perform data; `setNtr` fixes cursors and re-routes the sound.
- `polyFrom` / `isPolyT` are read by the voices, both sequencers (`makeSeq` and the generated `core/seq-core.js`), Pattern view, the generators, MIDI import, the track labels, and the Arrange / Song group boxes. Buses, sends, MIDI out, stems and renders do not depend on the split.

## The change

- Keep `S.ntr` (the total). Add `S.nmono` to the song, written only when it differs from the half rule, so untouched songs stay byte-identical and old builds read them. `polyFrom = S.nmono ?? ceil(ntr / 2)`. Limits: total 8 to 24 (32), each count 0 to the total.
- `ntrPerm(n0, m0, n1, m1)` takes the two counts; `setTracks(mono, poly)` beside `setNtr` (kept as a wrapper). Nothing is deleted when a count shrinks; one short notice says which tracks were hidden. One undo step.
- Two Settings rows with the total in their info lines.
- **When the total is full** (CodeRabbit on #140): hidden tracks are parked in the unused slots past the count, so they only fit while the total leaves room (32 slots in all). Rule: a count may not grow into slots that hold hidden tracks WITH notes, mixer settings, effects, a chain, a voice of their own, or Perform data (LFOs, locks, scene or macro targets, part values: they follow the track, so a track that holds only an LFO is not empty). The row stops there and its info line says why ("8 hidden poly tracks still hold notes: clear them or lower MONO first"); hidden tracks that are empty are simply dropped. Nothing is ever stored outside the 32 slots, so the song file needs no new field for it.
- **Hidden tracks keep their kind** (CodeRabbit on #142): the parked slots are two ordered runs, hidden mono tracks first and hidden poly tracks after them, and the song stores how many of each (`hmono`; the rest of the parked slots are poly). Growing MONO takes back the most recently hidden mono track first, never a poly one, and the other way round. Required test for stage 1: 8/8 → mono 6 → poly 6 → mono 8 → poly 8 gives back every track with its own notes, in its old place.

## Build order (three PRs; 1 and 2 ship together)

1. **Data** (agent, about 120 lines): `nmono` in the song with load / save / old-song default, `polyFrom` reading it, `ntrPerm` / `setTracks`. Invisible: the default equals today.
2. **Engine** (lead, about 40 lines): `polyFrom` in `makeSeq`, regenerate the core, bump `CORE_V` / `CORE_WANT` so a cached old core is refused, a test that page and core agree on which tracks are poly.
3. **Settings rows** (agent, about 120 lines): the two rows, info lines, the notice, MORE TRACKS growing only the kind it needs, help text.

## Risks

- The page and the audio-thread sequencer disagreeing on which tracks are poly (pinned by a test and the core version bump).
- A count of 0 (all mono or all poly): the voice and Pattern-view code assume at least one mono track and need guards.
- Changing a count while playing re-routes the sound, as TRACKS does today, and may click.

## Questions for you (the plan's suggested answers)

1. May either count be 0 (all mono, or all poly)? Suggested: yes, with at least 8 tracks in total.
2. Should making a count smaller ask first? Suggested: no (notes are kept), a short notice instead.
3. Should saving an uneven song warn about older builds? Suggested: no; it is written down here and in HANDOVER.
