package app.thunderplus.nativetest

import android.content.Context
import android.content.pm.PackageManager
import android.media.midi.MidiDevice
import android.media.midi.MidiDeviceInfo
import android.media.midi.MidiManager
import android.os.Handler
import android.os.Looper
import android.os.Process

/**
 * U36 T9: the Android side of MIDI OUT. Lists the output-capable devices (the ports the app WRITES to are a device's INPUT ports), builds the web-style ids and the tokens
 * (MidiModel.kt, unit-tested), opens each device (MidiManager.openDevice is ASYNCHRONOUS: the ports join the list only once the open has finished), hands the open MidiDevice to
 * AMidi (midi_jni.cpp) and keeps the core's device list current on every hot-plug. USB and virtual devices only: Bluetooth LE MIDI is NOT done in this slice (it would need
 * BLUETOOTH_CONNECT / BLUETOOTH_SCAN and a BluetoothDevice open; a paired BLE device that MidiManager lists anyway is opened like any other, but untested and without timestamps).
 * No MIDI permission is needed for USB or virtual devices (UNVERIFIED on the phone).
 * The callbacks and openDevice's listener run on the MAIN thread, but the bridge worker (LiveMidi.apply / publish) and the debug UI's work thread reach the same state: every method that reads or writes the
 * registries and the token sets takes `lock` (reentrant; verify B2). The lock is never held while waiting for the bridge: portsEvent copies under it and builds the JSON outside.
 */
class MidiPorts private constructor(private val app: Context) {
    private val main = Handler(Looper.getMainLooper())
    private val mgr: MidiManager? =
        if (app.packageManager.hasSystemFeature(PackageManager.FEATURE_MIDI)) app.getSystemService(Context.MIDI_SERVICE) as? MidiManager else null
    private val registry = MidiRegistry()
    private val registryIn = MidiRegistry()               // N2 H2: the ports the app READS (MIDI in); the same open MidiDevice serves both directions
    private val attachedIn = HashSet<Long>()              // tokens whose AMidi output port is open
    private val missingIn = HashSet<Long>()
    private val infos = HashMap<Int, MidiDeviceInfo>()
    private val opened = HashMap<Int, MidiDevice>()      // deviceId -> open device
    private val opening = HashSet<Int>()
    private val failed = HashSet<Int>()                  // open or AMidi attach failed: not retried until the device leaves
    private val attached = HashSet<Long>()               // tokens whose AMidi port is open
    private val missing = HashSet<Long>()                // tokens of an OPEN device whose port did not open (a partial attach): retried alone, the device stays open
    private val lock = Any()
    private var started = false
    private var busyRetries = 0
    private var assignedA = ""
    @Volatile private var note = ""

    private val callback = object : MidiManager.DeviceCallback() {
        override fun onDeviceAdded(device: MidiDeviceInfo) { rescan() }
        override fun onDeviceRemoved(device: MidiDeviceInfo) { rescan() }
        override fun onDeviceStatusChanged(status: android.media.midi.MidiDeviceStatus) { /* nothing the output needs */ }
    }

    val available: Boolean get() = mgr != null

    /** Registers for hot-plug and scans once. Idempotent. Main thread. */
    fun start() {
        val m = mgr ?: run { note = "no android.software.midi on this device"; return }
        if (started) return
        started = true
        m.registerDeviceCallback(callback, main)
        rescan()
    }

    private fun nameOf(i: MidiDeviceInfo): String {
        val p = i.properties
        return p.getString(MidiDeviceInfo.PROPERTY_NAME) ?: p.getString(MidiDeviceInfo.PROPERTY_PRODUCT) ?: p.getString(MidiDeviceInfo.PROPERTY_MANUFACTURER) ?: ""
    }

    private fun toRaw(i: MidiDeviceInfo): RawDevice = RawDevice(
        i.id, nameOf(i), Transport.fromAndroid(i.type),
        i.ports.map { RawPort(it.portNumber, it.name ?: "", it.type == MidiDeviceInfo.PortInfo.TYPE_INPUT) })

