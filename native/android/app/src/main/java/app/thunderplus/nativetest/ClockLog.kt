package app.thunderplus.nativetest

import org.json.JSONObject
import java.util.Locale

/**
 * Turns the core's `clock` events (10 a second) into one summary line a second for the overlay.
 * ratio = (frames rendered / sample rate) / (wall time elapsed), cumulative from the first clock event.
 * The first second is ignored for "under" (the plan's "0 underruns after the first second").
 */
class ClockLog {
    private var have = false
    private var f0 = 0L
    private var w0 = 0L
    private var nextSec = 1
    private var underAtSecond1 = -1
    var underTotal = 0
        private set
    private val all = ArrayList<String>()
    private val last = ArrayDeque<String>()

    @Synchronized fun reset() { have = false; nextSec = 1; underAtSecond1 = -1; underTotal = 0; all.clear(); last.clear() }

    @Synchronized fun onEvent(s: String) {
        if (!s.startsWith("{\"t\":\"clock\"")) return
        val o = try { JSONObject(s) } catch (e: Exception) { return }
        val f = o.getLong("f"); val wall = o.getLong("wallNs"); val sr = o.getInt("sr")
        val lat = o.getInt("outLat"); val under = o.getInt("under")
        underTotal = under
        if (!have) { have = true; f0 = f; w0 = wall; return }
        val ws = (wall - w0) / 1e9
        if (ws < nextSec) return
        nextSec = ws.toInt() + 1
        val fs = (f - f0).toDouble() / sr
        if (underAtSecond1 < 0) underAtSecond1 = under
        val line = String.format(Locale.US, "%4ds  audio %.3fs  wall %.3fs  ratio %.5f  outLat %.1fms  under %d (after 1s: %d)",
            ws.toInt(), fs, ws, fs / ws, lat * 1000.0 / sr, under, under - underAtSecond1)
        all.add(line)
        last.addLast(line); while (last.size > 10) last.removeFirst()
    }

    @Synchronized fun lastTen(): String = last.joinToString("\n")
    @Synchronized fun everything(): String = all.joinToString("\n")
}
