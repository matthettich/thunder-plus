package app.thunderplus.nativetest

import app.thunderplus.nativetest.BridgeTranslator.LiveCall
import app.thunderplus.nativetest.BridgeTranslator.Translation
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import java.nio.ByteBuffer
import java.nio.ByteOrder

// N2 H1b: the translation of CORE-BRIDGE v1 messages into live calls (BridgeTranslator.kt) and the put assembly (PutStore).
class BridgeTranslatorTest {
    private fun call(raw: String): LiveCall { val t = BridgeTranslator.translate(raw); assertTrue("not a call: $t", t is Translation.Call); return (t as Translation.Call).call }
    private fun refused(raw: String): Translation.Refused { val t = BridgeTranslator.translate(raw); assertTrue("not refused: $t", t is Translation.Refused); return t as Translation.Refused }

    @Test fun typeIsReadFromTheFront() {
        assertEquals("play", BridgeTranslator.typeOf("{\"t\":\"play\",\"row\":0}"))
        assertEquals("song.load", BridgeTranslator.typeOf("{ \"t\" : \"song.load\", \"S\": {\"t\":\"x\"}}"))
        assertNull(BridgeTranslator.typeOf("{\"row\":0}"))
    }

    @Test fun bigMessagesPassThroughUnparsed() {
        val msg = "{\"t\":\"song.load\",\"S\":{\"name\":\"x\",\"bpm\":131},\"vars\":null,\"rev\":4}"
        val c = call(msg) as LiveCall.SongLoad
        assertEquals(msg, String(c.raw, Charsets.UTF_8))
        assertTrue(call("{\"t\":\"kit.load\",\"kit\":{\"voices\":[]},\"rev\":1}") is LiveCall.KitLoad)
    }

    @Test fun playFields() {
        assertEquals(LiveCall.Play(0, 0, 0, -1, -1, 0, 0, 0, -1, 0, 0, 1, 0, 0), call("{\"t\":\"play\"}"))
        val p = call("{\"t\":\"play\",\"mode\":\"sect\",\"row\":5,\"track\":2,\"sect\":3,\"loop\":{\"y0\":4,\"y1\":8},\"seed\":99,\"pdc\":2,\"only\":[0,3],\"at\":48000}") as LiveCall.Play
        assertEquals(LiveCall.Play(1, 5, 2, 3, -1, 1, 4, 8, 48000, 1, 99, 2, 0b1001, 1), p)
        assertEquals(2, (call("{\"t\":\"play\",\"mode\":2}") as LiveCall.Play).mode)
        assertTrue(refused("{\"t\":\"play\",\"mode\":\"weird\"}").msg.contains("unknown play mode"))
        assertTrue(refused("{\"t\":\"play\",\"pdc\":5}").msg.contains("pdc"))
    }

    @Test fun transportCommands() {
        assertEquals(LiveCall.Stop(-1), call("{\"t\":\"stop\"}"))
        assertEquals(LiveCall.Stop(960), call("{\"t\":\"stop\",\"at\":960}"))
        assertEquals(LiveCall.Hold(-1), call("{\"t\":\"hold\",\"at\":null}"))
        assertEquals(LiveCall.Bpm(140.5, -1), call("{\"t\":\"bpm\",\"bpm\":140.5}"))
        assertEquals(LiveCall.Bpm(90.0, 7), call("{\"t\":\"bpm\",\"v\":90,\"at\":7}"))
        assertEquals(LiveCall.Loop(1, 2, 6, -1), call("{\"t\":\"loop\",\"y0\":2,\"y1\":6}"))
        assertEquals(LiveCall.Loop(0, 0, 0, -1), call("{\"t\":\"loop\",\"has\":false}"))
        assertEquals(LiveCall.Lead(0.25, -1), call("{\"t\":\"lead\",\"s\":0.25}"))
        assertEquals(LiveCall.Perf(3, 1, 0.5, -1), call("{\"t\":\"perf\",\"what\":3,\"i\":1,\"v\":0.5}"))
        assertTrue(refused("{\"t\":\"bpm\"}").msg.contains("bpm"))
    }

