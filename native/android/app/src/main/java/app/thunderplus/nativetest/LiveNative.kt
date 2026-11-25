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
    /** `at` in these calls is a HOST frame, or -1 (THUNDER_LIVE_NOW). */
    external fun nativeTransportStop(at: Long): Int
    external fun nativeBpm(bpm: Double, at: Long): Int
    external fun nativeLoop(has: Int, y0: Int, y1: Int, at: Long): Int
    external fun nativeMixSet(track: Int, key: Int, value: Double, ramp: Double, at: Long): Int
    /** The poller's latest state (JSON, see [LiveStatus.parse]). */
    external fun nativeStatus(): String
    external fun nativeStats(): String
    /** A5's clock-drift logger on the live callback (same JSON as [NativeEngine.nativeRtSnapshot]). */
    external fun nativeRtReset()
    external fun nativeRtSnapshot(): String

    // ---- H1b: the CORE-BRIDGE v1 messages (see BridgeTranslator.kt for what each page message becomes) ----
    /** song.load: the page's whole message as UTF-8 bytes. A JSON object {ok, rc, err}. */
    external fun nativeSongLoad(msg: ByteArray): String
    /** kit.load: the core renders the pads it can and publishes them. {ok, rc, err, pads, skipped, dry, renderMs, empty:[slots the page must put]}. */
    external fun nativeKitLoad(msg: ByteArray): String
    /** SongFit over the last song and kit: the `fit` event JSON. */
    external fun nativeFit(rev: Long): String
    /** put + buf.use 'pad': little-endian float32 channels; r is null for a mono pad. */
    external fun nativePadTake(pad: Int, l: ByteArray, r: ByteArray?, frames: Int, rate: Double): Int
    external fun nativePadsPublish(): Int
    external fun nativeVarSet(ii: Int, kind: Int, num: Int, pad: Int, l: ByteArray, frames: Int, rate: Double): Int
    external fun nativePlayEx(mode: Int, row: Int, track: Int, sect: Int, phrase: Int, hasLoop: Int, y0: Int, y1: Int, t0: Long, hasSeed: Int, seed: Int, pdc: Int, onlyBits: Long, hasOnly: Int): Int
    external fun nativeHold(at: Long): Int
    external fun nativeLead(seconds: Double, at: Long): Int
    external fun nativePerf(what: Int, index: Int, v: Double, at: Long): Int
    external fun nativeMetersWant(hz: Double): Int
    external fun nativeFxPlan(track: Int, slotsJson: ByteArray, bpm: Double): Int
    external fun nativeFxCommit(track: Int, at: Long): Int
    external fun nativeFxParam(track: Int, slot: Int, p: DoubleArray, at: Long): Int
    external fun nativeMasterConfig(mstJson: ByteArray, safety: Int): Int
    external fun nativeChainEdit(opsJson: ByteArray, at: Long): Int
    external fun nativeChainsOn(): Boolean
    /** The events the poller saw since the last call, one JSON object per line ("" when none). */
    external fun nativeDrainEvents(): String
    external fun nativeEngineOffset(): Long
    external fun nativePdcSet(mode: Int): Int
    /** pad.chop: slice points in FRAMES (divided by the engine rate in C++); nSlices 2..64. */
    external fun nativePadChop(pad: Int, loopSecs: Double, nSlices: Int, pointsFrames: DoubleArray): Int
    /** The callback's time over its period, peak-held (CORE-BRIDGE 7 G9). */
    external fun nativeLoad(): Double

    // ---- H1c ----
    /** Rebuilds the OUTPUT STREAM at the engine's rate and keeps the host (song, pads, rate). Stops the transport first and does not restart it. The rate, or a negative code (the host ended too when -1). */
    external fun nativeReopenStream(): Int
    /** {"calls", "state" (miniaudio's, 3 = started), "sr", "duck"} or "{}" without a host. */
    external fun nativeStreamState(): String
    /** The shell's output gain, 0..1 (audio-focus ducking), ramped in 150 ms by the audio thread. */
    external fun nativeSetDuck(gain: Float)
}
