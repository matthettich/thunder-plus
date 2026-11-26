package app.thunderplus.nativetest

import android.content.Context
import android.util.Log
import org.json.JSONObject
import java.io.File

/**
 * N2 H5-1 (owner decision 3, 25 Nov): a swipe-away from recents while playing STOPS playback, and the shell saves what the owner had not saved. Android shows no dialog there and the page is gone, so the shell asks the page
 * nothing: it keeps the last `song.load` message the page sent (the live host's whole song snapshot, already held for SongFit) with its title, and the file the page last opened or saved. [RecoveryTracker] (pure, JVM-tested)
 * says whether it differs from what was last saved; [RecoveryStore] (java.io only, JVM-tested) writes `recovery.thunder` + `recovery.json` in the app's private files dir. On the next start the shell tells the page
 * `{t:'recovery', title, time, file}` after `hello`; `recovery.get` returns the text, `recovery.drop` deletes it (docs/port/N2-NOTES.md "Verifier fixes H3..H5").
 */
class RecoveryTracker {
    class Snapshot(val text: ByteArray, val title: String, val file: String?)

    private var last: ByteArray? = null
    private var baseline: ByteArray? = null
    private var expectBaseline = true
    private var title = ""
    private var file: String? = null

    /** A page said `hello`: a new page session; its first song.load is what it started with, so it is clean. */
    @Synchronized fun onHello() { last = null; baseline = null; expectBaseline = true; title = ""; file = null }
    /** The page's `song.load` message (UTF-8 bytes, held, not copied). */
    @Synchronized fun onSongLoad(raw: ByteArray, title: String) { last = raw; this.title = title; if (expectBaseline) { baseline = raw; expectBaseline = false } }
    /** A file was opened: the song.load that follows is the file's content, so it is clean. */
    @Synchronized fun onOpened(file: String?) { this.file = file; expectBaseline = true }
    /** `file.save` / `file.saveAs` succeeded: what the page last sent is saved. */
    @Synchronized fun onSaved(file: String?) { if (file != null) this.file = file; baseline = last; expectBaseline = false }

    /** The title and file the page last had (for a put's sidecar). */
    @Synchronized fun info(): Pair<String, String?> = Pair(title, file)

    /** What a swipe-away must keep, or null when there is nothing or nothing unsaved. A song.load that differs only in its rev counts as unsaved: a spare copy is harmless. */
    @Synchronized fun unsaved(): Snapshot? {
        val l = last ?: return null
        val b = baseline
        if (b != null && (l === b || l.contentEquals(b))) return null
        return Snapshot(l, title, file)
    }
}

/** The recovery copy on disk. Commit order: text and sidecar are written as temp files, then the text is renamed, the sidecar last (a sidecar is what says "there is a recovery"). */
class RecoveryStore(private val dir: File) {
    class Meta(val title: String, val time: Long, val file: String?)

    private val textFile get() = File(dir, "recovery.thunder")
    private val side get() = File(dir, "recovery.json")

    fun write(s: RecoveryTracker.Snapshot, now: Long): Boolean {
        val t = File(dir, "recovery.thunder.tmp"); val m = File(dir, "recovery.json.tmp")
        try {
            if (!dir.isDirectory && !dir.mkdirs()) return false
            t.writeBytes(s.text)
            if (t.length() != s.text.size.toLong()) { t.delete(); return false }
            m.writeText(encodeMeta(Meta(s.title, now, s.file)))
            if (!t.renameTo(textFile)) { textFile.delete(); if (!t.renameTo(textFile)) { t.delete(); m.delete(); return false } }
            if (!m.renameTo(side)) { side.delete(); if (!m.renameTo(side)) { m.delete(); return false } }
            return true
        } catch (e: Exception) { t.delete(); m.delete(); return false }
    }

    // M4 / PC6: the page's own project text (`recovery.put {project}`), written as it arrives (an Android kill gives no swipe callback). Same commit order: text temp -> rename, sidecar last.
    private val putText get() = File(dir, "recovery.put.thunder")
    private val putSide get() = File(dir, "recovery.put.json")

    /** Atomic: temp files, then renames (the sidecar last). false when it could not be stored (the older copy stays). */
    fun writePut(text: ByteArray, title: String, file: String?, now: Long): Boolean {
        val t = File(dir, "recovery.put.thunder.tmp"); val m = File(dir, "recovery.put.json.tmp")
        try {
            if (!dir.isDirectory && !dir.mkdirs()) return false
            t.writeBytes(text)
            if (t.length() != text.size.toLong()) { t.delete(); return false }
            m.writeText(encodeMeta(Meta(title, now, file)))
            if (!t.renameTo(putText)) { putText.delete(); if (!t.renameTo(putText)) { t.delete(); m.delete(); return false } }
            if (!m.renameTo(putSide)) { putSide.delete(); if (!m.renameTo(putSide)) { m.delete(); return false } }
            return true
        } catch (e: Exception) { t.delete(); m.delete(); return false }
    }
    fun hasPut(): Boolean = putSide.isFile && putText.isFile
    private fun putMeta(): Meta? = try { if (hasPut()) decodeMeta(putSide.readText()) else null } catch (e: Exception) { null }