    @Test fun mixSet() {
        assertEquals(LiveCall.MixSet(3, 0, 200.0, -1.0, -1), call("{\"t\":\"mix.set\",\"tr\":3,\"k\":\"vol\",\"v\":200}"))
        assertEquals(LiveCall.MixSet(32, 6, 255.0, 0.0, 10), call("{\"t\":\"mix.set\",\"tr\":\"m\",\"k\":\"out\",\"v\":255,\"ramp\":0,\"at\":10}"))
        assertEquals(LiveCall.MixSet(5, 3, 1.0, 512.0, -1), call("{\"t\":\"mix.set\",\"tr\":5,\"k\":\"mute\",\"v\":1,\"ramp\":512}"))
        assertTrue(refused("{\"t\":\"mix.set\",\"tr\":1,\"k\":\"send\",\"i\":0,\"v\":3}").msg.contains("send"))
        assertTrue(refused("{\"t\":\"mix.set\",\"tr\":99,\"k\":\"vol\",\"v\":1}").msg.contains("tr"))
        assertTrue(refused("{\"t\":\"mix.set\",\"tr\":1,\"k\":\"zzz\",\"v\":1}").msg.contains("zzz"))
    }

    @Test fun effectsAndMaster() {
        val plan = call("{\"t\":\"fx.plan\",\"tr\":2,\"slots\":[{\"type\":\"glue\"},null],\"bpm\":131}") as LiveCall.FxPlan
        assertEquals(2, plan.track); assertEquals(131.0, plan.bpm, 0.0); assertTrue(String(plan.slotsJson).startsWith("[{\"type\":\"glue\"}"))
        assertEquals(LiveCall.FxCommit(2, 100), call("{\"t\":\"fx.commit\",\"tr\":2,\"at\":100}"))
        val fp = call("{\"t\":\"fx.param\",\"tr\":\"m\",\"slot\":1,\"p\":[1,2,3,4,5,6,7,8,9,10,11,12.5]}") as LiveCall.FxParam
        assertEquals(32, fp.track); assertEquals(1, fp.slot); assertEquals(12.5, fp.p[11], 0.0)
        assertTrue(refused("{\"t\":\"fx.param\",\"tr\":1,\"slot\":0,\"p\":[1,2]}").msg.contains("12"))
        val mc = call("{\"t\":\"master.config\",\"mst\":{\"limiter\":true},\"safety\":false}") as LiveCall.MasterConfig
        assertEquals(0, mc.safety); assertEquals("{\"limiter\":true}", String(mc.mstJson))
        val ce = call("{\"t\":\"chain.edit\",\"ops\":[{\"op\":\"param\",\"id\":\"a\",\"name\":\"b\",\"value\":1}]}") as LiveCall.ChainEdit
        assertTrue(String(ce.opsJson).contains("\"op\":\"param\""))
    }

    @Test fun fxSetIsTheVectorForm() {   // CORE-BRIDGE 7 G5a
        val c = call("{\"t\":\"fx.set\",\"tr\":3,\"slot\":2,\"p\":[0,1,2,3,4,5,6,7,8,9,10,11],\"at\":5}") as LiveCall.FxParam
        assertEquals(3, c.track); assertEquals(2, c.slot); assertEquals(11.0, c.p[11], 0.0); assertEquals(5L, c.at)
        val r = refused("{\"t\":\"fx.set\",\"tr\":1,\"slot\":0,\"p\":2,\"v\":0.5,\"id\":9}")   // the old single-parameter form
        assertEquals("fx.set", r.where); assertTrue(r.msg.contains("p")); assertEquals(9, r.replyId)
    }

    @Test fun section7Additions() {
        assertEquals(LiveCall.PadsCommit(40), call("{\"t\":\"pads.commit\",\"n\":40}"))
        val pc = call("{\"t\":\"pad.chop\",\"pad\":4,\"loopSecs\":1.5,\"slices\":[0,12000,24000,36000]}") as LiveCall.PadChop
        assertEquals(4, pc.pad); assertEquals(1.5, pc.loopSecs, 0.0); assertEquals(4, pc.nSlices); assertArrayEquals(doubleArrayOf(0.0, 12000.0, 24000.0, 36000.0), pc.slices, 0.0)
        val none = call("{\"t\":\"pad.chop\",\"pad\":4,\"loopSecs\":0}") as LiveCall.PadChop
        assertEquals(0, none.slices.size); assertEquals(2, none.nSlices)
        assertTrue(refused("{\"t\":\"pad.chop\",\"pad\":4,\"slices\":[-1]}").msg.contains("slice"))
        assertEquals(LiveCall.PdcSet(2), call("{\"t\":\"pdc.set\",\"mode\":2}"))
        assertTrue(refused("{\"t\":\"pdc.set\",\"mode\":3}").msg.contains("pdc"))
        assertTrue(call("{\"t\":\"engine.reset\"}") === LiveCall.EngineReset)
    }

