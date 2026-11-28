package app.thunderplus.nativetest

import android.content.Context
import android.content.Intent
import android.util.Log
import java.util.concurrent.atomic.AtomicBoolean

/**
 * N2 H1a: process-wide owner of the LIVE engine (LiveNative), the way [ThunderHost] owns the N0 test core. Start opens the stream and the foreground service; [loadAsset] renders and publishes
 * a project (call it off the main thread); [play] / [stop] are the transport (the notification's Play / Stop); [shutdown] ends the engine. The two engines never run together.
 * UNVERIFIED on a device (no Android in the build session): CI compiles it.
 */
object LiveEngine {
    private const val TAG = "ThunderLive"
    const val BUNDLED_SONG = "sounds/ELECTRO.thunder"   // copied into the APK's assets by app/build.gradle.kts (copyWebAssets: sounds/**)

    @Volatile var sampleRate = 0
        private set
    @Volatile var loaded: LoadResult? = null
        private set
    @Volatile var projectName: String = ""
        private set
    /** What the notification and the media session call the song (H1c): the page's title / name, else the loaded file's name. */
    @Volatile var songTitle: String = "Thunder Plus"
    /** The file the shell itself loaded (the bundled song): its name is the title when the song has only the default name. */
    @Volatile var projectFile: String? = null
        private set
    @Volatile var gate: GateState = GateState.Idle
        private set
    @Volatile var active = false
        private set
    private val busy = AtomicBoolean(false)
    @Volatile private var appCtx: Context? = null

    /** THUNDER_LIVE_CAP_* bits of this build (0 before the library is loaded: the call loads it). */
    fun caps(): Int = LiveNative.nativeCaps()

    /** ANR fix (25 Nov): nativeStart / nativeShutdown run on this ONE thread, in the order they were asked. nativeShutdown takes the native life lock, which a project load can hold for seconds: never on the main thread. */
    private val worker = OrderedWorker("thunder-live-life")
    private val lifeLock = Any()               // A1 / A2: guards `shutdowns` and `active` together: "check the generation and set active" and "bump the generation and clear active" never interleave
    private var shutdowns = 0                   // counts shutdown() calls: a start queued before a shutdown must not leave `active` true after it (under lifeLock)
    @Volatile private var up = false            // the native engine is started: written only on the worker
    @Volatile private var starting = false      // a start is running on the worker (the N0 core must wait for it, ThunderHost.ensureRunning)

    /** true while the live engine is up, active or being started: the N0 core must not open the output then. */
    fun isUpOrStarting(): Boolean = up || active || starting

    /** Starts the engine (and the foreground service) if it is not running. false: the stream or the host did not start. Waits for the worker (and so for a shutdown queued before it): NOT on the main thread. */
    fun start(ctx: Context): Boolean {
        val gen = synchronized(lifeLock) { shutdowns }
        return worker.call { startOnWorker(ctx, gen) }
    }

    /** Why the last start failed ("" after a good start): the hello's error reply and the debug panel say it. */
    @Volatile var lastStartError: String = ""
        private set

    private fun startOnWorker(ctx: Context, gen: Int): Boolean {
        if (up) {
            if (active) return true
            // Hello fixes (26 Nov): up but not active is stale (a racing shutdown, a stream that never finished starting): it would return false for ever. End the native side and start again in this call.
            Log.i(TAG, "stale engine (up, not active): shutting it down to start again")
            up = false
            try { LiveNative.nativeShutdown() } catch (e: Throwable) { Log.i(TAG, "stale shutdown: $e") }
        }
        if (synchronized(lifeLock) { shutdowns != gen }) { lastStartError = "a shutdown came first"; return false }   // a shutdown() came after this start was asked: nothing to open
        starting = true
        try {
            ThunderHost.shutdown()   // the N0 test core and the live host would both open the output
            val sr = LiveNative.nativeStart(true)
            if (sr <= 0) { Log.i(TAG, "start failed: $sr"); lastStartError = "nativeStart returned $sr"; return false }
            up = true
            val ok = synchronized(lifeLock) {
                if (shutdowns != gen) false else { sampleRate = sr; appCtx = ctx.applicationContext; active = true; true }
            }
            if (!ok) {   // A1: a shutdown() ran after the check above: its queued task sees `up == false` by the time it runs, so THIS thread ends the engine now (we are on the worker)
                up = false
                try { LiveNative.nativeShutdown() } catch (e: Throwable) { Log.i(TAG, "stale start shutdown: $e") }
                lastStartError = "a shutdown came first"
                return false
            }
            LiveMidi.engineUp()
            try { ctx.startForegroundService(Intent(ctx, ThunderService::class.java)) } catch (e: Exception) { Log.i(TAG, "service did not start: $e") }
            lastStartError = ""
            return true
        } catch (e: Throwable) {
            Log.i(TAG, "start threw: $e")
            lastStartError = "exception: $e"
            return false
        } finally { starting = false }
    }

    /** Reads an asset (a .thunder project), renders its pads and publishes song and pads. Blocks for seconds: NOT on the main thread. */
    fun loadAsset(ctx: Context, path: String): LoadResult {
        if (!start(ctx)) return LoadResult(false, -2, "the audio output did not start", 0, 0, 0, 0.0, 0.0, emptyList()).also { loaded = null }
        val bytes = try { ctx.assets.open(path).use { it.readBytes() } } catch (e: Exception) { return LoadResult(false, -1, "cannot read $path", 0, 0, 0, 0.0, 0.0, emptyList()) }
        val r = LoadResult.parse(LiveNative.nativeLoadProject(bytes))
        loaded = if (r.ok) r else null
        projectName = if (r.ok) path.substringAfterLast('/') else ""
        projectFile = if (r.ok) path else null
        songTitle = SongTitle.pick(null, null, projectFile)
        LiveSession.refresh()
        return r
    }