    /** One scan: diff, drop what left (list first, then the ports), open what is new. Main thread. N2 H2: both directions of every device (a keyboard has only a MIDI in). */
    fun rescan() = synchronized(lock) { rescanLocked() }
    private fun rescanLocked() {
        val m = mgr ?: return
        val now = m.devices.filter { it.inputPortCount > 0 || it.outputPortCount > 0 }
        infos.clear(); for (i in now) infos[i.id] = i
        val raw = now.map { toRaw(it) }
        val diff = registry.update(MidiScanSplit.outs(raw))
        val diffIn = registryIn.update(MidiScanSplit.ins(raw))
        if (diff.removed.isNotEmpty()) {
            diff.removed.forEach { attached.remove(it.token); missing.remove(it.token) }
            pushList()   // the core stops listing them BEFORE their ports close: a send that was in flight meets an unknown token (< 0), never a freed port
            NativeMidi.nativeDetach(diff.removed.map { it.token }.toLongArray())
        } else if (diff.changed.isNotEmpty()) pushList()
        if (diffIn.removed.isNotEmpty()) {
            diffIn.removed.forEach { attachedIn.remove(it.token); missingIn.remove(it.token) }
            NativeMidi.nativeDetachIn(diffIn.removed.map { it.token }.toLongArray())
            publishPorts()
        } else if (diffIn.changed.isNotEmpty()) publishPorts()
        val live = MidiScanSplit.deviceIds(registry.entries, registryIn.entries).toSet()
        for (id in (diff.removed + diffIn.removed).map { it.key.deviceId }.distinct()) if (id !in live) {
            opened.remove(id)?.let { try { it.close() } catch (e: Exception) { } }
            opening.remove(id); failed.remove(id)
        }
        for (id in live) {
            if (id in opened || id in opening || id in failed) continue
            val info = infos[id] ?: continue
            opening.add(id)
            m.openDevice(info, { dev -> onOpened(id, dev) }, main)
        }
    }

    private fun onOpened(id: Int, dev: MidiDevice?) = synchronized(lock) { onOpenedLocked(id, dev) }
    private fun onOpenedLocked(id: Int, dev: MidiDevice?) {
        opening.remove(id)
        val mine = registry.entries.filter { it.key.deviceId == id }
        val mineIn = registryIn.entries.filter { it.key.deviceId == id }
        if (dev == null) { failed.add(id); return }   // the open failed
        if (mine.isEmpty() && mineIn.isEmpty()) { try { dev.close() } catch (e: Exception) { }; return }   // the device left while it was opening
        val ok = if (mine.isEmpty()) LongArray(0) else NativeMidi.nativeAttach(dev, mine.map { it.token }.toLongArray(), mine.map { it.key.portNumber }.toIntArray(), mine.map { it.ts }.toBooleanArray())
        val okIn = if (mineIn.isEmpty()) LongArray(0) else NativeMidi.nativeAttachIn(dev, mineIn.map { it.token }.toLongArray(), mineIn.map { it.key.portNumber }.toIntArray())
        if (ok.isEmpty() && okIn.isEmpty()) { failed.add(id); try { dev.close() } catch (e: Exception) { }; note = "AMidi could not open device $id (tap MIDI A: next device to retry)"; return }
        opened[id] = dev
        ok.forEach { attached.add(it) }   // only the ports that really opened are listed
        okIn.forEach { attachedIn.add(it) }
        mine.forEach { if (it.token !in ok) missing.add(it.token) }
        mineIn.forEach { if (it.token !in okIn) missingIn.add(it.token) }
        pushList()
    }

    /** The core's list = the registry's entries whose port is open. */
    private fun pushList() = synchronized(lock) { pushListLocked() }
    private fun pushListLocked() {
        val l = registry.entries.filter { it.token in attached }
        val r = NativeMidi.nativeSetDevices(
            l.map { it.id.toByteArray(Charsets.UTF_8) }.toTypedArray(), l.map { it.name.toByteArray(Charsets.UTF_8) }.toTypedArray(),
            l.map { it.token }.toLongArray(), l.map { it.ts }.toBooleanArray())
        if (r == BUSY_MIDI || r == BUSY_LIVE) {   // B7: the control thread was busy (a load): the core kept the OLD list. Try again a few times, off this call (main must not wait), so a plugged-in device does get listed
            note = "set_devices busy, retrying"
            if (busyRetries < 6) { busyRetries++; main.postDelayed({ pushList() }, 400L) } else { busyRetries = 0; note = "set_devices refused: $r" }
        } else {
            busyRetries = 0
            if (r != 0) note = "set_devices refused: $r"
        }
        publishPorts()
    }

