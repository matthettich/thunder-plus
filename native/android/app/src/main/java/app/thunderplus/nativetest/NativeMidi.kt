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
}
