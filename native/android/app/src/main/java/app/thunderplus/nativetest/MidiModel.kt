package app.thunderplus.nativetest

// U36 T9: the PURE logic of the MIDI out device list: no android.* import, so it runs in the JVM unit tests (app/src/test). MidiPorts.kt feeds it what MidiManager reports.
// Ids are the web app's (spec 5.3 and the lead's ruling): "n:" + the name; a second device with the same id gets " #2" (a third " #3"); a device with SEVERAL output
// ports (the ports the app writes to = the device's INPUT ports) gets "n:Name / PortName" per port. Tokens: a counter, never reused for another device in this process.

enum class Transport { USB, VIRTUAL, BLUETOOTH, OTHER;
    companion object {
        // MidiDeviceInfo.TYPE_USB = 1, TYPE_VIRTUAL = 2, TYPE_BLUETOOTH = 3 (the constants' values are part of the Android API)
        fun fromAndroid(type: Int): Transport = when (type) { 1 -> USB; 2 -> VIRTUAL; 3 -> BLUETOOTH; else -> OTHER }
    }
}

/** One input port of a device (what the app writes to). [portNumber] is PortInfo.getPortNumber(), the number AMidiInputPort_open takes. */
data class ScanPort(val portNumber: Int, val name: String)

/** A device as one scan saw it. [ports] holds only its INPUT ports. [deviceId] = MidiDeviceInfo.getId() (new for every connection of the same hardware). */
data class ScanDevice(val deviceId: Int, val name: String, val transport: Transport, val ports: List<ScanPort>)

data class PortKey(val deviceId: Int, val portNumber: Int)

/** One listed output. [ts]: the shell hands this port timestamps (USB only until Bluetooth / virtual are measured). */
data class PortEntry(val token: Long, val key: PortKey, val id: String, val name: String, val ts: Boolean)

/** What changed between two scans. [changed]: same key, other id or name (an id shifts when one of two same-name devices leaves). */
data class ScanDiff(val added: List<PortEntry>, val removed: List<PortEntry>, val changed: List<PortEntry>, val kept: List<PortEntry>) {
    val isEmpty: Boolean get() = added.isEmpty() && removed.isEmpty() && changed.isEmpty()
}

object MidiIds {
    /** The core refuses an id of 96 bytes or more (the whole list with it). */
    const val MAX_ID_BYTES = 95

    fun utf8Len(s: String): Int = s.toByteArray(Charsets.UTF_8).size

    /** [s] cut to at most [maxBytes] UTF-8 bytes, never inside a character (a surrogate pair is kept whole or dropped). */
    fun clampUtf8(s: String, maxBytes: Int): String {
        if (maxBytes <= 0) return ""
        var bytes = 0
        var i = 0
        while (i < s.length) {
            val cp = s.codePointAt(i)
            val n = when { cp < 0x80 -> 1; cp < 0x800 -> 2; cp < 0x10000 -> 3; else -> 4 }
            if (bytes + n > maxBytes) break
            bytes += n
            i += Character.charCount(cp)
        }
        return s.substring(0, i)
    }

    private fun cleanName(n: String?): String {
        val t = (n ?: "").replace(Regex("[\\u0000-\\u001f]"), " ").trim()
        return if (t.isEmpty()) "MIDI device" else t
    }

    /**
     * The rows of a scan in a stable order (device id, then port number): (key, base id, display base). [suffix] is added by [build] for duplicates.
     * The device name part is shortened (never the suffix) so the whole id fits [MAX_ID_BYTES].
     */
    fun build(devices: List<ScanDevice>): List<Triple<PortKey, String, String>> {
        val out = ArrayList<Triple<PortKey, String, String>>()
        val seen = HashMap<String, Int>()
        val used = HashSet<String>()   // the FINAL ids: the core refuses the whole list for a repeated id
        for (d in devices.sortedBy { it.deviceId }) {
            val name = cleanName(d.name)
            val several = d.ports.size > 1
            for (p in d.ports.sortedBy { it.portNumber }) {
                val portName = if (several) cleanName(p.name.ifBlank { "Port ${p.portNumber + 1}" }) else ""
                val shown0 = if (several) "$name / $portName" else name
                var count = (seen[shown0] ?: 0) + 1
                var id: String
                var shown: String
                while (true) {   // a clash after the suffix or after shortening (e.g. "Synth", "Synth", "Synth #2") takes the next number
                    val suffix = if (count > 1) " #$count" else ""
                    // the whole "n:" + shown + suffix must fit: shorten the name (and the port name) first, never the suffix
                    val room = MAX_ID_BYTES - 2 - utf8Len(suffix)
                    val cut = if (utf8Len(shown0) <= room) shown0 else if (several) {
                        val pn = clampUtf8(portName, room / 3)
                        clampUtf8(name, room - utf8Len(" / ") - utf8Len(pn)) + " / " + pn
                    } else clampUtf8(name, room)
                    shown = cut + suffix
                    id = "n:$shown"
                    if (used.add(id)) break
                    count++
                }
                seen[shown0] = count
                out.add(Triple(PortKey(d.deviceId, p.portNumber), id, shown))
            }
        }
        return out
    }
}

/** Hands out tokens: 1, 2, 3 ... A key keeps its token while the device stays listed; once it left, the same key (or the same hardware) gets a NEW token. */
class TokenAllocator {
    private var next = 1L
    private val byKey = HashMap<PortKey, Long>()
    fun tokenFor(key: PortKey): Long = byKey.getOrPut(key) { next++ }
    fun forget(key: PortKey) { byKey.remove(key) }
    fun live(): Set<PortKey> = byKey.keys.toSet()
}

/** Scan after scan -> the list of entries and the difference. Pure; not thread safe (the caller serialises: MidiPorts runs it on the main thread). */
class MidiRegistry {
    private val tokens = TokenAllocator()
    private var current: List<PortEntry> = emptyList()
    val entries: List<PortEntry> get() = current

    fun update(scan: List<ScanDevice>): ScanDiff {
        val ts = HashMap<Int, Boolean>()
        for (d in scan) ts[d.deviceId] = d.transport == Transport.USB
        val rows = MidiIds.build(scan)
        val next = rows.map { (key, id, name) -> PortEntry(tokens.tokenFor(key), key, id, name, ts[key.deviceId] == true) }
        val old = current.associateBy { it.key }
        val nu = next.associateBy { it.key }
        val removed = current.filter { it.key !in nu }
        removed.forEach { tokens.forget(it.key) }
        val added = next.filter { it.key !in old }
        val changed = next.filter { val o = old[it.key]; o != null && (o.id != it.id || o.name != it.name || o.ts != it.ts) }
        val kept = next.filter { val o = old[it.key]; o != null && o.id == it.id && o.name == it.name && o.ts == it.ts }
        current = next
        return ScanDiff(added, removed, changed, kept)
    }
}
