package app.thunderplus.nativetest

import android.media.midi.MidiDevice

/** JNI face of the MIDI out (midi_jni.cpp). Strings go as UTF-8 byte arrays (GetStringUTFChars gives modified UTF-8). Every call is safe with no engine running. */
object NativeMidi {
    init { System.loadLibrary("thunder_android") }

    /** Opens the given INPUT ports (PortInfo port numbers) of an open [MidiDevice] under the given tokens. Returns the tokens that opened. */
    external fun nativeAttach(device: MidiDevice, tokens: LongArray, ports: IntArray, ts: BooleanArray): LongArray
    external fun nativeDetach(tokens: LongArray)
    /** The whole list of devices whose ports are open. 0 or a THUNDER_MIDIOUT_E_* code (negative). */
    external fun nativeSetDevices(ids: Array<ByteArray>, names: Array<ByteArray>, tokens: LongArray, ts: BooleanArray): Int
    /** Port letter 0..3 -> a device id ("" = none). */
    external fun nativeAssign(port: Int, id: ByteArray): Int
    /** The debug test (0 = started; -1 no engine, -2 port A has no listed device, -3 already running, -4 internal, -5 no clock map yet (try again)). */
    external fun nativeTest(): Int
    /** True while the test runs; also does its housekeeping. */
    external fun nativePoll(): Boolean
    external fun nativeSenderTid(): Int
    external fun nativeStatus(): String

    // ---- N2 H2 ----
    /** Opens the given OUTPUT ports (PortInfo TYPE_OUTPUT port numbers: the app READS them) of an open [MidiDevice] under the given tokens. Returns the tokens that opened. Messages arrive as `midi.in` bridge events. */
    external fun nativeAttachIn(device: MidiDevice, tokens: LongArray, ports: IntArray): LongArray
    external fun nativeDetachIn(tokens: LongArray)
    /** Port letter 0..3: its clock box, and its Gear delay in ms (-50..200). Saved without an engine, applied through the live control thread with one. */
    external fun nativeSetClock(port: Int, on: Boolean): Int
    external fun nativeSetGear(port: Int, ms: Double): Int
    /** {"ports":[{state,id,name,gear,clock} x4],"late","sent",...} */
    external fun nativePortsJson(): String
}
