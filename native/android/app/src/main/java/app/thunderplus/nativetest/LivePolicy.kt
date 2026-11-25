package app.thunderplus.nativetest

// N2 H1c: the PURE decisions of the audio-focus / device / notification side (no android.* import: the JVM tests in app/src/test run them). LiveSession.kt does the Android calls these decide.
// Every choice here is written down in docs/port/N2-NOTES.md ("H1c decisions"); the owner can change one by changing the constant or the branch it names.

/** What an audio-focus change does to the transport. */
object FocusPolicy {
    // AudioManager.AUDIOFOCUS_* values (copied so a JVM test needs no Android)
    const val GAIN = 1
    const val LOSS = -1
    const val LOSS_TRANSIENT = -2
    const val LOSS_TRANSIENT_CAN_DUCK = -3

    /** OWNER-CHANGEABLE. true: a transient loss (a call, a navigation voice) stops the song and a later GAIN starts it again from the row it was at. false: a transient loss is treated like a loss. */
    const val RESUME_AFTER_TRANSIENT = true
    /** OWNER-CHANGEABLE. The output gain while another app asks to "duck" (a notification sound, a navigation prompt): 0.25 = -12 dB, ramped by the audio thread in 150 ms. */
    const val DUCK_GAIN = 0.25f

    enum class Act {
        NONE,
        /** Another app took the audio for good: stop the transport, give the focus up, do not come back by itself. */
        STOP_FOR_GOOD,
        /** A call or similar: stop the transport and remember the row; GAIN resumes. */
        STOP_AND_REMEMBER,
        DUCK,
        /** Focus came back and nothing was remembered: only the ducking gain returns to 1. */
        UNDUCK,
        /** Focus came back after STOP_AND_REMEMBER: gain to 1 and play from the remembered row. */
        RESUME
    }

    /** [playing]: the transport is running (or held). [armed]: a STOP_AND_REMEMBER is waiting for its GAIN. [ducked]: the gain is below 1 because of a DUCK. */
    fun decide(change: Int, playing: Boolean, armed: Boolean, ducked: Boolean): Act = when (change) {
        LOSS -> if (playing || armed) Act.STOP_FOR_GOOD else Act.NONE
        LOSS_TRANSIENT -> when {
            !playing -> Act.NONE
            RESUME_AFTER_TRANSIENT -> Act.STOP_AND_REMEMBER
            else -> Act.STOP_FOR_GOOD
        }
        LOSS_TRANSIENT_CAN_DUCK -> if (playing) Act.DUCK else Act.NONE
        GAIN -> when { armed -> Act.RESUME; ducked -> Act.UNDUCK; else -> Act.NONE }
        else -> Act.NONE
    }
}

/**
 * Detects a stream that stopped calling back (a device vanished and miniaudio did not restart it). The watchdog feeds it the callback count every 250 ms or so; [stalled] is true after [limitMs] with no
 * new callback while the engine should be running. A stream that was only re-routed by Android keeps calling back: nothing is rebuilt then.
 */
class StreamWatch(private val limitMs: Long = 1000) {
    private var lastCalls = -1L
    private var sinceMs = 0L
    fun reset() { lastCalls = -1L; sinceMs = 0L }
    /** [calls]: the audio callback count (-1: no host). Returns true when the stream has been silent for the limit. */
    fun stalled(calls: Long, nowMs: Long): Boolean {
        if (calls < 0) { reset(); return false }
        if (calls != lastCalls) { lastCalls = calls; sinceMs = nowMs; return false }
        return nowMs - sinceMs >= limitMs
    }
}

/** What the notification and the media session say about the song. */
object SongTitle {
    /** The page's rule (index.html songNameDefault): "", "Song" and "Song N" are the default name, not a name somebody chose. */
    fun isDefault(n: String?): Boolean { val s = n?.trim().orEmpty(); return s.isEmpty() || Regex("^song(\\s+\\d+)?$", RegexOption.IGNORE_CASE).matches(s) }

    /** A project file name without its extension, at most 40 characters (the page's fileSongName). */
    fun fromFile(path: String?): String = (path ?: "").substringAfterLast('/').substringAfterLast('\\').replace(Regex("\\.(thunder|json)$", RegexOption.IGNORE_CASE), "").trim().take(40).trim()

    /** The title: the page's own `title` member when it sent one (it knows the file name), else the song's `name` unless that is the default, else the file name of what the shell loaded, else "Thunder Plus". */
    fun pick(title: String?, name: String?, file: String?): String = when {
        !title.isNullOrBlank() -> title.trim()
        !isDefault(name) -> name!!.trim()
        fromFile(file).isNotEmpty() -> fromFile(file)
        else -> "Thunder Plus"
    }

    /** Reads `title` and `name` out of the head of a song.load message (the page writes both near the top; only the first 4000 characters are looked at, so a big song costs nothing). */
    fun fromMessage(text: String, file: String? = null): String {
        val head = text.take(4000)
        return pick(FlatJson.str(head, "title"), FlatJson.str(head, "name"), file)
    }
}

/** The A5 timing gate (WORKPLAN H1): 10 minutes counted in AUDIO frames, so a locked screen cannot stretch it; PASS = ratio 1.0000 +- 0.001, 0 underruns, 0 late callbacks (rtlog.h decides it). */
object GateRule {
    const val SECONDS = 600
    fun elapsed(rt: Rt): Double = if (rt.pending) 0.0 else rt.elapsedS
    fun done(rt: Rt, seconds: Int = SECONDS): Boolean = !rt.pending && rt.elapsedS >= seconds
}

/** The gate's state, for the overlay and the copied results. */
sealed class GateState {
    object Idle : GateState()
    data class Running(val elapsedS: Double, val soFarOk: Boolean) : GateState()
    data class Finished(val pass: Boolean, val numbers: String) : GateState()
    data class Aborted(val reason: String, val elapsedS: Double) : GateState()
    fun line(): String = when (this) {
        Idle -> "A5 gate: not run"
        is Running -> String.format(java.util.Locale.US, "A5 gate RUNNING %d:%02d / 10:00 %s", elapsedS.toInt() / 60, elapsedS.toInt() % 60, if (soFarOk) "(so far OK)" else "(so far FAIL)")
        is Finished -> "A5 gate " + (if (pass) "PASS" else "FAIL") + ": " + numbers
        is Aborted -> String.format(java.util.Locale.US, "A5 gate ABORTED at %d:%02d: %s (not a pass)", elapsedS.toInt() / 60, elapsedS.toInt() % 60, reason)
    }
}
