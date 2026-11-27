package app.thunderplus.nativetest

/**
 * Hello fixes (26 Nov): pure state for "why is the page's native client off". No Android types, JVM-tested (HelloDiagTest).
 *
 * [PortGate]: the page's message port is handed over at most ONCE per page load (generation). The shim asks for it at document start (thunderPortAsk), onPageFinished is the fallback; whichever comes first wins,
 * the second is a no-op so a port the page already uses is never closed and replaced. [onPageStarted] opens a new generation.
 * [HelloLog]: how many hellos arrived, when the last one did, and how it ended.
 */
class PortGate {
    private var gen = 0
    private var handedGen = -1
    @Volatile var path: String = ""
        private set
    @Volatile var handedAtMs: Long = 0L
        private set

    @Synchronized fun onPageStarted() { gen++; path = ""; handedAtMs = 0L }

    /** true: the caller must attach now (first request of this load); false: the port is already out for this load. Before any onPageStarted (generation 0) the first request still counts as one load. */
    @Synchronized fun claim(via: String, nowMs: Long): Boolean {
        if (handedGen == gen) return false
        handedGen = gen; path = via; handedAtMs = nowMs
        return true
    }

    /** The attach itself failed or was not possible (feature missing): the fallback may try again. */
    @Synchronized fun release() { if (handedGen == gen) { handedGen = -1; path = "" } }

    fun line(nowMs: Long): String = if (path.isEmpty()) "port to page this load: NOT handed over yet" else "port to page this load: handed over by $path, ${HelloLog.ago(nowMs - handedAtMs)} ago"
}

class HelloLog {
    @Volatile var count = 0
        private set
    @Volatile var lastAtMs = 0L
        private set
    @Volatile var lastResult = "none"
        private set

    @Synchronized fun began(nowMs: Long) { count++; lastAtMs = nowMs; lastResult = "running" }
    @Synchronized fun ok() { lastResult = "ok" }
    @Synchronized fun failed(why: String) { lastResult = "FAILED: $why" }

    fun line(nowMs: Long): String =
        if (count == 0) "hellos from the page: 0 (the page has not asked the shell yet)"
        else "hellos from the page: $count, last ${ago(nowMs - lastAtMs)} ago: $lastResult"

    companion object {
        fun ago(ms: Long): String { val s = (if (ms < 0) 0 else ms) / 1000; return if (s < 120) "$s s" else "${s / 60} min" }
    }
}

object HelloDiag {
    val port = PortGate()
    val hello = HelloLog()
    fun text(nowMs: Long, startError: String): String =
        hello.line(nowMs) + "\n" + (if (startError.isEmpty()) "last engine start error: none" else "last engine start error: $startError") + "\n" + port.line(nowMs)
}
