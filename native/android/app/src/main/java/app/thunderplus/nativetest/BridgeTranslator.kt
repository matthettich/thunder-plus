package app.thunderplus.nativetest

import org.json.JSONArray
import org.json.JSONException
import org.json.JSONObject

// N2 H1b: the PURE translation of the page's CORE-BRIDGE v1 messages (docs/port/CORE-BRIDGE.md section 3) into thunder_live_* calls. No android.* import: LiveBridgeTest runs it on the JVM.
// THE SHELL'S READING OF THE CONTRACT (where CORE-BRIDGE gives only a message name and a field list; the page-client designer (docs/port/N2-PAGE-CLIENT.md) and this file must agree):
//   * `at` (any command): a HOST frame (a number), absent or null = NOW (-1). `play.at` is play's t0.
//   * song.load {S, vars, rev}   -> the raw message goes to the core (the S text is cut out of it unparsed): thunder_live_song_publish.
//   * kit.load {kit, rev}        -> raw message to the core: the pads it can render are rendered and published; the answer event `kit` lists the `empty` slots the page must put.
//   * song.patch, kit.patch      -> NOT supported by the live host (ruling D3: whole text only): err event, the page sends song.load / kit.load.
//   * play {mode:'song'|'sect'|'phrase'|0|1|2, row, track?, sect?, phrase?, loop?:{y0,y1}, seed?, pdc?:0|1|2 (default 1), only?:[tracks], at?} -> thunder_live_play.
//   * stop {at?}, hold {at?}, loop {has?, y0, y1, at?}, bpm {bpm|v, at?}, lead {s|v, at?}, perf {what, i, v, at?} -> thunder_live_stop / hold / loop / bpm / lead / perf.
//   * mix.set {tr: 0..31 | 'm', k: 'vol'|'trim'|'pan'|'mute'|'solo'|'out', v, ramp?, at?} -> thunder_live_mix_set (tr 'm' = 32). k 'send' has no live call: err event.
//   * CORE-BRIDGE section 7 (the lead's additions, 24 Nov): fx.plan {tr, slots, bpm} -> thunder_live_fx_plan then fx_commit at NOW; fx.set {tr, slot, p:[12 numbers], at?} -> thunder_live_fx_param (G5a: the vector form;
//     the old single-parameter form {tr, slot, p:n, v} is refused with a message); fx.commit {tr, at?} and fx.param (the same as fx.set with a vector) are kept; master.config {mst, safety?} -> master_config (G5b);
//     chain.edit {ops, at?} (G5c); pdc.set {mode} (G8); pads.commit {n} -> pads_publish (G3); pad.chop {pad, loopSecs, slices:[frames], n?} -> pad_chop + pad_hits (G4); engine.reset {} (G13);
//     song.load may be an ask: reply {ok, rev, reasons, warn} from SongFit (G14).
//   * chain.edit {ops: [..], at?} -> thunder_live_chain_edit (needs the chains host).
//   * meters.want {rate} -> thunder_live_meters_want (rate 0: off; > 30 is clamped to 30).
//   * put (header + ArrayBuffer messages, see PutStore) then buf.use {id, pad, layer, kind:'pad'|'var', ii, vk: 'm3' | 'g1' (kind letter + number: the page's key, G4)} -> thunder_live_pad_take / thunder_live_var_set; pads become audible at pads.commit (no automatic publish). buf.free {id}.
//     kinds 'sample', 'ir', 'wt', 'sf': err event (they are inputs of the core's pad render, which the page does not ask for here; 'sf' is E_UNSUPPORTED).
//   * hello (ask) {live:true} -> the live engine starts (if it did not) and the reply carries caps; without `live` the N0 test core answers as before (Bridge.kt).
//   * not on the live host (live.h NOT IN THIS API / later stages): note.on, note.off, dev.list, dev.open, rec.*, render.*, link.set, lxp, song.patch, kit.patch, fx.set: an err event (`ask`: an error reply).
object BridgeTranslator {
    const val NOW = -1L

