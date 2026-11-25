package app.thunderplus.nativetest

/** N2 H1a: the JNI face of the LIVE host (live_jni.cpp, thunder_live_*). Beside [NativeEngine] (the N0 test core): the two never run together, [LiveEngine] and [ThunderHost] stop one before starting the other. */
object LiveNative {
    init { System.loadLibrary("thunder_android") }

    /** thunder_live_caps(): the optional parts of this build (THUNDER_LIVE_CAP_* bits). */
    external fun nativeCaps(): Int
    /** Opens the output, creates the live host (F_EFFECTS | F_MASTER | F_METERS | F_MIDI, plus F_CHAINS when the build has it and [chains] is true) and starts its threads and the stream. The sample rate, or a negative code. */
    external fun nativeStart(chains: Boolean): Int
    /** stop, two callbacks, stream off, threads joined, destroy. Safe when not started. */
    external fun nativeShutdown()
    /** A .thunder project's bytes: renders the pads with the core, publishes the song and the pads. Blocks (seconds): call it from a worker thread. A JSON object, see [LoadResult.parse]. */
    external fun nativeLoadProject(bytes: ByteArray): String
    external fun nativePlay(row: Int): Int
    external fun nativeTransportStop(): Int
    external fun nativeBpm(bpm: Double): Int
    external fun nativeLoop(has: Int, y0: Int, y1: Int): Int
    external fun nativeMixSet(track: Int, key: Int, value: Double, ramp: Double): Int
    /** The poller's latest state (JSON, see [LiveStatus.parse]). */
    external fun nativeStatus(): String
    external fun nativeStats(): String
    /** A5's clock-drift logger on the live callback (same JSON as [NativeEngine.nativeRtSnapshot]). */
    external fun nativeRtReset()
    external fun nativeRtSnapshot(): String
}
