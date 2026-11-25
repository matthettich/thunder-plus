# Android window checklist: fold, back, keyboard, screen off (on the Fold, about 20 minutes)

What this checks: the app's window behaviour around the sound. The rule behind every item: **the interface may stop or
resize; the audio must not.** The page lives in one web view that must survive all of it; the sound engine does not
depend on the screen at all.

Needs a build from branch `n1/n2-h5` or later (any channel; the **beta** is fine). Notifications allowed. Load a song first:
open `sounds/ELECTRO` (or any song of yours) so it plays for a few minutes without you.

How to read it: **PASS** is written after each step. Anything else: write down the step number, what you saw, and (if the
app shows one) the line under "native live engine" in the dark panel. Press **COPY RESULTS** at the end and send it.

## 0. Set up

1. Open the app, load a song, press Play. Wait for the click or the song to be steady, with one hand note of the time.
2. In the dark panel the line `native live engine:` shows `playing` (or the song name).

## 1. Fold and unfold (song playing the whole time)

| # | Do | PASS looks like |
|---|---|---|
| 1.1 | Close the phone (inner screen to outer screen) while it plays. | The sound **does not stop and does not glitch** (a small dip when the display switches is not a fail; a stop is). |
| 1.2 | Look at the outer screen. | The page is there, **at the same place** (same view, same song position moving), not reloaded and not at its start page. |
| 1.3 | Open the phone again. | Same: sound continues, the page is where you left it, nothing reloaded (no white flash of loading). |
| 1.4 | Do 1.1 to 1.3 three more times quickly. | Never a stop, never a reload, never a second "playing" notification. |
| 1.5 | Rotate the phone on each screen. | The page re-fits; sound continues. |
| 1.6 | Split screen with another app (page in one half), then drag the divider. | The page re-fits; sound continues; the page is **not** reloaded. |
| 1.7 | Stop the song, fold and unfold once with nothing playing. | Page stays; no sound starts by itself. |

## 2. Back gesture and Back button

| # | Do | PASS looks like |
|---|---|---|
| 2.1 | Song playing. Swipe back from the edge (or press Back) on the main page. | The app goes to the **home screen but is not closed**: the sound **keeps playing**, the notification stays. |
| 2.2 | Open the app again from the home screen or the notification. | The page is **exactly as it was** (not reloaded). |
| 2.3 | Tap **Bridge test** in the panel, then swipe back. | You return to the app page (one step back), the sound is unchanged. |
| 2.4 | On the app page swipe back again. | As 2.1 (home, still playing). |
| 2.5 | Start the back gesture slowly and look at the preview (Android 13 and newer). | It shows the **home screen** (the app is not about to close). Let go to cancel: nothing changes. |
| 2.6 | Swipe the app away in the recent-apps list **while playing**. | Report what happens: the page goes; does the sound stop? (Not decided: expected is the notification stays and the sound continues until you press stop in it. Say what you saw.) |

## 3. Keyboard

| # | Do | PASS looks like |
|---|---|---|
| 3.1 | In the page, tap a text field (a song name, a note field). | The on-screen keyboard opens and the **field stays visible above it** (the page is pushed up or shrinks; it is not hidden behind the keys). |
| 3.2 | Type something, close the keyboard. | The page gets its full height back, nothing is left shifted. |
| 3.3 | With the song playing, repeat 3.1 and 3.2. | The sound does not hiccup when the keyboard opens or closes. |
| 3.4 | Open the keyboard, then fold the phone. | The page fits the new screen; nothing is stuck above where the keyboard was. |
| 3.5 | Attach a USB or Bluetooth **keyboard** (if you have one) and press a key that the page uses (space). | The page reacts as on the computer; the page is not reloaded when the keyboard attaches. |

## 4. Screen off (the interface may stop, the audio must not)

| # | Do | PASS looks like |
|---|---|---|
| 4.1 | Song playing. Press the power button (screen off). Wait 2 minutes. | The sound **continues without a gap** (listen the whole time). |
| 4.2 | Wake the screen. | The page is **where it was and up to date** (the song position moved on; if the page was frozen it catches up within a second or two). Not reloaded. |
| 4.3 | Screen off for **10 minutes** (let the phone sit, do not touch). | Still playing at the end; the song's tempo was not drifting (compare the beat against a metronome if you can). |
| 4.4 | Screen off, then press Stop in the **notification** or the lock-screen player. | The sound stops at once. Wake the screen: the page shows stopped. |
| 4.5 | Screen off, play from the lock-screen player. | The song plays. (If there is no Play there, say so: not required.) |
| 4.6 | Play, screen off, take the headphones out (if you have wired or Bluetooth ones). | The sound **stops** (it must not start on the speaker), the page shows stopped when you wake it. |
| 4.7 | Play, then start the **A5 gate** button (10 minutes) and switch the screen off, back on, and fold / unfold twice during it. | At the end the line says `PASS` with the underrun count 0. A `FAIL` with `under` above 0 means the sound glitched while the screen was changing: report it. |

## 5. Things to note even if they pass

- Any time the page **reloaded** or went back to its start (that is the failure this list exists for).
- Any second notification or second player.
- How long the app takes to come back after being in the background for 10 minutes or more.
- The phone model and Android version (the panel's COPY RESULTS has them).

## What a developer reads from this

Items 1.x, 3.x and 4.x test `android:configChanges` in the manifest (the activity is never re-created), the inset handling for the
keyboard and the hinge, and that the engine and the service do not depend on the activity. Items 2.x test the back handling in
`MainActivity` (`ShellPolicy.back`). The manifest's part is also checked on every CI run by `ShellPolicyTest`; the behaviour above
can only be seen on the phone.
