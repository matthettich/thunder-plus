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
    private val main = Handler(Looper.getMainLooper())

    @Synchronized fun ensureRunning(ctx: Context): Boolean {
        if (running) return true
        val sr = NativeEngine.nativeStart()
        if (sr <= 0) return false
        sampleRate = sr
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
            if (!any) try { Thread.sleep(5) } catch (e: InterruptedException) { }
        }
    }

    /** Ends the engine. Safe to call twice. */
    @Synchronized fun shutdown() {
        if (!running) return
        running = false
        poller?.join(500)
        poller = null
        NativeEngine.nativeStop()
    }

    /** Stops the foreground service, whose onDestroy ends the engine. */
    fun stop(ctx: Context) {
        ctx.stopService(Intent(ctx, ThunderService::class.java))
        shutdown()
    }

    val isRunning: Boolean get() = running

    /** A command from the page or the overlay (JSON text). `open` / `close` / `play` / `stop` also manage the engine and service. */
    @Synchronized fun command(ctx: Context, json: String): Boolean {
        val t = try { JSONObject(json).optString("t") } catch (e: Exception) { return false }
        return when (t) {
            "open" -> ensureRunning(ctx)
            "close" -> { main.post { stop(ctx) }; true }
            "play" -> ensureRunning(ctx) && NativeEngine.nativePost(json)
            "stop" -> { NativeEngine.nativePost(json); main.postDelayed({ stop(ctx) }, 150); true }
            else -> running && NativeEngine.nativePost(json)
        }
    }
}
