package app.thunderplus.nativetest

import android.app.Activity
import android.content.ContentResolver
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.provider.DocumentsContract
import android.provider.OpenableColumns
import android.util.Base64
import android.util.Log
import org.json.JSONObject
import java.io.File
import java.lang.ref.WeakReference
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.Callable
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import java.util.concurrent.TimeoutException

/**
 * N2 H3: the shell side of songs and samples on the phone. A songs folder and a samples folder are picked ONCE with the Storage Access Framework (ACTION_OPEN_DOCUMENT_TREE, the grant is persisted:
 * takePersistableUriPermission), a `.thunder` file opened from a file manager (VIEW / SEND) reaches the page as a `file.opened` event, the page asks for the newest-first list and opens, saves and
 * saves-as through `file.*` messages. The page's own song handling stays the page's (it parses the project and sends `song.load` / `kit.load` to the live engine): the shell moves BYTES and never
 * parses or rewrites the JSON (FileModel.kt: TextCodec). The model of the messages is the desktop app's `thunderDesktop.files` (BRIDGE-TODAY 4.1: open / read / save / saveAs / onOpenPath / ready).
 * The rules (sorting, changed-underneath, names, byte round trip) are pure and JVM-tested (FileModel.kt); this file only calls Android. UNVERIFIED on a device: CI compiles it.
 *
 * Threads (H3-4): requests are taken in order by ONE dispatcher thread, but each one runs on a pool thread that the dispatcher waits for with a time limit (SaveRules.timeoutMs): a provider that does not answer
 * (a cloud drive, a sleeping SD card) costs the page `{ok:false, timeout:true}` after 20 / 30 s and the next request goes on; the stuck call keeps running (its late answer is dropped, a late write may still land).
 * `file.ready` never waits behind a provider call. The picker results come back on the main thread and finish the waiting request the same way.
 * Safety (H3-1 / 2 / 3): a save writes nothing without a valid text, keeps a verified backup of the old content first (no backup, no save), checks free space, and puts the backup back at once when the write fails.
 * Everything that can throw, OutOfMemoryError included, is caught per request: the process (and the sound) never dies from a file.
 */
object SongFiles {
    private const val TAG = "ThunderFiles"
    private const val PREFS = "thunder_files"
    const val REQ_TREE = 7301
    const val REQ_CREATE = 7302
    const val REQ_OPEN = 7303
    private const val MAX_TEXT = SaveRules.MAX_TEXT        // 16 MiB: what a WebMessage and a phone's heap carry (H3-3)
    private const val MAX_SAMPLE = SaveRules.MAX_SAMPLE
    private const val KEEP_BACKUPS = 3
    private const val MAX_FILE_GRANTS = 8                  // H3-7: persisted grants of single files that stay (the folder grants always stay)

    private val exec = Executors.newSingleThreadExecutor { Thread(it, "thunder-files").also { t -> t.isDaemon = true } }          // the dispatcher: order
    private val io = Executors.newCachedThreadPool { Thread(it, "thunder-files-io").also { t -> t.isDaemon = true } }              // the provider calls
    private val quick = Executors.newSingleThreadExecutor { Thread(it, "thunder-files-quick").also { t -> t.isDaemon = true } }   // file.ready: no provider call
    private val timedOut = ConcurrentHashMap.newKeySet<Int>()
    private var act: WeakReference<Activity>? = null
    @Volatile private var app: Context? = null

    /** What the shell knows of an open file: the stamp at open / last save, the BOM flag (kept on save), the name. */
    private class Session(var stamp: Stamp, val bom: Boolean, var name: String)
    private val sessions = ConcurrentHashMap<String, Session>()

    /** A request waiting for a picker (the answer comes in onActivityResult). */
    private class Pending(val req: Int, val id: Int, val kind: String, val text: String?, val name: String?, val bom: Boolean)
    @Volatile private var pending: Pending? = null

    private val opened = ArrayList<JSONObject>()   // files opened from a file manager, waiting for the page's `file.ready`
    @Volatile private var pageReady = false

    fun attach(a: Activity) { act = WeakReference(a); app = a.applicationContext }

    /** The page was (re)loaded: it has to say `file.ready` again before it gets `file.opened`. */
    fun pageAttached() { pageReady = false }

