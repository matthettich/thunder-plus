# Thunder Plus test checklist, by priority
> Snapshot of the owner's Claude Doc "Thunder Plus test checklist, by priority" (https://claude.ai/artifact/LrzQ18ur18Fc5gkvxmQDbK), exported 14 Nov 2026 with his ticks as of that morning.
> The live list is the Claude Doc; the source of every item is "Owner's punch list" in HANDOVER.md (build numbers are in brackets).
> Re-export this file when the doc changes.

14 Nov 2026 · Matt

## How to use this list

This list is in order of importance, so start at the top.

Do section 1 first; it is short and covers what could lose work or sound, plus the fixes from your feedback.

Do sections 2 and 3 as time allows.

Section 4 can be skipped unless something looks wrong; tell Claude "close the skip list" and those items are closed as not tested.

The Parked items are open but block nothing, and the Set aside table is probably superseded.

If something fails, tell Claude the build number and what happened; tick an item when it passes.

| Section | Open items | Mostly on | Why it matters |
| --- | --- | --- | --- |
| 1. Do these first | 31 | Phone and computer | Lost work or sound, and the re-checks of your last fixes |
| 2. Really worth trying | 33 | Phone | Everyday features nobody has tried on a real device |
| 3. If you have time | 47 | Phone, then gear | Secondary features, by-ear comparisons and hardware |
| 4. Safe to skip unless something looks wrong | 55 | Mixed | Cosmetic, older, or already covered by automated tests |
| Parked | 6 | Phone | When the native apps start; whether three optional things get built |
| Set aside | 5 | none | Nothing: probably superseded |

## 1. Do these first

### New in build 586: the phone pass

*The phone version of …chains and …waves. Play / Edit is the part most worth your time.*

- [ ] Folded phone: tap Play in the top bar. Can you play a patch with one thumb (sliders, dropdowns, keys) without ever moving, folding or deleting a module? Is it obvious which mode you are in? Should the phone start in Play or in Edit? (586)
- [ ] Folded phone: drag a slider sideways: it moves from where it was instead of jumping to your finger; slide your finger up or down away from it while dragging for fine control; double-tap resets it. (586)
- [ ] Folded phone: drag up or down starting on a slider: the patch pans instead of the value changing. Does the sideways-versus-up-down choice feel right? (586)
- [ ] Folded phone: press and hold a module's title bar: its menu opens (Group, Copy, Duplicate, Delete). Hold a slider: Reset, Type a value, MIDI learn. Hold empty space: Add a module here. (586)
- [ ] Folded phone: tap a value once: the box to type a number opens, and the number you type is what you get. (586)
- [ ] Folded phone: the top bar is one row (name, Play / Edit, +, undo, Fit, …), and File, Settings, Help, MIDI learn, Select and zoom are all under the … button. Anything you reach for that is now too far away? (586)
- [ ] Folded phone: … then Select: tap several modules, or drag a box around them, then move them together. (586)
- [ ] Folded phone: a very long module (pump GATE in …chains) opens folded into sections; open one section at a time. (586)
- [ ] Folded phone, …waves: the on-screen keys show a whole octave, and the octave buttons move it up and down. (586)
- [ ] Folded phone: opening an editor no longer puts a tip box over the patch; the Tips button at the bottom left opens it. (586)
- [ ] Unfolded phone: the same Play / Edit switch and long-press menus work, and the top bar still shows everything. (586)
- [ ] Computer: with a mouse nothing above changed how the editors behave; Alt+E switches Play / Edit. (586)

### Re-check the fixes from your feedback

*These check the fixes made from your 13 Nov results.*

- [ ] Computer and phone: double-tap or double-click a cable: it is removed and "Find a module" does not open. (584)
- [ ] Computer: collapse a Stereo mixer with many inputs patched: nothing is cut off and every jack sits inside the frame. (583)
- [ ] Computer: a modulation cable into a collapsed module ends on the left, below the audio inputs, with the parameter's name beside it. (583)
- [ ] Computer: drag an LFO to the small jack beside a mixer's Pan slider: it connects on the first try. (584)
- [ ] Computer: Help shows Signal kinds as three tidy rows; choosing "Modules" in Help scrolls to that section and does not open a second editor. (584)
- [ ] Computer: in Chains and Waves the File menu starts with New project and has no Undo / Redo; Edit has them; the Tracker says "New project" too. (584)
- [ ] Computer, inside Thunder Plus (chains): MIDI-map a slider and put another in the bottom panel, then collapse the module; a small "CC" and a dot show on its summary line. (579) (could not test MIDI)

### New in build 585: the editors' small fixes (pass 2)

*These decide whether the phone pass for …waves and …chains can start.*

- [ ] Computer: double-click a value and type one (try a delay Time in ms, a filter Cutoff in Hz or kHz, a Mix in %): the value shown is the value you typed. (585)
- [ ] Computer: drag a slider once, then Undo once: it goes back to where it started in one step. (585)
- [ ] Computer: a slider or dropdown that sits under a cable still responds to a click. (585)
- [ ] Computer, Waves view inside Thunder Plus: Voices and On-screen keys can be reached. (585)

