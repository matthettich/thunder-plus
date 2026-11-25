package app.thunderplus.nativetest

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File

// N2 verifier fixes H3 / H5 (25 Nov): the pure parts of the safe save, the limits, the grants, the backups and the recovery copy. No Android.
class SaveSafetyTest {
    private fun tmp(): File = java.nio.file.Files.createTempDirectory("thunder-test").toFile().also { it.deleteOnExit() }

    @Test fun emptyOrMissingTextIsRefused() {   // H3-2
        assertNotNull(SaveRules.refuseText(false, ""))
        assertNotNull(SaveRules.refuseText(true, null))
        assertNotNull(SaveRules.refuseText(true, ""))
        assertNull(SaveRules.refuseText(true, "{\"a\":1}"))
    }

    @Test fun limits() {   // H3-3
        assertEquals(16L shl 20, SaveRules.MAX_TEXT)
        assertTrue(SaveRules.MAX_SAMPLE <= 24L shl 20)
        assertNotNull(SaveRules.refuseText(true, "x".repeat((SaveRules.MAX_TEXT + 1).toInt())))
        assertTrue(SaveRules.describe(OutOfMemoryError()).startsWith("out of memory"))
        assertEquals("shell error: java.lang.IllegalStateException: x", SaveRules.describe(IllegalStateException("x")))
    }

    @Test fun space() {   // H3-1
        assertFalse(SaveRules.backupSpaceOk(100, 1000))
        assertTrue(SaveRules.backupSpaceOk(5L shl 20, 1000))
        assertTrue(SaveRules.targetSpaceOk(null, 10, 1_000_000))            // the provider does not say: no objection
        assertFalse(SaveRules.targetSpaceOk(0, 500, 1_000_000))
        assertTrue(SaveRules.targetSpaceOk(0, 2_000_000, 1_000_000))        // the truncation gives the old size back
        assertFalse(SaveRules.targetSpaceOk(1000, 0, 1_000_000))
    }

    @Test fun timeouts() {   // H3-4
        assertEquals(SaveRules.SAVE_MS, SaveRules.timeoutMs("file.save"))
        assertEquals(SaveRules.SAVE_MS, SaveRules.timeoutMs("file.saveAs"))
        assertEquals(SaveRules.QUICK_MS, SaveRules.timeoutMs("file.list"))
    }

    @Test fun grantsKeepFoldersAndNewestFiles() {   // H3-7
        val g = listOf(Grant("tree-songs", 1), Grant("tree-samples", 2), Grant("f1", 10), Grant("f2", 20), Grant("f3", 30), Grant("f4", 40))
        assertEquals(listOf("f2", "f1"), GrantRules.toRelease(g, setOf("tree-songs", "tree-samples"), 2))
        assertEquals(emptyList<String>(), GrantRules.toRelease(g, setOf("tree-songs", "tree-samples"), 8))
        assertEquals(listOf("tree-songs", "tree-samples", "f1", "f2", "f3", "f4").sorted(), GrantRules.toRelease(g, emptySet(), 0).sorted())   // folders are protected only through [keep]
    }

    @Test fun backupIsVerifiedAndPruned() {   // H3-1
        val d = tmp()
        val old = ByteArray(1000) { it.toByte() }
        for (i in 1..5) { assertTrue(Backups.make(d, "ab", old, 3, 1000L + i)); File(d, "ab-${1000L + i}.bak").setLastModified(1000L + i) }
        val left = d.listFiles()!!.map { it.name }.sorted()
        assertEquals(listOf("ab-1003.bak", "ab-1004.bak", "ab-1005.bak"), left)
        assertEquals(1000, File(d, "ab-1005.bak").length().toInt())
        assertTrue(Backups.make(d, "ab", ByteArray(0), 3, 2000))            // an empty file has an (empty) backup
    }

    @Test fun backupFailureIsReported() {   // H3-1: the caller must then refuse the save
        val d = tmp(); val notADir = File(d, "plain"); notADir.writeText("x")
        assertFalse(Backups.make(File(notADir, "sub"), "ab", ByteArray(10), 3, 1))   // the directory cannot be made
        assertEquals(listOf("plain"), d.listFiles()!!.map { it.name })               // nothing partial left
    }

    // ---- H5-1: the recovery copy ----

    private fun b(s: String) = s.toByteArray()

    @Test fun nothingUnsavedAtStartOrAfterSave() {
        val t = RecoveryTracker()
        assertNull(t.unsaved())
        t.onHello(); t.onSongLoad(b("A"), "Song")
        assertNull(t.unsaved())                    // the first song.load is what the page started with
        t.onSongLoad(b("B"), "Song")
        assertEquals("B", String(t.unsaved()!!.text))
        t.onSaved("content://x/1")
        assertNull(t.unsaved())                    // saved
        t.onSongLoad(b("C"), "Song 2")
        val u = t.unsaved()!!
        assertEquals("C", String(u.text)); assertEquals("Song 2", u.title); assertEquals("content://x/1", u.file)
    }

    @Test fun openedFileIsCleanAndKeepsItsName() {
        val t = RecoveryTracker()
        t.onHello(); t.onSongLoad(b("A"), "x"); t.onSongLoad(b("B"), "x")
        t.onOpened("content://x/2")                // the owner opened another file
        t.onSongLoad(b("F"), "File")               // the page loads it
        assertNull(t.unsaved())
        t.onSongLoad(b("F2"), "File")
        assertEquals("content://x/2", t.unsaved()!!.file)
    }

    @Test fun newPageSessionStartsClean() {
        val t = RecoveryTracker()
        t.onHello(); t.onSongLoad(b("A"), "x"); t.onSongLoad(b("B"), "x")
        t.onHello()
        assertNull(t.unsaved())
    }

    @Test fun storeRoundTrip() {
        val st = RecoveryStore(tmp())
        assertNull(st.meta()); assertNull(st.text())
        val snap = RecoveryTracker.Snapshot("{\"S\":\"é☃\"}".toByteArray(Charsets.UTF_8), "My song", "content://x/1")
        assertTrue(st.write(snap, 1234L))
        val m = st.meta()!!
        assertEquals("My song", m.title); assertEquals(1234L, m.time); assertEquals("content://x/1", m.file)
        assertEquals(String(snap.text, Charsets.UTF_8), String(st.text()!!, Charsets.UTF_8))
        val ev = org.json.JSONObject(RecoveryStore.event(m))
        assertEquals("recovery", ev.getString("t")); assertEquals("My song", ev.getString("title")); assertEquals(1234L, ev.getLong("time")); assertEquals("content://x/1", ev.getString("file"))
        assertTrue(st.write(RecoveryTracker.Snapshot(b("N"), "T", null), 5L))     // a newer copy replaces it; no file
        assertTrue(org.json.JSONObject(RecoveryStore.event(st.meta()!!)).isNull("file"))
        st.drop()
        assertNull(st.meta()); assertNull(st.text())
    }

    @Test fun halfWrittenStoreIsNoRecovery() {
        val d = tmp(); val st = RecoveryStore(d)
        File(d, "recovery.thunder").writeText("x")      // no sidecar: not committed
        assertNull(st.meta()); assertNull(st.text())
        File(d, "recovery.json").writeText("not json")
        assertNull(st.meta())
    }
}
