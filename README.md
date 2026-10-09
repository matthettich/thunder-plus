# ...Thunder Plus

...Thunder with **…waves** built in: a modular synth whose Out modules feed a new oscillator type in the layers. It's a separate app from ...Thunder (https://matthettich.github.io/Thunder-/), which stays as it is.

**Using …waves.** **…waves** is a third screen next to Tracker and Synth: the modular patch editor (the same as https://matthettich.github.io/waves/). The kit holds one waves patch, saved into the kit as you edit; switching to another screen pauses the editor's sound and re-renders the pads that use it. In any layer, choose **…waves patch** as the oscillator and pick an **Output** (Waves In 1–8): the layer plays whatever the patch's Out modules send to that output. The patch is played for one note, held for the pad's Gate (synth slots: up to Max len), then the layer's envelope, filters and the pad's effects apply as usual. Tune 0 st plays middle C (C4) into the patch's Host In, so Tune and the pitch envelope move the patch's pitch. Sequencers and LFOs inside the patch run during that note, at the song's tempo.

**Separate from ...Thunder.** Thunder Plus saves its kit, song and settings under its own names, so it never changes ...Thunder's. The first time it opens, it starts from a copy of ...Thunder's kit and song. Its offline cache is its own too.

**Desktop app.** The Electron build serves the app from its own `app://thunder/` address instead of opening plain files, so the …waves editor frame can load its sound engine and save into the kit as it does on the web. Build it as below (`npm start` to try it); `waves/` is copied in with the rest.

**Files.** `waves/` holds the …waves editor and engine (`waves/waves-worklet.js`, from the waves repo); Thunder Plus loads the same engine to render patches into the pads, using the Plaits, Elements and effects code already in `index.html`.

**...Thunder opens Thunder Plus files.** ...Thunder (https://matthettich.github.io/Thunder-/) is built from this app with the …waves and …chains editors hidden: `python3 tools/build-thunder.py ../Thunder-` writes its `index.html` and desktop `electron/main.js` and copies the two sound engines, so ...Thunder behaves the same on the web, on the phone and as a desktop app. Songs and kits go both ways between the apps, and …waves layers and …chains effects play in ...Thunder too. After changing this app, rebuild ...Thunder so the two stay in step.

The rest of this README is ...Thunder's.

# ...Thunder

An 8-pad drum synth and 16-track tracker for the browser, built for a Galaxy Z Fold 6, for making one-shots to use in Koala or SunVox. It uses the Minimal skin from ...Seeds.

## What it does

- **8 pads, 8 layers each.** Every layer has its own oscillator, pitch envelope, amp envelope (A·H·D or ADSR), low-pass and high-pass filters with an envelope, delay and clap-style bursts.
- **Oscillators**
  - Standard: sine, triangle, saw, square, pulse, click
  - FM: 2-op, feedback, metal, cross-mod
  - Additive: harmonic, odd, membrane, bell, bar, chord
  - Noise: white, pink, brown, blue, 808 metal, digital LFSR, crackle, sample-and-hold
  - Plaits: all 24 models of Mutable Instruments Plaits (synth voices, 6-op FM, chords, speech, strings, modal, drums), run from the original code
  - Elements: Mutable Instruments' modal voice, with bow, blow and strike exciters and modal, string or strings resonators
  - Samples loaded from audio files
  - PWM sampler: a sample (or a built-in cymbal) played like old hardware, with a clock of 2–32 kHz that follows the pitch and no smoothing, through 1-bit PWM (PC speaker / Atari), NES DPCM, or 2–8-bit PCM (6-bit is the TR-909 cymbal sound). Load sample on a PWM layer keeps its clock and mode
- **Samples.** Load an audio file into any layer with Load sample, then pitch, reverse and filter it like any other layer.
- **Export.** Mono WAV, 44.1k or 48k, 16 or 24-bit, normalised and tail-trimmed. Save one pad or all 8 as a zip, or share straight to Koala on Android. Optionally add the note to the file name (for example `03 Pluck C3.wav`).
- **Effects per pad.** Up to 6 in any order, with a tail length: Rings resonator, Clouds (granular, stretch, delay, spectral, with a hold-the-tail switch), Airwindows Density saturation and ToTape6, DaisySP chorus and bitcrush.
- **MIDI.** Plug in a controller, tap ⚙ Settings → MIDI → Connect MIDI. Pads 00–07 answer notes 36–43 with velocity; other notes play the selected synth. **Learn**: tap any of the 16 pads and hit a key to give it that note (synth pads hold while the key is down), or touch any slider and turn a knob to put it on that CC. A learned knob works that slider for whichever pad, layer or effect is open, and shows its CC next to the slider's name. **Unlearn** clears the selected pad or slider; **Reset all** goes back to the defaults. Mappings are kept on the device.
- **Kits.** Kits save in the browser automatically and can be saved or loaded as `.json`, recorded samples included.

**Pad effects.** Each pad can have up to 6 effects, run top to bottom after the layers: EQ, Compressor, Reverb and Tape delay (Airwindows EQ, Pressure4, Galactic and TapeDelay2, the same as the tracker's track effects), Rings, Clouds, Saturation, Tape, Chorus and Bitcrush. Each effect card has a level meter showing the peak coming out of that effect while the pad plays, with the last peak in dB beside it. Reverb and delay keep the dry hit at full level until their Mix passes the half-way point.

## Settings

The top bar has two menus that work like ...Seeds': **File ▾** and **⚙ Settings ▾** each open a dropdown under the button. Only one is open at a time; tap outside it, tap the button again, or press Esc to close it.

- **File ▾**: Save project, Save as… and Load project…, with their keyboard shortcuts (plus Export / Import chains; on the …waves and …chains screens it drives that patch instead).
- **⚙ Settings ▾**: one scrolling list with a section per topic, ruled off from each other: **Kit** (save, load, factory, clear), **MIDI**, **Export**, **Skin** and **Audio**. On the tracker, a **Project** section comes first with everything on the Project screen as touch controls (checkboxes, dropdowns, sliders with − and +, buttons), grouped into Input, Screen, Song, Render, Project file and App. On the …waves and …chains screens a section for that patch comes first instead. The Project screen is still there and changes the same settings.

**Colourful boxes.** Everything you press at the bottom of the screen uses the look of the tracker's PAD-mode nav pad: a darker grey panel, each button in one of the screens' pastels on a white offset block. That's the nav pad, the command row, the KEYS-mode keypad (PLAY is always green, sharps stay black) and the 16 pads on the synth page.

**Audio** picks the output device (Chrome, Edge and the desktop app on a computer; Android plays through the system's output, so pick it in the system's media output switcher), the overall level, the latency (Lowest, Balanced or Safest, for when you hear crackles) and the sample rate (Device, 44.1k or 48k). The output and level change at once; latency and rate need **Restart audio**, which reloads the app with your song and kit kept. **Show device names** appears when the browser only numbers the outputs (it asks for the microphone once, just to get the names, and closes it). **Test sound** plays a short tone.

**Tracker display settings** (in the menu and on the Project screen):

- **Hide chains page** (on by default) hides the chain screen; see Tracker below.
- **Display steps as decimal** (on by default) numbers the rows in decimal: song rows 000–255, phrase rows 00–31, chain rows 00–15, the arrange overview too. Off shows hex. Phrase, chain and instrument numbers and values stay hex.
- **View neighboring phrases** (off by default): on the Phrase screen, a faint, read-only copy of the notes the tracks either side play at the same song row, the left track's in violet before the note column and the right track's in mustard after it, so you can see the parts around the one you're editing.
- **Follow playhead** (off by default) scrolls Song and Phrase with the playhead while the song plays; the cursor rides along and Phrase switches to whatever the cursor's track is playing.
- **Show side panel** (on by default): the grey column at the right on wide screens. Turn it off for more room.
- **Text size** (60–200%) makes the tracker's text and lines bigger or smaller. Bigger shows fewer rows; when a line gets wider than the screen it scrolls sideways to follow the cursor.

## Skins

**Settings → Skin** (or SKIN in the tracker's settings and Project screen) switches between the ...Seeds skins: Pastel (follows your system), Pastel light, Minimal, Minimal colors, Monotone, Minimal black, Minimal colors black and Neon. The choice is remembered.

## Synth slots

The kit has 16 slots. 1–8 are the drum pads (one-shots). 9–16 are synths for melodic and harmonic parts, starting as Bass, Pluck, Keys, Pad, Lead, Bell, Organ and Sub. A synth note holds at its sustain level for as long as it is held (up to the slot's Max len), then fades over the longest release of its ADSR layers. They are tuned so C-4 plays middle C. On the synth page, the computer keyboard and the pads hold synth notes while pressed (hold several keys for a chord); Space and Shift+number play them for the slot's Gate; MIDI notes that aren't mapped to a drum pad play the selected synth, held until note-off. Exports still make one-shots (the Gate length), named with their note if you turn that on. Kits saved before the synth slots load with the factory synths in 9–16.

## Playing pads from the computer keyboard

On the synth page the keyboard plays the selected pad chromatically: Z to / is the lower octave (S D G H J for sharps), Q to P the upper (2 3 5 6 7 9 0 for sharps). C-4 is the pad at its own pitch; − and = (or numpad / and *) change the octave. Shift+1–8 or ◀ ▶ pick a pad (◀ ▶ go through all 16 slots), Space plays it as it is. On a synth slot, notes hold while the keys are down.

## Tracker

Press **?** (or F1, or the **?** button at the top right) for a help screen with every key and a guide to how the tracker works.

Tap **Tracker** at the top for a sequencer that plays the kit. 16 tracks:

- **Song** – 256 rows × 16 tracks of chain numbers (up to 32: Settings → Song → Tracks; Song and Mixer then show as many tracks as fit and scroll sideways with the cursor, or by swiping left and right). Each track loops back to the top of its block of filled rows when it reaches an empty row.
- **Chain** – up to 16 phrases in order, each with a transpose.

**Chains are hidden by default** (Settings → Hide chains page, or Project → CHAINS). The song then works like a pattern sequence: EDIT×2 (Enter×2) on an empty song cell makes a new block that already holds one phrase (numbered the same when that's free), and SHIFT+▶ goes straight from the song to that phrase. EDIT×2 on a filled block makes a unique copy of it and its phrase. Nothing is deleted: set CHAINS to SHOWN and the chain screen is back, with everything in it.

**Arrange.** On a wide screen (the Fold unfolded, a tablet, a computer) a blue song overview sits at the right of the tracker: every filled song row as blocks, one column per track, with the playing rows lit. Tap a block to go to it (from the Phrase screen it opens that block's phrase), tap it twice to open its phrase, tap a row number to play from there, and tap a track number to mute the track. **Alt-click or hold a block to mute it**: it stays on the song with its phrase, just silent, so you can drop the hi-hats out of a section and bring them back without touching the phrases. TAP: MUTE in its header makes every tap a mute (handy on touch). Muted blocks are struck through on the Song screen too, and M toggles the block under the cursor in KEYS mode. Drag the grip on its left edge to make it wider (the blocks get bigger with it), or double-tap the grip to jump between big and normal; it remembers the size. Project → ARRANGE: AUTO (the default) shows it when the screen is at least 600 × 500, which includes the Fold 6 unfolded in either orientation; ON shows it at any size; OFF hides it. Its info line shows this screen's size.
- **Phrase** – up to 32 rows of note, volume, instrument and three effects. **ROWS** (− 16 +, at the right of the tab bar) sets how many rows the open phrase plays, 1–32; tap the number to type one. On the Chain screen it changes the phrase on the cursor's row. Press ▲ on the top row to reach the same LEN plus LPB (lines per beat for this phrase; -- uses the song's).
- **Inst** – 16 instruments. Each plays one of the 8 pads with its own transpose, volume, pan, reverse and start point. C-4 plays a pad at its own pitch.
- **Mixer** – volume, pan, mute, solo and effects per track, level meters per track (green, yellow above −12 dB, red above −6 dB), and a stereo master meter with peak and CLIP.
- **FX** – up to 4 effects per track and 4 on the master, running live as the song plays (see below).
- **Project** – tempo, LPB (lines per beat: 4 = 16ths, 8 = 32nds, 3 or 6 = triplets), TICKS (ticks a step, 6 by default), PATTERN LEN (how many rows a new phrase starts with, 16 by default; ALL PHRASES TO sets every phrase to it), swing, limiter, render settings, renders, and project files (song and kit together).

**Chords and polyphony.** The second half of the tracks is polyphonic, whatever the track count (9–16 with 16 tracks, 17–32 with 32; with an odd count the mono half gets the extra one), and the Song and Mixer screens outline them in green; the first half, outlined in pink, plays one note at a time. A phrase opened from one of them shows four note columns (N1–N4), so a row can hold a chord, and notes ring over each other up to 4 per track (a fifth steals the oldest). A note OFF in any note column releases every note on the track; on synth slots that's their release, drum pads stop at once. Tracks 1–8 stay monophonic and play only N1. Instruments 08–0F play the synth slots in new songs (older songs keep their instruments as they were). Songs saved before the chord columns open with their notes in N1.

**FX column.** Sequencer commands:

| | | | |
|---|---|---|---|
| `ARP` arpeggio (+X, +Y semitones) | `ARC` arpeggio pattern (X: up, down, up-down, random) and speed (Y ticks) | `CHA` chance the note plays | `DEL` delay in ticks |
| `GRV` / `GGR` groove: X ticks on even steps, Y on odd | `HOP` end the phrase, next one starts at row Y | `INS` play with instrument XX | `KIL` stop the note after XX ticks |
| `OFF` fade the note out after XX ticks | `NTH` play on pass Y of every X | `RET` retrigger every Y ticks, X fades | `REP` repeat the last command, adding XX |
| `RND` randomise the command to the left (alone: the note's pitch), −X / +Y | `RNL` the same as a random walk | `PSL` pitch slide | `PBN` pitch bend across the step |
| `PVB` vibrato (X speed, Y depth) | `SED` seed the random numbers | `TPO` tempo from this row on | `TSP` transpose the track from here on |
| `TIC` ticks a step from this row on (00 goes back to the project's) | | | |

Instrument commands: `VOL` volume, `PIT` pitch (signed semitones), `FIN` fine tune (cents), `PLY` 00 forwards / 01 reversed, `STA` start point, `PAN`. On a row with no note they change the note that is already playing (so do `ARP`, `PBN`, `PVB`, `KIL` and `OFF`). There are 6 ticks to a step unless Project → TICKS or a `TIC` command changes it. Not here yet: tables (`TBL`, `TBX`, `THO`, `TIC`), `SNG`, `NXT`, `RTO`, `PVX`, filter and MIDI commands. In this version `GRV` sets the groove for every track, the same as `GGR`. Songs from earlier versions are converted on load (`CUT` becomes `KIL`, `OFS` `STA`, `REV` `PLY`).

**Track effects.** Open FX from the tab, from the mixer's FX row (EDIT), or with SHIFT+▶ in the mixer (on the MASTER row it opens the master chain). Each track has 4 slots that run top to bottom; EDIT on an empty slot adds an effect, EDIT+arrows picks another type, OPTION+EDIT removes it. The settings of the slot you were last on are listed underneath, as hex values like the rest of the tracker. The effects:

- **EQ** – Airwindows EQ: treble, mid and bass, their frequencies, lowpass, highpass, output
- **COMP** – Airwindows Pressure4 compressor, with a mix control for parallel compression
- **VERB** – Airwindows Galactic reverb
- **DLY** – Airwindows TapeDelay2, timed in steps so it follows the tempo
- **SAT** – Airwindows Density · **TAPE** – Airwindows ToTape6
- **CHOR** – DaisySP chorus · **CRSH** – DaisySP bitcrush
- **RING** – Mutable Instruments Rings resonator · **CLDS** – Mutable Instruments Clouds

Renders include the effects and let reverb and delay tails ring out. Stems skip the master effects and the limiter, so they add up to the mix when the master chain is empty. Each effect costs some phone CPU; Clouds and Rings are the heaviest. The **CPU** meter in the top bar shows the load (Chrome's own audio load where it reports one, otherwise the share of real time the track effects take); it turns gold past 50% and red past 80% or on dropouts. Tap it to see which tracks cost the most.

**One finger is enough.** Every button combination works without holding two buttons:

- **Sticky keys** – tap SHIFT or OPTION on its own and it lights up and stays on for the next button (tap it again to cancel). So SHIFT, then ▶ changes screen; OPTION, then EDIT clears; SHIFT, then OPTION selects; SHIFT, then EDIT pastes; SHIFT, then PLAY plays the song. Holding still works as before.
- **Command row** – a row of buttons above the d-pad: − − / − / + / + + (change the value), ◀ PREV / NEXT ▶ (other chain, phrase, instrument or FX track), CLEAR, SELECT, COPY, CUT, PASTE and ▶ SONG.

Both are on by default on touch screens and can be turned off in Project (COMMANDS, STICKY KEYS).

**Two input modes** (⚙ Settings → Input, or Project → INPUT):

- **PAD** – a d-pad plus SHIFT, PLAY, OPTION and EDIT, used like this: SHIFT+arrows changes screen, EDIT adds (twice for a new chain or phrase), EDIT+arrows changes a value, OPTION+EDIT clears, SHIFT+OPTION selects, SHIFT+EDIT pastes. On a computer keyboard: arrows, Shift, Z = Option, X = Edit, Space = Play.
- **KEYS** – direct entry. On a keyboard: notes on Z–M and Q–I, `-`/`=` octave, `1` note off, 0–F for hex, letters for effects, Enter to add or open (on a phrase row it also picks up what's there as the current values: the row's instrument, plus the note, volume, or effect and its value under the cursor, which new entries then use; the hint line shows them after NOW), Delete to clear, Alt+arrows to change values, `[` `]` for the previous/next chain or phrase, Ctrl/⌘ B, C, X, V to select, copy, cut and paste. On a touch screen the dock turns into a keypad that follows the cursor: a piano in the note column, 0–F in hex columns, effect names in effect columns.

**Computer keyboard** (KEYS mode):

- **Screens:** Alt+1–7 for Song, Chain, Phrase, Inst, Mixer, FX, Project (Alt+0 goes back to the synth); F2 Phrase, F3 Mixer, F4 Inst. Shift+◀▶ step through every screen in tab order (Song, Chain, Phrase, Inst, Mixer, FX, Project), and Ctrl/⌘+Shift+arrows change screen from anywhere (▲ Project, ▼ Mixer), since Shift+▲▼ selects lines.
- **Transport:** F5 plays the song from the top, F6 this screen, F7 the song from the cursor row, F8 stops. Space plays this screen, Shift+Space the song.
- **Notes:** Z to / and Q to P (S D G H J and 2 3 5 6 7 9 0 for sharps), 1 or Caps Lock for note off, − = or numpad / * for octave.
- **Editing:** Delete clears the whole row and steps down; Shift+Delete clears just the cell; Backspace does the same as Delete (nothing below moves); Insert adds a blank row; Ctrl/⌘+Backspace removes the row and pulls the rows below up. { and } set the edit step (how far the cursor moves after typing). Ctrl/⌘+Z undo, Ctrl/⌘+Y or Shift+Z redo.
- **Lines and clipboard:** Shift+▲▼ selects whole lines in Song, Chain and Phrase (the first press takes the cursor's line, the next ones grow it); on a touch screen, tap the row numbers instead. Ctrl/⌘+C copies, Ctrl/⌘+X cuts, Ctrl/⌘+V pastes at the cursor row (or over a selection, from its top), Ctrl/⌘+A selects every line, Ctrl/⌘+B starts a block selection, Esc drops it. Lines copied in one phrase paste into any other phrase. A paste or cut undoes in one step.
- **Files:** Ctrl/⌘+S saves the project, Shift+Ctrl/⌘+S saves it as a new file, Ctrl/⌘+O opens one (these work on the synth page too). In desktop Chrome and Edge, Save writes straight back to the file you opened or last saved; other browsers download a copy, and Save As asks for a name first.
- **Moving:** Tab and Shift+Tab jump between the note, volume, instrument and FX columns (or tracks); Home, End, Page Up/Down; Ctrl/⌘+▲▼ or [ ] for the previous/next chain or phrase.

These screen, transport and undo keys also work in PAD mode.

**Tempo and ticks.** In KEYS mode, **OCT − / +** beside ROWS sets the octave for typed and keypad notes (tap the number for octave 4). Beside ROWS, the tracker shows the tempo and the ticks a step. While it plays they follow `TPO` and `TIC` (in gold), and the dots light up tick by tick through each step. Tap them for a reminder of where to set them.

**On a phone** the tabs take the first line under the top bar, with tempo, ticks and ROWS on the second. The buttons under the screen are taller for thumbs, and the screen's text shrinks as needed so 16 rows always fit.

**Moving a project between phone and computer.** Project → SHARE PROJECT (on phones) sends the project file, song and kit together, to Drive, Gmail, Quick Share or any app on the share sheet. On the other device, Project → LOAD PROJECT (Ctrl/⌘+O) opens it. SAVE works too: it downloads the `.json` on a phone, and writes straight back to the file on desktop Chrome and Edge, so you can keep one file in a synced folder.

**Screen colour.** Every tracker screen has its own pastel, on its tab and its button in the side map: Song blue, Chain lilac, Phrase mint, Inst peach, Mixer butter, FX pink, Project aqua. Project → SCREEN set to BY SCREEN (the default) colours the screen to match; or pick one pastel for all of them (blue, lilac, mint, peach, butter), or black. The pastels use dark text at readable contrast.

On touch, tap a cell to move there, tap it again to open or add, and drag up or down on the cursor's cell to change the value. Drag a finger up or down anywhere else on the screen (between or beside the lines, the title, the info lines) to scroll through the rows; sideways too when big text makes the lines wider than the screen. Tap a track number to mute it.

**Rendering.** Project → RENDER SONG saves a stereo WAV; RENDER STEMS saves a zip with the mix plus one WAV per track. Stems are all the same length and include each track's effects; they skip the limiter and the master effects. Rate, bits and normalising follow the Export settings.

The tracker keeps playing while you switch back to the synth, so you can change a pad and hear it in the loop.

## Running it

It's a plain web app with no build step: `index.html`, `manifest.webmanifest`, `sw.js` and the `icons` folder. Turn on GitHub Pages for this repo (Settings → Pages → deploy from `main`, root folder), then open **https://matthettich.github.io/Thunder-/** in Chrome.

## Install it

- **Android (Chrome):** open the address, tap **⋮** → **Install app** (or **Add to Home screen**).
- **Mac / PC (Chrome or Edge):** click the install icon in the address bar.

It opens full screen with its own icon and works offline once it has loaded. To update, push a new `index.html`. The installed app has no pull-to-refresh, so it checks for a new version whenever it opens or comes back to the front and shows a **New version ready · tap to update** button; Project → UPDATE APP reloads with the newest version any time. Your song and kit are kept.

## Credits

Synthesis and effects come from Mutable Instruments (Plaits, Rings, Clouds, Elements; Emilie Gillet), Airwindows (Chris Johnson) and DaisySP (Electrosmith), all MIT licensed. See `THIRD_PARTY_NOTICES.md`; build sources are in `dsp/` (the tracker's effects module is in `dsp/tfx/`).

## Desktop app (Electron)

The `electron` folder wraps the same `index.html` as a desktop app for Windows, macOS or Linux. You need [Node.js](https://nodejs.org) (the LTS version).

```
cd electron
npm install
npm start            # run it
npm run dist:win     # Windows installer + portable .exe  → electron/dist
npm run dist:mac     # macOS .dmg (build this on a Mac)   → electron/dist
```

`npm start` and the `dist` scripts copy the current `index.html`, manifest and icons into `electron/app` first, so the desktop app always matches the site. MIDI and the save/open dialogs are allowed; audio keeps playing when the window is in the background. The builds aren't code-signed: Windows SmartScreen asks once (More info → Run anyway), and on a Mac right-click the app → Open the first time.
