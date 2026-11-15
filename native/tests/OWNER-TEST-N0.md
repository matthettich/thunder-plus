# Your test of the Android spike (about 30 minutes, on the Fold)

This checks one thing: does the sound stay on time with the screen off and with the app in the background? The test
app plays a plain click pattern (not your songs) from the phone's own sound code, so nothing in Chrome can disturb it.
It is a separate test app: it does not touch Thunder Plus on your phone, or your songs.

## 1. Install it

1. On the phone, open the link you were sent for the **test build** (GitHub, "NATIVE TEST BUILD N0 (Android)"), in Chrome.
2. Under "Assets", tap the file ending in `.apk`. Chrome says the file can harm your device: tap **Download anyway**
   (it is your own build).
3. When it finishes, tap **Open**. Android asks once to **allow Chrome to install apps**: tap Settings, switch it on, go
   back, tap **Install**. If Play Protect asks, tap **More details**, then **Install anyway**.
4. Open **Thunder Plus (native TEST)**. Say Allow to notifications when asked (needed so the playing notice can show).
5. You see the normal Thunder Plus page with a dark panel at the bottom. You do not need to use the page. The panel has:
   a big word at the top (`ready`, then `RUNNING ...`, then `PASS` or `FAIL`), a few lines of numbers, three lines
   `SCREEN ON / SCREEN OFF / BACKGROUND: not run yet`, three run buttons, and the buttons RESET, COPY RESULTS,
   Bridge test, App and Hide. Hide folds the numbers away; tap it again to bring them back.

## 2. The three 5-minute runs

Use headphones or the phone speaker, at a volume you can hear. You should hear a steady click like a metronome (four loud
clicks with softer ones between). Each run starts itself at zero, plays for exactly 5 minutes and then stops by itself,
with the answer left on the screen. The 5 minutes are counted from the sound itself, so the screen being off cannot
throw the timer off. Do the runs in any order; each button only changes its own line.

**Run 1: tap SCREEN ON 5 min.** The big word turns to `RUNNING SCREEN ON 0:12 / 5:00` and the sound starts. Leave the
phone alone with the screen on (it may dim: tap it if it locks). After 5:00 the sound stops and the big word says
`SCREEN ON: PASS` (green) or `SCREEN ON: FAIL` (red).

**Run 2: tap SCREEN OFF 5 min.** Press the power button so the screen goes dark. Wait 5 minutes plus 20 seconds, then
press the power button and unlock. The sound will have stopped on its own, and `SCREEN OFF: PASS` or `FAIL` is on the
screen. Listen while it plays: the click must carry on with no gaps.

**Run 3: tap BACKGROUND 5 min.** Press Home and use another app (Messages, a browser, anything) for 5 minutes plus 20
seconds. A "Thunder Plus (native test) Playing" notice stays in the notification shade. Come back to the test app: the
verdict is there. (You can tap the notice's Stop once, in any run, to check it works; that run is then thrown away and you
redo it.)

If you mess a run up (a phone call, you pressed the wrong button), tap **RESET** and start that run again.

## 3. What the numbers mean (what PASS is)

The line under the big word reads like `ratio 1.0000 s per second (+12 ppm)  underruns 0  late callbacks 0  longest gap 21.3 ms
longest callback 0.40 ms  output latency 14.2 ms`.

- **ratio**: how many seconds of sound the phone made per real second. It must be 1.0000, give or take 0.001 (the same
  as within 1000 ppm). The first second of each run is left out.
- **underruns**: must be 0. **late callbacks**: must be 0 (a late callback is the engine taking longer than the sound it made).
- **longest gap** and **longest callback**: just recorded; a gap of 100 ms or more is a stutter you would also hear.
- **output latency**: just recorded.
- Below that, a line "cross-check (core clock events)" is a second, independent count; ignore it unless the two disagree.

PASS needs the ratio, the underruns and the late callbacks all inside those limits. Also tell me by ear for each run:
"steady", or "gaps / rushing" and roughly when.

## 4. Send me the results

After all three runs, tap **COPY RESULTS**. The text of all three runs goes to the clipboard (and also appears in a
yellow box under the buttons, in case the clipboard refuses: then long-press the yellow text, Select all, Copy).
Paste it into the chat with me (long-press the message box, Paste), and add your by-ear notes. If a run fails, do not
retry it ten times: send the text as it is. A fail is a useful answer. If only some runs are done, COPY RESULTS still
works and says "NOT RUN" for the others.

## Optional, only if you feel like it (about 10 minutes more): the bridge test

Tap **Bridge test**. Tap "Echo test, 20 s (try)" first, then "1. Echo test, 5 min" with the screen on and the app in front
(Hide the strip for room). Then buttons 2, 3 and 4 (each moves a 53 MB buffer and takes a while). Tap **Copy results**
and paste it to me. You can skip this; it is only timing numbers for the engineering.
