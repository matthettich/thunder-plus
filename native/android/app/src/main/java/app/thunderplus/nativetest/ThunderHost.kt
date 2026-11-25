package app.thunderplus.nativetest

import android.content.Context
import android.content.Intent
import android.os.Handler
import android.os.Looper
import org.json.JSONObject
import java.util.concurrent.CopyOnWriteArrayList

/**
 * Process-wide owner of the native engine, its poller thread and the overlay's clock log.
 * Play starts the engine and the foreground service; Stop (or the notification's Stop) ends both.
 */
object ThunderHost {
    val clock = ClockLog()
    val listeners = CopyOnWriteArrayList<(String) -> Unit>()   // every core event, as JSON text
    @Volatile var sampleRate = 0
        private set
    @Volatile private var running = false
    private var poller: Thread? = null
    // N1 review A4 (UNVERIFIED at run time: no Android here). The core must never be destroyed while the poller may be inside it.
    // shutdown() waits for the poller; if a listener keeps it longer, the core is left alone and the poller itself ends the engine
    // when it leaves its loop (it is the only thread that calls nativePoll).
    private val pendingStop = java.util.concurrent.atomic.AtomicBoolean(false)
    private val main = Handler(Looper.getMainLooper())

    // ---- the owner's three 5-minute runs (A6) -------------------------------------------------------------------------
    const val RUN_SECONDS = 300
    val RUN_LABELS = listOf("SCREEN ON", "SCREEN OFF", "BACKGROUND")
    val results = java.util.concurrent.ConcurrentHashMap<String, RunResult>()
    @Volatile var activeRun: String? = null
        private set
    @Volatile var lastFinished: String? = null
        private set
    @Volatile private var appCtx: Context? = null
    private var lastRtPush = 0L

    /** Resets the logger and the clock cross-check, starts the pattern and times 5 minutes by AUDIO FRAMES (so a locked screen cannot stop the timer). */
    fun startRun(ctx: Context, label: String): Boolean {   // may wait for the live engine's shutdown: call it OFF the main thread (MainActivity does)
        if (!ensureRunning(ctx)) return false
        NativeEngine.nativeRtReset(); clock.reset()
        activeRun = label; results.remove(label); lastFinished = null
        synchronized(this) { NativeEngine.nativePost("{\"t\":\"play\"}") }   // N1 A4: one producer at a time (command() is @Synchronized too)
        return true
    }

    /** RESET: ends the current run without keeping it, stops the sound, clears the logger. */
    fun resetRun(ctx: Context) {
        activeRun = null; lastFinished = null
        if (running) command(ctx, "{\"t\":\"stop\"}")
        NativeEngine.nativeRtReset(); clock.reset()
    }

    private fun checkRun() {
        val label = activeRun ?: return
        val rt = Rt.now()
        if (rt.pending || rt.elapsedS < RUN_SECONDS) return
        results[label] = RunResult(label, rt.pass, rt.numbers(), clock.lastLine())
        lastFinished = label; activeRun = null
        appCtx?.let { c -> main.post { command(c, "{\"t\":\"stop\"}") } }
    }

    /** NOT on the main thread when the live engine may be up: the live engine's nativeShutdown can wait behind a project load. It is awaited HERE, before ThunderHost's lock is taken (the worker needs that lock to start). */
    fun ensureRunning(ctx: Context): Boolean {
        if (!running) LiveEngine.shutdownAndWait()   // N2 H1a: the live host and the N0 test core would both open the output
        return ensureRunningLocked(ctx)
    }

    @Synchronized private fun ensureRunningLocked(ctx: Context): Boolean {
        if (running) return true
        LiveEngine.shutdown()   // (already ended above; a no-op unless a live start slipped in)
        val sr = NativeEngine.nativeStart()
        if (sr <= 0) return false
        sampleRate = sr
        appCtx = ctx.applicationContext
        running = true
        clock.reset()
        poller = Thread({ pollLoop() }, "thunder-poller").also { it.start() }
        try { ctx.startForegroundService(Intent(ctx, ThunderService::class.java)) } catch (e: Exception) { /* engine still runs; the owner is told by the overlay */ }
        return true
    }

    private fun pollLoop() {
        while (running) {
            var any = false
            while (running) {
                val s = NativeEngine.nativePoll() ?: break
                any = true
                clock.onEvent(s)
                for (l in listeners) l(s)
            }
            val now = System.nanoTime()
            if (now - lastRtPush > 250_000_000L) {
                lastRtPush = now
                checkRun()
                val snap = NativeEngine.nativeRtSnapshot()
                if (snap.isNotEmpty()) { val ev = snap.replaceFirst("{", "{\"t\":\"rtlog\","); for (l in listeners) l(ev) }   // the bridge's view of the logger
            }
            if (!any) try { Thread.sleep(5) } catch (e: InterruptedException) { }
        }
        if (pendingStop.compareAndSet(true, false)) NativeEngine.nativeStop()   // shutdown() gave up waiting: the poller is out of the core now
    }

    /** Ends the engine. Safe to call twice. */
    @Synchronized fun shutdown() {
        if (!running) return
        running = false
        val p = poller
        poller = null
        try { p?.join(2000) } catch (e: InterruptedException) { }
        if (p != null && p.isAlive) {
            pendingStop.set(true)   // the poller is inside a listener: do not destroy the core under it; it ends the engine itself
            if (!p.isAlive && pendingStop.compareAndSet(true, false)) NativeEngine.nativeStop()   // it left in the meantime
            return
        }
        NativeEngine.nativeStop()
    }

    /** Stops the foreground service, whose onDestroy ends the engine. */
    fun stop(ctx: Context) {
        ctx.stopService(Intent(ctx, ThunderService::class.java))
        shutdown()
    }

    val isRunning: Boolean get() = running

    /** A command from the page or the overlay (JSON text). `open` / `close` / `play` / `stop` also manage the engine and service. */
    fun command(ctx: Context, json: String): Boolean {
        if (!running && (json.contains("\"open\"") || json.contains("\"play\""))) LiveEngine.shutdownAndWait()   // before the lock, see ensureRunning
        return commandLocked(ctx, json)
    }

    @Synchronized private fun commandLocked(ctx: Context, json: String): Boolean {
        val t = try { JSONObject(json).optString("t") } catch (e: Exception) { return false }
        return when (t) {
            "open" -> ensureRunningLocked(ctx)
            "close" -> { main.post { stop(ctx) }; true }
            "play" -> ensureRunningLocked(ctx) && NativeEngine.nativePost(json)
            "stop" -> { NativeEngine.nativePost(json); main.postDelayed({ stop(ctx) }, 150); true }
            else -> running && NativeEngine.nativePost(json)
        }
    }
}