    sealed class LiveCall {
        class SongLoad(val raw: ByteArray) : LiveCall()
        class KitLoad(val raw: ByteArray) : LiveCall()
        data class Play(val mode: Int, val row: Int, val track: Int, val sect: Int, val phrase: Int, val hasLoop: Int, val y0: Int, val y1: Int, val t0: Long,
                        val hasSeed: Int, val seed: Int, val pdc: Int, val onlyBits: Long, val hasOnly: Int) : LiveCall()
        data class Stop(val at: Long) : LiveCall()
        data class Hold(val at: Long) : LiveCall()
        data class Bpm(val bpm: Double, val at: Long) : LiveCall()
        data class Loop(val has: Int, val y0: Int, val y1: Int, val at: Long) : LiveCall()
        data class Lead(val seconds: Double, val at: Long) : LiveCall()
        data class Perf(val what: Int, val index: Int, val v: Double, val at: Long) : LiveCall()
        data class MixSet(val track: Int, val key: Int, val value: Double, val ramp: Double, val at: Long) : LiveCall()
        class FxPlan(val track: Int, val slotsJson: ByteArray, val bpm: Double) : LiveCall()
        data class FxCommit(val track: Int, val at: Long) : LiveCall()
        class FxParam(val track: Int, val slot: Int, val p: DoubleArray, val at: Long) : LiveCall()
        class MasterConfig(val mstJson: ByteArray, val safety: Int) : LiveCall()
        class ChainEdit(val opsJson: ByteArray, val at: Long) : LiveCall()
        data class MetersWant(val hz: Double) : LiveCall()
        data class PadsCommit(val n: Int) : LiveCall()
        class PadChop(val pad: Int, val loopSecs: Double, val nSlices: Int, val slices: DoubleArray) : LiveCall()
        data class PdcSet(val mode: Int) : LiveCall()
        object EngineReset : LiveCall()
        data class BufUse(val id: Int, val pad: Int, val layer: Int, val kind: String, val ii: Int, val vk: Int, val num: Int) : LiveCall()
        data class BufFree(val id: Int) : LiveCall()
        data class Hello(val id: Int, val live: Boolean) : LiveCall()
        /** H2: midi.cfg {outs:[id x4], clockOut:[bool x4], gearDelay:[ms x4], clockIn}: the four letters (A..D), their clock boxes and Gear delays (-50..200 ms, whole ms). clockIn is read and ignored (clock follow is a fallback condition). */
        data class MidiCfg(val outs: List<String>, val clockOut: List<Boolean>, val gear: List<Double>, val clockIn: Boolean) : LiveCall()
    }

    sealed class Translation {
        class Call(val call: LiveCall) : Translation()
        /** An error event for the page (`err {where, msg, fatal:false}`); [replyId] >= 0: the message was an `ask`, so an error reply carries it too. */
        data class Refused(val where: String, val msg: String, val replyId: Int = -1) : Translation()
        /** Nothing to do (a message of the N0 path, or one the page sends that the shell does not know). */
        data class Ignored(val type: String) : Translation()
    }

    private val MIX_KEYS = mapOf("vol" to 0, "trim" to 1, "pan" to 2, "mute" to 3, "solo" to 4, "out" to 6)
    private val UNSUPPORTED = mapOf(
        "song.patch" to "the live host takes the whole song: send song.load",
        "kit.patch" to "the live host takes the whole kit: send kit.load",
        "note.on" to "notes from the host are not in the live API (live.h NOT IN THIS API)", "note.off" to "notes from the host are not in the live API (live.h NOT IN THIS API)",
        "dev.list" to "audio devices are the shell's own (H1c)", "dev.open" to "audio devices are the shell's own (H1c)",
        "rec.arm" to "recording is not on the live host", "rec.start" to "recording is not on the live host", "rec.stop" to "recording is not on the live host",
        "render.start" to "renders do not go through the live host", "render.cancel" to "renders do not go through the live host", "link.set" to "Link is not on the live host", "lxp" to "the drum module command is not on the live host")

    /** The message's type without parsing the whole text (a song.load is a megabyte): the "t" member near the front, which JSON.stringify of {t: ..} writes first. */
    fun typeOf(raw: String): String? = Regex("\"t\"\\s*:\\s*\"([^\"]*)\"").find(raw.take(256))?.groupValues?.get(1)