    private fun emit(json: String) { for (l in ThunderHost.listeners) l(json) }
    private fun reply(id: Int, body: JSONObject) { if (id >= 0 && !timedOut.remove(id)) emit(body.put("t", "reply").put("id", id).toString()) }   // a request the page was already told timed out gets no second answer
    private fun prefs(ctx: Context) = ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
    private fun cr(ctx: Context): ContentResolver = ctx.contentResolver

    // ---- messages from the page ---------------------------------------------------------------------------------------------------------------------------------------------------

    fun handle(ctx: Context, raw: String) {
        val type = BridgeTranslator.typeOf(raw) ?: ""   // not a parse of the whole (up to 16 MiB) message
        if (type == "file.ready") { quick.execute { pageReady = true; flushOpened() }; return }
        val id = idOf(raw)
        exec.execute { bounded(id, SaveRules.timeoutMs(type)) { run(ctx, raw) } }
    }

    /** H3-3 / H3-4: [work] on a pool thread, waited for at most [ms]; any Throwable (OutOfMemoryError too) becomes an error reply, a timeout becomes `{timeout:true}` and the late answer is dropped. */
    private fun bounded(id: Int, ms: Long, work: () -> Unit) {
        val f = io.submit(Callable { try { work() } catch (t: Throwable) { Log.i(TAG, "request: $t"); reply(id, FileProtocol.error(SaveRules.describe(t))) } })
        try { f.get(ms, TimeUnit.MILLISECONDS) } catch (e: TimeoutException) {
            if (id >= 0) { if (timedOut.size > 256) timedOut.clear(); emit(FileProtocol.error("the storage did not answer in ${ms / 1000} s (a cloud or network folder, or a card that is asleep); a save may still complete").put("timeout", true).put("t", "reply").put("id", id).toString()); timedOut.add(id) }
        } catch (e: Exception) { Log.i(TAG, "wait: $e") }
    }

    private val ID = Regex("\"id\"\\s*:\\s*([0-9]+)")
    /** The request id without parsing a message that can be megabytes: the page writes `id` near the front (JSON.stringify keeps the member order), else near the end. */
    private fun idOf(raw: String): Int = (ID.find(raw.take(1024)) ?: ID.findAll(raw.takeLast(300)).lastOrNull())?.groupValues?.get(1)?.toIntOrNull() ?: -1

    private fun run(ctx: Context, raw: String) {
        val j = JSONObject(raw)
        val id = j.optInt("id", -1)
        when (j.optString("t")) {
            "file.ready" -> { pageReady = true; flushOpened() }
            "file.folders" -> reply(id, folders(ctx))
            "file.pick" -> startPick(ctx, id, if (j.optString("kind") == "samples") "samples" else "songs")
            "file.forget" -> { val k = if (j.optString("kind") == "samples") "samples" else "songs"; val old = prefs(ctx).getString(k + "Tree", null); prefs(ctx).edit().remove(k + "Tree").apply(); releaseTree(ctx, old); reply(id, folders(ctx)) }
            "file.list" -> reply(id, list(ctx, if (j.optString("kind") == "samples") "samples" else "songs", j.optString("dir", "")))
            "file.open" -> if (j.optBoolean("dialog", false) || !j.has("uri")) startOpenDialog(ctx, id) else reply(id, openUri(ctx, j.optString("uri"), Uri.parse(j.optString("uri"))))
            "file.read" -> reply(id, readSample(ctx, j.optString("uri")))
            "file.save" -> {
                val bad = SaveRules.refuseText(j.has("text") && !j.isNull("text"), j.optString("text"))   // H3-2
                reply(id, if (bad != null) FileProtocol.error(bad).put("noText", true) else save(ctx, j.optString("uri"), j.optString("text"), j.optBoolean("force", false)))
            }
            "file.saveAs" -> {
                val bad = SaveRules.refuseText(j.has("text") && !j.isNull("text"), j.optString("text"))
                if (bad != null) reply(id, FileProtocol.error(bad).put("noText", true)) else saveAs(ctx, id, j.optString("name"), j.optString("text"), j.optBoolean("dialog", false))
            }
            else -> Log.i(TAG, "ignored ${j.optString("t")}")
        }
    }

    // ---- the folders ------------------------------------------------------------------------------------------------------------------------------------------------------------------

    private fun treeOf(ctx: Context, kind: String): Uri? {
        val s = prefs(ctx).getString(if (kind == "samples") "samplesTree" else "songsTree", null) ?: return null
        val u = Uri.parse(s)
        // the grant must still be there (the user can revoke it in Settings, the folder can be deleted)
        return if (cr(ctx).persistedUriPermissions.any { it.uri == u && it.isReadPermission }) u else null
    }

