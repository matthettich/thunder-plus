package app.thunderplus.nativetest

import org.json.JSONObject
import java.util.Locale

/** A snapshot of A5's clock-drift logger (rtlog.h), as the native layer reports it. */
class Rt(j: JSONObject) {
    val pending = j.optInt("pending") == 1
    val sr = j.optInt("sr", 48000)
    val frames = j.optLong("frames")
    val calls = j.optLong("calls")
    val late = j.optLong("late")
    val under = j.optLong("under")
    val audioS = j.optDouble("audioS")
    val wallS = j.optDouble("wallS")
    val ppm = j.optDouble("ratioPpm")
    val gapMs = j.optDouble("maxGapMs")
    val workMs = j.optDouble("maxWorkMs")
    val pass = j.optInt("pass") == 1
    val latMs = j.optDouble("outLatMs")

    /** Seconds of audio rendered since the reset (the run clock: it counts audio frames, so it keeps time with the screen off). */
    val elapsedS: Double get() = if (sr > 0) frames.toDouble() / sr else 0.0
    val secondsPerSecond: Double get() = 1.0 + ppm / 1e6

    fun numbers(): String = String.format(Locale.US,
        "ratio %.4f s per second (%+.0f ppm)  underruns %d  late callbacks %d  longest gap %.1f ms  longest callback %.2f ms  output latency %.1f ms",
        secondsPerSecond, ppm, under, late, gapMs, workMs, latMs)

    companion object {
        fun now(): Rt = Rt(try { JSONObject(NativeEngine.nativeRtSnapshot()) } catch (e: Exception) { JSONObject("{\"pending\":1}") })
    }
}

/** One finished run (kept on screen and in the copied text). */
class RunResult(val label: String, val pass: Boolean, val numbers: String, val crossCheck: String)
