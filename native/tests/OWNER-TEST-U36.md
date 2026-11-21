# Owner test U36 / T9: MIDI out from the Android app (about 10 minutes)

What this proves: the app can send timed MIDI notes and MIDI clock out of the phone's USB port through the new native scheduler, queue and sender thread, and it follows a
plug / unplug. It is a debug path (there is no live song player in the app yet): the button plays a built-in two-bar C major arpeggio.

You need: the Galaxy Z Fold 6, the APK of this PR's CI run ("thunder-plus-native-n0-android-apk" artifact, installed as before), a USB MIDI interface or a synth with a USB
MIDI port (USB-C OTG adapter or cable), and something that makes sound from MIDI (the synth itself, or a keyboard / module on the interface's MIDI OUT). If you have a way to
watch the MIDI clock (a drum machine that shows tempo, or a MIDI monitor), better.

## 1. Plug in, pick the device
1. Install and open the app. Plug the MIDI device into the phone (allow the prompt if Android shows one).
2. At the bottom strip you will see a blue-grey text block. Before the audio engine runs it says `MIDI out: engine not running`; the part under `devices:` already lists your
   device, for example `n:Your Device`. If `devices:` says `none` after 5 seconds, unplug and replug once; if still none, tap **COPY MIDI** and report the text (that is already a
   finding). The lines `MIDI out ports:`, `stats:` and `core:` appear after you tap **MIDI out test** once (which starts the engine).
3. If the device has several MIDI outputs, each shows as `n:Name / PortName`. Tap **MIDI A: next device** until the line `A = ...` is the one you want.

## 2. The test
1. Tap **MIDI out test**. The audio engine starts (silent) and, about half a second later, the synth should play: ONE high note (C6, a quarter second) immediately, then after about
   half a second a C major arpeggio, four notes a bar, two bars, at 120 bpm (about 4 seconds), then silence. The notes are MIDI channel 1.
2. If your gear shows the MIDI clock: it should start (Start message), run at 120 bpm for the two bars, and Stop.
3. Tap **MIDI out test** again twice: same result each time, nothing stuck on. (Tapping it while it is still running just says "already running".)
4. Wait for the text to settle, then read the lines `stats:` and `core:`.

PASS: you hear the high note, then eight notes (C E G C, C E G C) evenly spaced, no stuck note, no long silence in the middle, and `stats:` shows `err 0`, `gone 0`, `hang 0`.
Please also say by ear: "tight" or "jittery / late / early" (the notes should be evenly spaced like a metronome, not wobbling).

## 3. Hot-plug
1. Start the test and, while it plays (within the first 3 seconds), unplug the USB device. Expected: the sound stops, the app does not crash. The text now shows
   `A=(not connected)`, and `devices: none`.
   The interrupted test keeps running silently until it ends (about 5 s): wait for it before tapping **MIDI out test** again (otherwise it says "already running").
   If your synth keeps sounding a note (it got a Note On and never the Note Off), switch the synth off and on, or press its panic / All Notes Off, before going on.
2. Plug it back in. Expected: within a few seconds `devices:` lists it again and the letter shows `A=n:Your Device` again by itself (the letter keeps its choice). Tap
   **MIDI out test** again: it plays as before.

## 3b. Stop against a note (stuck notes)
Use a USB synth. Start the test and tap Stop (or let the app stop the engine) just BEFORE a note is due: do it ten times, at different moments, and again at a fast tempo (the song's tempo up as far as the app lets you). PASS: after every Stop the synth is silent and no note hangs. If one note hangs, silence the synth (power it off and on), then tap **COPY MIDI** straight away and send the text, plus which try it was (first / later) and the tempo.

## 4. What to send back
Tap **COPY MIDI** (it puts the blue text on the clipboard; paste it into the chat): the full block that starts `MIDI out ports:` after one finished test, plus:
- the device name and what it is (interface / synth / Bluetooth does NOT count: USB only),
- tight or jittery, notes correct or not, clock seen or not,
- the unplug / replug result (did A show "(not connected)" and then rejoin?),
- the `core:` line's `map exact` / `fallback` / `odd` / `offset` numbers, and whether the test felt late on the FIRST tap after the engine started (the audio callback reads the AAudio timestamp every 20 ms: say if the sound ever glitched),
- anything the app did that looked wrong.

How to read the numbers (so you know what you are reporting): `sent` = messages handed to the phone's MIDI driver (about 205 for one test: 190 clock ticks + notes + Start / Stop);
`late` = messages sent more than 2 ms after their time; `nomap` should be 0; `map exact` / `fallback` say whether Android gave a precise audio timestamp (`exact` is the good
case; `fallback` means it did not and the timing is rougher); `offset ... frames` is a number I want to see (it should be 0 or a small multiple of 96); `held` is for devices that
cannot take timestamps (a USB device should show `held 0`, and its device line says `[timestamps]`); `sender prio ok` says the sender thread got its high priority.