    /** N2 H2: the `midi.ports` event for the page (outputs, inputs with their `midi.in` port numbers, the four letters, late and sent counts). Only while the live bridge listens. Main thread or any. */
    fun portsEvent(): String {
        val (outs, ins) = synchronized(lock) { Pair(registry.entries.filter { it.token in attached }, registryIn.entries.filter { it.token in attachedIn }) }
        return MidiPortsEvent.build(outs, ins, NativeMidi.nativePortsJson())
    }
    fun publishPorts() { if (LiveEngine.active && LiveBridge.liveMode) LiveBridge.send(portsEvent()) }

    val listedIds: List<String> get() = synchronized(lock) { registry.entries.filter { it.token in attached }.map { it.id } }

    /** Debug UI: letter A to the next listed device (or the first). */
    fun cycleA(): String = synchronized(lock) {
        retryFailed()
        val ids = listedIds
        if (ids.isEmpty()) { assignedA = ""; NativeMidi.nativeAssign(0, ByteArray(0)); note = "no MIDI output device listed"; return@synchronized note }
        val i = ids.indexOf(assignedA)
        assignedA = ids[(i + 1) % ids.size]
        NativeMidi.nativeAssign(0, assignedA.toByteArray(Charsets.UTF_8))
        note = "A = $assignedA"
        note
    }

    /** A device whose open failed is tried again (a failure can be temporary): on every debug-UI tap, never in a loop. */
    private fun retryFailed() = synchronized(lock) {
        if (failed.isNotEmpty()) { failed.clear(); rescanLocked() }
        retryMissing()
    }

    /** A port of an open device that did not open (a partial attach) is attached again, alone: the device stays open and the ports already attached are untouched. */
    private fun retryMissing() = synchronized(lock) { retryMissingLocked() }
    private fun retryMissingLocked() {
        if (missing.isEmpty() && missingIn.isEmpty()) return
        var any = false
        for ((id, dev) in opened) {
            val mine = registry.entries.filter { it.key.deviceId == id && it.token in missing }
            if (mine.isEmpty()) continue
            val ok = NativeMidi.nativeAttach(dev, mine.map { it.token }.toLongArray(), mine.map { it.key.portNumber }.toIntArray(), mine.map { it.ts }.toBooleanArray())
            ok.forEach { attached.add(it); missing.remove(it); any = true }
        }
        for ((id, dev) in opened) {   // the same for the input ports
            val mineIn = registryIn.entries.filter { it.key.deviceId == id && it.token in missingIn }
            if (mineIn.isEmpty()) continue
            val okIn = NativeMidi.nativeAttachIn(dev, mineIn.map { it.token }.toLongArray(), mineIn.map { it.key.portNumber }.toIntArray())
            okIn.forEach { attachedIn.add(it); missingIn.remove(it); any = true }
        }
        if (any) pushList()
    }

    /** Debug UI: starts the engine if needed, puts A on a device if it has none, runs the two-bar test. Returns what to show. */
    fun runTest(ctx: Context): String {
        if (!ThunderHost.ensureRunning(ctx)) return "audio did not start"
        retryFailed()
        if (synchronized(lock) { assignedA.isEmpty() || assignedA !in listedIds }) cycleA()
        val r = NativeMidi.nativeTest()
        val tid = NativeMidi.nativeSenderTid()
        if (tid > 0) try { Process.setThreadPriority(tid, Process.THREAD_PRIORITY_URGENT_AUDIO) } catch (e: Exception) { }
        note = when (r) {
            0 -> "MIDI out test running: about 5 s to port A (${synchronized(lock) { assignedA }})"
            -2 -> "port A has no listed device (plug in a USB MIDI device)"
            -3 -> "a test is already running (wait for it to finish)"
            -5 -> "the engine has only just started: tap again in a second"
            -1 -> "no engine"
            else -> "MIDI out test failed ($r)"
        }
        return note
    }

    fun statusText(): String {
        NativeMidi.nativePoll()
        val counts = synchronized(lock) { "open devices: ${opened.size}, opening: ${opening.size}, failed: ${failed.size}" }
        return NativeMidi.nativeStatus() + (if (note.isNotEmpty()) "\nnote: $note" else "") + "\n" + counts
    }

    companion object {
        private const val BUSY_MIDI = -3   // THUNDER_MIDI_E_BUSY: the scheduler's command ring was full
        private const val BUSY_LIVE = -8   // THUNDER_LIVE_E_BUSY: the live control thread's runner answered busy (a load is running)
        @Volatile private var inst: MidiPorts? = null
        fun get(ctx: Context): MidiPorts = inst ?: synchronized(this) { inst ?: MidiPorts(ctx.applicationContext).also { inst = it } }
    }
}
