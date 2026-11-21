package app.thunderplus.nativetest

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class MidiModelTest {
    private fun dev(id: Int, name: String, t: Transport = Transport.USB, vararg ports: Pair<Int, String>) =
        ScanDevice(id, name, t, (if (ports.isEmpty()) arrayOf(0 to "") else ports).map { ScanPort(it.first, it.second) })

    @Test fun singlePortIsPlainName() {
        val r = MidiIds.build(listOf(dev(5, "Roland UM-ONE")))
        assertEquals("n:Roland UM-ONE", r[0].second)
        assertEquals("Roland UM-ONE", r[0].third)
    }

    @Test fun duplicateNamesGetNumbers() {
        val r = MidiIds.build(listOf(dev(9, "Synth"), dev(3, "Synth"), dev(7, "Synth")))
        // ordered by device id: 3, 7, 9
        assertEquals(listOf("n:Synth", "n:Synth #2", "n:Synth #3"), r.map { it.second })
        assertEquals(listOf(3, 7, 9), r.map { it.first.deviceId })
    }

    @Test fun severalPortsGetPortNames() {
        val r = MidiIds.build(listOf(dev(1, "Interface", Transport.USB, 0 to "MIDI 1", 1 to "MIDI 2", 2 to "")))
        assertEquals(listOf("n:Interface / MIDI 1", "n:Interface / MIDI 2", "n:Interface / Port 3"), r.map { it.second })
    }

    @Test fun emptyNameAndControlCharacters() {
        assertEquals("n:MIDI device", MidiIds.build(listOf(dev(1, "  ")))[0].second)
        assertEquals("n:A B", MidiIds.build(listOf(dev(1, "A\u0000B")))[0].second)
    }

    @Test fun idsNeverExceed95BytesAndKeepTheSuffix() {
        val long = "x".repeat(200)
        val r = MidiIds.build(listOf(dev(1, long), dev(2, long)))
        assertTrue(r.all { MidiIds.utf8Len(it.second) <= 95 })
        assertTrue(r[1].second.endsWith(" #2"))
        assertNotEquals(r[0].second, r[1].second)
        val multi = MidiIds.build(listOf(dev(1, long, Transport.USB, 0 to "y".repeat(100), 1 to "z")))
        assertTrue(multi.all { MidiIds.utf8Len(it.second) <= 95 })
        assertTrue(multi[0].second.contains(" / y"))
    }

    @Test fun clampNeverSplitsACharacter() {
        val s = "abé🎹cd"   // a, b, e-acute (2 bytes), keyboard emoji (4 bytes), c, d
        assertEquals("abé", MidiIds.clampUtf8(s, 4))
        assertEquals("abé🎹", MidiIds.clampUtf8(s, 8))
        assertEquals("ab", MidiIds.clampUtf8(s, 3 - 1))
        assertEquals("", MidiIds.clampUtf8(s, 0))
        assertEquals(s, MidiIds.clampUtf8(s, 100))
    }

    @Test fun transportAndTimestampCaps() {
        assertEquals(Transport.USB, Transport.fromAndroid(1))
        assertEquals(Transport.VIRTUAL, Transport.fromAndroid(2))
        assertEquals(Transport.BLUETOOTH, Transport.fromAndroid(3))
        assertEquals(Transport.OTHER, Transport.fromAndroid(99))
        val reg = MidiRegistry()
        reg.update(listOf(dev(1, "U", Transport.USB), dev(2, "V", Transport.VIRTUAL), dev(3, "B", Transport.BLUETOOTH)))
        assertEquals(listOf(true, false, false), reg.entries.map { it.ts })
    }

    @Test fun tokensAreACounterNeverReused() {
        val reg = MidiRegistry()
        val d1 = reg.update(listOf(dev(1, "A"), dev(2, "B")))
        assertEquals(listOf(1L, 2L), d1.added.map { it.token })
        // B leaves, C arrives: C is a NEW token, not B's
        val d2 = reg.update(listOf(dev(1, "A"), dev(3, "C")))
        assertEquals(listOf(2L), d2.removed.map { it.token })
        assertEquals(listOf(3L), d2.added.map { it.token })
        assertEquals(listOf(1L), d2.kept.map { it.token })
        // the same hardware plugged back in (a new Android id, or even the same one) gets a new token too
        val d3 = reg.update(listOf(dev(1, "A")))
        assertEquals(listOf(3L), d3.removed.map { it.token })
        val d4 = reg.update(listOf(dev(1, "A"), dev(3, "C")))
        assertEquals(listOf(4L), d4.added.map { it.token })
    }

    @Test fun diffOfTwoScans() {
        val reg = MidiRegistry()
        assertTrue(reg.update(emptyList()).isEmpty)
        reg.update(listOf(dev(1, "X"), dev(2, "X")))
        assertEquals(listOf("n:X", "n:X #2"), reg.entries.map { it.id })
        assertTrue(reg.update(listOf(dev(1, "X"), dev(2, "X"))).isEmpty)
        // the first X leaves: the second keeps its token but its id shifts to the plain name (the web's known limit)
        val d = reg.update(listOf(dev(2, "X")))
        assertEquals(1, d.removed.size)
        assertEquals(1, d.changed.size)
        assertEquals("n:X", d.changed[0].id)
        assertEquals(2L, d.changed[0].token)
        assertFalse(d.isEmpty)
    }

    @Test fun portKeysKeepTheirTokenAcrossScans() {
        val reg = MidiRegistry()
        reg.update(listOf(dev(4, "Multi", Transport.USB, 0 to "a", 1 to "b")))
        val t = reg.entries.map { it.token }
        val d = reg.update(listOf(dev(4, "Multi", Transport.USB, 0 to "a", 1 to "b")))
        assertTrue(d.isEmpty)
        assertEquals(t, reg.entries.map { it.token })
        assertEquals(listOf(0, 1), reg.entries.map { it.key.portNumber })
    }

    @Test fun idsAreUniqueAfterSuffixAndAfterTruncation() {
        val r = MidiIds.build(listOf(dev(1, "Synth"), dev(2, "Synth"), dev(3, "Synth #2")))
        assertEquals(3, r.map { it.second }.toSet().size)
        assertEquals(listOf("n:Synth", "n:Synth #2", "n:Synth #2 #2"), r.map { it.second })
        // two different long names with the same first 93 characters become one id after shortening: the second takes a number
        val a = "x".repeat(150) + "a"; val b = "x".repeat(150) + "b"
        val t = MidiIds.build(listOf(dev(1, a), dev(2, b)))
        assertEquals(2, t.map { it.second }.toSet().size)
        assertTrue(t.all { MidiIds.utf8Len(it.second) <= 95 })
        assertTrue(t[1].second.endsWith(" #2"))
    }
}
