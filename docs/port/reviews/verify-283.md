# verify-283: independent review of PR #283 (U37 P2 LiveHost core)

VERDICT: (PARTIAL, in progress; read the findings so far)

## Findings so far (partial 1)

F1 (should-fix, Android blocking) BACKWARD RE-ANCHOR HANGS THE SONG. live_host.cpp:490-499 reanchor() only restarts the quantum grid. The Seq's own time (seconds) is not touched, and nextClockAt_ / nextPosAt_ / nextLateAt_ (set once at :478), endStopAt_ and pend_ (timed commands) keep absolute frame values. Sequence: play; process(frame0 = 480000) ... then the shell restarts its stream and calls process(frame0 = 0). runner.h:64 steps only while `s->time() < now + lead`, and the catch-up skip fires only when the Seq is BEHIND (`time < now - 0.03`). Seq.time() is ~10 s ahead, so no step runs for ~10 s: PLAYING and silent (voices queued earlier ring out), no CLOCK / POS / LATE records for that time either, the ENDED timer (endStopAt_) and far `at` commands wait as long. A forward gap is fine (runner skip). Fix: on a re-anchor with skipped < 0, shift the Seq (`seq->shift(t0new + 0.02 - seq->time())` or equivalent), and reset nextClockAt_/nextPosAt_/nextLateAt_ = frame0 and rebase endStopAt_; document. Test: process to 10 s, then frame0 = 0, assert steps keep coming within 0.2 s.

F2 (should-fix) poll() CAN DELIVER OUT OF seq ORDER (live_host.cpp ~640-660). It peeks evCrit_ head, then evRing_ head, then picks the lower seq. Sequence: consumer loads evCrit_.head (empty); audio posts STATE (seq 5, critical) then ROWS (seq 6); consumer loads evRing_.head (sees 6) and delivers 6; the next poll delivers 5. The two ring heads are not one atomic view. Fix: after choosing a record from one ring re-load the other ring's head and re-compare (or take the critical head last; a second look at the first ring settles it).

F3 (should-fix) NON-FATAL ERRORs ARE ON THE CRITICAL RING. REANCHOR (:490) and CMD (:538) are `THUNDER_LIVE_EV_ERROR`, so post() (:773) sends them to evCrit_ (64 slots) together with STATE / HELD / SONG / PADS. A shell with a jittery counter re-anchors on every call (every 2.7 ms): 64 records in 0.17 s fills the ring if poll is not called that often, and then STATE (STOPPED / HELD) is the record that is refused (evCrit_ refuses the newest). A 65-command burst posts one CMD error per quantum, too. Fix: rate-limit non-fatal ERROR records (one per N quanta, count the rest in a field), or put non-fatal ERRORs on the ordinary ring.

F4 (note/should-fix, for P4) ENDED TIMER STARTS FROM THE QUANTUM THAT FINDS THE END (live_host.cpp:710-727, departure 7). `lead` accepts 0.05 .. 2 s (:309). With lead = 2 s the step that ends the song is stepped up to 2 s ahead of t0, so STOPPED (arg 1; later the MIDI stop) is posted at t0 + 0.3 = endT - 1.7 s: BEFORE the last notes sound. With the default lead the early amount is ~lead. Fix: endStopAt_ = max(t0, endT) + 0.3 (departure 7's "one line").

F5 (note) songPublish IDLE path returns E_BUSY (:131) AFTER replacing songCur_ / varsCur_ / songRev_ (live_host.h says a SongAck failure keeps the publish): contract ERRORS says "a failed call changes nothing". A caller that plays before its retry plays the new song. Either return OK and count the missing ack, or restore songCur_.

F6 (note) control state after HELD / ENDED->IDLE: controlPlaying_ stays true (only stop() clears it), so a songPublish after the song ran out still builds a successor snapshot on the dead session (session bytes grow; harmless but wasted; stats.session_full may fire). Accept or clear it from the STOPPED record in poll.

F7 (note) build-r.log (189 lines) is committed at the repo root of the PR.

(Further items in progress.)