    @Test fun midiCfgIsFourLetters() {   // CORE-BRIDGE 3 midi.cfg (H2)
        val c = call("{\"t\":\"midi.cfg\",\"outs\":[\"n:Synth\",\"\",\"n:Drums / A\"],\"clockOut\":[true,0,1],\"gearDelay\":[12.4,-80,500,3],\"clockIn\":\"n:Keys\"}") as LiveCall.MidiCfg
        assertEquals(listOf("n:Synth", "", "n:Drums / A", ""), c.outs)
        assertEquals(listOf(true, false, true, false), c.clockOut)
        assertEquals(listOf(12.0, -50.0, 200.0, 3.0), c.gear)   // whole ms, clamped to -50..200
        assertTrue(c.clockIn)
        val e = call("{\"t\":\"midi.cfg\"}") as LiveCall.MidiCfg   // nothing given: four empty letters, no clocks, no delay
        assertEquals(listOf("", "", "", ""), e.outs); assertEquals(listOf(false, false, false, false), e.clockOut); assertEquals(listOf(0.0, 0.0, 0.0, 0.0), e.gear)
        assertEquals("midi.cfg", refused("{\"t\":\"midi.cfg\",\"outs\":\"n:x\"}").where)
    }

    @Test fun metersClamp() {
        assertEquals(LiveCall.MetersWant(30.0), call("{\"t\":\"meters.want\",\"rate\":60}"))
        assertEquals(LiveCall.MetersWant(0.0), call("{\"t\":\"meters.want\",\"rate\":-1}"))
        assertEquals(LiveCall.MetersWant(10.0), call("{\"t\":\"meters.want\",\"rate\":10,\"tracks\":[0]}"))
    }

    @Test fun bufUse() {
        assertEquals(LiveCall.BufUse(4, 9, 0, "pad", 0, 'g'.code, 0), call("{\"t\":\"buf.use\",\"id\":4,\"pad\":9,\"kind\":\"pad\"}"))
        assertEquals(LiveCall.BufUse(5, 9, 0, "var", 3, 'm'.code, 7), call("{\"t\":\"buf.use\",\"id\":5,\"pad\":9,\"kind\":\"var\",\"ii\":3,\"vk\":\"m7\"}"))
        assertEquals(LiveCall.BufUse(6, 2, 0, "var", 0, 'g'.code, 1), call("{\"t\":\"buf.use\",\"id\":6,\"pad\":2,\"kind\":\"var\",\"ii\":0,\"vk\":\"g1\"}"))
        assertTrue(refused("{\"t\":\"buf.use\",\"id\":5,\"pad\":9,\"kind\":\"var\",\"ii\":3,\"vk\":\"x7\"}").msg.contains("vk"))
        assertTrue(refused("{\"t\":\"buf.use\",\"id\":5,\"pad\":9,\"kind\":\"ir\"}").msg.contains("ir"))
        assertTrue(refused("{\"t\":\"buf.use\",\"id\":5,\"pad\":9,\"kind\":\"var\"}").msg.contains("ii"))
        assertEquals(LiveCall.BufFree(5), call("{\"t\":\"buf.free\",\"id\":5}"))
    }

    @Test fun lxpOnlyChangesTheVerdict() {   // verify-n2 F6: the page's live transpose is a SongFit condition (G11), not an error
        assertEquals(LiveCall.Lxp(5), call("{\"t\":\"lxp\",\"st\":5}"))
        assertEquals(LiveCall.Lxp(0), call("{\"t\":\"lxp\"}"))
    }