    private fun folderJson(ctx: Context, kind: String): Any {
        val t = treeOf(ctx, kind) ?: return JSONObject.NULL
        val docUri = DocumentsContract.buildDocumentUriUsingTree(t, DocumentsContract.getTreeDocumentId(t))
        val name = query(ctx, docUri, arrayOf(DocumentsContract.Document.COLUMN_DISPLAY_NAME)) { it.getString(0) } ?: return JSONObject.NULL   // the folder is gone
        return JSONObject().put("uri", t.toString()).put("name", name)
    }

    private fun folders(ctx: Context): JSONObject = JSONObject().put("ok", true).put("songs", folderJson(ctx, "songs")).put("samples", folderJson(ctx, "samples"))

    private fun startPick(ctx: Context, id: Int, kind: String) {
        val a = act?.get() ?: run { reply(id, FileProtocol.error("no activity to show the folder picker")); return }
        if (pending != null) { reply(id, FileProtocol.error("another picker is open")); return }
        pending = Pending(REQ_TREE, id, kind, null, null, false)
        a.runOnUiThread {
            val i = Intent(Intent.ACTION_OPEN_DOCUMENT_TREE).addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_WRITE_URI_PERMISSION or Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION or Intent.FLAG_GRANT_PREFIX_URI_PERMISSION)
            try { a.startActivityForResult(i, REQ_TREE) } catch (e: Exception) { pending = null; reply(id, FileProtocol.error("no folder picker on this device")) }
        }
    }

    private fun startOpenDialog(ctx: Context, id: Int) {
        val a = act?.get() ?: run { reply(id, FileProtocol.error("no activity to show the file picker")); return }
        if (pending != null) { reply(id, FileProtocol.error("another picker is open")); return }
        pending = Pending(REQ_OPEN, id, "songs", null, null, false)
        a.runOnUiThread {
            val i = Intent(Intent.ACTION_OPEN_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType("*/*").addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_WRITE_URI_PERMISSION or Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION)
            try { a.startActivityForResult(i, REQ_OPEN) } catch (e: Exception) { pending = null; reply(id, FileProtocol.error("no file picker on this device")) }
        }
    }

    /** MainActivity.onActivityResult. Main thread: the rest runs on the worker. */
    fun onResult(ctx: Context, req: Int, resultCode: Int, data: Intent?) {
        val p = pending ?: return
        if (p.req != req) return
        pending = null
        val uri = if (resultCode == Activity.RESULT_OK) data?.data else null
        exec.execute { bounded(p.id, SaveRules.timeoutMs(if (p.req == REQ_CREATE) "file.saveAs" else "file.open")) {
            try {
                if (uri == null) { reply(p.id, JSONObject().put("ok", false).put("cancelled", true)); return@bounded }
                try { cr(ctx).takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_WRITE_URI_PERMISSION) } catch (e: Exception) { Log.i(TAG, "persist: $e") }
                when (p.req) {
                    REQ_TREE -> {
                        val k = if (p.kind == "samples") "samplesTree" else "songsTree"
                        val old = prefs(ctx).getString(k, null)
                        prefs(ctx).edit().putString(k, uri.toString()).apply()
                        if (old != null && old != uri.toString()) releaseTree(ctx, old)   // H3-7: the replaced folder's grant is no longer used
                        reply(p.id, folders(ctx).put("picked", p.kind))
                    }
                    REQ_OPEN -> reply(p.id, openUri(ctx, uri.toString(), uri))
                    REQ_CREATE -> reply(p.id, writeNew(ctx, uri, p.text ?: "", p.bom))
                }
                trimGrants(ctx)
            } catch (t: Throwable) { Log.i(TAG, "result: $t"); reply(p.id, FileProtocol.error(SaveRules.describe(t))) }
        } }
    }

    // ---- persisted grants (H3-7) -----------------------------------------------------------------------------------------------------------------------------------------------------

    private fun releaseTree(ctx: Context, tree: String?) {
        if (tree == null) return
        val u = Uri.parse(tree)
        // the other kind may use the same folder
        if (listOf("songsTree", "samplesTree").any { prefs(ctx).getString(it, null) == tree }) return
        try { cr(ctx).releasePersistableUriPermission(u, Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_WRITE_URI_PERMISSION) } catch (e: Exception) { Log.i(TAG, "release: $e") }
    }

    /** Gives back persisted grants of files opened earlier (all but the newest [MAX_FILE_GRANTS]); the songs and samples folder grants stay. */
    private fun trimGrants(ctx: Context) {
        try {
            val keep = listOf("songsTree", "samplesTree").mapNotNull { prefs(ctx).getString(it, null) }.toSet()
            val all = cr(ctx).persistedUriPermissions.map { Grant(it.uri.toString(), it.persistedTime) }
            for (g in GrantRules.toRelease(all, keep, MAX_FILE_GRANTS)) try { cr(ctx).releasePersistableUriPermission(Uri.parse(g), Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_WRITE_URI_PERMISSION) } catch (e: Exception) { }
        } catch (e: Exception) { Log.i(TAG, "trim: $e") }
    }

    // ---- listing --------------------------------------------------------------------------------------------------------------------------------------------------------------------

    private fun list(ctx: Context, kind: String, dirId: String): JSONObject {
        val tree = treeOf(ctx, kind) ?: return FileProtocol.error("no $kind folder is chosen").put("needFolder", true)
        val parent = if (dirId.isEmpty()) DocumentsContract.getTreeDocumentId(tree) else dirId
        val all = children(ctx, tree, parent) ?: return FileProtocol.error("the folder cannot be read (removed, or the permission was withdrawn)").put("needFolder", true)
        return FileProtocol.list(kind, if (kind == "samples") FileRules.browse(all) else FileRules.songsNewestFirst(all))
    }

    private fun children(ctx: Context, tree: Uri, parentDocId: String): List<FileEntry>? {
        val kids = DocumentsContract.buildChildDocumentsUriUsingTree(tree, parentDocId)
        val cols = arrayOf(DocumentsContract.Document.COLUMN_DOCUMENT_ID, DocumentsContract.Document.COLUMN_DISPLAY_NAME, DocumentsContract.Document.COLUMN_MIME_TYPE,
            DocumentsContract.Document.COLUMN_LAST_MODIFIED, DocumentsContract.Document.COLUMN_SIZE)
        val out = ArrayList<FileEntry>()
        try {
            cr(ctx).query(kids, cols, null, null, null)?.use { c ->
                while (c.moveToNext()) {
                    val docId = c.getString(0) ?: continue
                    val name = c.getString(1) ?: continue
                    val dir = c.getString(2) == DocumentsContract.Document.MIME_TYPE_DIR
                    out.add(FileEntry(DocumentsContract.buildDocumentUriUsingTree(tree, docId).toString(), docId, name, if (c.isNull(3)) 0L else c.getLong(3), if (c.isNull(4)) -1L else c.getLong(4), dir))
                }
            } ?: return null
        } catch (e: Exception) { Log.i(TAG, "children: $e"); return null }
        return out
    }

    private fun <T> query(ctx: Context, uri: Uri, cols: Array<String>, f: (android.database.Cursor) -> T): T? =
        try { cr(ctx).query(uri, cols, null, null, null)?.use { c -> if (c.moveToFirst()) f(c) else null } } catch (e: Exception) { null }

    /** The file's stamp now; null when it is gone or unreadable. */
    private fun stampOf(ctx: Context, uri: Uri): Stamp? = query(ctx, uri, arrayOf(DocumentsContract.Document.COLUMN_LAST_MODIFIED, DocumentsContract.Document.COLUMN_SIZE)) { c ->
        Stamp(if (c.isNull(0)) 0L else c.getLong(0), if (c.isNull(1)) -1L else c.getLong(1))
    } ?: query(ctx, uri, arrayOf(OpenableColumns.SIZE)) { c -> Stamp(0L, if (c.isNull(0)) -1L else c.getLong(0)) }   // a provider that is not a document provider (a VIEW intent's content URI)

    private fun nameOf(ctx: Context, uri: Uri): String = query(ctx, uri, arrayOf(OpenableColumns.DISPLAY_NAME)) { it.getString(0) } ?: (uri.lastPathSegment ?: "song").substringAfterLast('/')

    // ---- open, read, save -----------------------------------------------------------------------------------------------------------------------------------------------------------

    private fun readBytes(ctx: Context, uri: Uri, max: Long): ByteArray? {
        cr(ctx).openInputStream(uri)?.use { s ->
            val out = java.io.ByteArrayOutputStream()
            val buf = ByteArray(1 shl 16)
            var total = 0L
            while (true) { val n = s.read(buf); if (n < 0) break; total += n; if (total > max) return null; out.write(buf, 0, n) }
            return out.toByteArray()
        }
        return null
    }

    /** Opens a project file: its text for the page, and a session so a later save can tell whether the file changed underneath. */
    private fun openUri(ctx: Context, key: String, uri: Uri): JSONObject {
        val stamp = stampOf(ctx, uri)   // BEFORE the read: a write during the read then shows as changed at the next save
        if (stamp != null && stamp.size > MAX_TEXT) return FileProtocol.error("the file is ${stamp.size shr 20} MiB: bigger than the ${MAX_TEXT shr 20} MiB a song can be")   // H3-3: judged by the provider's size BEFORE anything is read
        val bytes = try { readBytes(ctx, uri, MAX_TEXT) } catch (e: Exception) { return FileProtocol.error("cannot read the file: $e") } ?: return FileProtocol.error("the file is empty, unreadable or bigger than ${MAX_TEXT shr 20} MiB")
        val d = TextCodec.decode(bytes)
        if (d.text == null) return FileProtocol.error(d.error ?: "not a text file")
        val name = nameOf(ctx, uri)
        sessions[key] = Session(stamp ?: Stamp(0, bytes.size.toLong()), d.bom, name)
        val s = sessions[key]!!.stamp
        Recovery.tracker.onOpened(key)
        return JSONObject().put("ok", true).put("uri", key).put("name", name).put("text", d.text).put("mtime", s.mtime).put("size", s.size)
    }

    private fun readSample(ctx: Context, uri: String): JSONObject {
        val u = Uri.parse(uri)
        val sz = stampOf(ctx, u)?.size ?: -1L
        if (sz > MAX_SAMPLE) return FileProtocol.error("the sample is ${sz shr 20} MiB: bigger than the ${MAX_SAMPLE shr 20} MiB that fit through the page's message (a binary route comes later)")
        val bytes = try { readBytes(ctx, u, MAX_SAMPLE) } catch (e: Exception) { return FileProtocol.error("cannot read the file: $e") } ?: return FileProtocol.error("the file is unreadable or bigger than ${MAX_SAMPLE shr 20} MiB")
        // GAP: bytes ride in the reply as base64 (a third larger); a binary `take` over the port is the later route (CORE-BRIDGE 3: render results by take)
        return JSONObject().put("ok", true).put("uri", uri).put("name", nameOf(ctx, u)).put("size", bytes.size).put("b64", Base64.encodeToString(bytes, Base64.NO_WRAP))
    }

    /** Free bytes of the storage a file lives on, when the provider shows it (a file-backed provider such as the device's own storage: fstatvfs); null when it does not. */
    private fun freeSpace(ctx: Context, uri: Uri): Long? = try {
        cr(ctx).openFileDescriptor(uri, "r")?.use { pfd -> val v = android.system.Os.fstatvfs(pfd.fileDescriptor); v.f_bavail * v.f_frsize }
    } catch (e: Throwable) { null }

    /** Writes [bytes] over the file. null = done; else the error reply. [opened] tells the caller whether the file was opened for writing (so it may have been truncated). */
    private class Overwrite(val error: JSONObject?, val opened: Boolean)
    private fun overwrite(ctx: Context, uri: Uri, bytes: ByteArray): Overwrite {
        var opened = false
        try {
            val os = cr(ctx).openOutputStream(uri, "wt") ?: return Overwrite(FileProtocol.error("the file cannot be written").put("readOnly", true), false)
            opened = true
            os.use { it.write(bytes); it.flush() }
            return Overwrite(null, true)
        } catch (e: java.io.FileNotFoundException) { return Overwrite(FileProtocol.error("the file cannot be written: ${e.message}").put("readOnly", true), opened)
        } catch (e: SecurityException) { return Overwrite(FileProtocol.error("no permission to write this file").put("readOnly", true), opened)
        } catch (e: Exception) { return Overwrite(FileProtocol.error("write failed: $e"), opened) }
    }

    /** Save in place. Refused with {changed:true} when the file is not what the page opened (unless [force]). H3-1: no verified private backup of the old content, no save (`noBackup`); not enough room (`noSpace`), no save; the new
     *  bytes are written only after the backup exists; a failed or short write puts the backup back at once (`restored`), so a song is never left truncated. */
    private fun save(ctx: Context, key: String, text: String, force: Boolean): JSONObject {
        val uri = Uri.parse(key)
        val s = sessions[key] ?: return FileProtocol.error("this file was not opened through the shell (open it again, or save as)").put("noSession", true)
        val bytes = TextCodec.encode(text, s.bom) ?: return FileProtocol.error("the song contains text that is not valid Unicode")
        if (bytes.size > MAX_TEXT) return FileProtocol.error("the song is bigger than ${MAX_TEXT shr 20} MiB: not saved")
        val now = stampOf(ctx, uri)
        val v = FileRules.underneath(s.stamp, now)
        if (v != Underneath.SAME && !force) return FileProtocol.conflict(s.stamp, now)
        var old: ByteArray? = null
        if (now != null) {   // the file exists: its content must be safe in a backup BEFORE anything is written
            if (now.size > MAX_TEXT) return FileProtocol.error("the file on disk is bigger than ${MAX_TEXT shr 20} MiB, so no backup can be kept: not saved").put("noBackup", true)
            old = try { readBytes(ctx, uri, MAX_TEXT) } catch (e: Exception) { null }
            if (old == null) return FileProtocol.error("the file on disk cannot be read, so no backup can be kept: not saved").put("noBackup", true)
            if (!SaveRules.backupSpaceOk(ctx.filesDir.usableSpace, old.size.toLong())) return FileProtocol.error("not enough free space on the phone to keep a backup: not saved").put("noBackup", true).put("noSpace", true)
            if (!Backups.make(File(ctx.filesDir, "song-backups"), Integer.toHexString(key.hashCode()), old, KEEP_BACKUPS, System.currentTimeMillis())) return FileProtocol.error("the backup could not be written: not saved").put("noBackup", true)
        }
        if (!SaveRules.targetSpaceOk(freeSpace(ctx, uri), old?.size?.toLong() ?: 0L, bytes.size.toLong())) return FileProtocol.error("not enough free space where this file is: not saved (the file is unchanged)").put("noSpace", true)
        val w = overwrite(ctx, uri, bytes)
        var failure: JSONObject? = w.error
        if (failure == null) {
            val after = stampOf(ctx, uri)
            if (after != null && after.size >= 0 && after.size != bytes.size.toLong()) failure = FileProtocol.error("the file on disk has ${after.size} bytes, expected ${bytes.size}: not saved completely").put("short", true)
            else { s.stamp = after ?: Stamp(0, bytes.size.toLong()); Recovery.tracker.onSaved(key); return JSONObject().put("ok", true).put("uri", key).put("name", s.name).put("mtime", s.stamp.mtime).put("size", bytes.size) }
        }
        // the write failed or came out short: put the previous content back now (the file may have been truncated)
        if (old != null && (w.opened || failure!!.optBoolean("short", false))) {
            val back = overwrite(ctx, uri, old)
            val st = stampOf(ctx, uri)
            val restored = back.error == null && (st == null || st.size < 0 || st.size == old.size.toLong())
            if (restored) s.stamp = st ?: Stamp(0, old.size.toLong())
            failure!!.put("restored", restored).put("error", failure!!.optString("error") + if (restored) "; the previous version was put back" else "; the previous version could NOT be put back: it is in the app's backups (song-backups)")
        }
        return failure!!
    }

    // ---- save as ---------------------------------------------------------------------------------------------------------------------------------------------------------------------

    private fun saveAs(ctx: Context, id: Int, want: String, text: String, dialog: Boolean) {
        val bytes = TextCodec.encode(text, false)
        if (bytes == null) { reply(id, FileProtocol.error("the song contains text that is not valid Unicode")); return }
        val tree = if (dialog) null else treeOf(ctx, "songs")
        val name = FileRules.songFileName(want)
        if (tree == null) {   // no songs folder (or the owner wants the dialog): the system's own "save as" screen
            val a = act?.get() ?: run { reply(id, FileProtocol.error("no activity to show the save dialog").put("needFolder", !dialog)); return }
            if (pending != null) { reply(id, FileProtocol.error("another picker is open")); return }
            pending = Pending(REQ_CREATE, id, "songs", text, name, false)
            a.runOnUiThread {
                val i = Intent(Intent.ACTION_CREATE_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType("application/octet-stream").putExtra(Intent.EXTRA_TITLE, name)
                try { a.startActivityForResult(i, REQ_CREATE) } catch (e: Exception) { pending = null; reply(id, FileProtocol.error("no save dialog on this device")) }
            }
            return
        }
        if (bytes.size > MAX_TEXT) { reply(id, FileProtocol.error("the song is bigger than ${MAX_TEXT shr 20} MiB: not saved")); return }
        val rootId = DocumentsContract.getTreeDocumentId(tree)
        val existing = children(ctx, tree, rootId)?.map { it.name } ?: return reply(id, FileProtocol.error("the songs folder cannot be read").put("needFolder", true))
        val unique = FileRules.unique(name, existing)
        val parent = DocumentsContract.buildDocumentUriUsingTree(tree, rootId)
        val made = try { DocumentsContract.createDocument(cr(ctx), parent, "application/octet-stream", unique) } catch (e: Exception) { null }   // a generic type keeps the name as typed
            ?: return reply(id, FileProtocol.error("the songs folder refused a new file"))
        reply(id, writeNew(ctx, made, text, false))
    }

    /** Writes a NEW file (just created by a provider or the system's save dialog) and opens a session on it. H3-5: when the write fails, the empty file the shell just made is deleted again (it would sit at the top of the song list). */
    private fun writeNew(ctx: Context, uri: Uri, text: String, bom: Boolean): JSONObject {
        val bytes = TextCodec.encode(text, bom) ?: run { deleteNew(ctx, uri); return FileProtocol.error("the song contains text that is not valid Unicode") }
        if (bytes.size > MAX_TEXT) { deleteNew(ctx, uri); return FileProtocol.error("the song is bigger than ${MAX_TEXT shr 20} MiB: not saved") }
        var ok = false
        try {
            val o = overwrite(ctx, uri, bytes)
            if (o.error != null) return FileProtocol.error(o.error.optString("error").ifEmpty { "the new file cannot be written" })
            val st0 = stampOf(ctx, uri)
            if (st0 != null && st0.size >= 0 && st0.size != bytes.size.toLong()) return FileProtocol.error("the new file has ${st0.size} bytes, expected ${bytes.size}: not saved completely").put("short", true)
            ok = true
        } finally { if (!ok) deleteNew(ctx, uri) }
        val key = uri.toString()
        val name = nameOf(ctx, uri)
        val st = stampOf(ctx, uri) ?: Stamp(0, bytes.size.toLong())
        sessions[key] = Session(st, bom, name)
        Recovery.tracker.onSaved(key)
        return JSONObject().put("ok", true).put("uri", key).put("name", name).put("mtime", st.mtime).put("size", bytes.size)
    }

    private fun deleteNew(ctx: Context, uri: Uri) { try { DocumentsContract.deleteDocument(cr(ctx), uri) } catch (e: Throwable) { Log.i(TAG, "delete new: $e") } }

    // ---- a file opened from a file manager ------------------------------------------------------------------------------------------------------------------------------------------

    /** MainActivity.onCreate / onNewIntent: a VIEW or SEND of a project file. Read on the worker, held until the page says `file.ready`, then sent as {t:'file.opened', ...the open reply}. */
    @Suppress("DEPRECATION")
    fun onIntent(ctx: Context, intent: Intent?) {
        val i = intent ?: return
        val uri: Uri = when (i.action) {
            Intent.ACTION_VIEW -> i.data
            Intent.ACTION_SEND -> if (android.os.Build.VERSION.SDK_INT >= 33) i.getParcelableExtra(Intent.EXTRA_STREAM, Uri::class.java) else i.getParcelableExtra<Uri>(Intent.EXTRA_STREAM)
            else -> null
        } ?: return
        val writable = (i.flags and Intent.FLAG_GRANT_WRITE_URI_PERMISSION) != 0
        if (i.flags and Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION != 0) try { cr(ctx).takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION or (if (writable) Intent.FLAG_GRANT_WRITE_URI_PERMISSION else 0)) } catch (e: Exception) { }
        i.action = null   // the same Intent is not handled again when the activity is recreated
        exec.execute {
            bounded(-1, SaveRules.QUICK_MS) {
                val r = openUri(ctx, uri.toString(), uri)
                r.put("t", "file.opened").put("writable", writable)
                synchronized(opened) { opened.add(r) }
                flushOpened()
                trimGrants(ctx)
            }
        }
    }

    private fun flushOpened() {
        if (!pageReady) return
        val send = synchronized(opened) { val l = ArrayList(opened); opened.clear(); l }
        for (r in send) emit(r.toString())
    }
}
