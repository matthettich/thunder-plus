package app.thunderplus.nativetest

import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import app.thunderplus.nativetest.FocusPolicy.Act as A

class LivePolicyTest {

    @Test fun lossStopsForGood() {
        assertEquals(A.STOP_FOR_GOOD, FocusPolicy.decide(FocusPolicy.LOSS, playing = true, armed = false, ducked = false))
        assertEquals(A.STOP_FOR_GOOD, FocusPolicy.decide(FocusPolicy.LOSS, playing = false, armed = true, ducked = false))   // a remembered resume is cancelled by a real loss
        assertEquals(A.NONE, FocusPolicy.decide(FocusPolicy.LOSS, playing = false, armed = false, ducked = false))
    }

    @Test fun transientLossStopsAndRemembers() {
        assertEquals(A.STOP_AND_REMEMBER, FocusPolicy.decide(FocusPolicy.LOSS_TRANSIENT, true, false, false))
        assertEquals(A.NONE, FocusPolicy.decide(FocusPolicy.LOSS_TRANSIENT, false, false, false))   // stopped already: nothing to remember
    }

    @Test fun duckOnlyWhilePlaying() {
        assertEquals(A.DUCK, FocusPolicy.decide(FocusPolicy.LOSS_TRANSIENT_CAN_DUCK, true, false, false))
        assertEquals(A.NONE, FocusPolicy.decide(FocusPolicy.LOSS_TRANSIENT_CAN_DUCK, false, false, false))
    }

    @Test fun gainResumesOrUnducks() {
        assertEquals(A.RESUME, FocusPolicy.decide(FocusPolicy.GAIN, false, true, false))
        assertEquals(A.RESUME, FocusPolicy.decide(FocusPolicy.GAIN, false, true, true))
        assertEquals(A.UNDUCK, FocusPolicy.decide(FocusPolicy.GAIN, true, false, true))
        assertEquals(A.NONE, FocusPolicy.decide(FocusPolicy.GAIN, true, false, false))
        assertEquals(A.NONE, FocusPolicy.decide(12345, true, false, false))
    }

    @Test fun duckGainIsQuietButAudible() { assertTrue(FocusPolicy.DUCK_GAIN in 0.1f..0.5f) }

    @Test fun streamWatchSeesAStall() {
        val w = StreamWatch(1000)
        assertFalse(w.stalled(10, 0)); assertFalse(w.stalled(20, 250)); assertFalse(w.stalled(20, 900))
        assertTrue(w.stalled(20, 1300))          // 1050 ms since the count last moved
        assertFalse(w.stalled(21, 1400))         // it moved again
        assertFalse(w.stalled(-1, 5000))         // no host: never "stalled", and the watch forgets
        assertFalse(w.stalled(5, 5100)); assertFalse(w.stalled(5, 5500)); assertTrue(w.stalled(5, 6200))
    }

    @Test fun songTitle() {
        assertEquals("ELECTRO", SongTitle.fromFile("sounds/ELECTRO.thunder"))
        assertEquals("acid", SongTitle.fromFile("C:\\songs\\acid.json"))
        assertTrue(SongTitle.isDefault("Song 1")); assertTrue(SongTitle.isDefault(" song ")); assertTrue(SongTitle.isDefault(null)); assertFalse(SongTitle.isDefault("Song of the sea"))
        assertEquals("Night drive", SongTitle.pick(null, "Night drive", "x.thunder"))
        assertEquals("x", SongTitle.pick(null, "Song 1", "folder/x.thunder"))            // the default name loses to the file name, as on the page
        assertEquals("From page", SongTitle.pick("From page", "Night drive", "x.thunder"))
        assertEquals("Thunder Plus", SongTitle.pick(null, "Song 2", null))
        assertEquals("Night drive", SongTitle.fromMessage("{\"t\":\"song.load\",\"rev\":3,\"song\":{\"app\":\"thunder-song\",\"v\":6,\"name\":\"Night drive\",\"bpm\":120}}"))
        assertEquals("Thunder Plus", SongTitle.fromMessage("{\"t\":\"song.load\",\"song\":{\"name\":\"Song 1\"}}"))
        assertEquals("T", SongTitle.fromMessage("{\"t\":\"song.load\",\"title\":\"T\",\"song\":{\"name\":\"Song 1\"}}"))
    }

    private fun rt(frames: Long, ppm: Double, pass: Int, pending: Int = 0) = Rt(JSONObject("{\"pending\":$pending,\"sr\":48000,\"frames\":$frames,\"calls\":1,\"late\":0,\"under\":0,\"audioS\":1,\"wallS\":1,\"ratioPpm\":$ppm,\"maxGapMs\":1,\"maxWorkMs\":1,\"pass\":$pass,\"outLatMs\":0}"))

    @Test fun gateCountsAudioFrames() {
        assertFalse(GateRule.done(rt(48000L * 599, 0.0, 1)))
        assertTrue(GateRule.done(rt(48000L * 600, 0.0, 1)))
        assertFalse(GateRule.done(rt(48000L * 700, 0.0, 1, pending = 1)))
        assertEquals(0.0, GateRule.elapsed(rt(48000L * 5, 0.0, 1, pending = 1)), 0.0)
    }

    @Test fun gateLines() {
        assertTrue(GateState.Running(75.0, true).line().contains("1:15 / 10:00 (so far OK)"))
        assertTrue(GateState.Finished(false, "ratio 1.0020").line().startsWith("A5 gate FAIL"))
        assertTrue(GateState.Aborted("audio focus lost", 30.0).line().contains("not a pass"))
    }
}