    @Test fun helloAndNotOnTheLiveHost() {
        assertEquals(LiveCall.Hello(3, true), call("{\"t\":\"hello\",\"id\":3,\"v\":1,\"app\":\"thunder\",\"live\":true}"))
        for (t in listOf("song.patch", "kit.patch", "note.on", "note.off", "dev.list", "dev.open", "rec.arm", "render.start", "link.set")) {
            val r = refused("{\"t\":\"$t\",\"id\":1}")
            assertEquals(t, r.where); assertEquals(1, r.replyId)
        }
        assertTrue(BridgeTranslator.translate("{\"t\":\"something.new\"}") is Translation.Ignored)
        assertTrue(BridgeTranslator.translate("{\"t\":\"play\",") is Translation.Refused)
        assertTrue(BridgeTranslator.translate("garbage") is Translation.Ignored)
    }

    // ---- PutStore ----
    private fun floats(vararg f: Float): ByteArray { val b = ByteBuffer.allocate(f.size * 4).order(ByteOrder.LITTLE_ENDIAN); f.forEach { b.putFloat(it) }; return b.array() }

    @Test fun putAssemblesChunksInTwoChannels() {
        val s = PutStore()
        fun send(h: String, bytes: ByteArray) = run { assertNull(s.header(h)); s.chunk(bytes) }
        var r = send("{\"pid\":7,\"id\":11,\"c\":0,\"o\":0,\"n\":4,\"last\":false,\"sr\":44100,\"ch\":2,\"frames\":4}", floats(1f, 2f))
        assertNull(r.first); assertEquals(-1, r.second)
        r = send("{\"pid\":7,\"id\":11,\"c\":0,\"o\":2,\"n\":4,\"last\":false}", floats(3f, 4f)); assertEquals(-1, r.second)
        r = send("{\"pid\":7,\"id\":11,\"c\":1,\"o\":0,\"n\":4,\"last\":true}", floats(5f, 6f, 7f, 8f))
        assertNull(r.first); assertEquals(11, r.second); assertEquals(7, s.lastPid)
        val b = s.take(11)!!
        assertEquals(2, b.ch); assertEquals(4, b.frames); assertEquals(44100.0, b.sr, 0.0)
        assertArrayEquals(floats(1f, 2f, 3f, 4f), b.data[0]); assertArrayEquals(floats(5f, 6f, 7f, 8f), b.data[1])
        assertEquals(32L, s.held); s.free(11); assertEquals(0L, s.held); assertNull(s.take(11))
    }

    @Test fun putRefusals() {
        val s = PutStore(maxBytes = 64)
        assertNull(s.header("{\"pid\":1,\"id\":1,\"o\":0,\"n\":4,\"last\":true}"))
        assertTrue(s.chunk(floats(1f, 2f, 3f, 4f)).first!!.contains("sample rate"))
        assertNull(s.header("{\"pid\":2,\"id\":2,\"o\":0,\"n\":4,\"last\":true,\"sr\":48000}"))
        assertTrue(s.chunk(ByteArray(7)).first!!.contains("whole floats"))
        assertNull(s.header("{\"pid\":3,\"id\":3,\"o\":2,\"n\":4,\"last\":true,\"sr\":48000}"))
        assertTrue(s.chunk(floats(1f, 2f, 3f)).first!!.contains("past the end"))
        assertTrue(s.chunk(floats(1f)).first!!.contains("without a put.header"))
        assertNull(s.header("{\"pid\":4,\"id\":4,\"o\":0,\"n\":100,\"last\":false,\"sr\":48000}"))
        assertTrue(s.chunk(floats(1f)).first!!.contains("too much"))
        assertNull(s.header("{\"pid\":5,\"id\":5,\"o\":0,\"n\":1,\"sr\":48000}"))
        assertTrue(s.header("{\"pid\":5}")!!.contains("still expected"))
        assertTrue(PutStore().header("not json")!!.contains("not JSON"))
    }

    @Test fun aNewPutUnderTheSameIdReplacesTheOld() {
        val s = PutStore()
        assertNull(s.header("{\"pid\":1,\"id\":1,\"o\":0,\"n\":2,\"last\":true,\"sr\":48000}")); s.chunk(floats(1f, 2f))
        assertNull(s.header("{\"pid\":2,\"id\":1,\"o\":0,\"n\":2,\"last\":true,\"sr\":48000}")); s.chunk(floats(9f, 9f))
        assertArrayEquals(floats(9f, 9f), s.take(1)!!.data[0]); assertEquals(8L, s.held)
    }