### Could lose work or sound

*A failure here means lost notes, a lost save, a hang, or no sound.*

- [ ] Phone, Mobile setup: new song, SHIFT+▶ on the empty Arrange cell, type notes, PLAY with FOLLOW ON for over 5 s; the notes stay on screen, are on song row 000 and play (the lost-notes bug). (571)
- [ ] Phone and computer: Reset audio engine when healthy gives a toast and no dialog; after a runaway on the Fold a "Reload the app?" dialog appears and Reload brings back song, screen and kit; "Not now" leaves everything; with a recording running it refuses or asks first. (563)
- [ ] Phone, runaway kick retest: simple song with the delay, Page sequencer, PLAY, screen off, press a volume key, wait 15 s, screen on, STOP, Copy report; a short gap at the kick, then the song carries on (report: kicked 1x, moved on 0x). (557)
- [ ] Phone: leave the app stopped for 2 minutes, then tap a pad and PLAY; the first pad sounds at once, PLAY starts cleanly, a long reverb tail is not cut off. (553)
- [ ] Any device: reload a song with samples and Save to a file at once (it waits or asks); reopen the file; every sample plays. (552)
- [ ] Computer, Audio thread sequencer: STOP late in a long song (row 60+) and RESUME; no freeze, and it continues exactly where it was. (552)
- [ ] Computer: unplug and replug the interface, then Reset audio engine; if the engine will not run you get the dialog; say whether the desktop app's own reload fires. (563)
- [ ] Computer: sleep and wake (Chrome, then the Mac and Windows apps once built); after waking, PLAY makes sound without a reload. (552)

## 2. Really worth trying

### On the phone, playing a song

*Normal playing on the Fold; a failure here gets in the way every session.*

- [ ] Phone: PLAY, lock the screen, press a volume key; is the slider the MEDIA one (no vibration), does the sound carry on, and is there a media control on the lock screen? Then Copy playback report. (558)
- [ ] Phone, Audio thread sequencer, song with patterns of different lengths: turn the screen off and on mid-song, and STOP / PLAY with RESUME; no track jumps back to an earlier row. (571)
- [ ] Phone, Background play on: play 2 minutes on Song, Phrase, Mixer and FX; every readout still updates live (BPM, rows, headings, VOL / PAN, sliders), nothing stale after changing track, instrument or tempo. (563)
- [ ] Phone, Audio thread sequencer, long song playing: hold ▲ then ▼ on Song / Arrange; the cursor keeps up and the audio does not lag or glitch. (569)
- [ ] Phone: stop a song and tap PLAY again at once; it plays from the cursor and never jumps to the top (hold ▲ or tap the first Arrange block to go there; say if you want a one-tap way). (576)
- [ ] Phone, ELECTRO playing, Settings, Screen, Meters: Light steps the Mixer meters about 10 times a second and a drum hit still shows; Off keeps them empty and they return at STOP; Smooth as before. Does the phone run cooler with Light or Off? (576)

### Synth page rework (builds 578 and 582)

*The rebuilt Synth page; nobody has tried it on a real device yet.*

- [ ] Phone and computer: the engine picker is reachable without scrolling with the Pad / Sequence / Tuning folds closed and open; the folds remember open or closed after a reload. (578)
- [ ] Folded phone (360 wide): labels read in full on Layers, Bass, Machine, Drift and Blip; sliders are wide enough; no value is cut off. (578)
- [ ] Phone and computer: the Synth page takes the skin's colour in IDM pink & green, IDM and one Low power skin. (578)
- [ ] Phone: the Synth page buttons are comfortable to hit with a thumb. (578)
- [ ] Folded phone with Waves and Chains shown (Settings, App): the tabs read Tracker, Synth, Waves, Chains in full and do not scroll. (578)
- [ ] Computer at 1280 wide: the top bar is one row on the Synth and Tracker views with everything still there. (578)
- [ ] Phone or computer, Synth page, Sample layer: Reverse is two buttons (Off / On); double-click the word to reset to Off; right-click offers Reset to default and Learn a MIDI knob; a learned knob moves the buttons; same on a Digital layer's Loop (Long / Short). (582)
- [ ] Phone and computer: select a different pad; the waveform and "in use" line change to the new pad, and "rendering…" shows only briefly if at all. (578)
- [ ] Phone, and computer with keyboard: the pickers (layer type, LFO shape, Add effect) open a list, pick, and close; on the computer arrow keys step the value and Enter / Space opens the list. (578)
- [ ] Phone and computer: two-state rows (Loop, Reverse, Freeze…) are two buttons; right-click on one offers no "Type a value…". (578)
- [ ] Phone and computer: names read right: Pad (drum and synth), Cutoff, Macro osc and Resonator (the old layer types), kHz and ct. (578)

### On the phone, editing

*Cursor, Arrange and tracker behaviour while you write a song.*

