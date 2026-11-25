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
import java.util.concurrent.Executors

/**
 * N2 H3: the shell side of songs and samples on the phone. A songs folder and a samples folder are picked ONCE with the Storage Access Framework (ACTION_OPEN_DOCUMENT_TREE, the grant is persisted:
 * takePersistableUriPermission), a `.thunder` file opened from a file manager (VIEW / SEND) reaches the page as a `file.opened` event, the page asks for the newest-first list and opens, saves and
 * saves-as through `file.*` messages. The page's own song handling stays the page's (it parses the project and sends `song.load` / `kit.load` to the live engine): the shell moves BYTES and never
 * parses or rewrites the JSON (FileModel.kt: TextCodec). The model of the messages is the desktop app's `thunderDesktop.files` (BRIDGE-TODAY 4.1: open / read / save / saveAs / onOpenPath / ready).
 * The rules (sorting, changed-underneath, names, byte round trip) are pure and JVM-tested (FileModel.kt); this file only calls Android. UNVERIFIED on a device: CI compiles it.
 *
 * Threads: every message runs on ONE worker thread (the SAF calls block); the picker results come back on the main thread and finish the waiting request on the worker.
 */
object SongFiles {
    private const val TAG = "ThunderFiles"
    private const val PREFS = "thunder_files"
    const val REQ_TREE = 7301
    const val REQ_CREATE = 7302
    const val REQ_OPEN = 7303
    private const val MAX_TEXT = 128L shl 20      // a project bigger than this is refused (it would not fit a WebMessage anyway)
    private const val MAX_SAMPLE = 64L shl 20
    private const val KEEP_BACKUPS = 3

    private val exec = Executors.newSingleThreadExecutor { Thread(it, "thunder-files").also { t -> t.isDaemon = true } }
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
    private fun reply(id: Int, body: JSONObject) { if (id >= 0) emit(body.put("t", "reply").put("id", id).toString()) }
    private fun prefs(ctx: Context) = ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
    private fun cr(ctx: Context): ContentResolver = ctx.contentResolver

    // ---- messages from the page ---------------------------------------------------------------------------------------------------------------------------------------------------

    fun handle(ctx: Context, raw: String) { exec.execute { try { run(ctx, raw) } catch (e: Exception) { Log.i(TAG, "handle: $e"); val id = idOf(raw); reply(id, FileProtocol.error("shell error: $e")) } } }

    private fun idOf(raw: String): Int = try { JSONObject(raw).optInt("id", -1) } catch (e: Exception) { -1 }

    private fun run(ctx: Context, raw: String) {
        val j = JSONObject(raw)
        val id = j.optInt("id", -1)
        when (j.optString("t")) {
            "file.ready" -> { pageReady = true; flushOpened() }
            "file.folders" -> reply(id, folders(ctx))
            "file.pick" -> startPick(ctx, id, if (j.optString("kind") == "samples") "samples" else "songs")
            "file.forget" -> { prefs(ctx).edit().remove(if (j.optString("kind") == "samples") "samplesTree" else "songsTree").apply(); reply(id, folders(ctx)) }
            "file.list" -> reply(id, list(ctx, if (j.optString("kind") == "samples") "samples" else "songs", j.optString("dir", "")))
            "file.open" -> if (j.optBoolean("dialog", false) || !j.has("uri")) startOpenDialog(ctx, id) else reply(id, openUri(ctx, j.optString("uri"), Uri.parse(j.optString("uri"))))
            "file.read" -> reply(id, readSample(ctx, j.optString("uri")))
            "file.save" -> reply(id, save(ctx, j.optString("uri"), j.optString("text"), j.optBoolean("force", false)))
            "file.saveAs" -> saveAs(ctx, id, j.optString("name"), j.optString("text"), j.optBoolean("dialog", false))
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
        exec.execute {
            try {
                if (uri == null) { reply(p.id, JSONObject().put("ok", false).put("cancelled", true)); return@execute }
                try { cr(ctx).takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_WRITE_URI_PERMISSION) } catch (e: Exception) { Log.i(TAG, "persist: $e") }
                when (p.req) {
                    REQ_TREE -> { prefs(ctx).edit().putString(if (p.kind == "samples") "samplesTree" else "songsTree", uri.toString()).apply(); reply(p.id, folders(ctx).put("picked", p.kind)) }
                    REQ_OPEN -> reply(p.id, openUri(ctx, uri.toString(), uri))
                    REQ_CREATE -> reply(p.id, writeNew(ctx, uri, p.text ?: "", p.bom))
                }
            } catch (e: Exception) { Log.i(TAG, "result: $e"); reply(p.id, FileProtocol.error("shell error: $e")) }
        }
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
        val bytes = try { readBytes(ctx, uri, MAX_TEXT) } catch (e: Exception) { return FileProtocol.error("cannot read the file: $e") } ?: return FileProtocol.error("the file is empty, unreadable or bigger than ${MAX_TEXT shr 20} MiB")
        val d = TextCodec.decode(bytes)
        if (d.text == null) return FileProtocol.error(d.error ?: "not a text file")
        val name = nameOf(ctx, uri)
        sessions[key] = Session(stamp ?: Stamp(0, bytes.size.toLong()), d.bom, name)
        val s = sessions[key]!!.stamp
        return JSONObject().put("ok", true).put("uri", key).put("name", name).put("text", d.text).put("mtime", s.mtime).put("size", s.size)
    }

