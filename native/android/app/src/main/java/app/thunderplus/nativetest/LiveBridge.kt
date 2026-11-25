package app.thunderplus.nativetest

import android.content.Context
import android.util.Log
import org.json.JSONObject
import java.util.concurrent.Executors
import java.util.concurrent.ScheduledFuture
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean

/**
 * N2 H1b: the shell side of CORE-BRIDGE v1 on the LIVE host. Bridge.kt hands every page message here once the page said `hello {live:true}`; [BridgeTranslator] says what each one means (pure, JVM-tested);
 * this object runs the calls on ONE worker thread, in the order the page sent them (the core's control calls are serial anyway), and sends the answers and the core's events back through
 * [ThunderHost.listeners] (Bridge.kt forwards them to the page). Nothing here is on the audio thread. UNVERIFIED on a device (CI compiles it).
 */
object LiveBridge {
    private const val TAG = "ThunderLiveBridge"
    private val exec = Executors.newSingleThreadExecutor { Thread(it, "thunder-live-bridge") }
    private val timers = Executors.newSingleThreadScheduledExecutor { Thread(it, "thunder-live-timers").also { t -> t.isDaemon = true } }
    private val puts = PutStore()
    @Volatile private var flag = false
    private var pump: ScheduledFuture<*>? = null
    private var publishTimer: ScheduledFuture<*>? = null
    private val publishPending = AtomicBoolean(false)
    @Volatile private var lastRev = 0L

    /** True once the page said hello {live:true} and while the live engine runs. */
    val liveMode: Boolean get() = flag && LiveEngine.active

    /** Events and replies go to every listener (ThunderHost.listeners: the bridge's forward). */
    private fun emit(json: String) { for (l in ThunderHost.listeners) l(json) }
    private fun err(where: String, msg: String) = emit(JSONObject().put("t", "err").put("where", where).put("msg", msg).put("fatal", false).toString())
    private fun reply(id: Int, body: JSONObject) { if (id >= 0) emit(body.put("t", "reply").put("id", id).toString()) }

    /** A page message (JSON text) or put header. Returns at once; the work runs on the worker thread in order. */
    fun handle(ctx: Context, raw: String) { exec.execute { try { runMessage(ctx, raw) } catch (e: Exception) { Log.i(TAG, "handle: $e"); err(BridgeTranslator.typeOf(raw) ?: "?", "shell error: $e") } } }
    fun onBytes(bytes: ByteArray) { exec.execute { try { onChunk(bytes) } catch (e: Exception) { Log.i(TAG, "bytes: $e") } } }

    private fun onChunk(bytes: ByteArray) {
        val (e, done) = puts.chunk(bytes)
        val pid = puts.lastPid
        if (e != null) { err("put", e); emit(JSONObject().put("t", "put.done").put("pid", pid).put("ok", false).put("info", e).toString()); return }
        if (done >= 0) { val b = puts.take(done); emit(JSONObject().put("t", "put.done").put("pid", pid).put("ok", true).put("info", "${b?.frames} frames x ${b?.ch}").toString()) }
    }

    private fun runMessage(ctx: Context, raw: String) {
        val type = BridgeTranslator.typeOf(raw)
        if (type == "put.header") { puts.header(raw)?.let { err("put", it) }; return }
        when (val tr = BridgeTranslator.translate(raw)) {
            is BridgeTranslator.Translation.Ignored -> if (tr.type.isNotEmpty()) Log.i(TAG, "ignored ${tr.type}")
            is BridgeTranslator.Translation.Refused -> { err(tr.where, tr.msg); reply(tr.replyId, JSONObject().put("err", tr.msg)) }
            is BridgeTranslator.Translation.Call -> execute(ctx, tr.call, type ?: "?")
        }
    }

    private fun check(where: String, rc: Int) { if (rc != 0) err(where, "rc $rc" + (if (rc == -9) " (not supported by this build)" else if (rc == -2) " (not allowed now: " + (if (!LiveEngine.active) "engine off" else "state") + ")" else "")) }