- [ ] Phone, FOLLOW ON then FOLLOW PATTERN, playhead looping on the top rows: hold ▼ over 5 s; the cursor runs past the bottom, the view pages with it, nothing jumps back. (569)
- [ ] Phone, ON STOP at STAYS: PLAY from the pad, ▼ a few rows, STOP before the playhead reaches row 2; the cursor stays where you put it. (569)
- [ ] Phone and computer, Phrase and Pattern view playing: playing-row marks and per-track bands move with the sound (FOLLOW off / PATTERN / ON, after a HOP, across a song wrap); typing, selecting and moving the cursor feel instant. (575)
- [ ] Phone: tap the small |◁ in Arrange's top-left corner; cursor and Arrange go to row 000 on the same track, also while playing, nothing starts or stops, and the view is not pulled back to the playhead; try on Song and beside Phrase, in IDM and a Low power skin; is it big enough to hit? (577)
- [ ] Phone, both sequencers, a song where one track's blocks start a few rows down and another has a gap: let it play past the end twice; every track comes round on its own rows, the gap track returns after its gap, nothing jumps to another section. (570)
- [ ] Phone unfolded, song playing, Phrase or Pattern view with Arrange beside it: Arrange looks exactly as before (cursor and playing-block outlines, clipping) and the screen feels smoother than 576. (577)

### On the computer

*Everyday desktop features that have not been tried on a real machine.*

- [ ] Any device: on Phrase, Mixer and FX, the top-bar Play plays the whole song from the cursor's song row, and stops when pressed again. (572)
- [ ] Phone unfolded or computer: the control bar's ▶↻ plays with LOOP lit; LOOP mid-song carries on as normal play with no jump; ▶↻ again stops; pad PLAY and Space play plain. (570)
- [ ] Computer: double-click BPM or LPB in the transport and type a value; it takes the edit and shows the new number. (563)
- [ ] Computer: render a song where one track has a gap and another comes in on a later row; both are in the WAV in the right place. (571)
- [ ] …waves inside Thunder Plus: open the screen and play a key; sound starts on the first note (a short first-time delay is expected); held keys, MIDI notes and voice count changes work; leave and return: still plays. (548)
- [ ] Computer: open a chains patch and a waves patch that use Flex and Step gate (one on its own custom steps, one with something patched into Pattern); they play; Flex SCRATCH may be a little shallower, anything else wrong is a bug. (562)

### Live synths and held notes

*Commands and filters on held notes; by ear.*

- [ ] By ear: on a long bass (or acid / machine) note, CUT 30 then CUT FF a bar later goes dark and opens again without retriggering; RES, ATK, DEC, REL likewise. (555)
- [ ] By ear: MOD on a live synth: the TARGET list has CUT, RES, DEC, ENV, ATK, SUS, REL; an LFO on CUT sweeps the held note; try ENV and SUS; steppy or clicky at fast rates? (555)
- [ ] After playing with commands and LFOs, the pad's own cutoff and envelope knobs read what you set. (555)
- [ ] By ear: the same on …pump live synths (mono, poly, orbit, gloss, wob: filter and envelope; coil, tract, freeze: envelope only). (555)

## 3. If you have time

### On the phone: screen off, background play and sound

- [ ] Phone, Audio thread sequencer: after a clock hiccup message, keep playing or stop; 10 to 20 s of steady clock later the report says "audio clock steady again" and the next PLAY is on the audio thread again. (573)
- [ ] Phone: switch Background play off (a note says it is for testing only), PLAY, lock the screen a few seconds, come back; a message offers "Turn on" and tapping it turns it on again. (560)
- [ ] Phone: turn Background play back on (Reset audio settings does it), then repeat the screen-off and locked-screen volume key tests and Copy playback report; its third line lists the settings and media session lines appear at PLAY. (559)
- [ ] Phone, headphones or monitors, quiet song: can you hear or measure a constant 40 Hz tone at about -60 dB while playing? If it bothers you it can go lower or off. (558)
- [ ] Phone: after a runaway, do pads feel late (report header 389 to 391 ms output delay vs 43 before)? Does reloading fix it? (557)
- [ ] Phone: PLAY, screen off, then let another app make sound (a notification or video); does Thunder keep playing? The report shows whether background play paused and restarted. (552)
- [ ] Phone, Audio thread sequencer (only if you use it): a very slow song (e.g. 20 BPM at 1 LPB) keeps playing instead of stopping after 3 s. (567)
- [ ] Phone, Audio thread sequencer (only if you try it): fewer or no crackles than before. (566)

### On the phone: notes, stutter, effects and the CPU panel