    private fun at(j: JSONObject): Long = if (j.has("at") && !j.isNull("at")) j.optDouble("at", -1.0).toLong() else NOW
    private fun num(j: JSONObject, vararg keys: String): Double? { for (k in keys) if (j.has(k) && !j.isNull(k)) { val v = j.optDouble(k, Double.NaN); if (!v.isNaN()) return v } ; return null }
    private fun track(v: Any?): Int? = when (v) { is Number -> v.toInt().takeIf { it in 0..32 }; is String -> if (v == "m") 32 else null; else -> null }

    fun translate(raw: String): Translation {
        val t = typeOf(raw) ?: return Translation.Ignored("")
        // the two big messages are passed whole
        if (t == "song.load") return Translation.Call(LiveCall.SongLoad(raw.toByteArray(Charsets.UTF_8)))
        if (t == "kit.load") return Translation.Call(LiveCall.KitLoad(raw.toByteArray(Charsets.UTF_8)))
        val j = try { JSONObject(raw) } catch (e: JSONException) { return Translation.Refused(t, "the message is not JSON") }
        val id = if (j.has("id")) j.optInt("id", -1) else -1
        UNSUPPORTED[t]?.let { return Translation.Refused(t, it, id) }
        fun refuse(msg: String) = Translation.Refused(t, msg, id)
        fun call(c: LiveCall) = Translation.Call(c)
        return when (t) {
            "hello" -> call(LiveCall.Hello(id, j.optBoolean("live", false)))
            "play" -> {
                val mode = when (val m = j.opt("mode")) { is Number -> m.toInt(); "sect" -> 1; "phrase" -> 2; null, "song" -> 0; else -> return refuse("unknown play mode $m") }
                if (mode !in 0..2) return refuse("play mode out of range")
                val loop = j.optJSONObject("loop")
                var only = 0L; var hasOnly = 0
                j.optJSONArray("only")?.let { a -> hasOnly = 1; for (i in 0 until a.length()) { val tr = a.optInt(i, -1); if (tr in 0..31) only = only or (1L shl tr) } }
                val pdc = j.optInt("pdc", 1)
                if (pdc !in 0..2) return refuse("pdc must be 0, 1 or 2")
                call(LiveCall.Play(mode, j.optInt("row", 0), j.optInt("track", 0), j.optInt("sect", -1), j.optInt("phrase", -1),
                    if (loop != null) 1 else 0, loop?.optInt("y0", 0) ?: 0, loop?.optInt("y1", 0) ?: 0, at(j),
                    if (j.has("seed") && !j.isNull("seed")) 1 else 0, j.optLong("seed", 0).toInt(), pdc, only, hasOnly))
            }
            "stop" -> call(LiveCall.Stop(at(j)))
            "hold" -> call(LiveCall.Hold(at(j)))
            "loop" -> { val y0 = j.optInt("y0", 0); val y1 = j.optInt("y1", 0); call(LiveCall.Loop(if (j.optBoolean("has", j.has("y0"))) 1 else 0, y0, y1, at(j))) }
            "bpm" -> call(LiveCall.Bpm(num(j, "bpm", "v") ?: return refuse("bpm without a number"), at(j)))
            "lead" -> call(LiveCall.Lead(num(j, "s", "v") ?: return refuse("lead without a number"), at(j)))
            "perf" -> call(LiveCall.Perf(j.optInt("what", -1), j.optInt("i", 0), num(j, "v") ?: return refuse("perf without v"), at(j)))
            "mix.set" -> {
                val tr = track(j.opt("tr")) ?: return refuse("mix.set: tr must be 0..31 or 'm'")
                val k = j.optString("k")
                if (k == "send") return refuse("mix.set send has no live call (the page folds sends into the song text)")
                val key = MIX_KEYS[k] ?: return refuse("mix.set: unknown key '$k'")
                call(LiveCall.MixSet(tr, key, num(j, "v") ?: return refuse("mix.set without v"), num(j, "ramp") ?: -1.0, at(j)))
            }
            "fx.plan" -> {
                val tr = track(j.opt("tr")) ?: return refuse("fx.plan: bad tr")
                val slots = j.opt("slots")
                val text = when (slots) { is JSONArray -> slots.toString(); is JSONObject -> slots.toString(); is String -> slots; else -> return refuse("fx.plan without slots") }
                call(LiveCall.FxPlan(tr, text.toByteArray(Charsets.UTF_8), num(j, "bpm") ?: 120.0))
            }
            "fx.set", "fx.param" -> {
                val tr = track(j.opt("tr")) ?: return refuse("$t: bad tr")
                val p = j.optJSONArray("p") ?: return refuse("$t needs p: the slot's 12 values (a change of effect goes by fx.plan)")
                if (p.length() != 12) return refuse("$t needs exactly 12 values")
                val d = DoubleArray(12) { p.optDouble(it, Double.NaN) }
                if (d.any { it.isNaN() || it.isInfinite() }) return refuse("$t: a value is not a number")
                call(LiveCall.FxParam(tr, j.optInt("slot", -1), d, at(j)))
            }
            "midi.cfg" -> {
                val outs = j.optJSONArray("outs"); val clk = j.optJSONArray("clockOut"); val gear = j.optJSONArray("gearDelay")
                if (j.has("outs") && outs == null) return refuse("midi.cfg: outs must be an array of four ids")
                call(LiveCall.MidiCfg(
                    List(4) { i -> val v = outs?.opt(i); if (v is String) v else "" },
                    List(4) { i -> val v = clk?.opt(i); v == true || (v is Number && v.toInt() != 0) },
                    List(4) { i -> val v = gear?.opt(i); Math.round(((v as? Number)?.toDouble() ?: 0.0).coerceIn(-50.0, 200.0)).toDouble() },
                    j.opt("clockIn").let { it == true || (it is String && it.isNotEmpty()) }))
            }
            "pads.commit" -> call(LiveCall.PadsCommit(j.optInt("n", 0)))
            "pad.chop" -> {
                if (!j.has("pad")) return refuse("pad.chop needs pad")
                val s = j.optJSONArray("slices")
                val pts = DoubleArray(s?.length() ?: 0) { s!!.optDouble(it, Double.NaN) }
                if (pts.any { it.isNaN() || it < 0 }) return refuse("pad.chop: a slice point is not a number")
                val n = j.optInt("n", pts.size).coerceIn(2, 64)
                call(LiveCall.PadChop(j.optInt("pad"), num(j, "loopSecs") ?: 0.0, n, pts))
            }
            "pdc.set" -> { val m = j.optInt("mode", -1); if (m !in 0..2) return refuse("pdc.set mode must be 0, 1 or 2"); call(LiveCall.PdcSet(m)) }
            "engine.reset" -> call(LiveCall.EngineReset)
            "fx.commit" -> call(LiveCall.FxCommit(track(j.opt("tr")) ?: return refuse("fx.commit: bad tr"), at(j)))
            "master.config" -> {
                val m = j.opt("mst")
                val text = when (m) { is JSONObject -> m.toString(); is JSONArray -> m.toString(); else -> return refuse("master.config without mst") }
                call(LiveCall.MasterConfig(text.toByteArray(Charsets.UTF_8), if (j.optBoolean("safety", true)) 1 else 0))
            }
            "chain.edit" -> { val a = j.optJSONArray("ops") ?: return refuse("chain.edit without ops"); call(LiveCall.ChainEdit(a.toString().toByteArray(Charsets.UTF_8), at(j))) }
            "meters.want" -> { val r = num(j, "rate") ?: return refuse("meters.want without rate"); call(LiveCall.MetersWant(if (r < 0) 0.0 else if (r > 30) 30.0 else r)) }
            "buf.use" -> {
                val kind = j.optString("kind", "pad")
                if (kind != "pad" && kind != "var") return refuse("buf.use kind '$kind' is not an input of the live host")
                if (!j.has("id") || !j.has("pad")) return refuse("buf.use needs id and pad")
                // the page's variation key (G4): vk 'm3' = a MUT variation number 3, 'g1' = GEN number 1; `ii` the instrument
                val vk = j.optString("vk", "")
                val m = Regex("^([gm])([0-9]{1,3})$").find(vk)
                if (kind == "var" && (!j.has("ii") || m == null)) return refuse("buf.use var needs ii and vk like 'm3' or 'g1'")
                call(LiveCall.BufUse(j.optInt("id"), j.optInt("pad"), j.optInt("layer", 0), kind, j.optInt("ii", 0), m?.groupValues?.get(1)?.get(0)?.code ?: 'g'.code, m?.groupValues?.get(2)?.toInt() ?: 0))
            }
            "buf.free" -> call(LiveCall.BufFree(j.optInt("id", -1)))
            "open", "close", "put.header", "info" -> Translation.Ignored(t)   // the N0 path's messages (Bridge.kt)
            else -> Translation.Ignored(t)
        }
    }
}

