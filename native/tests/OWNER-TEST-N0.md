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
5. You see the normal Thunder Plus page with a dark strip at the bottom: **Play, Stop, Copy, Bridge test, App, Hide**.
   The page may look as usual; it is the real app. You do not need to use it.

## 2. The three 5-minute runs

Use headphones or the phone speaker, at a volume you can hear. A click should repeat steadily, like a metronome
(120 beats a minute: four loud clicks, with softer ones between).

**Run 1: screen on.** Tap **Play**. The strip shows `RUNNING` and, once a second, a line like
`  64s  audio 64.000s  wall 64.001s  ratio 1.00000  outLat 12.3ms  under 0 (after 1s: 0)`. Leave the phone alone with the
screen on for 5 minutes (it may dim: that is fine; tap it if it locks). Listen: is the click steady, any gaps, any rush?
At 5 minutes tap **Copy**, then **Stop**.

**Run 2: screen off.** Tap **Play**. Press the power button so the screen goes dark. Wait 5 minutes (a timer helps).
Listen: does the click carry on with no gaps? Press the power button, unlock, tap **Copy**, then **Stop**.

**Run 3: in the background.** Tap **Play**. Press Home and open another app (YouTube muted, Messages, anything) and use it for
5 minutes. A "Thunder Plus (native test) Playing" notice should stay in the notification shade. Come back to the test
app, tap **Copy**, then **Stop**.

## 3. Send me the numbers

After each run, paste what **Copy** put on the clipboard into the chat with me (long-press in the message box, Paste).
That is three pastes. Add, in your own words, for each run: "steady", or "gaps / rushing" and roughly when.
Also tell me if the Stop button in the notification works (tap it during a run once).

## What counts as a pass

- Every line's **ratio** reads `1.000` (between 0.999 and 1.001) in all three runs.
- **after 1s** reads `0` (no dropouts after the first second) in all three runs.
- By ear: no gaps, no rushing.
- `outLat` (the output delay in milliseconds) is just recorded: I do not need it to be any particular number.

If a run fails, do not retry it ten times: send the paste as it is. A fail is a useful answer.

## Optional, only if you feel like it (about 10 minutes more): the bridge test

Tap **Bridge test**. Tap "Echo test, 20 s (try)" first, then "1. Echo test, 5 min" with the screen on and the app in front
(Hide the strip for room). Then buttons 2, 3 and 4 (each moves a 53 MB buffer and takes a while). Tap **Copy results**
and paste it to me. You can skip this; it is only timing numbers for the engineering.