- [ ] Phone: new song, type three notes into a new pattern, PLAY at once; the notes are still there, also after reloading. If it hangs, did the app reload or did the notes vanish? (570)
- [ ] Any device: add the first effect to an effect-free song while it plays; it is heard without a reload (report any brief click). (545)
- [ ] Phone: the stutter on a pad with a sample that stopped under LOOP (MUT); is it better? (545)
- [ ] Phone: the song that stutters with Arrange showing: play it once, STOP, Copy report (slow and long frame lines); does it use MUT, CUT, RES or DEC on sample pads? (New on 10 Nov)
- [ ] Phone, song playing, on Mixer, Inst, FX, MOD and Project: everything looks as before and nothing is stale after a tap or edit; a pattern-command value may show up to half a second late; say if that bothers you. (575)
- [ ] Phone: does it run cooler / longer on battery, and does the CPU % read lower on Mixer and Pattern view than on build 574? (575)
- [ ] Phone, song playing: tap the CPU %; heaviest parts first, a line on whether it is keeping up, one "Try", and the two buttons work; say what is unclear. (The note that screen drawing is not measured is out of date: build 575 adds "The screen".) (573)
- [ ] Phone: open a song saved under a file name and PLAY; the notification shows that name and the app's icon; rename while playing and it follows. (573)
- [ ] Phone: with Low power (hot pink) or (mustard), turn Low power display ON (the skin stays) and OFF (back to your earlier skin). (568)
- [ ] MOD on the Fold: touch hold-and-drag on SHAPE / RATE / DEPTH works; tabs fit folded. (9 Nov)

### On the computer: playback and audio

- [ ] Computer: the first pump or track effect added while playing causes no click or stumble. (552)
- [ ] Computer: reload and make PLAY your very first click; the song starts cleanly after under a second's pause; later PLAYs start at once. (548)
- [ ] Computer, Audio thread sequencer, a busy song for 5 minutes: sounds exactly as before; Flex, Step gate and Retrigger in time; fades and glides on live engines in time. (567)

### By ear: effects, sound and modulation

- [ ] Phone and computer, by ear: add Lite reverb to a drum track and an acid line; try its six presets and the extremes of its knobs; a usable small room to long wash, no clicks, no unwanted ringing, silent when nothing plays; on the phone with four tracks at once, each should be a few percent at most in the CPU %. (574)
- [ ] By ear: the 20 new Acid presets (Phuture 303 … Wow siren), 7 Bass and the new ones on other synth engines; say which to keep, cut or retune. (574)
- [ ] By ear: MOD on a …pump EQ band (right-click FREQ or GAIN, Add LFO…): that band sweeps, others stay; a second LFO on another band works; MOD lists only bands in use; same for BAND / WIDE / HEAT. (556)
- [ ] By ear: MOD on the master out (VOL · master out): the whole mix swells and dips, the limiter is not pushed harder, and OUT reads what you set after STOP. (556)
- [ ] By ear: a rendered song with fast LFOs sounds like it did live. (556)
- [ ] Old songs that used CUT / RES on a live synth now use the synth's own filter; does any song sound different in a way you do not want? (555)
- [ ] Scenes / macros on CUT for a live synth with no CUT command: the value is an offset from the knob (FF = the knob, lower closes); OK, or should it be absolute? (555)
- [ ] By ear: MOD on a …pump effect knob (Add LFO…): it sweeps while the song plays, returns to your setting on STOP, the knob shows \~; zipper noise at fast rates? (554)
- [ ] Phone and computer, by ear: stop a song with the master limiter and compressor on, wait 5 s, PLAY; the first note is at normal level, no dip or pump; meters fall to silence when stopped. (563)
- [ ] By ear: a track with Flex or Step gate, song stopped over 14 s, then PLAY; the pattern starts clean, no stale audio. (563)
- [ ] By ear: a Vox pad with REL at 1000 ms or more rings on after the note ends; old Vox songs sound right (long-REL notes ring longer than before). (563)
- [ ] Phone and computer: play two or three songs you know (one with …pump synths, one with a chains or waves track); they sound as before and no "stopped working" toast. (561)
- [ ] Play the same 15 s on two settings (Safest vs Balanced), STOP, Copy report, compare the "level over this play" lines (under about 1 dB is nothing). (554)
- [ ] By ear: reversed pads, pads with CUT / RES / DEC, stereo pads with moving pan, bass, vox and pitch effects sound as before. (553)

### Files, settings and recording

- [ ] Settings, Audio, Reset audio settings: change a few audio settings, press it, confirm; they return to default and song, skin, output and MIDI setup are untouched. (551)
- [ ] Record a sample: pick an input and mode, change a REC / metronome setting, reload; the choices are still there. (549)

### Needs gear plugged in (do these in one sitting)

*Audio interface, MIDI gear, Windows or Ableton Link needed.*

