# Step 1 retest: 9 Nov fixes on beta, build 544

Headless Chromium (Playwright), a clone of `beta` at build 544 served from `scratchpad/step1/repo` (nothing in /home/claude/thunder-plus touched). Phone items: Pixel 7 profile with touch, DSF 1 (and 360 px, and a 720 px Fold-unfolded stand-in); desktop items 1400x900. Scripts: `scratchpad/step1/*.mjs`, screenshots `scratchpad/step1/*.png`. `npm test` subset (play-keys, app-logic, usability, chains-feed, acid-mutate, mod-scope, hangs): 85 of 85 pass.

Counts: FIXED 44, NOT FIXED 1, UNTESTED 14, CAN'T TEST HEADLESS 4 (total 63).

Caveat on this environment: headless audio is slow and a few runs lost the browser mid-run; those items were rerun or marked UNTESTED. The headless machine runs the first PLAY slowly (2-4 s).

| # | Item | Source | Status | Evidence |
|---|---|---|---|---|
| 1 | Phrase ◀▶ on the pad step one column at a time, reaching every column (FX numbers, INST, poly note columns); phone touch taps | 9 Nov: phone bug list (529) / 9 Nov: what to test on the phone (530) #5 | FIXED | Pixel 7 + touch, pad ▶ x14 on MONO track: x = 0,4,5,6,7,8,9,10,11 then stays; POLY track 0..11; ◀ walks back the same. No skips, no double step (b1.mjs). |
| 2 | Same with the desktop keyboard, non-pattern view | 9 Nov: phone bug list (529) | FIXED | 1400x900, pvSetMode(false), ArrowRight/Left: identical sequences. (Pattern view instead hops to the next track past the last column; by design.) |
| 3 | Empty block beside Phrase: tap it, then enter a note: the note and a fresh phrase go into THAT block (phraseToCursor) | 9 Nov: phone bug list (529) / 9 Nov: what to test on the phone (530) #3 | FIXED | Strip cell (row 1, track 3) tapped twice (first tap only gives the strip the keys): pendPlace={x:2,y:1,p:1}; tap grid, EDIT: block 1:2 = phrase 1 holds the note; phrase 0 untouched (b2.mjs). |
| 4 | Note + instrument survive LOOP and the block plays | 9 Nov: what to test on the phone (530) #3 | FIXED | Note C-4 and inst bumped in the new block, pad LOOP: block 1:2 and its row unchanged; LOOP lit; PLAY: tp.pos shows track 3 at row 1. |
| 5 | Acid line on an empty track lands in that block | 9 Nov: what to test on the phone (530) #4 | FIXED | Empty strip cell, Acid line > Generate: block 1:2 created, 5 notes in phrase 1, phrase 0 still 1 note (b3.mjs). |
| 6 | Acid line is audible and plays only its own track (the 'soloed' behaviour) | 9 Nov: what to test on the phone (530) #4 | FIXED | Acid panel ▶/■: tp.mode='phrase', only track 3 in tp.pos; master RMS peaked 0.15-0.16 (analyser). Stops cleanly. (By ear: owner.) |
| 7 | FOLLOW holds the cursor and open phrase for 4 s after a key / press | 9 Nov: phone bug list (529) / 9 Nov: what to test on the phone (530) #6 | FIXED | FOLLOW PATTERN on the Phrase screen after the hold: cPhrase followed 0->5. Song screen: cursor y stayed until about 4.3-4.8 s after the press, then followed (c1/c2). |
| 8 | Arrange pages again after the 4 s hold: keyboard key | 9 Nov: phone bug list (529) | FIXED | Desktop, ArrowDown while playing, playhead far past the page: first page at 5.3 s (FOLLOW ON) and 4.3 s (FOLLOW PATTERN) after the key; not before (c2.mjs). |
| 9 | Arrange pages after the hold: tap in Arrange, selecting patterns doesn't fight | 9 Nov: phone bug list (529) / 9 Nov: what to test on the phone (530) #6 | FIXED | Phone, tap a block in Arrange: no paging until 4.3-4.8 s (FOLLOW ON), 9 s sample (FOLLOW PATTERN). |
| 10 | Arrange pages after the hold when the last input was a PAD press (the phone's keys) | 9 Nov: phone bug list (529) / 9 Nov: what to test on the phone (530) #6 | NOT FIXED | Pad ▼ then Arrange pages after about 1.4 s (ON), 1.3 s (PATTERN), 2.1 s (OFF); keyboard on desktop honours 4 s / 60 s. See NOT FIXED #1. |
| 11 | FOLLOW OFF keeps its minute (keyboard) | 9 Nov: phone bug list (529) | PARTLY TESTED | Desktop FOLLOW OFF: no paging for 18 s after ArrowDown (the full minute was not waited out). (Phone pad: see the NOT FIXED item.) |
| 12 | curToPlay clears the .pg flag so the cursor isn't hidden after stopping | 9 Nov: phone bug list (529) | UNTESTED | cs.*.pg not read after a stop; only cursor position checked. |
| 13 | PHRASE ON STOP = STAYS keeps the cursor and phrase | 9 Nov: phone bug list (529) / 9 Nov: what to test on the phone (530) #7 | FIXED | FOLLOW PATTERN carried cPhrase to 5 while playing; STOP: cPhrase 0, row 3 as before (i1.mjs). |
| 14 | ARRANGE ON STOP = STAYS keeps the Song cursor and view | 9 Nov: phone bug list (529) / 9 Nov: what to test on the phone (530) #7 | FIXED | Song screen, FOLLOW ON and PATTERN, long song: after STOP y and top back to their pre-PLAY values (y 1/4, top 0) (c1.mjs). |
| 15 | PHRASE ON STOP = PLAYHEAD moves to the playhead | 9 Nov: what to test on the phone (530) #7 | FIXED | After STOP cursor y=23, cPhrase 5, row 14 (the playing spot) (i2.mjs M=ph). |
| 16 | ARRANGE ON STOP = PLAYHEAD | 9 Nov: what to test on the phone (530) #7 | UNTESTED | The run was lost to a browser closing; not repeated. |
| 17 | STOP tapped twice within 450 ms: 'Back to the top of the song', cursor row 0 | 9 Nov: what to test on the phone (530) #7 | FIXED | Two presses 150 ms apart: toast 'Back to the top of the song.', y 7 -> 0 (k1.mjs C=stop). |
| 18 | LEFT timer lands on the song's end after heavy LOOP use | 9 Nov: phone bug list (529) / 9 Nov: what to test on the phone (530) #8 | FIXED | 6-block song (12 s), LOOP SEL rows 1-2, played through 4 loops (20 s): LEFT read 9,8 / 7,6 on each pass and reset to 9 at every repeat; no drift (c3.mjs). |
| 19 | Pad layout: Save beside MUTE and the same size (unfolded) | 9 Nov: builds 531-535 (531) / 9 Nov: what to test on the phone (530) | FIXED | 720 px wide touch profile: Save 63x81 at x=280, MUTE 63x81 at x=352, same row; screenshot e1_fold_solo.png looked right. |
| 20 | LOOP lights when engaged | 9 Nov: builds 531-535 (531) | FIXED | After pad LOOP: #kLoop has class 'on' and loopPat=true. |
| 21 | Pad PLAY lights while playing | 9 Nov: builds 531-535 (531) | FIXED | #kPlay class 'k kw on', <html> class 'tpon' while playing. |
| 22 | Play-song button (#tSong) lights while playing | 9 Nov: builds 531-535 (531) | FIXED | #tSong class 'tsong on' while playing, 'tsong' stopped. |
| 23 | MUTE and SOLO pad buttons act on the track and clear each other | 9 Nov: phone bug list (529) | FIXED | Song screen, track 1: MUTE -> mute true; SOLO -> mute false, solo true; SOLO again -> off (e2.mjs). Caveat: see NEW FINDING A. |
| 24 | MUTE / SOLO styling: pink / green off, light when latched | 9 Nov: phone bug list (529) / 9 Nov: what to test on the phone (530) | FIXED | Screenshot: SOLO green, MUTE pink; MUTE class 'lt' when latched. |
| 25 | Pad Save key saves the song | 9 Nov: builds 531-535 / 9 Nov: phone bug list (529) | UNTESTED | Tap produced no download or toast within 4 s in headless; not conclusive. |
| 26 | RTG on a slot saved off switches it on; back as saved on release or RTG 00 | 9 Nov: phone bug list (529) / 9 Nov: what to test on the phone (530) #10 | FIXED | Sequencer events for a Retrigger slot saved off: RTG 42 gives sw0=1 at the row and sw0=-1 two rows later; RTG 00 gives sw0=-1 (g1.mjs). The audio effect itself not heard. |
| 27 | CUT / RES reach the sound events on a synth track | 9 Nov: phone bug list (529) / 9 Nov: what to test on the phone (530) #9 | FIXED | makeSeq events: CUT 60 RES 100 -> tw {cut:60,res:100}; CUT FF clears it. |
| 28 | CUT / RES on a LIVE engine (liveCut low-pass) | 9 Nov: phone bug list (529) / 9 Nov: what to test on the phone (530) #9 | UNTESTED | No live engine node was ever built for a track note in 3 attempts (voice 8 and pad 0 set to 'Mono live'); no filter created. |
| 29 | ATK / REL on a rendered sound | 9 Nov: phone bug list (529) / 9 Nov: what to test on the phone (530) #9 | FIXED | Events: ATK 80 -> fdi 0.057; REL 90 -> rlo 0.072 (d1.mjs). Audible shape: owner. |
| 30 | Playhead takes off moFxLat() (picture no longer ahead of the sound) | 9 Nov: phone bug list (529) / 9 Nov: what to test on the phone (530) #2 | UNTESTED | Source line 30933 has it; moFxLat() was 0 in every headless setup I tried, so no difference could be seen. |
| 31 | Low power display setting turns the Low power (black) skin on and back to your skin off | 9 Nov: builds 531-535 (532, 534) | FIXED | idm -> lowpower (saved 'idm') -> idm. Screenshot h1_lowpower.png: pure black, high contrast. |
| 32 | Low power skin applies even if storage fails | 9 Nov: builds 531-535 (534) | FIXED | setItem throwing: skin still 'lowpower'; off restores 'idm'. |
| 33 | A stale saved skin is cleared | 9 Nov: builds 531-535 (534) | FIXED | Stored lowpower-skin 'matrix' removed when LP turned on while the skin was already lowpower. |
| 34 | Low power display caps drawing at ~24 fps | 9 Nov: builds 531-535 (532) | UNTESTED | Frame rate not measured. |
| 35 | Engine sleeps after 30 s of silence with Low power on; not while playing; wakes on next play | 9 Nov: builds 531-535 (533) | FIXED | Stopped: running at 10 s, suspended by 25 s (to 34 s); PLAY: running again. Playing 34 s: stays running (h3.mjs). |
| 36 | Engine stays awake while a pad sounds / MIDI input / rendering / recording | 9 Nov: builds 531-535 (533) | UNTESTED | Only song playback exercised. |
| 37 | Top ▶ (#tSong) on Phrase plays the phrase, and stops | 9 Nov: control bar Play (538) | FIXED | Phone 412, 360, Fold 720: tp.mode='phrase'; a second tap stops (a few first-use runs missed a step; NEW FINDING A). |
| 38 | Pad PLAY plays the song; SHIFT then PLAY plays this screen; each stops | 9 Nov: pad PLAY / ALT (537) | FIXED | Phone: PLAY mode 'song', SHIFT+PLAY 'phrase'. |
| 39 | Space plays the song, Shift+Space this screen (desktop) | 9 Nov: pad PLAY / ALT (537) | FIXED | Desktop Phrase screen: Space -> 'song', Shift+Space -> 'phrase'. |
| 40 | #tSong shown on a folded / narrow phone, no horizontal overflow, top bar fits | 9 Nov: control bar Play (538) | FIXED | 412, 360 and 720 wide: visible, inside the viewport, no overflow (f1_small.png). |
| 41 | COMMANDS row drops its ▶ under 700 px in PAD mode | 9 Nov: control bar Play (538) | FIXED | 412 and 360: display none; 720: present. |
| 42 | OPTION is labelled ALT | 9 Nov: pad PLAY / ALT (537) | FIXED | Pad shows SHIFT / ALT / EDIT in every screenshot. |
| 43 | M.PAT on Arrange mutes / unmutes the block | 9 Nov: phone bug list (529) / 9 Nov: what to test on the phone (530) | FIXED | Song screen: block muted true, again false. |
| 44 | RET and RTG are labelled note repeat and FX stutter; no duplicate retrigger commands | 9 Nov: phone bug list (529) | FIXED | Command help: RET 'Note repeat…', RTG 'FX stutter…'; the 78 command names are unique. |
| 45 | New songs start with mastering off; the master FX screen lists CALM, COMP, TONE | 9 Nov: phone bug list (529) | FIXED | New song: pEQ, pCALM, pCOMP, pTONE, pMASTER present and off; master FX screenshot k4_mfx.png shows their windows. |
| 46 | Chains tab always shown unfolded | 9 Nov: phone bug list (529) | FIXED | 720 px: Tracker \| Synth \| Chains; 412: Tracker \| Synth. |
| 47 | Synth pads scroll on a phone | 9 Nov: phone bug list (529) | FIXED | Pads panel scrollHeight 180 > clientHeight 125, overflow auto. |
| 48 | Messages at the top on phones | 9 Nov: phone bug list (529) | FIXED | Toast appears over the top bar (e1_fold_solo.png). |
| 49 | Wider resize grip | 9 Nov: phone bug list (529) / 9 Nov: what to test on the phone (530) | UNTESTED | .dkgrip measured 0x0 on the screens tried. |
| 50 | Battery: background timers rest when nothing plays or the app is hidden | 9 Nov: phone bug list (529) | UNTESTED | Only the Low power sleep was exercised; hidden-tab behaviour not driven. |
| 51 | Phone: less layout work while playing (535) | 9 Nov: builds 531-535 | CAN'T TEST HEADLESS | Needs a phone's CPU; headless timing is not comparable. |
| 52 | Page sequencer lead scales with output latency (Safest) | 9 Nov: phone bug list (529) | UNTESTED | Not exercised. |
| 53 | Playback report logs the level 4 s into PLAY | 9 Nov: phone bug list (529) | FIXED | Report: '4 s into PLAY: level -29.4 dB'. |
| 54 | Report logs slow work and long frames (544) | Owner's punch list (+10 Nov items) | UNTESTED | No slow case produced. |
| 55 | Worklets Off (test): the page makes zero addModule and zero AudioWorkletNode | Owner's punch list (+10 Nov items) (10 Nov) | FIXED | On: 8 addModule, 4 nodes; Off after reload: 0 and 0, song still plays. |
| 56 | Unique per track: a pattern brought from another track gets its own copy; moving, repeating, rearranging make no new ones | Owner's punch list (+10 Nov items) (10 Nov) | FIXED | Copy track 1 block -> paste on empty track 4: new phrase 1. Copy that block, paste on same track: still 1. Cut/paste lower on the track: still 1. Another empty track: new copy. |
| 57 | Alt+7 opens MOD | Owner's punch list (+10 Nov items) | FIXED | Desktop: tv='mod', no errors. |
| 58 | MOD Ctrl/Cmd+arrows, Add LFO, the ~ mark, scopes, SUM scope, Low power still shapes | Owner's punch list (+10 Nov items) | UNTESTED | Browser not driven; tests/mod-scope.test.js passes (85 of 85 in the Node run). |
| 59 | Acid Mutate: intensity, hand tweaks survive, Original restores, TIME off keeps rhythm | Owner's punch list (+10 Nov items) | UNTESTED | Browser not driven; tests/acid-mutate.test.js passes. |
| 60 | Chains feed view | Owner's punch list (+10 Nov items) | UNTESTED | Browser not driven; tests/chains-feed.test.js passes. |
| 61 | MOD by ear, MOD on master audible, Fold touch hold-and-drag, rail line looks, 909 contrast | Owner's punch list (+10 Nov items) | CAN'T TEST HEADLESS | Ears / eyes / a real Fold. |
| 62 | Stutter, audio clock racing, quieter Safest, notes after stop, hidden / screen-off Worklets test, playhead vs sound by ear | 9 Nov: what to test on the phone (530) / Owner's punch list (+10 Nov items) | CAN'T TEST HEADLESS | Real audio output and a real phone. |
| 63 | Desktop helper, Windows, Ableton Link, hardware MIDI | Owner's punch list (+10 Nov items) (carried over) | CAN'T TEST HEADLESS | Out of scope for headless. |

## NOT FIXED

### NOT FIXED #1 (item 10): Arrange pages during the 4 s hold when the last input was a pad press (phone)
Claim: FOLLOW holds 4 s after a key or a press on grid, pad or Arrange; a follow mode pages Arrange after that hold; FOLLOW OFF keeps its minute.
Reproduce (c1.mjs, c2.mjs): Pixel 7 profile with touch. New song, track 0 filled with 80 song rows (phrase 16 rows, BPM 300, LPB 16, so the playhead moves about 4.6 rows/s). Song screen (13 rows visible), `setFollow(2)` (also tried 1 and 0). Tap pad PLAY, wait until the playhead is on row 3, tap pad ▼ once, then read `cs.song.top` every 400 ms.
- FOLLOW ON: `top` becomes 13 at 1.4 s after the press (cursor y stays 1). Expected: stays 0 until about 4 s.
- FOLLOW PATTERN: `top` 13 at 1.3-1.7 s; the cursor itself only moves at about 4.8 s (that hold works).
- FOLLOW OFF: `top` 13 at 2.1 s after the press. Expected: nothing for a minute.
Same song, desktop keyboard ArrowDown: first page at 5.3 s (ON) / 4.3 s (PATTERN), none in 18 s (OFF), so the key path is right. A tap on a block in Arrange also holds (first page at 4.3-4.8 s).
Where: `arrFollowPage` (index.html about line 23217-23223) only looks at `arrHandT`, `arrTouchT` (pointerdown on #tArr) and `arrActT`. `arrActT` is set by the window `keydown` listener (about line 23213); the window `pointerdown` listener (about line 23214) sets only `workT`, which holds the cursor / phrase in `followPlay` but never reaches `arrFollowPage`. On a phone the pad is the keyboard, so pad presses never hold the page. Probable fix: in that `pointerdown` listener, when the target is in `#tPad`, also set `arrActT = workT`.

## NEW FINDINGS (not in the handover list)
A. **Pad keys can fire twice on a slow press.** The `click` handler for pad keys (about line 30990) is ignored only while `ptrJust()` (pointerdown less than 800 ms ago, line 8613) is true. When a first press is slow (the first MUTE/SOLO builds the audio graph, a first PLAY starts the engine), the click arrives later than 800 ms and presses the key again. Seen in headless: the first SOLO tap ran `soloKey` twice (solo on then off); a pad PLAY / STOP tap left the song in the opposite state in three runs. SAVE already uses a per-button flag (`k._pd`) for this; the same flag would fix every pad key. On a loaded phone (the owner's stutter reports) the same could happen.
B. Cosmetic: on 412 px and 360 px the Tracker | Synth switch is clipped to "Synt".

## Only the owner can check on the phone
- Playhead against the sound (moFxLat, and mastering off); Safest vs Balanced level and lead; build 530 list item 11.
- Stutter easing on the second pass with Arrange showing; stutter that stops under LOOP; whether a song uses MUT / CUT / RES / DEC on sample pads.
- Pad feel: Save beside MUTE, key size under a thumb, messages at the top, resize bar by finger, Synth pads scroll by finger.
- Pad presses while Arrange pages (NOT FIXED #1 and NEW FINDING A on the real device).
- Worklets On vs Off screen-off test (copy the report) and by ear which stutters; stopped-and-hidden test.
- CUT / RES on a live synth by ear; ATK / REL on a rendered sound by ear; RTG slot switching by ear.
- Low power display: smoothness at 24 fps, Low power skin legibility, engine wake latency on the first note after 30 s.
- MOD by ear (fast rates, master, Fold touch drag), chains rail lines and the 909 skin, LFO scopes against what you hear.
- Desktop helper, Windows MIDI/recording, Ableton Link, hardware MIDI.