/**
 * `put` (CORE-BRIDGE section 2) on the live host. The page sends, per chunk of at most 1 MB, a header message and then an ArrayBuffer:
 *   {t:'put.header', pid, id, c: channel (0 default), o: float offset in that channel, n: floats in that channel, last: true on the very last chunk of the very last channel, sr, ch, frames}
 * (`sr`, `ch`, `frames` on every header or at least the first; N0's header had only pid, id, o, n, last: the live host needs sr, and `ch` defaults to 1). The store assembles little-endian float32
 * channels; buf.use takes them out. Pure: no Android type.
 */
class PutStore(private val maxBytes: Long = 256L shl 20) {
    class Buf(val id: Int, val sr: Double, val ch: Int, val frames: Int, val data: Array<ByteArray>)
    private class Pending(val pid: Int, val id: Int, val sr: Double, val ch: Int, val frames: Int, val data: Array<ByteArray>)
    private var cur: JSONObject? = null
    private val parts = HashMap<Int, Pending>()   // by pid
    private val done = HashMap<Int, Buf>()        // by id
    var held = 0L; private set
    /** The pid of the chunk just handled (so the shell can answer `put.done {pid}` whether it worked or not). */
    var lastPid = -1; private set

    /** A header message (JSON text). Returns an error text, or null. */
    fun header(text: String): String? {
        val j = try { JSONObject(text) } catch (e: JSONException) { return "put.header is not JSON" }
        if (cur != null) return "put.header while a chunk is still expected"
        cur = j
        return null
    }