    private fun readSample(ctx: Context, uri: String): JSONObject {
        val u = Uri.parse(uri)
        val bytes = try { readBytes(ctx, u, MAX_SAMPLE) } catch (e: Exception) { return FileProtocol.error("cannot read the file: $e") } ?: return FileProtocol.error("the file is unreadable or bigger than ${MAX_SAMPLE shr 20} MiB")
        // GAP: bytes ride in the reply as base64 (a third larger); a binary `take` over the port is the later route (CORE-BRIDGE 3: render results by take)
        return JSONObject().put("ok", true).put("uri", uri).put("name", nameOf(ctx, u)).put("size", bytes.size).put("b64", Base64.encodeToString(bytes, Base64.NO_WRAP))
    }

    /** Save in place. Refused with {changed:true} when the file is not what the page opened (unless [force]); the previous content is kept in the app's private backups first. */
    private fun save(ctx: Context, key: String, text: String, force: Boolean): JSONObject {
        val uri = Uri.parse(key)
        val s = sessions[key] ?: return FileProtocol.error("this file was not opened through the shell (open it again, or save as)").put("noSession", true)
        val bytes = TextCodec.encode(text, s.bom) ?: return FileProtocol.error("the song contains text that is not valid Unicode")
        val now = stampOf(ctx, uri)
        val v = FileRules.underneath(s.stamp, now)
        if (v != Underneath.SAME && !force) return FileProtocol.conflict(s.stamp, now)
        if (v == Underneath.SAME || now != null) backup(ctx, key, s.name, uri)
        try {
            val os = cr(ctx).openOutputStream(uri, "wt") ?: return FileProtocol.error("the file cannot be written").put("readOnly", true)
            os.use { it.write(bytes); it.flush() }
        } catch (e: java.io.FileNotFoundException) { return FileProtocol.error("the file cannot be written: ${e.message}").put("readOnly", true)
        } catch (e: SecurityException) { return FileProtocol.error("no permission to write this file").put("readOnly", true)
        } catch (e: Exception) { return FileProtocol.error("write failed: $e") }
        val after = stampOf(ctx, uri)
        if (after != null && after.size >= 0 && after.size != bytes.size.toLong()) return FileProtocol.error("the file on disk has ${after.size} bytes, expected ${bytes.size}: not saved completely (the previous version is in the shell's backups)").put("short", true)
        s.stamp = after ?: Stamp(0, bytes.size.toLong())
        return JSONObject().put("ok", true).put("uri", key).put("name", s.name).put("mtime", s.stamp.mtime).put("size", bytes.size)
    }

    /** The last [KEEP_BACKUPS] contents of a file before the shell overwrote it: filesDir/song-backups/<hash>-<n>.bak. A failure here never stops the save. */
    private fun backup(ctx: Context, key: String, name: String, uri: Uri) {
        try {
            val dir = File(ctx.filesDir, "song-backups").also { it.mkdirs() }
            val old = readBytes(ctx, uri, MAX_TEXT) ?: return
            val tag = Integer.toHexString(key.hashCode())
            val mine = dir.listFiles { f -> f.name.startsWith("$tag-") }?.sortedBy { it.lastModified() } ?: emptyList()
            File(dir, "$tag-${System.currentTimeMillis()}.bak").writeBytes(old)
            mine.dropLast(KEEP_BACKUPS - 1).forEach { it.delete() }
        } catch (e: Exception) { Log.i(TAG, "backup: $e") }
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
        val rootId = DocumentsContract.getTreeDocumentId(tree)
        val existing = children(ctx, tree, rootId)?.map { it.name } ?: return reply(id, FileProtocol.error("the songs folder cannot be read").put("needFolder", true))
        val unique = FileRules.unique(name, existing)
        val parent = DocumentsContract.buildDocumentUriUsingTree(tree, rootId)
        val made = try { DocumentsContract.createDocument(cr(ctx), parent, "application/octet-stream", unique) } catch (e: Exception) { null }   // a generic type keeps the name as typed
            ?: return reply(id, FileProtocol.error("the songs folder refused a new file"))
        reply(id, writeNew(ctx, made, text, false))
    }

    /** Writes a NEW file (just created by a provider or the system's save dialog) and opens a session on it. */
    private fun writeNew(ctx: Context, uri: Uri, text: String, bom: Boolean): JSONObject {
        val bytes = TextCodec.encode(text, bom) ?: return FileProtocol.error("the song contains text that is not valid Unicode")
        try { cr(ctx).openOutputStream(uri, "wt")?.use { it.write(bytes); it.flush() } ?: return FileProtocol.error("the new file cannot be written") } catch (e: Exception) { return FileProtocol.error("write failed: $e") }
        val key = uri.toString()
        val name = nameOf(ctx, uri)
        val st = stampOf(ctx, uri) ?: Stamp(0, bytes.size.toLong())
        sessions[key] = Session(st, bom, name)
        return JSONObject().put("ok", true).put("uri", key).put("name", name).put("mtime", st.mtime).put("size", bytes.size)
    }

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
            try {
                val r = openUri(ctx, uri.toString(), uri)
                r.put("t", "file.opened").put("writable", writable)
                synchronized(opened) { opened.add(r) }
                flushOpened()
            } catch (e: Exception) { Log.i(TAG, "intent: $e") }
        }
    }

    private fun flushOpened() {
        if (!pageReady) return
        val send = synchronized(opened) { val l = ArrayList(opened); opened.clear(); l }
        for (r in send) emit(r.toString())
    }
}