- [ ] Web, hardware synth: PLAY a MIDI track, hide the window, STOP (or come back and STOP at once); no notes play on. Say if drum gear sounding booked hits for up to 1.2 s matters. (548)
- [ ] Computer with the audio interface: idle 5 minutes (no MIDI keyboard, MIDI clock out or waves / chains screens open), then play; any click from the interface or late first note? (553)
- [ ] Computer: with your interface as Output, unplug it while stopped; a message says it plays on the system default; plug back: "is back" and sound returns. Repeat while a song plays. (552)
- [ ] Computer: change the system default device (once with System default chosen in the app, once with a specific device); sound follows or stays as chosen, no reload. (552)
- [ ] Computer: Reset audio engine with the interface chosen; sound comes back on the interface (say if a reload was still needed). (552)
- [ ] Computer: play a key on the …waves screen; it sounds on the chosen output. (552)
- [ ] Computer: the desktop v1.3.0 helper checklist (claude.ai artifact link in the file), including monitoring after reopening the window and the clock in, which seemed slightly out of sync. (Carried over)
- [ ] Windows: recording (WASAPI) and MIDI (WinMM). (Carried over)
- [ ] Ableton Link: CI build on Mac / Windows, then against Live or Koala. (Carried over)
- [ ] Hardware MIDI tests. (Carried over)

## 4. Safe to skip unless something looks wrong

These can be closed in one go: cosmetic details, older builds that a later build reworked, and items an automated test already covers.