    /** Play from the first row. Asks for the audio focus first (H1c): -3 when another app holds it (a call). */
    fun play(): Int { if (!active || loaded == null) return -2; if (!LiveSession.beforePlay()) return -3; return LiveNative.nativePlay(0) }
    /** The USER's stop (button, notification, media session): ends a remembered resume, gives the focus up, cancels the A5 gate. */
    fun stop(): Int { if (!active) return -2; val rc = LiveNative.nativeTransportStop(-1L); LiveSession.afterUserStop(); return rc }

    // ---- H1c: the A5 timing gate on the LIVE callback (WORKPLAN H1: 1.00 +- 0.1 % for 10 minutes) ----
    /** Resets the logger and plays the song (it is replayed when it ends, so the 10 minutes are not silence); [gateTick] (the service's 500 ms ticker) ends it after 600 s of audio frames. */
    fun startGate(): Int {
        if (!active || loaded == null) return -2
        LiveNative.nativeRtReset()
        synchronized(gateLock) { gate = GateState.Running(0.0, true) }
        val rc = play()
        if (rc != 0) synchronized(gateLock) { gate = GateState.Aborted("play failed (rc $rc)", 0.0) }
        return rc
    }
    /** The gate moves only from the Running state this call SAW (verify-n2 F7a): an abort from another thread between the read and the write is never overwritten (an aborted run cannot end as PASS). */
    private val gateLock = Any()
    private fun gateMove(from: GateState.Running, to: GateState): Boolean = synchronized(gateLock) { if (gate === from) { gate = to; true } else false }
    fun gateAbort(reason: String) { synchronized(gateLock) { val g = gate; if (g is GateState.Running) gate = GateState.Aborted(reason, g.elapsedS) } }
    fun gateTick() {
        val g0 = gate as? GateState.Running ?: return
        if (!active) { gateAbort("the engine ended"); return }
        val rt = Rt(try { org.json.JSONObject(LiveNative.nativeRtSnapshot()) } catch (e: Exception) { return })
        if (GateRule.done(rt)) {
            if (gateMove(g0, GateState.Finished(rt.pass, rt.numbers()))) { LiveNative.nativeTransportStop(-1L); LiveSession.afterUserStop() }
            return
        }
        if (!gateMove(g0, GateState.Running(GateRule.elapsed(rt), rt.pass || rt.elapsedS < 2))) return
        val s = status()
        if (s.state == LiveState.ENDED || s.state == LiveState.IDLE) LiveNative.nativePlay(0)   // the song ended inside the 10 minutes: again
    }

    /** H1c: the output device changed and the stream did not follow. Rebuilds the stream at the engine's rate, keeps the host. false: no stream could be opened, the engine is down. */
    fun reopenStream(): Boolean {
        if (!active) return false
        gateAbort("the output stream was rebuilt")
        val r = LiveNative.nativeReopenStream()
        if (r < 0) { shutdown(); return false }
        return true
    }
    fun status(): LiveStatus = if (active) LiveStatus.parse(LiveNative.nativeStatus()) else LiveStatus.EMPTY

    /** One line for the overlay. */
    fun statusText(): String {
        val diag = HelloDiag.text(System.currentTimeMillis(), lastStartError)
        if (!active) return "native live engine: off" + (if (up) " (native side up but not active)" else "") + "\n" + diag
        val l = loaded
        return diag + "\nlive $sampleRate Hz  " + (if (l == null) "no song loaded" else "$projectName: ${l.summary()}") + "\n" + status().line() + "\n" + (try { LiveNative.nativeTune() } catch (e: Throwable) { "tuner: n/a" }) + "\n" + gate.line() + (if (LiveSession.lastEvent.isEmpty()) "" else "\nlast device / focus event: " + LiveSession.lastEvent)
    }

    /** Runs [work] on a worker thread unless one is already running (the buttons and the notification can not queue two loads). */
    fun async(work: () -> Unit): Boolean {
        if (!busy.compareAndSet(false, true)) return false
        Thread({ try { work() } catch (e: Exception) { Log.i(TAG, "async: $e") } finally { busy.set(false) } }, "thunder-live-work").start()
        return true
    }

    /** Ends the engine. Returns AT ONCE (safe on the main thread): `active` is false before it returns, so every new call is refused; nativeShutdown runs on the worker, behind anything queued before it. */
    fun shutdown() {
        synchronized(lifeLock) { shutdowns++; active = false }
        LiveBridge.reset()
        loaded = null; projectName = ""; projectFile = null; songTitle = "Thunder Plus"
        gateAbort("the engine ended")
        worker.post {
            if (!up) return@post
            up = false
            try { LiveNative.nativeShutdown() } catch (e: Throwable) { Log.i(TAG, "shutdown: $e") }
        }
    }

    /** [shutdown], then waits until nativeShutdown has finished (the N0 test core is about to open the output). NOT on the main thread, and not while holding a lock the worker needs (ThunderHost's). */
    fun shutdownAndWait() { shutdown(); worker.drain() }

    /** Stops the service (whose onDestroy ends the engine). */
    fun quit(ctx: Context) { ctx.stopService(Intent(ctx, ThunderService::class.java)); shutdown() }
}