    // ---- verify-n2 F5: a missing chunk is an error, abandoned transfers are released ----
    @Test fun aMissingChunkIsAnErrorNotSilence() {
        val s = PutStore()
        assertNull(s.header("{\"pid\":1,\"id\":1,\"c\":0,\"o\":0,\"n\":4,\"last\":false,\"sr\":48000,\"ch\":1}")); assertNull(s.chunk(floats(1f, 2f)).first)
        // the chunk for floats 2..3 never arrives; `last` comes with an empty chunk
        assertNull(s.header("{\"pid\":1,\"id\":1,\"c\":0,\"o\":4,\"n\":4,\"last\":true}"))
        val r = s.chunk(ByteArray(0))
        assertTrue(r.first!!.contains("incomplete")); assertEquals(-1, r.second)
        assertNull(s.take(1)); assertEquals(0, s.inFlight); assertEquals(0L, s.held)   // dropped at once, memory released
    }

    @Test fun aStereoPutWithoutItsRightChannelIsAnError() {
        val s = PutStore()
        assertNull(s.header("{\"pid\":2,\"id\":2,\"c\":0,\"o\":0,\"n\":2,\"last\":true,\"sr\":48000,\"ch\":2}"))
        assertTrue(s.chunk(floats(1f, 2f)).first!!.contains("channel 1"))
        assertNull(s.take(2)); assertEquals(0L, s.held)
    }

    @Test fun aPidIsNotReusedForAnotherId() {
        val s = PutStore()
        assertNull(s.header("{\"pid\":3,\"id\":3,\"o\":0,\"n\":4,\"last\":false,\"sr\":48000}")); s.chunk(floats(1f, 2f))
        assertNull(s.header("{\"pid\":3,\"id\":9,\"o\":2,\"n\":4,\"last\":true}"))
        assertTrue(s.chunk(floats(3f, 4f)).first!!.contains("not 9")); assertEquals(0, s.inFlight); assertEquals(0L, s.held)
    }

    @Test fun aSecondHeaderReplacesTheStaleOne() {
        val s = PutStore()
        assertNull(s.header("{\"pid\":4,\"id\":4,\"o\":0,\"n\":1,\"last\":true,\"sr\":48000}"))
        assertTrue(s.header("{\"pid\":5,\"id\":5,\"o\":0,\"n\":1,\"last\":true,\"sr\":48000}")!!.contains("dropped"))
        val r = s.chunk(floats(7f)); assertNull(r.first); assertEquals(5, r.second); assertEquals(5, s.lastPid)   // the bytes belong to the NEW header
    }

    @Test fun abandonedTransfersAreReleasedByTimeoutAndByCommit() {
        var now = 0L
        val s = PutStore(staleMs = 1000, clockMs = { now })
        assertNull(s.header("{\"pid\":6,\"id\":6,\"o\":0,\"n\":100,\"last\":false,\"sr\":48000}")); s.chunk(floats(1f))
        assertEquals(1, s.inFlight); assertEquals(400L, s.held)
        now = 500; assertEquals(0, s.sweep()); assertEquals(1, s.inFlight)
        now = 1500; assertEquals(1, s.sweep()); assertEquals(0, s.inFlight); assertEquals(0L, s.held)
        // a later header sweeps too
        now = 2000; assertNull(s.header("{\"pid\":7,\"id\":7,\"o\":0,\"n\":100,\"last\":false,\"sr\":48000}")); s.chunk(floats(1f))
        now = 4000; assertNull(s.header("{\"pid\":8,\"id\":8,\"o\":0,\"n\":100,\"last\":false,\"sr\":48000}")); assertEquals(0, s.inFlight)
        s.chunk(floats(1f)); assertEquals(1, s.inFlight)
        assertEquals(1, s.sweep(force = true)); assertEquals(0L, s.held)   // pads.commit
    }

    @Test fun theDrumModuleEnginesAreListedOnlyWhenTheBuildHasThem() {   // verify-n2 F13
        assertTrue(BridgeTranslator.engineList(true).containsAll(listOf("lxdrum", "lxsnare", "lxcym", "lxhat")))
        assertTrue(BridgeTranslator.engineList(false).none { it.startsWith("lx") })
        assertTrue(BridgeTranslator.engineList(false).containsAll(listOf("layers", "blip", "bass", "vox")))
    }
}