- [ ] Phone: simple song with the synced delay, screen off: do the repeats still bend in pitch while the song is held? (552)
- [ ] Phone: after waking from a locked screen, do pads feel late until Reset audio engine? (551)
- [ ] Phone, your normal song with effects, Page sequencer: PLAY, screen off 20 to 30 s, back, STOP, Copy report; does it keep its tempo (gaps expected, rushing not)? (546)
- [ ] Phone: the same song with the screen on for a minute; any small pauses that were not there before? (546)
- [ ] Phone: a song with no effects, Page sequencer, Worklets On: the report's second line says "worklets none"; PLAY, screen off 20 s, back, STOP; clock lines about 1.00 s in 1.00 s and steady by ear. (545)
- [ ] Phone: your normal song with effects plays with its effects as before and the report lists what loaded; it may still rush when hidden. (545)
- [ ] Phone or computer, Pattern view: PLAY with the cursor on one track while another track HOPs or plays a different pattern; that track's column shows its own moving band, cleared on STOP; is the band easy to tell from the cursor row's tint? (571)
- [ ] Any device: INS on Phrase and on the Song adds an empty row below the cursor, the cursor stays, one Undo reverts it; also in the Edit menu (folded Song screen: menus only). (570)
- [ ] Computer, PAD mode: Shift+▼ on Song / Arrange opens the block's phrase (from Phrase it still goes to Mixer). (571)
- [ ] Phone unfolded and computer, each skin you use: the loop play button shows a play sign inside a circular loop (word LOOP beside it on the thin bar), a stop mark while playing, still the icon after switching pad / keys mode. (573)
- [ ] Phone unfolded: in Arrange select rows 4 to 7 and press LP.SEL; those rows loop and the button is lit, pressed again it stops; with nothing selected it loops the cursor's row and says to select rows; folded: Edit menu and Arrange right-click menu only. (573)
- [ ] Phone unfolded, song playing, Phrase with Arrange beside it: tap ALT then ▲ repeatedly across the playhead's row (FOLLOW off, PATTERN, ON) and hold it; the pattern you went to stays open and in view, Arrange moves back once after you stop for 4 s (a minute with FOLLOW off); once with the Audio thread sequencer. (573)
- [ ] Phone unfolded, Arrange holding the keys, cursor around rows 8 to 12, song playing: tap PLAY twice quickly; the cursor stays where you left it. (The part about a double tap going to the top with Phrase holding the keys is out of date: build 576 removed it.) (573)
- [ ] Arrange: M.PAT (or Alt-click / long press) on a master-column block strikes it through; while muted its commands do nothing; unmute and they act again; survives copy, paste, move, undo, save and load. (570)
- [ ] Phone, pad: SHIFT+▼ on Arrange opens the block's phrase on its track; on an empty cell a new phrase opens and nothing goes on the song until a note is entered. (570)
- [ ] With a bus that has an old note pattern: CLN↓ many times leaves no empty row (a clone landing on a block inserts a whole song row by design); say which rows if one still appears. (570)
- [ ] Phone: GEN on a block opens its phrase with the generator menu; on an empty cell a new pattern is made and the menu shows; on a bus or master column only a message. (569)
- [ ] Phone or computer: select several blocks in Arrange, SEMI + / − (or Alt+, / Alt+.); every pattern moves once, the message counts them and says how many are used elsewhere; one Undo puts it all back; no selection: the block under the cursor. (569)
- [ ] Phone unfolded or computer: solo and mute a few tracks, tap SOLO (or MUTE) twice quickly; every solo and mute clears, per-block mutes stay; a single tap still acts at once; check the marks on Song header, Mixer, Phrase title and pad keys. (569)
- [ ] Phone or computer: pasting notes into a bus track's phrase pastes nothing (commands still paste) with a message; pasting, Alt-dragging or duplicating a pattern with notes onto a bus column is refused; a command-only pattern goes in. (569)
- [ ] Phone, the three Low power skins, folded and unfolded: in Arrange the cursor, selected blocks, playing block and muted blocks can each be told apart at a glance. (569)
- [ ] Phone unfolded, pad mode, Song / Arrange, Android font size largest: MUTE / PAT, LOOP / SEL and CLN / ↓ read in full; no COMMANDS heading; folded shows one more Arrange row; say if UNIQ and COPY drawn smaller than CUT and PST looks wrong. (576)
- [ ] Phone, pad mode: the tab row holds SONG to PROJECT plus BPM, LPB, dots, ROWS and REC, no TRANSP (back in KEYS mode); is the row under the tabs gone on your phone unfolded? (573)
- [ ] Phone folded: the Mixer's master row reads AUTO MASTER ON / OFF without clipping. (570)
- [ ] Any device: tap the CPU %; under the audio list a section "The screen" shows Screen drawing and Sequencer (page), not added to the audio percentage. (575)
- [ ] Any device: playback report after a STOP says "voices 0". (575)
- [ ] Phone: effect-off commands (FXO 10 switches slot 1 off, FXO 90 all slots): the effect stays off until another command turns it on, also across a song wrap; STOP then PLAY starts clean. (570)
- [ ] Phone (Fold, OLED): pick Low power (hot pink), then (mustard); in daylight check the tracker columns, Song / Arrange, Mixer, pads and Settings switches are readable; no blue, cyan, purple or violet anywhere, background pure black. (568)
- [ ] Fold unfolded (and computer): Song screen with SECT lane has about equal space on its right as the lane takes on its left; nothing cut off (MST column whole). (565)
- [ ] Fold folded: the Tracker button's left edge lines up with SONG below it. (564)
- [ ] Fold unfolded: the word "Thunder" is whole or gone, never cut off; with it gone Tracker starts at the left edge; rotate / fold / unfold brings it back when there is room. (564)
- [ ] Fold folded: Synth page, pick an engine: the page scrolls so its editor is at the top; sheets (Skin, Preset) have an easy Close / Done. (563)
- [ ] Phone: Settings, Screen: ROW COLOR IN PATTERN says "press and hold a row number"; in Pattern view that opens the colour menu (not yet tested with a real finger). (563)
- [ ] Phone: while a song plays, the tracker's title, info line and PLAYING / STOPPED state still update when you move or switch view. (562)
- [ ] Idle look: the CPU bar steps instead of gliding; Mixer meters, faders and the stopped Mixer spectrum look as before. (548)
- [ ] Phone, Arrange: after a pad press it holds its page 4 s (FOLLOW on) or a minute (FOLLOW off). (548)
- [ ] Phone, pad keys: a slow press never acts twice. (548)
- [ ] Folded: Tracker | Synth shows both words; taller File rows, "Help & keys" last (keep the Help header?); top bar about 6 px taller (OK?); FX opens on SLOT 1 first; Settings has a pinned Done; pop-up messages sit below the tab strip; POP-UP MESSAGES off hides all but the problem ones (any you miss?); stars, Arrange zoom, PIN and Settings switches easier to hit. (548)
- [ ] Folded: Settings, Screen, BIGGER CELLS ON TOUCH (off by default): Arrange cells grow to about 28 by 26 px and scroll sideways; playhead, selection and bus bars line up; zoom buttons work; Off brings your zoom back. (550)
- [ ] Windows: the Song screen's SELECTING hint says "Ctrl+C", not "Ctrl/⌘"; in the IDM, IDM pink paper, IDM pink & green and 909 skins BPM / LPB are readable while playing, "Stop" is dark on pink in pink paper, and the POLY 09 to 16 label is readable. (563)
- [ ] Phone or computer: on an empty track, a pattern brought from another track gets its own copy; moving, repeating or rearranging it makes no new patterns; should Alt-drag copies onto another track also get their own copy? (New on 10 Nov)
- [ ] Add LFO… inside a …pump window: right-click a knob; the entry is there (not on switches) and the window closes after adding; is closing right? (556)
- [ ] Starting mid-song: earlier CUT / ATK commands are now in force for live synths. (555)
- [ ] MOD on the master and on a bus: an LFO on a master effect and one on a bus effect both move while the song plays and the scope's dot keeps time; an LFO on a silent track carries on in phase when the track returns. (553)
- [ ] MOD in Phrase / BUS PLAY: playing one phrase moves only that track's, its buses' and the master's LFOs. (553)
- [ ] Add LFO… from an FX parameter's right-click (FX screen and FX bar); the \~ mark on modulated rows. (9 Nov)
- [ ] LFO scopes: shapes look right; the dot keeps time with what you hear; SUM scope with two LFOs on one target; Low power display shows still shapes; no extra load on the phone. (9 Nov)
- [ ] Computer: MOD keys: Alt+7 opens MOD; Ctrl/⌘+arrows step TARGET (▲▼ by eight), SHAPE, RATE, DEPTH; ⌘ on a real Mac. (9 Nov)
- [ ] Chains rail lines: straight accent lines from real cables look right on your patches; 909 skin contrast OK? (9 Nov)
- [ ] Chains feed view: a track feeding a chain shows it, its path lit, the rest greyed; the output track's view unchanged. (9 Nov)
- [ ] By ear on any device: Bass engine and mastering sound unchanged. (545)
- [ ] Acid Mutate: low vs high intensity stays related to the line; hand tweaks survive; Original (O) restores; TIME off keeps the rhythm. (9 Nov)
- [ ] Save with a missing impulse response or preset sample (only if you see it): the question names what is missing. (554)
- [ ] Phone, builds 529 to 535 on a real Fold: the lists in "9 Nov, end of session: what to test on the phone" and "9 Nov: builds 531 to 535". (Carried over)
- [ ] Phone: clock toggle test: a simple song hidden about 20 s with Background play off, then with Keep screen on off; Copy report after each. (Carried over)

