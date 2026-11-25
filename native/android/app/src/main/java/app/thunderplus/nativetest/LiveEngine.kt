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
        return r
    }

    fun play(): Int = if (active && loaded != null) LiveNative.nativePlay(0) else -2
    fun stop(): Int = if (active) LiveNative.nativeTransportStop() else -2
    fun status(): LiveStatus = if (active) LiveStatus.parse(LiveNative.nativeStatus()) else LiveStatus.EMPTY

    /** One line for the overlay. */
    fun statusText(): String {
        if (!active) return "native live engine: off"
        val l = loaded
        return "live $sampleRate Hz  " + (if (l == null) "no song loaded" else "$projectName: ${l.summary()}") + "\n" + status().line()
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
        LiveNative.nativeShutdown()
        loaded = null; projectName = ""
    }

    /** Stops the service (whose onDestroy ends the engine). */
    fun quit(ctx: Context) { ctx.stopService(Intent(ctx, ThunderService::class.java)); shutdown() }
}
