package app.thunderplus.nativetest

/** JNI face of libthunder_android.so (miniaudio output + the C++ test core). One engine per process. */
object NativeEngine {
    init { System.loadLibrary("thunder_android") }

    /** Opens the output and starts the callback. Returns the sample rate, or -1. */
    external fun nativeStart(): Int
    external fun nativeStop()
    /** One caller at a time (the core's command queue has a single producer). */
    external fun nativePost(json: String): Boolean
    /** One caller only: the poller thread. */
    external fun nativePoll(): String?
    external fun nativePutBytes(id: Int, meta: String, bytes: ByteArray, off: Int, len: Int): Boolean
    external fun nativePutInfo(id: Int): String?
    external fun nativeInfo(): String
    /** A5's clock-drift logger: reset (done by the audio thread at its next callback) and a JSON snapshot. */
    external fun nativeRtReset()
    external fun nativeRtSnapshot(): String
}