## Parked: open, not blocking anything

These decide when the native apps start and whether three optional things get built; nothing else waits on them.

### Decides the native apps

- [ ] Phone, Background play ON: your normal song with effects, PLAY, screen off 3 to 5 minutes, press a volume key once, screen on, STOP, Copy playback report; steady by ear the whole time, and the volume key changes media volume (no vibration); report clock lines near 1.00 with few held / kicked counts and underruns. (Native port test)
- [ ] Phone: the same test with a heavier song (more effects, live synths). (Native port test)

13 Nov: one 55-second screen-off run on battery looked steady (clock 0.99 to 1.02, one 0.06 s hold, no kicks), which is too short to decide. The test needs 3 to 5 minutes, twice.

### Decides whether something gets built, and how

- [x] Phone: Copy a playback report from one run plugged in and one on battery, same song and buffer; the report shows battery % and charging state; does charging help the clock? (573)
- [ ] Phone, your experiment: play 2 minutes without touching the screen, then 2 minutes tapping somewhere harmless every 10 to 15 s; does the clock hiccup only come when untouched? (573)
- [ ] Phone or computer: an LFO at 1/16 or 1/32 on a built-in effect's filter cutoff, and on a delay time or pitch; acceptable, or a staircase? (556)
- [ ] Phone (Fold), simple song, Page sequencer: with Worklets On, PLAY, screen off about 20 s, back, STOP, Copy playback report; then Worklets Off (test), Apply (reload), the same steps; check the report's second line matches the mode; by ear, does Off stutter or rush less? (Effects are silent in Off.) (New on 10 Nov)
- [ ] Phone: part of the same Worklets test: in each mode, PLAY a few seconds, STOP, screen off about 15 s, back, Copy report. (New on 10 Nov)

## Set aside: probably superseded

These were re-tested or made moot by a later block, so skip them unless you want to; they are not in any tier above and have no tick boxes.

| Item | Build | Replaced by |
| --- | --- | --- |
| Runaway kick test, Fold (551) | 551 | Retested in 557 (runaway kick, retest) |
| Volume keys with the screen off: media or ringer? (557) | 557 | Asked again in 558 and 559 (locked-screen volume key) |
| Five-minute computer test, Audio thread sequencer (566) | 566 | 567 says "Same test as build 566" |
| MOD by ear: do fast rates sound steppy? (New on 9 Nov) | New on 9 Nov | 556 asks the same (Smoothing check) |
| MOD on the master: does an LFO move? (New on 9 Nov) | New on 9 Nov | 556 (MOD on the master out) |

Two items stay in the list (sections 3 and 4) with a note because only part of them is out of date: the Build 573 double-tap PLAY item (the part about going to the top was removed in 576) and the Build 573 CPU panel item (the note that screen drawing is not measured; 575 adds "The screen").

## Done on 13 Nov

### Collapsed modules (579)

- [x] Phone, folded and unfolded: double-tap a module's title bar to collapse, again to open; is the chevron easier to hit than before? (579)
- [x] Phone: a collapsed module's text is readable at the zoom you really use (the patch still opens at 25%; that is pass 2). (579)
- [x] Computer: in …chains and …waves, in IDM pink & green and 909, collapse all (Alt+C) on a real patch; can you read what every module is doing without opening one, with nothing overlapping or cut off? (579)
- [x] Computer: is the first choice plus the first two sliders the right summary? Name the modules where another value matters more (the Mixers show "Inputs 4" first; you probably want Master level). (579)
- [x] Computer: a collapsed module shows its summary, a thin level strip, only patched ports, and "+N" for unpatched ones (click it: it opens); a modulated slider has a labelled stub and a purple value. (579)
- [x] Computer: collapse a module with modulation cables in, then drag, zoom and expand it; the cables stay on it and never jump. (579)
- [x] Computer: Collapse all, then Undo: every module comes back in one step; same for Expand all. (579)
- [x] Computer: a collapsed Track In or Chain Out reads "Track In" with "Track 1" below (waves: "Out", "Waves In 1"); the chevron tooltip says "Expand" while folded. (579)
- [x] Computer: open a patch saved before this build with some modules collapsed; they come back collapsed in the new look. (579)

### Signal kinds and cables (580)