    private fun execute(ctx: Context, c: BridgeTranslator.LiveCall, type: String) {
        when (c) {
            is BridgeTranslator.LiveCall.Hello -> hello(ctx, c)
            is BridgeTranslator.LiveCall.SongLoad -> {
                val r = LiveNative.nativeSongLoad(c.raw)
                val ok = FlatJson.bool(r, "ok") ?: false
                if (!ok) { err("song.load", FlatJson.str(r, "err").orEmpty().ifEmpty { "rc " + FlatJson.num(r, "rc") }); return }
                lastRev = revOf(c.raw)
                emit(LiveNative.nativeFit(lastRev))
            }
            is BridgeTranslator.LiveCall.KitLoad -> {
                val r = LiveNative.nativeKitLoad(c.raw)
                if (!(FlatJson.bool(r, "ok") ?: false)) { err("kit.load", FlatJson.str(r, "err").orEmpty().ifEmpty { "rc " + FlatJson.num(r, "rc") }); return }
                emit("{\"t\":\"kit\"," + r.substring(1))   // {ok, rc, err, pads, skipped, dry, renderMs, empty:[the slots the page must put]}
                emit(LiveNative.nativeFit(lastRev))
            }
            is BridgeTranslator.LiveCall.Play -> check(type, LiveNative.nativePlayEx(c.mode, c.row, c.track, c.sect, c.phrase, c.hasLoop, c.y0, c.y1, c.t0, c.hasSeed, c.seed, c.pdc, c.onlyBits, c.hasOnly))
            is BridgeTranslator.LiveCall.Stop -> check(type, LiveNative.nativeTransportStop(c.at))
            is BridgeTranslator.LiveCall.Hold -> check(type, LiveNative.nativeHold(c.at))
            is BridgeTranslator.LiveCall.Bpm -> check(type, LiveNative.nativeBpm(c.bpm, c.at))
            is BridgeTranslator.LiveCall.Loop -> check(type, LiveNative.nativeLoop(c.has, c.y0, c.y1, c.at))
            is BridgeTranslator.LiveCall.Lead -> check(type, LiveNative.nativeLead(c.seconds, c.at))
            is BridgeTranslator.LiveCall.Perf -> check(type, LiveNative.nativePerf(c.what, c.index, c.v, c.at))
            is BridgeTranslator.LiveCall.MixSet -> check(type, LiveNative.nativeMixSet(c.track, c.key, c.value, c.ramp, c.at))
            is BridgeTranslator.LiveCall.FxPlan -> check(type, LiveNative.nativeFxPlan(c.track, c.slotsJson, c.bpm))
            is BridgeTranslator.LiveCall.FxCommit -> check(type, LiveNative.nativeFxCommit(c.track, c.at))
            is BridgeTranslator.LiveCall.FxParam -> check(type, LiveNative.nativeFxParam(c.track, c.slot, c.p, c.at))
            is BridgeTranslator.LiveCall.MasterConfig -> check(type, LiveNative.nativeMasterConfig(c.mstJson, c.safety))
            is BridgeTranslator.LiveCall.ChainEdit -> check(type, LiveNative.nativeChainEdit(c.opsJson, c.at))
            is BridgeTranslator.LiveCall.MetersWant -> check(type, LiveNative.nativeMetersWant(c.hz))
            is BridgeTranslator.LiveCall.BufFree -> puts.free(c.id)
            is BridgeTranslator.LiveCall.BufUse -> {
                val b = puts.take(c.id) ?: run { err("buf.use", "no put with id ${c.id}"); return }
                if (c.kind == "var") { check("buf.use", LiveNative.nativeVarSet(c.ii, c.vk, c.num, c.pad, b.data[0], b.frames, b.sr)); return }
                check("buf.use", LiveNative.nativePadTake(c.pad, b.data[0], if (b.ch > 1) b.data[1] else null, b.frames, b.sr))
                schedulePublish()
            }
        }
    }

    /** One pads_publish for a burst of buf.use messages (live.h: set all of a pad's variations, then publish once): 50 ms after the last. */
    private fun schedulePublish() {
        publishPending.set(true)
        publishTimer?.cancel(false)
        publishTimer = timers.schedule(Runnable { exec.execute { if (publishPending.getAndSet(false)) check("pads.publish", LiveNative.nativePadsPublish()) } }, 50, TimeUnit.MILLISECONDS)
    }

    private fun revOf(raw: String): Long = Regex("\"rev\"\\s*:\\s*(-?[0-9]+)").findAll(raw.takeLast(200)).lastOrNull()?.groupValues?.get(1)?.toLongOrNull() ?: 0L

    /** The engines the core's pad renderer has (xeng.h and the layer stack; GUESS: kept by hand, a mismatch costs a fit reason, never a wrong sound) and the effect families (GUESS). */
    private val ENGINES = listOf("layers", "tone", "modal", "swarm", "drift", "piano", "ep", "organ", "breath", "strings", "fmdrum", "wind", "phase", "partials", "skin", "chip", "table", "vector", "scanned", "resynth", "bytebeat", "acid", "mach", "blip", "bass", "vox", "lxdrum", "lxsnare", "lxcym", "lxhat")

    private fun hello(ctx: Context, h: BridgeTranslator.LiveCall.Hello) {
        if (!LiveEngine.start(ctx)) { err("hello", "the live engine did not start"); reply(h.id, JSONObject().put("err", "live engine did not start")); return }
        flag = h.live
        startPump()
        val caps = LiveNative.nativeCaps()
        val engines = org.json.JSONArray(); ENGINES.forEach { engines.put(it) }
        val body = JSONObject().put("v", 1).put("core", "n2-live").put("sampleRate", LiveEngine.sampleRate)
            .put("caps", JSONObject().put("nativeAudio", true).put("midi", false).put("link", false).put("plugins", false).put("inputs", false).put("render", false)
                .put("live", true).put("chains", LiveNative.nativeChainsOn()).put("sf", (caps and 8) != 0).put("engines", engines)
                .put("fx", org.json.JSONArray(listOf("tfx", "pump", "modfx", "master"))).put("abi", 1).put("liveCaps", caps))
        reply(h.id, body)
    }

    private fun startPump() {
        if (pump != null) return
        pump = timers.scheduleWithFixedDelay(Runnable {
            try {
                if (!LiveEngine.active) { pump?.cancel(false); pump = null; return@Runnable }
                val ev = LiveNative.nativeDrainEvents()
                if (ev.isNotEmpty()) for (line in ev.split('\n')) if (line.isNotEmpty()) emit(line)
            } catch (e: Exception) { Log.i(TAG, "pump: $e") }
        }, 20, 20, TimeUnit.MILLISECONDS)
    }

    /** Forgets what the page put (the engine was shut down or rebuilt). */
    fun reset() { flag = false; puts.clear(); pump?.cancel(false); pump = null }
}
