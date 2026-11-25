package app.thunderplus.nativetest

import org.json.JSONArray
import org.json.JSONObject
import java.nio.ByteBuffer
import java.nio.charset.CharacterCodingException
import java.nio.charset.CodingErrorAction

// N2 H3: the PURE parts of the songs / samples folder (no android.* import: the JVM tests in app/src/test run them). SongFiles.kt does the Storage Access Framework calls these rules decide about.

/** One entry of a listed folder. [uri] is what the page keeps and gives back (a content:// document URI); [docId] is the document id (for `dir` in `file.list`). [mtime] 0 = unknown, [size] -1 = unknown. */
data class FileEntry(val uri: String, val docId: String, val name: String, val mtime: Long, val size: Long, val dir: Boolean)

/** What a file looked like when the shell read it or last wrote it. */
data class Stamp(val mtime: Long, val size: Long)

enum class Underneath { SAME, CHANGED, GONE }

object FileRules {
    private val SONG_EXT = listOf(".thunder", ".json")   // the page's own filter (desktop dialog: .thunder / .json)
    private val SAMPLE_EXT = listOf(".wav", ".aif", ".aiff", ".flac", ".mp3", ".ogg", ".oga", ".opus", ".m4a", ".aac")

    fun isSong(name: String): Boolean = SONG_EXT.any { name.endsWith(it, ignoreCase = true) }
    fun isSample(name: String): Boolean = SAMPLE_EXT.any { name.endsWith(it, ignoreCase = true) }

    /** The songs of a folder, newest first (last modified, then name). Subfolders and other files are not listed. */
    fun songsNewestFirst(all: List<FileEntry>): List<FileEntry> =
        all.filter { !it.dir && isSong(it.name) }.sortedWith(compareByDescending<FileEntry> { it.mtime }.thenBy { it.name.lowercase() })

    /** A sample folder as one level of a browser: folders first, then audio files, each by name. */
    fun browse(all: List<FileEntry>): List<FileEntry> =
        all.filter { it.dir || isSample(it.name) }.sortedWith(compareBy<FileEntry> { !it.dir }.thenBy { it.name.lowercase() })

    /**
     * Has the file changed since [open]? The page is asked to confirm before a save over a changed file. A provider that does not know the modification time (0) is judged by size alone; one that
     * knows neither says SAME (nothing to compare: the save goes on, and the backup is the safety net). A same-size edit inside one timestamp tick is not seen (UNVERIFIED how fine each provider's tick is).
     */
    fun underneath(open: Stamp, now: Stamp?): Underneath {
        if (now == null) return Underneath.GONE
        val mtimeKnown = open.mtime > 0 && now.mtime > 0
        val sizeKnown = open.size >= 0 && now.size >= 0
        if (mtimeKnown && open.mtime != now.mtime) return Underneath.CHANGED
        if (sizeKnown && open.size != now.size) return Underneath.CHANGED
        return Underneath.SAME
    }

    /** A file name safe on every provider: no path separators or reserved characters, at most 80 characters, ends in .thunder or .json (the page's filter), never empty. */
    fun songFileName(want: String?): String {
        var n = (want ?: "").replace(Regex("[\\\\/:*?\"<>|\\u0000-\\u001f]"), "_").trim().trim('.').take(80).trim()
        if (n.isEmpty()) n = "Song"
        return if (isSong(n)) n else "$n.thunder"
    }

    /** [name] unless it exists in [existing] (case-insensitive), then "name 2.ext", "name 3.ext" ... */
    fun unique(name: String, existing: Collection<String>): String {
        val have = existing.map { it.lowercase() }.toHashSet()
        if (name.lowercase() !in have) return name
        val dot = name.lastIndexOf('.')
        val base = if (dot > 0) name.substring(0, dot) else name
        val ext = if (dot > 0) name.substring(dot) else ""
        var i = 2
        while ("$base $i$ext".lowercase() in have) i++
        return "$base $i$ext"
    }
}

/** The song file as bytes <-> the page's text. The shell never parses or re-writes the JSON: it hands over the characters of the file and writes back exactly the characters the page returns (UTF-8, a leading BOM kept). */
object TextCodec {
    class Decoded(val text: String?, val bom: Boolean, val error: String?)

    fun decode(b: ByteArray): Decoded {
        val bom = b.size >= 3 && b[0] == 0xEF.toByte() && b[1] == 0xBB.toByte() && b[2] == 0xBF.toByte()
        val off = if (bom) 3 else 0
        return try {
            val dec = Charsets.UTF_8.newDecoder().onMalformedInput(CodingErrorAction.REPORT).onUnmappableCharacter(CodingErrorAction.REPORT)
            Decoded(dec.decode(ByteBuffer.wrap(b, off, b.size - off)).toString(), bom, null)
        } catch (e: CharacterCodingException) { Decoded(null, bom, "the file is not valid UTF-8") }
    }

    /** null when [text] is not valid Unicode (a lone surrogate would be written as "?" and change the song). */
    fun encode(text: String, bom: Boolean): ByteArray? {
        val body = try {
            val enc = Charsets.UTF_8.newEncoder().onMalformedInput(CodingErrorAction.REPORT).onUnmappableCharacter(CodingErrorAction.REPORT)
            val bb = enc.encode(java.nio.CharBuffer.wrap(text))
            ByteArray(bb.remaining()).also { bb.get(it) }
        } catch (e: CharacterCodingException) { return null }
        return if (bom) byteArrayOf(0xEF.toByte(), 0xBB.toByte(), 0xBF.toByte()) + body else body
    }
}

/** The JSON of the file replies (CORE-BRIDGE 7 proposal in N2-NOTES.md "H3"). */
object FileProtocol {
    fun entry(e: FileEntry): JSONObject = JSONObject().put("uri", e.uri).put("name", e.name).put("mtime", e.mtime).put("size", e.size).also { if (e.dir) it.put("dir", true).put("id", e.docId) }
    fun list(kind: String, entries: List<FileEntry>): JSONObject = JSONObject().put("ok", true).put("kind", kind).put("files", JSONArray().also { a -> entries.forEach { a.put(entry(it)) } })
    fun error(msg: String): JSONObject = JSONObject().put("ok", false).put("error", msg)
    /** save refused because the file is not what the page opened: the page asks the owner, and sends the save again with force:true if they agree. */
    fun conflict(open: Stamp, now: Stamp?): JSONObject = JSONObject().put("ok", false).put("changed", true).put("gone", now == null)
        .put("open", JSONObject().put("mtime", open.mtime).put("size", open.size))
        .also { if (now != null) it.put("now", JSONObject().put("mtime", now.mtime).put("size", now.size)) }
}