- [x] Phone, folded and unfolded: tap a cable; it selects and shows its handles; two quick taps still remove it. (580)
- [x] Computer: in …chains and …waves, IDM pink & green and 909, patch a Euclidean's Gate into a Sample & hold's Trig and an LFO into its In; gate jacks are diamonds, gate cables dotted, control cables dashed, audio cables solid. (580)
- [x] Computer, white theme: the same patch; gate is a pink dotted line into a diamond jack, control stays purple and dashed. (580)
- [x] Computer: a collapsed module with a patched gate port (a Euclidean's Gate out) shows a diamond; a control stub is still round. (580)
- [x] Computer: hover a cable (thickens, grey halo), click it (stays thick, stronger halo), press Delete (just that cable goes), then Undo brings it back in one step; Escape or a click on the background lets go. (580)
- [x] Computer: hover a bent cable (Alt-drag makes a bend); a small square shows on each bend; drag it and the bend moves; a plain click only selects; at 50% zoom the handle is still about 12 px and easy to hit. (580)
- [x] Computer: click a module's title; its outline reads as selection (not audio, control or gate) and cables that do not touch it fade back; click the background and they return. (580)
- [x] Computer: open Help in each editor; a "Signal kinds" strip shows the three line styles and the round and diamond jacks, and the Patching text says the same. (580)
- [x] Computer: open a patch saved before this build; every cable keeps its kind (gate cables are dotted now), and a patch saved in this build opens in 579 as before. (580)

### Wording and type (581)

- [x] Phone, folded (344 px) and unfolded: text size is comfortable; the smallest text is now 11 px; say if anything is too small or crowded in the patch, the module list or Help. (581)
- [x] Phone, folded and unfolded: the first time …chains (or …waves) opens, a short tip note is at the top left; after a reload only a small Tips button stays; tap it to open, × folds it back. On a computer the long note is as before and × turns tips off. (581)
- [x] Computer: in …chains and …waves, white theme and IDM pink & green, add long-named modules (Convolution reverb, Sidechain comp, Function generator, Transient shaper, Multiband squash), open and collapsed; titles read in full (a long name wraps, never "Tape de…") and no right-hand value is cut. Tell me if "Convolution reverb" breaking inside its first word looks wrong in IDM pink & green. (581)
- [x] Computer: open the module list, a module's title bar, its ? card and Help; each module has the same name in all four (renamed, e.g. Envelope Follow → Envelope follow, Sidechain Comp → Sidechain comp, the second Compressor → Simple compressor; the list lost its brackets). (581)
- [x] Computer: hover a jack; its tooltip says "audio", "control" or "gate" (e.g. "Cutoff: control input", "Trig: gate input"). (581)
- [x] Computer: five ports that showed a raw key now have names: Blades filter Resonance, Meta modulator Modulator, Flex and Step gate Pattern, Sample & hold Noise, Clock Ramp and Bar (chains). (581)
- [x] Computer, IDM pink & green and 909: open Help in each editor and scroll to Dynamics; each card has its module's colour, a light tint and readable text; the CPU readout's amber is darker. (581)
- [x] Computer: in …waves with the window about 400 px tall, right-click a module; the menu stays on screen and scrolls with the wheel. (581)
- [x] Computer: open a patch saved before this build; every module, cable and value is as it was, and a patch saved in this build opens in 580. (581)

### Review fixes (582)

- [x] Phone and computer: new patch, 1440 and a small window: the tip box is two lines with a More / Less button; Fit leaves no module under it. On a phone, Tips and × keep keyboard focus. (582)
- [x] Computer: in …chains, collapse a Sidechain Out and play drums into it; a thin level strip along the bottom moves with the SEND meter. (582)
- [x] Computer: in …chains and …waves, IDM pink & green and 909, add Convolution reverb and Compressor; titles break between words only (never "Convoluti / on"); the BYP button may be a little narrower. (582)

### Settles Audition on the Synth page

- [x] Phone and computer: move a slider on a drum pad, then a synth pad; after a short pause you hear the new sound and a long drag keeps playing along. Does the repeat rate feel right, and should Audition be on by default? (578)
- [x] Phone and computer: with a song playing, is Audition in the way? Say if it should stay quiet while the song plays. (578)

### Build 585 on the phone, and decisions (13 Nov, night)

- [x] Folded phone: open …chains or …waves with a patch in it: it opens big enough to read (about 80%), on the first module, not as a tiny overview. (585)
- [x] Folded phone: the Fit button is in the top bar, and the module list closes after you add a module, which is brought into view. (585)
- [x] Phone: tap the × on a module: it asks "Remove …?" first; Cancel keeps it; after Remove, Undo brings it back. (585)
- [x] Phone: tap a module's title bar without dragging: the module does not move. (585)
- [x] Folded phone: the first time you open an editor, does the tip box sitting over the first module's title bar bother you? (585)
- [x] Computer: collapsed mixers show the master level first; modules with a Mix or Level show those first. Compressors now show Mix and Makeup: would you rather see Threshold and Ratio? (583)