    /** The bytes that follow a header. Returns (error, completedId): an error text; completedId >= 0 when the last chunk finished the buffer. */
    fun chunk(bytes: ByteArray): Pair<String?, Int> {
        val h = cur ?: return Pair("an ArrayBuffer without a put.header", -1)
        cur = null
        val pid = h.optInt("pid", -1); val id = h.optInt("id", -1)
        lastPid = pid
        val c = h.optInt("c", 0); val o = h.optLong("o", 0); val n = h.optLong("n", 0)
        if (id < 0 || pid < 0) return Pair("put.header without id / pid", -1)
        if ((bytes.size and 3) != 0) return Pair("put chunk is not whole floats", -1)
        var p = parts[pid]
        if (p == null) {
            val ch = h.optInt("ch", 1); val sr = h.optDouble("sr", Double.NaN)
            if (ch !in 1..2) return Pair("put: ch must be 1 or 2", -1)
            if (sr.isNaN() || sr < 1 || sr > 1e6) return Pair("put.header without a sample rate (sr)", -1)
            if (n <= 0 || n > Int.MAX_VALUE / 4) return Pair("put.header n out of range", -1)
            val frames = h.optInt("frames", n.toInt())
            if (frames.toLong() != n) return Pair("put: frames and n disagree", -1)
            val need = n * 4 * ch
            if (held + need > maxBytes) return Pair("put: too much audio held", -1)
            p = Pending(pid, id, sr, ch, frames, Array(ch) { ByteArray((n * 4).toInt()) })
            parts[pid] = p; held += need
        }
        if (c !in 0 until p.ch) return Pair("put: channel $c out of range", -1)
        if (o < 0 || o * 4 + bytes.size > p.data[c].size) return Pair("put: chunk past the end of the buffer", -1)
        System.arraycopy(bytes, 0, p.data[c], (o * 4).toInt(), bytes.size)
        if (h.optBoolean("last", false)) {
            parts.remove(pid)
            done.remove(p.id)?.let { held -= it.data.sumOf { d -> d.size.toLong() } }   // a new put under the same id replaces the old
            done[p.id] = Buf(p.id, p.sr, p.ch, p.frames, p.data)
            return Pair(null, p.id)
        }
        return Pair(null, -1)
    }

    fun take(id: Int): Buf? = done[id]
    fun free(id: Int) { done.remove(id)?.let { held -= it.data.sumOf { d -> d.size.toLong() } } }
    fun clear() { cur = null; parts.clear(); done.clear(); held = 0 }
}
