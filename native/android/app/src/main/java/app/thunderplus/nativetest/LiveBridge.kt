package app.thunderplus.nativetest

import android.content.Context
import android.util.Log
import org.json.JSONObject
import java.util.concurrent.Executors
import java.util.concurrent.ScheduledFuture
import java.util.concurrent.TimeUnit

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
    /** True from the page's hello {live:true} until a hello without live (verify-n2 F16): it survives engine.reset and a failed stream reopen, so a page that talks to the live host never falls through to the N0 test core while the engine is down. */
    @Volatile private var session = false
    val liveSession: Boolean get() = session
    fun endSession() { session = false }
    /** What only the page knows and SongFit needs (songfit.h FitContext): bit 0 live transpose on (lxp st != 0), bit 1 external MIDI clock follow (midi.cfg clockIn; set by H2's MIDI handling). */
    @Volatile private var lxpActive = false
    @Volatile var clockFollow = false
    private fun fitFlags(): Int = (if (lxpActive) 1 else 0) or (if (clockFollow) 2 else 0)
    /** The fit event again (a condition the verdict depends on changed). */
    fun refit() { if (LiveEngine.active) emit(LiveNative.nativeFit(lastRev, fitFlags())) }
    private var pump: ScheduledFuture<*>? = null
    @Volatile private var lastRev = 0L

    /** True once the page said hello {live:true} and while the live engine runs. */
    val liveMode: Boolean get() = flag && LiveEngine.active

    /** Events and replies go to every listener (ThunderHost.listeners: the bridge's forward). */
    private fun emit(json: String) { for (l in ThunderHost.listeners) l(json) }
    /** H1c: the shell's own events (device change, focus) to the page. */
    fun send(json: String) = emit(json)
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
            is BridgeTranslator.Translation.Refused -> { err(tr.where, tr.msg); reply(tr.replyId, JSONObject().put("err", tr.msg).put("ok", false).put("error", tr.msg)) }
            is BridgeTranslator.Translation.Call -> execute(ctx, tr.call, type ?: "?")
        }
    }

    private fun check(where: String, rc: Int) { if (rc != 0) err(where, "rc $rc" + (if (rc == -9) " (not supported by this build)" else if (rc == -2) " (not allowed now: " + (if (!LiveEngine.active) "engine off" else "state") + ")" else "")) }

    private fun execute(ctx: Context, c: BridgeTranslator.LiveCall, type: String) {
        // The engine is down (engine.reset in progress, or a stream reopen failed and the page was told engine up:false): nothing goes to the N0 test core; the page gets an error and, for an ask, an error reply (F16)
        if (!LiveEngine.active && c !is BridgeTranslator.LiveCall.Hello && c !is BridgeTranslator.LiveCall.EngineReset) {
            val msg = "the live engine is down; the page re-sends its state after {t:'engine',up:true}"
            err(type, msg)
            if (c is BridgeTranslator.LiveCall.FxValues && c.id >= 0) reply(c.id, JSONObject().put("ok", false).put("error", "engine down"))
            if (c is BridgeTranslator.LiveCall.SongLoad) reply(idOf(String(c.raw, Charsets.UTF_8)), JSONObject().put("ok", false).put("err", msg))
            return
        }
        when (c) {
            is BridgeTranslator.LiveCall.Hello -> hello(ctx, c)
            is BridgeTranslator.LiveCall.MidiCfg -> {
                LiveMidi.apply(ctx, c).let { if (it.isNotEmpty()) err("midi.cfg", it) }
                if (c.clockIn != clockFollow) { clockFollow = c.clockIn; refit() }   // clock follow is a fallback condition (SongFit clock-follow, F6)
            }
            is BridgeTranslator.LiveCall.SongLoad -> {
                // CORE-BRIDGE 7 G14: sent as an ask, the reply is {ok, rev, reasons, warn}; ok:false is the fallback verdict (the core refused the song, or SongFit says it cannot play natively)
                val text = String(c.raw, Charsets.UTF_8)
                val id = idOf(text); lastRev = revOf(text)
                Recovery.tracker.onSongLoad(c.raw, SongTitle.fromMessage(text, LiveEngine.projectFile))   // H5-1: what a swipe-away from recents keeps if it was not saved
                val r = LiveNative.nativeSongLoad(c.raw)
                val published = FlatJson.bool(r, "ok") ?: false
                if (published) { LiveEngine.songTitle = SongTitle.fromMessage(text, LiveEngine.projectFile); LiveSession.refresh() }   // H1c: the notification and the media session say this name
                val fit = if (published) LiveNative.nativeFit(lastRev, fitFlags()) else ""
                val fitOk = published && (FlatJson.bool(fit, "ok") ?: false)
                val reasons = if (published) FlatJson.stringArray(fit, "reasonTexts") else listOf(FlatJson.str(r, "err").orEmpty().ifEmpty { "the native engine refused the song (rc " + FlatJson.num(r, "rc") + ")" })
                val warn = if (published) FlatJson.stringArray(fit, "warn") else emptyList()
                if (!published) err("song.load", reasons[0])
                if (fit.isNotEmpty()) emit(fit)
                // pending: no kit.load has reached the shell yet, so this is NOT a verdict (ok:false only because none can be given); the `fit` event after kit.load carries it (verify-n2 F6)
                reply(id, JSONObject().put("ok", fitOk).put("pending", FlatJson.bool(fit, "pending") ?: false).put("rev", lastRev).put("reasons", org.json.JSONArray(reasons)).put("warn", org.json.JSONArray(warn)))
            }
            is BridgeTranslator.LiveCall.PadsCommit -> { puts.sweep(force = true); check("pads.commit", LiveNative.nativePadsPublish()) }   // transfers still open at the commit never finished: released (F5)
            is BridgeTranslator.LiveCall.Lxp -> { val on = c.st != 0; if (on != lxpActive) { lxpActive = on; refit() } }
            is BridgeTranslator.LiveCall.PadChop -> check("pad.chop", LiveNative.nativePadChop(c.pad, c.loopSecs, c.nSlices, c.slices))
            is BridgeTranslator.LiveCall.PdcSet -> check("pdc.set", LiveNative.nativePdcSet(c.mode))
            is BridgeTranslator.LiveCall.EngineReset -> engineReset(ctx)
            is BridgeTranslator.LiveCall.KitLoad -> {
                val r = LiveNative.nativeKitLoad(c.raw)
                if (!(FlatJson.bool(r, "ok") ?: false)) { err("kit.load", FlatJson.str(r, "err").orEmpty().ifEmpty { "rc " + FlatJson.num(r, "rc") }); return }
                emit("{\"t\":\"kit\"," + r.substring(1))   // {ok, rc, err, pads, skipped, dry, renderMs, empty:[the slots the page must put]}
                emit(LiveNative.nativeFit(lastRev, fitFlags()))
            }
            is BridgeTranslator.LiveCall.Play -> if (!LiveSession.beforePlay()) err(type, "audio focus refused (a call or another app holds the audio)") else check(type, LiveNative.nativePlayEx(c.mode, c.row, c.track, c.sect, c.phrase, c.hasLoop, c.y0, c.y1, c.t0, c.hasSeed, c.seed, c.pdc, c.onlyBits, c.hasOnly))
            is BridgeTranslator.LiveCall.Stop -> { check(type, LiveNative.nativeTransportStop(c.at)); LiveSession.afterUserStop() }
            is BridgeTranslator.LiveCall.Hold -> check(type, LiveNative.nativeHold(c.at))
            is BridgeTranslator.LiveCall.Bpm -> check(type, LiveNative.nativeBpm(c.bpm, c.at))
            is BridgeTranslator.LiveCall.Loop -> check(type, LiveNative.nativeLoop(c.has, c.y0, c.y1, c.at))
            is BridgeTranslator.LiveCall.Lead -> check(type, LiveNative.nativeLead(c.seconds, c.at))
            is BridgeTranslator.LiveCall.Perf -> check(type, LiveNative.nativePerf(c.what, c.index, c.v, c.at))
            is BridgeTranslator.LiveCall.MixSet -> check(type, LiveNative.nativeMixSet(c.track, c.key, c.value, c.ramp, c.at))
            is BridgeTranslator.LiveCall.FxPlan -> {   // G5a: plan, then commit at NOW
                val rc = LiveNative.nativeFxPlan(c.track, c.slotsJson, c.bpm)
                check(type, rc)
                if (rc == 0) check("fx.commit", LiveNative.nativeFxCommit(c.track, BridgeTranslator.NOW))
            }
            is BridgeTranslator.LiveCall.FxCommit -> check(type, LiveNative.nativeFxCommit(c.track, c.at))
            is BridgeTranslator.LiveCall.FxParam -> check(type, LiveNative.nativeFxParam(c.track, c.slot, c.p, c.at))
            is BridgeTranslator.LiveCall.MasterConfig -> check(type, LiveNative.nativeMasterConfig(c.mstJson, c.safety))
            is BridgeTranslator.LiveCall.ChainEdit -> check(type, LiveNative.nativeChainEdit(c.opsJson, c.at))
            is BridgeTranslator.LiveCall.MetersWant -> check(type, LiveNative.nativeMetersWant(c.hz))
            is BridgeTranslator.LiveCall.MetersSet -> check(type, LiveNative.nativeMetersSet(c.hz, c.kinds, c.fxTracks, c.chainsTracks))   // E_ARG: a bad rate / track bit, E_STATE: no meters in this build, E_BUSY: ring full (the page re-sends its whole state on its next change)
            is BridgeTranslator.LiveCall.FxValues -> LiveNative.nativeFxValues(c.track, c.slot, c.kind, c.v, c.at).let { rc ->
                // the core refuses the EQ's phase / lin_quality and a linear-phase EQ (they ride in the song), a wrong value count, an empty or non-hosted slot; the audio is untouched.
                // An ask (id) is answered either way; without an id a refusal is an err event.
                if (c.id >= 0) reply(c.id, BridgeTranslator.fxValuesReply(rc))
                else if (rc != 0) err(type, "rc $rc" + when (rc) { -1 -> " (refused: not a hosted slot of that kind, a wrong value count, or a value the core keeps in the song)"; -2 -> " (not allowed now)"; -8 -> " (busy, not applied)"; -9 -> " (not supported by this build)"; else -> "" })
            }
            is BridgeTranslator.LiveCall.BufFree -> puts.free(c.id)
            is BridgeTranslator.LiveCall.BufUse -> {
                if (c.kind == "pad" && c.frames == 0) { check("buf.use", LiveNative.nativePadRemove(c.pad)); return }   // the page's removal: no put, zero frames (live.h pad_set: frames == 0 removes)
                val b = puts.take(c.id) ?: run { err("buf.use", "no put with id ${c.id}"); return }
                if (c.kind == "var") { check("buf.use", LiveNative.nativeVarSet(c.ii, c.vk, c.num, c.pad, b.data[0], b.frames, b.sr)); return }
                check("buf.use", LiveNative.nativePadTake(c.pad, b.data[0], if (b.ch > 1) b.data[1] else null, b.frames, b.sr))   // audible at the page's pads.commit
            }
        }
    }

    private fun idOf(raw: String): Int = Regex("\"id\"\\s*:\\s*([0-9]+)").findAll(raw.takeLast(200)).lastOrNull()?.groupValues?.get(1)?.toIntOrNull() ?: -1

    private fun revOf(raw: String): Long = Regex("\"rev\"\\s*:\\s*(-?[0-9]+)").findAll(raw.takeLast(200)).lastOrNull()?.groupValues?.get(1)?.toLongOrNull() ?: 0L



    private fun hello(ctx: Context, h: BridgeTranslator.LiveCall.Hello) {
        session = h.live
        Recovery.tracker.onHello()   // a new page session
        if (!LiveEngine.start(ctx)) { err("hello", "the live engine did not start"); reply(h.id, JSONObject().put("err", "live engine did not start")); return }
        flag = h.live
        startPump()
        val caps = LiveNative.nativeCaps()
        val engines = org.json.JSONArray(); BridgeTranslator.engineList(LiveNative.nativeHasLxr()).forEach { engines.put(it) }
        val stats = LiveNative.nativeStats()
        val body = JSONObject().put("v", 1).put("core", "n2-live").put("sampleRate", LiveEngine.sampleRate)
            .put("sr", LiveEngine.sampleRate).put("outLat", (FlatJson.num(LiveNative.nativeStatus(), "outLat") ?: 0.0).toInt()).put("host", "android")   // CORE-BRIDGE 7 G1
            .put("maxPadBytes", (FlatJson.num(stats, "padBudget") ?: (128.0 * 1048576)).toLong()).put("maxSongBytes", 16L * 1048576)
            .put("caps", JSONObject().put("nativeAudio", true).put("midi", LiveNative.nativeMidiOn()).put("link", false).put("plugins", false).put("inputs", false).put("render", false).put("files", true)
                .put("live", true).put("meters2", (caps and 0x400) != 0).put("fxValues", (caps and 0x800) != 0).put("recoveryPut", true).put("chains", LiveNative.nativeChainsOn()).put("sf", (caps and 8) != 0).put("engines", engines)
                .put("fx", org.json.JSONArray(listOf("tfx", "pump", "modfx", "master"))).put("abi", 1).put("liveCaps", caps))
        reply(h.id, body)
        LiveMidi.publish(ctx)   // H2: the page learns the ports (midi.ports) right after the hello
        Recovery.announce(ctx)  // H5-1: {t:'recovery', ...} when a swipe-away left an unsaved copy
    }

    private fun startPump() {
        if (pump != null) return
        pump = timers.scheduleWithFixedDelay(Runnable {
            try {
                if (!LiveEngine.active) { pump?.cancel(false); pump = null; return@Runnable }
                val ev = LiveNative.nativeDrainEvents()
                if (ev.isNotEmpty()) {
                    val load = "%.3f".format(java.util.Locale.US, LiveNative.nativeLoad())
                    for (line in ev.split('\n')) if (line.isNotEmpty()) emit(if (line.startsWith("{\"t\":\"clock\"")) line.dropLast(1) + ",\"load\":" + load + "}" else line)   // CORE-BRIDGE 7 G9
                }
            } catch (e: Exception) { Log.i(TAG, "pump: $e") }
        }, 20, 20, TimeUnit.MILLISECONDS)
    }

    /** CORE-BRIDGE 7 G13: stop, destroy, create. The page re-sends song, kit and pads when it sees {t:'engine', up:true}. */
    private fun engineReset(ctx: Context) {
        emit("{\"t\":\"engine\",\"up\":false}")
        val was = flag
        LiveEngine.shutdown()
        puts.clear()
        if (!LiveEngine.start(ctx)) { err("engine.reset", "the live engine did not restart"); return }
        flag = was
        startPump()
        emit("{\"t\":\"engine\",\"up\":true,\"sr\":" + LiveEngine.sampleRate + "}")
    }

    /** Forgets what the page put (the engine was shut down or rebuilt). */
    fun reset() { flag = false; puts.clear(); pump?.cancel(false); pump = null }
}
