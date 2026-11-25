package app.thunderplus.nativetest

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

// N2 H1a: the pure parts of the live engine's Kotlin side (LiveModel.kt) against the JSON live_engine.cpp / live_jni.cpp write.
class LiveModelTest {
    private val statusJson = "{\"state\":1,\"row\":12,\"step\":3,\"bpm\":131.0,\"clockF\":96000,\"voices\":9,\"under\":2,\"outLat\":480,\"songRev\":1,\"padsId\":2,\"records\":55,\"gaps\":0," +
        "\"pkL\":0.5000,\"pkR\":0.2500,\"errors\":1,\"fatal\":0,\"lastError\":\"where 3 code 0 fatal 0 value -5\",\"notesLate\":0,\"runnerSkips\":0}"

    @Test fun statusParses() {
        val s = LiveStatus.parse(statusJson)
        assertEquals(LiveState.PLAYING, s.state)
        assertEquals(12, s.row); assertEquals(3, s.step); assertEquals(131.0, s.bpm, 1e-9); assertEquals(9, s.voices); assertEquals(2, s.under)
        assertEquals(0.5, s.peakL, 1e-9); assertEquals(0.25, s.peakR, 1e-9); assertEquals(1, s.errors); assertFalse(s.dead)
        assertEquals("where 3 code 0 fatal 0 value -5", s.lastError)
        assertTrue(s.line().contains("playing")); assertTrue(s.line().contains("131.0 bpm")); assertTrue(s.line().contains("errors 1"))
    }

    @Test fun emptyOrForeignJsonIsIdle() {
        assertEquals(LiveStatus.EMPTY, LiveStatus.parse("{}"))
        assertEquals(LiveStatus.EMPTY, LiveStatus.parse(""))
        assertEquals(LiveState.IDLE, LiveState.of(99))
        assertEquals(LiveState.HELD, LiveState.of(3))
    }

    @Test fun fatalMeansDead() {
        val s = LiveStatus.parse(statusJson.replace("\"fatal\":0", "\"fatal\":1"))
        assertTrue(s.dead); assertTrue(s.line().contains("ENGINE DEAD"))
        assertEquals("Engine stopped (error)", LiveButtons.notificationText(s, true))
        assertFalse(LiveButtons.canPlay(s, true))
    }

    @Test fun loadResultParses() {
        val j = "{\"ok\":true,\"rc\":0,\"err\":\"\",\"pads\":38,\"skipped\":2,\"dry\":2,\"renderMs\":1234,\"bpm\":131.0,\"notes\":[\"pad 3 (Kick): rendered without a pad effect\",\"pad 9 (\\\"X\\\"): not rendered\"]}"
        val r = LoadResult.parse(j)
        assertTrue(r.ok); assertEquals(38, r.pads); assertEquals(2, r.skipped); assertEquals(2, r.dry); assertEquals(1234.0, r.renderMs, 1e-9)
        assertEquals(2, r.notes.size); assertEquals("pad 9 (\"X\"): not rendered", r.notes[1])
        assertTrue(r.summary().contains("38 pads")); assertTrue(r.summary().contains("2 EMPTY")); assertTrue(r.summary().contains("2 without their pad effect"))
    }

    @Test fun failedLoadSaysWhy() {
        val r = LoadResult.parse("{\"ok\":false,\"rc\":-3,\"err\":\"not a .thunder project: no song member\"}")
        assertFalse(r.ok); assertEquals(-3, r.rc)
        assertEquals("load failed (rc -3): not a .thunder project: no song member", r.summary())
        assertEquals(false, LoadResult.parse("garbage").ok)
    }

    @Test fun flatJsonReaders() {
        val j = "{\"a\":-1.5e2,\"b\":true,\"c\":\"x\\\\y\",\"d\":[\"p\",\"q\"]}"
        assertEquals(-150.0, FlatJson.num(j, "a")!!, 1e-9); assertEquals(true, FlatJson.bool(j, "b")); assertEquals("x\\y", FlatJson.str(j, "c"))
        assertEquals(listOf("p", "q"), FlatJson.stringArray(j, "d")); assertNull(FlatJson.num(j, "zzz")); assertEquals(emptyList<String>(), FlatJson.stringArray(j, "zzz"))
    }

    @Test fun buttonsFollowState() {
        val idle = LiveStatus.EMPTY
        val playing = LiveStatus.parse(statusJson)
        assertEquals("No song loaded", LiveButtons.notificationText(idle, false))
        assertEquals("Stopped", LiveButtons.notificationText(idle, true))
        assertEquals("Playing", LiveButtons.notificationText(playing, true))
        assertTrue(LiveButtons.canPlay(idle, true)); assertFalse(LiveButtons.canPlay(idle, false)); assertFalse(LiveButtons.canPlay(playing, true))
    }
}
