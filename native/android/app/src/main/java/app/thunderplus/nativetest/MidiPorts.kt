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
 * Everything here runs on the MAIN thread (the callbacks and openDevice's listener are given the main handler), so the registry needs no lock.
 */
class MidiPorts private constructor(private val app: Context) {
    private val main = Handler(Looper.getMainLooper())
    private val mgr: MidiManager? =
        if (app.packageManager.hasSystemFeature(PackageManager.FEATURE_MIDI)) app.getSystemService(Context.MIDI_SERVICE) as? MidiManager else null
    private val registry = MidiRegistry()
    private val infos = HashMap<Int, MidiDeviceInfo>()
    private val opened = HashMap<Int, MidiDevice>()      // deviceId -> open device
    private val opening = HashSet<Int>()
    private val failed = HashSet<Int>()                  // open or AMidi attach failed: not retried until the device leaves
    private val attached = HashSet<Long>()               // tokens whose AMidi port is open
    private var started = false
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

    private fun toScan(i: MidiDeviceInfo): ScanDevice = ScanDevice(
        i.id, nameOf(i), Transport.fromAndroid(i.type),
        i.ports.filter { it.type == MidiDeviceInfo.PortInfo.TYPE_INPUT }.map { ScanPort(it.portNumber, it.name ?: "") })

    /** One scan: diff, drop what left (list first, then the ports), open what is new. Main thread. */
    fun rescan() {
        val m = mgr ?: return
        val now = m.devices.filter { it.inputPortCount > 0 }
        infos.clear(); for (i in now) infos[i.id] = i
        val diff = registry.update(now.map { toScan(it) })
        if (diff.removed.isNotEmpty()) {
            diff.removed.forEach { attached.remove(it.token) }
            pushList()   // the core stops listing them BEFORE their ports close: a send that was in flight meets an unknown token (< 0), never a freed port
            NativeMidi.nativeDetach(diff.removed.map { it.token }.toLongArray())
            val live = registry.entries.map { it.key.deviceId }.toSet()
            for (id in diff.removed.map { it.key.deviceId }.distinct()) if (id !in live) {
                opened.remove(id)?.let { try { it.close() } catch (e: Exception) { } }
                opening.remove(id); failed.remove(id)
            }
        } else if (diff.changed.isNotEmpty()) pushList()
        for (id in registry.entries.map { it.key.deviceId }.distinct()) {
            if (id in opened || id in opening || id in failed) continue
            val info = infos[id] ?: continue
            opening.add(id)
            m.openDevice(info, { dev -> onOpened(id, dev) }, main)
        }
    }

    private fun onOpened(id: Int, dev: MidiDevice?) {
        opening.remove(id)
        val mine = registry.entries.filter { it.key.deviceId == id }
        if (dev == null) { failed.add(id); return }   // the open failed
        if (mine.isEmpty()) { try { dev.close() } catch (e: Exception) { }; return }   // the device left while it was opening
        val ok = NativeMidi.nativeAttach(dev, mine.map { it.token }.toLongArray(), mine.map { it.key.portNumber }.toIntArray(), mine.map { it.ts }.toBooleanArray())
        if (ok.isEmpty()) { failed.add(id); try { dev.close() } catch (e: Exception) { }; note = "AMidi could not open device $id (tap MIDI A: next device to retry)"; return }
        opened[id] = dev
        ok.forEach { attached.add(it) }   // only the ports that really opened are listed
        pushList()
    }

    /** The core's list = the registry's entries whose port is open. */
    private fun pushList() {
        val l = registry.entries.filter { it.token in attached }
        val r = NativeMidi.nativeSetDevices(
            l.map { it.id.toByteArray(Charsets.UTF_8) }.toTypedArray(), l.map { it.name.toByteArray(Charsets.UTF_8) }.toTypedArray(),
            l.map { it.token }.toLongArray(), l.map { it.ts }.toBooleanArray())
        if (r != 0) note = "set_devices refused: $r"
    }

    val listedIds: List<String> get() = registry.entries.filter { it.token in attached }.map { it.id }

    /** Debug UI: letter A to the next listed device (or the first). */
    fun cycleA(): String {
        retryFailed()
        val ids = listedIds
        if (ids.isEmpty()) { assignedA = ""; NativeMidi.nativeAssign(0, ByteArray(0)); note = "no MIDI output device listed"; return note }
        val i = ids.indexOf(assignedA)
        assignedA = ids[(i + 1) % ids.size]
        NativeMidi.nativeAssign(0, assignedA.toByteArray(Charsets.UTF_8))
        note = "A = $assignedA"
        return note
    }

    /** A device whose open failed is tried again (a failure can be temporary): on every debug-UI tap, never in a loop. */
    private fun retryFailed() { if (failed.isNotEmpty()) { failed.clear(); rescan() } }

    /** Debug UI: starts the engine if needed, puts A on a device if it has none, runs the two-bar test. Returns what to show. */
    fun runTest(ctx: Context): String {
        if (!ThunderHost.ensureRunning(ctx)) return "audio did not start"
        retryFailed()
        if (assignedA.isEmpty() || assignedA !in listedIds) cycleA()
        val r = NativeMidi.nativeTest()
        val tid = NativeMidi.nativeSenderTid()
        if (tid > 0) try { Process.setThreadPriority(tid, Process.THREAD_PRIORITY_URGENT_AUDIO) } catch (e: Exception) { }
        note = when (r) {
            0 -> "MIDI out test running: about 5 s to port A ($assignedA)"
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
        return NativeMidi.nativeStatus() + (if (note.isNotEmpty()) "\nnote: $note" else "") + "\nopen devices: ${opened.size}, opening: ${opening.size}, failed: ${failed.size}"
    }

    companion object {
        @Volatile private var inst: MidiPorts? = null
        fun get(ctx: Context): MidiPorts = inst ?: synchronized(this) { inst ?: MidiPorts(ctx.applicationContext).also { inst = it } }
    }
}
