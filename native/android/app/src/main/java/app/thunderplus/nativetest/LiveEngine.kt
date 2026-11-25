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

    /** Starts the engine (and the foreground service) if it is not running. false: the stream or the host did not start. */
    @Synchronized fun start(ctx: Context): Boolean {
        if (active) return true
        ThunderHost.shutdown()   // the N0 test core and the live host would both open the output
        val sr = LiveNative.nativeStart(true)
        if (sr <= 0) { Log.i(TAG, "start failed: $sr"); return false }
        sampleRate = sr; active = true; appCtx = ctx.applicationContext
        try { ctx.startForegroundService(Intent(ctx, ThunderService::class.java)) } catch (e: Exception) { Log.i(TAG, "service did not start: $e") }
        return true
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
        if (!active) return "native live engine: off"
        val l = loaded
        return "live $sampleRate Hz  " + (if (l == null) "no song loaded" else "$projectName: ${l.summary()}") + "\n" + status().line() + "\n" + gate.line() + (if (LiveSession.lastEvent.isEmpty()) "" else "\nlast device / focus event: " + LiveSession.lastEvent)
    }

    /** Runs [work] on a worker thread unless one is already running (the buttons and the notification can not queue two loads). */
    fun async(work: () -> Unit): Boolean {
        if (!busy.compareAndSet(false, true)) return false
        Thread({ try { work() } catch (e: Exception) { Log.i(TAG, "async: $e") } finally { busy.set(false) } }, "thunder-live-work").start()
        return true
    }

    @Synchronized fun shutdown() {
        if (!active) return
        active = false
        LiveBridge.reset()
        LiveNative.nativeShutdown()
        loaded = null; projectName = ""; projectFile = null; songTitle = "Thunder Plus"
        gateAbort("the engine ended")
    }

    /** Stops the service (whose onDestroy ends the engine). */
    fun quit(ctx: Context) { ctx.stopService(Intent(ctx, ThunderService::class.java)); shutdown() }
}
