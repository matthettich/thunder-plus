package app.thunderplus.nativetest

// N2 H1a: the PURE parts of the live engine's Kotlin side (no android.* import, no org.json: the JVM unit tests in app/src/test run it). LiveEngine.kt feeds it what live_jni.cpp returns.

/** THUNDER_LIVE_STATE_* as the poller reports it. */
enum class LiveState(val code: Int, val label: String) {
    IDLE(0, "stopped"), PLAYING(1, "playing"), ENDED(2, "song ended"), HELD(3, "held");
    companion object { fun of(code: Int): LiveState = values().firstOrNull { it.code == code } ?: IDLE }
}

/** A flat JSON number / string reader for the one-level objects live_jni.cpp writes (no nesting except the `notes` string array, which [stringArray] reads). */
object FlatJson {
    fun num(json: String, key: String): Double? {
        val m = Regex("\"" + Regex.escape(key) + "\"\\s*:\\s*(-?[0-9]+(?:\\.[0-9]+)?(?:[eE][-+]?[0-9]+)?)").find(json) ?: return null
        return m.groupValues[1].toDoubleOrNull()
    }
    fun bool(json: String, key: String): Boolean? {
        val m = Regex("\"" + Regex.escape(key) + "\"\\s*:\\s*(true|false)").find(json) ?: return null
        return m.groupValues[1] == "true"
    }
    fun str(json: String, key: String): String? {
        val m = Regex("\"" + Regex.escape(key) + "\"\\s*:\\s*\"((?:[^\"\\\\]|\\\\.)*)\"").find(json) ?: return null
        return m.groupValues[1].replace("\\\"", "\"").replace("\\\\", "\\")
    }
    fun stringArray(json: String, key: String): List<String> {
        val start = Regex("\"" + Regex.escape(key) + "\"\\s*:\\s*\\[").find(json) ?: return emptyList()
        val rest = json.substring(start.range.last + 1)
        val end = rest.indexOf(']').let { if (it < 0) rest.length else it }
        return Regex("\"((?:[^\"\\\\]|\\\\.)*)\"").findAll(rest.substring(0, end)).map { it.groupValues[1].replace("\\\"", "\"").replace("\\\\", "\\") }.toList()
    }
}

/** What nativeLoadProject answers. [dry]: pads rendered without a pad effect the core does not host. [skipped]: pads left empty. */
data class LoadResult(val ok: Boolean, val rc: Int, val err: String, val pads: Int, val skipped: Int, val dry: Int, val renderMs: Double, val bpm: Double, val notes: List<String>) {
    /** One line for the overlay, and the message the owner reads when something is missing. */
    fun summary(): String {
        if (!ok) return "load failed (rc $rc): " + (if (err.isEmpty()) "no text" else err)
        val sb = StringBuilder("loaded: $pads pads in ${renderMs.toInt()} ms")
        if (skipped > 0) sb.append(", $skipped EMPTY (the core cannot render them)")
        if (dry > 0) sb.append(", $dry without their pad effect")
        return sb.toString()
    }
    companion object {
        fun parse(json: String): LoadResult = LoadResult(
            FlatJson.bool(json, "ok") ?: false, (FlatJson.num(json, "rc") ?: -99.0).toInt(), FlatJson.str(json, "err") ?: "",
            (FlatJson.num(json, "pads") ?: 0.0).toInt(), (FlatJson.num(json, "skipped") ?: 0.0).toInt(), (FlatJson.num(json, "dry") ?: 0.0).toInt(),
            FlatJson.num(json, "renderMs") ?: 0.0, FlatJson.num(json, "bpm") ?: 0.0, FlatJson.stringArray(json, "notes"))
    }
}

/** The poller's state (LiveEngine::statusJson). */
data class LiveStatus(val state: LiveState, val row: Int, val step: Int, val bpm: Double, val voices: Int, val under: Int, val errors: Int, val fatal: Int,
                      val lastError: String, val peakL: Double, val peakR: Double, val records: Long, val gaps: Long) {
    val dead: Boolean get() = fatal > 0
    fun line(): String = "${state.label}  row $row step $step  ${"%.1f".format(java.util.Locale.US, bpm)} bpm  voices $voices  peak ${"%.2f".format(java.util.Locale.US, maxOf(peakL, peakR))}" +
        "  underruns $under" + (if (errors > 0) "  errors $errors ($lastError)" else "") + (if (dead) "  ENGINE DEAD" else "")
    companion object {
        val EMPTY = LiveStatus(LiveState.IDLE, 0, 0, 0.0, 0, 0, 0, 0, "", 0.0, 0.0, 0, 0)
        fun parse(json: String): LiveStatus {
            if (!json.contains("\"state\"")) return EMPTY
            fun n(k: String) = FlatJson.num(json, k) ?: 0.0
            return LiveStatus(LiveState.of(n("state").toInt()), n("row").toInt(), n("step").toInt(), n("bpm"), n("voices").toInt(), n("under").toInt(), n("errors").toInt(), n("fatal").toInt(),
                FlatJson.str(json, "lastError") ?: "", n("pkL"), n("pkR"), n("records").toLong(), n("gaps").toLong())
        }
    }
}

/** What the notification and the overlay's Play / Stop do in each state (pure, so a test can say it). */
object LiveButtons {
    /** The notification's text for a state. */
    fun notificationText(s: LiveStatus, loaded: Boolean): String = when {
        s.dead -> "Engine stopped (error)"
        !loaded -> "No song loaded"
        s.state == LiveState.PLAYING || s.state == LiveState.HELD -> "Playing"
        else -> "Stopped"
    }
    /** True when a Play press should (re)start the song: loaded, and not already playing. */
    fun canPlay(s: LiveStatus, loaded: Boolean): Boolean = loaded && !s.dead && s.state != LiveState.PLAYING && s.state != LiveState.HELD
}
