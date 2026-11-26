package app.thunderplus.nativetest

import android.content.Context
import android.os.Process
import android.util.Log

/**
 * N2 H2: the shell side of MIDI on the LIVE engine. The native host (midi_out.cpp, external mode) runs the core's own MIDI pump on the live handle's thunder_midi; this object only
 * (a) applies the page's `midi.cfg` (the four letters, their clock boxes and Gear delays), (b) raises the sender thread's priority when the engine starts, and (c) sends the page the `midi.ports`
 * event. Device scanning, hot-plug and the AMidi ports are MidiPorts.kt (U36), unchanged in how they work; MIDI in arrives as `midi.in` events pushed by the native reader straight into the
 * bridge's event queue (midi_in.h). The SONG's own routing (which instrument goes to which port and channel) is the song's: the core's scheduler reads it, the shell does not.
 */
object LiveMidi {
    private const val TAG = "ThunderLiveMidi"
    @Volatile var lastCfg: BridgeTranslator.LiveCall.MidiCfg? = null
        private set

    /** The engine is up: the sender thread gets the audio priority (the native side asks for it too; Android may refuse it there), and the saved letters are in force already (the host applies them at start). */
    fun engineUp() {
        val tid = NativeMidi.nativeSenderTid()
        if (tid > 0) try { Process.setThreadPriority(tid, Process.THREAD_PRIORITY_URGENT_AUDIO) } catch (e: Exception) { Log.i(TAG, "sender priority: $e") }
    }

    /** midi.cfg: letter i -> device id, clock box, Gear delay. Remembered, so a rebuilt engine gets the same letters (the native host also saves them). */
    fun apply(ctx: Context, c: BridgeTranslator.LiveCall.MidiCfg): String {
        lastCfg = c
        var bad = ""
        for (i in 0..3) {
            val r = NativeMidi.nativeAssign(i, c.outs[i].toByteArray(Charsets.UTF_8))
            if (r < 0 && bad.isEmpty()) bad = "port ${'A' + i}: assign refused ($r)"
            NativeMidi.nativeSetClock(i, c.clockOut[i])
            NativeMidi.nativeSetGear(i, c.gear[i])
        }
        MidiPorts.get(ctx).publishPorts()
        return bad
    }

    /** The page asked (hello) or the ticker says it is time: the current `midi.ports` event. */
    fun publish(ctx: Context) { MidiPorts.get(ctx).publishPorts() }
}
