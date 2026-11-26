package app.thunderplus.nativetest

import org.json.JSONObject
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

// N2 H3: the folder rules, and the gate of WORKPLAN H3: open, edit, save, reopen gives identical song JSON (the byte round trip).
class FileModelTest {
    private fun e(name: String, mtime: Long, size: Long = 10, dir: Boolean = false) = FileEntry("content://x/$name", name, name, mtime, size, dir)

    @Test fun newestFirst() {
        val l = FileModel_list(e("old.thunder", 100), e("new.thunder", 300), e("notes.txt", 999), e("sub", 500, dir = true), e("mid.JSON", 200), e("Same.thunder", 200))
        assertEquals(listOf("new.thunder", "mid.JSON", "Same.thunder", "old.thunder"), FileRules.songsNewestFirst(l).map { it.name })   // a tie goes by name
    }
    private fun FileModel_list(vararg x: FileEntry) = x.toList()

    @Test fun sampleBrowserFoldersFirst() {
        val l = FileModel_list(e("snare.wav", 1), e("Drums", 1, dir = true), e("a.mp3", 1), e("readme.txt", 1), e("Bass", 1, dir = true), e("kick.FLAC", 1))
        assertEquals(listOf("Bass", "Drums", "a.mp3", "kick.FLAC", "snare.wav"), FileRules.browse(l).map { it.name })
    }

    @Test fun changedUnderneath() {
        val open = Stamp(1000, 500)
        assertEquals(Underneath.SAME, FileRules.underneath(open, Stamp(1000, 500)))
        assertEquals(Underneath.CHANGED, FileRules.underneath(open, Stamp(2000, 500)))   // touched
        assertEquals(Underneath.CHANGED, FileRules.underneath(open, Stamp(1000, 501)))   // same stamp, other size
        assertEquals(Underneath.GONE, FileRules.underneath(open, null))
        assertEquals(Underneath.SAME, FileRules.underneath(Stamp(0, 500), Stamp(0, 500)))      // a provider with no modification time: judged by size
        assertEquals(Underneath.CHANGED, FileRules.underneath(Stamp(0, 500), Stamp(0, 400)))
        assertEquals(Underneath.SAME, FileRules.underneath(Stamp(0, -1), Stamp(0, -1)))        // nothing known: nothing to compare
        assertEquals(Underneath.SAME, FileRules.underneath(Stamp(1000, -1), Stamp(1000, 40)))
    }

    @Test fun fileNames() {
        assertEquals("My song.thunder", FileRules.songFileName("My song"))
        assertEquals("a_b_c.thunder", FileRules.songFileName("a/b\\c"))
        assertEquals("Song.thunder", FileRules.songFileName("  ..  "))
        assertEquals("x.json", FileRules.songFileName("x.json"))
        assertEquals("x.THUNDER", FileRules.songFileName("x.THUNDER"))
        assertEquals(80 + ".thunder".length, FileRules.songFileName("n".repeat(200)).length)
        assertEquals("a.thunder", FileRules.unique("a.thunder", listOf("b.thunder")))
        assertEquals("a 2.thunder", FileRules.unique("a.thunder", listOf("A.thunder")))
        assertEquals("a 3.thunder", FileRules.unique("a.thunder", listOf("a.thunder", "a 2.thunder")))
    }

    private val song = "{\"app\":\"thunder-project\",\"v\":1,\"build\":548,\"song\":{\"name\":\"Nacht über 東京 🎵\",\"bpm\":131.5,\"notes\":[0,1,-1]},\"kit\":{\"names\":[\"kick\",\"sn\\\"are\"]}}"

    @Test fun roundTripIsByteIdentical() {   // the H3 gate: what the page gets is what the file held, and what it saves is what the file holds
        val bytes = song.toByteArray(Charsets.UTF_8)
        val d = TextCodec.decode(bytes)
        assertNull(d.error); assertEquals(song, d.text); assertFalse(d.bom)
        assertArrayEquals(bytes, TextCodec.encode(d.text!!, d.bom))
        // edit: the page changes the bpm and saves; reopening gives exactly the edited text
        val edited = d.text!!.replace("131.5", "140")
        val saved = TextCodec.encode(edited, d.bom)!!
        val again = TextCodec.decode(saved)
        assertEquals(edited, again.text)
        assertEquals(JSONObject(edited).toString(), JSONObject(again.text!!).toString())
    }

    @Test fun bomCrlfAndBigFilesSurvive() {
        val withBom = byteArrayOf(0xEF.toByte(), 0xBB.toByte(), 0xBF.toByte()) + "{\"a\":1}\r\n".toByteArray()
        val d = TextCodec.decode(withBom)
        assertTrue(d.bom); assertEquals("{\"a\":1}\r\n", d.text)   // the BOM is not in the text the page parses ...
        assertArrayEquals(withBom, TextCodec.encode(d.text!!, d.bom))   // ... and comes back on save
        val big = "{\"x\":\"" + "é🎵".repeat(2_000_000) + "\"}"   // about 14 MB of UTF-8
        val bb = big.toByteArray(Charsets.UTF_8)
        assertArrayEquals(bb, TextCodec.encode(TextCodec.decode(bb).text!!, false))
        assertEquals("", TextCodec.decode(ByteArray(0)).text)
    }

    @Test fun badBytesAreRefusedNotRepaired() {
        val bad = byteArrayOf('{'.code.toByte(), 0xFF.toByte(), 0xFE.toByte(), '}'.code.toByte())
        val d = TextCodec.decode(bad)
        assertNull(d.text); assertNotNull(d.error)
        assertNull(TextCodec.encode("a\uD800b", false))   // a lone surrogate would be written as '?': refused
    }

    @Test fun replies() {
        val j = FileProtocol.list("songs", listOf(e("a.thunder", 5, 7), e("d", 0, dir = true)))
        assertTrue(j.getBoolean("ok")); assertEquals(2, j.getJSONArray("files").length())
        assertEquals("a.thunder", j.getJSONArray("files").getJSONObject(0).getString("name")); assertEquals(5L, j.getJSONArray("files").getJSONObject(0).getLong("mtime"))
        assertTrue(j.getJSONArray("files").getJSONObject(1).getBoolean("dir"))
        val c = FileProtocol.conflict(Stamp(1, 2), Stamp(3, 4))
        assertFalse(c.getBoolean("ok")); assertTrue(c.getBoolean("changed")); assertFalse(c.getBoolean("gone")); assertEquals(3L, c.getJSONObject("now").getLong("mtime"))
        assertTrue(FileProtocol.conflict(Stamp(1, 2), null).getBoolean("gone"))
        assertEquals("x", FileProtocol.error("x").getString("error"))
    }
}