    /** null when there is no (complete) recovery copy. The page's put, when there is one, comes before the song.load copy. */
    fun meta(): Meta? = putMeta() ?: try { if (side.isFile && textFile.isFile) decodeMeta(side.readText()) else null } catch (e: Exception) { null }
    fun text(): ByteArray? = try { if (putMeta() != null) putText.readBytes() else if (meta() != null) textFile.readBytes() else null } catch (e: Exception) { null }
    /** "put" (the page's project text), "song.load" (the last song message), or null. */
    fun source(): String? = if (putMeta() != null) "put" else if (meta() != null) "song.load" else null
    /** Deletes both copies. */
    fun drop() { side.delete(); textFile.delete(); dropPut() }
    /** The put only (the song was saved: what the page last put is no longer unsaved work). */
    fun dropPut() { putSide.delete(); putText.delete() }

    /** The reply to `recovery.get {id}`: `source` says which copy `text` is ("put" or "song.load"). */
    fun getReply(id: Int): JSONObject {
        val r = JSONObject().put("t", "reply").put("id", id)
        val m = meta(); val b = text(); val src = source()
        if (m == null || b == null || src == null) return r.put("ok", false).put("error", "there is no recovery copy")
        return r.put("ok", true).put("title", m.title).put("time", m.time).put("file", m.file ?: JSONObject.NULL).put("source", src).put("text", String(b, Charsets.UTF_8))
    }

    companion object {
        fun encodeMeta(m: Meta): String = JSONObject().put("title", m.title).put("time", m.time).also { if (m.file != null) it.put("file", m.file) }.toString()
        fun decodeMeta(s: String): Meta? = try { val j = JSONObject(s); Meta(j.optString("title", ""), j.optLong("time", 0L), if (j.has("file") && !j.isNull("file")) j.optString("file") else null) } catch (e: Exception) { null }
        /** The `recovery` event that follows `hello`. */
        fun event(m: Meta): String = JSONObject().put("t", "recovery").put("title", m.title).put("time", m.time).put("file", m.file ?: JSONObject.NULL).toString()
    }
}

/** The shell's recovery: one tracker, the store in filesDir, the three messages. */
object Recovery {
    private const val TAG = "ThunderRecovery"
    val tracker = RecoveryTracker()
    private val io = java.util.concurrent.Executors.newSingleThreadExecutor { Thread(it, "thunder-recovery").also { t -> t.isDaemon = true } }
    private fun store(ctx: Context) = RecoveryStore(ctx.filesDir)
    /** The page's own limit (RCV_MAX): a longer project text is not stored; the song.load copy stays. */
    const val PUT_MAX_CHARS = 24_000_000

    /** `file.save` / `file.saveAs` succeeded: the tracker's baseline moves, and the page's last put (older than the save) is deleted. */
    fun onSaved(ctx: Context, key: String?) { tracker.onSaved(key); io.execute { try { store(ctx).dropPut() } catch (t: Throwable) { Log.i(TAG, "onSaved: $t") } } }

    /** ThunderService.onTaskRemoved (off the main thread): keep what was not saved. Never throws. */
    fun saveForTaskRemoved(ctx: Context) {
        try { tracker.unsaved()?.let { if (!store(ctx).write(it, System.currentTimeMillis())) Log.i(TAG, "recovery copy not written") } } catch (t: Throwable) { Log.i(TAG, "recovery: $t") }
    }

    /** After `hello` (live or N0): tell the page there is a recovery copy. */
    fun announce(ctx: Context) {
        io.execute { try { store(ctx).meta()?.let { for (l in ThunderHost.listeners) l(RecoveryStore.event(it)) } } catch (t: Throwable) { Log.i(TAG, "announce: $t") } }
    }

    /** The project text of a `recovery.put` message, or null: not a string, empty, or above the page's cap (the song.load copy stays then). */
    fun putText(j: JSONObject): String? = (j.opt("project") as? String)?.takeIf { it.isNotEmpty() && it.length <= PUT_MAX_CHARS }

    /** `recovery.put {project}`: stored now, off the main and audio threads (this runs on the recovery thread). Over the cap, not a string, or a failed write: ignored, the older copy stays. */
    private fun put(s: RecoveryStore, j: JSONObject) {
        val p = putText(j) ?: return
        val (title, file) = tracker.info()
        if (!s.writePut(p.toByteArray(Charsets.UTF_8), title, file, System.currentTimeMillis())) Log.i(TAG, "recovery.put not written")
    }

    /** `recovery.get {id}` -> `{t:'reply', id, ok, title, time, file, source:'put'|'song.load', text}`; `recovery.drop {id}` -> `{t:'reply', id, ok}`. */
    fun handle(ctx: Context, raw: String) {
        io.execute {
            try {
                val j = JSONObject(raw); val id = j.optInt("id", -1)
                val s = store(ctx)
                var r = JSONObject().put("t", "reply").put("id", id)
                when (j.optString("t")) {
                    "recovery.get" -> r = s.getReply(id)
                    "recovery.drop" -> { s.drop(); r.put("ok", true) }
                    "recovery.put" -> { put(s, j); return@execute }   // no reply
                    else -> return@execute
                }
                if (id >= 0) for (l in ThunderHost.listeners) l(r.toString())
            } catch (t: Throwable) { Log.i(TAG, "handle: $t") }
        }
    }
}
