package app.thunderplus.nativetest

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.media.AudioAttributes
import android.media.AudioDeviceCallback
import android.media.AudioDeviceInfo
import android.media.AudioFocusRequest
import android.media.AudioManager
import android.media.MediaMetadata
import android.media.session.MediaSession
import android.media.session.PlaybackState
import android.os.Handler
import android.os.Looper
import android.util.Log
import java.util.concurrent.Executors

/**
 * N2 H1c: everything around the live engine that is Android's: audio focus, the headphones-pulled broadcast, output-device changes, the media session and the 500 ms ticker (stream watchdog, A5 gate,
 * notification refresh). All CONTROL side: the audio thread never sees any of it (the only thing it reads is the ducking gain LiveNative.nativeSetDuck sets). The DECISIONS are in LivePolicy.kt (pure, JVM-tested);
 * this file only calls Android and the engine. UNVERIFIED on a device (no Android here: CI compiles it); the owner's test list is in docs/port/N2-NOTES.md.
 *
 * Attached by ThunderService while the live engine runs, detached when the service ends.
 */
object LiveSession {
    private const val TAG = "ThunderLiveSession"
    private val main = Handler(Looper.getMainLooper())
    /** Engine calls block for milliseconds (E_BUSY retry): one worker, never the main thread. */
    private val exec = Executors.newSingleThreadExecutor { Thread(it, "thunder-live-session").also { t -> t.isDaemon = true } }

    private var service: ThunderService? = null
    private var am: AudioManager? = null
    private var focusReq: AudioFocusRequest? = null
    private var mediaSession: MediaSession? = null
    private var art: Bitmap? = null
    private var noisy: BroadcastReceiver? = null
    private var devCb: AudioDeviceCallback? = null
    private val watch = StreamWatch(1000)
    private var lastSig = ""

    @Volatile var hasFocus = false
        private set
    /** A STOP_AND_REMEMBER is waiting for its GAIN. */
    @Volatile var armed = false
        private set
    @Volatile private var ducked = false
    @Volatile private var rememberedRow = 0
    @Volatile private var lastPlayReqMs = 0L
    private val focusLock = Any()
    @Volatile var lastEvent = ""
        private set

    val session: MediaSession? get() = mediaSession

    fun attach(svc: ThunderService) {
        if (service === svc) return
        detach()
        service = svc
        val a = svc.getSystemService(Context.AUDIO_SERVICE) as AudioManager
        am = a
        art = try { svc.assets.open("icons/plus-icon-512.png").use { BitmapFactory.decodeStream(it) } } catch (e: Exception) { null }   // the app's own icon, from the page's assets
        mediaSession = MediaSession(svc, "ThunderPlus").apply {
            setCallback(object : MediaSession.Callback() {
                override fun onPlay() { exec.execute { LiveEngine.play(); refresh() } }
                override fun onPause() { exec.execute { LiveEngine.stop(); refresh() } }   // there is no pause: a song is not resumable from the middle (H1c decision), so Pause is Stop
                override fun onStop() { exec.execute { LiveEngine.stop(); refresh() } }
            })
            isActive = true
        }
        // headphones pulled (or a Bluetooth sink dropped): Android sends this BEFORE it re-routes, so the song never leaks to the speaker
        noisy = object : BroadcastReceiver() {
            override fun onReceive(c: Context?, i: Intent?) {
                if (i?.action != AudioManager.ACTION_AUDIO_BECOMING_NOISY) return
                lastEvent = "output unplugged: stopped"
                exec.execute { if (LiveEngine.active) { LiveEngine.stop(); LiveBridge.send("{\"t\":\"device\",\"reason\":\"noisy\",\"stopped\":true}") }; refresh() }
            }
        }
        androidx.core.content.ContextCompat.registerReceiver(svc, noisy, IntentFilter(AudioManager.ACTION_AUDIO_BECOMING_NOISY), androidx.core.content.ContextCompat.RECEIVER_NOT_EXPORTED)
        devCb = object : AudioDeviceCallback() {
            override fun onAudioDevicesAdded(added: Array<out AudioDeviceInfo>?) { onDevices("added", added) }
            override fun onAudioDevicesRemoved(removed: Array<out AudioDeviceInfo>?) { onDevices("removed", removed) }
        }
        a.registerAudioDeviceCallback(devCb, main)
        lastSig = ""
        watch.reset()
        main.postDelayed(ticker, 500)
        Log.i(TAG, "attached")
    }

    fun detach() {
        main.removeCallbacks(ticker)
        val svc = service ?: return
        service = null
        try { devCb?.let { am?.unregisterAudioDeviceCallback(it) } } catch (e: Exception) { }
        try { noisy?.let { svc.unregisterReceiver(it) } } catch (e: Exception) { }
        abandonFocus()
        try { mediaSession?.isActive = false; mediaSession?.release() } catch (e: Exception) { }
        mediaSession = null; devCb = null; noisy = null; am = null; armed = false; ducked = false
    }

    // ---- audio focus ----------------------------------------------------------------------------------------------------------------------------------------------------------------------

    /** Asks for the audio focus before a Play. false: refused (a call is in progress): the transport must not start. Safe from any thread. */
    @Synchronized fun beforePlay(): Boolean {
        lastPlayReqMs = System.currentTimeMillis()
        armed = false
        if (hasFocus) return true
        val a = am ?: return true   // no service yet (a JVM or a very early call): do not block the song on the notification
        val req = AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN)
            .setAudioAttributes(AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_MEDIA).setContentType(AudioAttributes.CONTENT_TYPE_MUSIC).build())
            .setOnAudioFocusChangeListener({ change -> onFocusChange(change) }, main)
            .setWillPauseWhenDucked(false)   // we duck ourselves (the output gain), the system must not do it for us
            .build()
        val rc = a.requestAudioFocus(req)
        if (rc == AudioManager.AUDIOFOCUS_REQUEST_GRANTED) { focusReq = req; hasFocus = true; return true }
        Log.i(TAG, "audio focus refused: $rc")
        return false
    }

    @Synchronized private fun abandonFocus() {
        focusReq?.let { try { am?.abandonAudioFocusRequest(it) } catch (e: Exception) { } }
        focusReq = null; hasFocus = false
        if (ducked) { ducked = false; LiveNative.nativeSetDuck(1f) }
    }

    /** The user pressed Stop (anywhere) or a song ended for good: no remembered resume, focus given up, the A5 gate cancelled. */
    fun afterUserStop() { armed = false; LiveEngine.gateAbort("stopped"); abandonFocus(); refresh() }

    /** Main thread (the listener's handler). The DECISION and the new `armed` / `ducked` happen here, at once, in order; only the engine calls go to the worker (verify-n2 F4). */
    private fun onFocusChange(change: Int) {
        val st = LiveEngine.status()
        val playing = st.state == LiveState.PLAYING || st.state == LiveState.HELD
        val act: FocusPolicy.Act
        synchronized(focusLock) {
            act = FocusPolicy.decide(change, playing, armed, ducked)
            val n = FocusPolicy.next(act, FocusPolicy.State(armed, ducked))
            if (act == FocusPolicy.Act.STOP_AND_REMEMBER) rememberedRow = st.row
            armed = n.armed; ducked = n.ducked
        }
        lastEvent = "focus $change -> $act"
        Log.i(TAG, lastEvent)
        exec.execute {
            when (act) {
                FocusPolicy.Act.NONE -> {}
                FocusPolicy.Act.STOP_FOR_GOOD -> { LiveNative.nativeTransportStop(-1L); LiveEngine.gateAbort("audio focus lost"); abandonFocus(); LiveNative.nativeSetDuck(1f) }
                FocusPolicy.Act.STOP_AND_REMEMBER -> { LiveNative.nativeTransportStop(-1L); LiveEngine.gateAbort("audio focus lost (transient)") }
                FocusPolicy.Act.DUCK -> LiveNative.nativeSetDuck(FocusPolicy.DUCK_GAIN)
                FocusPolicy.Act.UNDUCK -> LiveNative.nativeSetDuck(1f)
                FocusPolicy.Act.RESUME -> { LiveNative.nativeSetDuck(1f); if (LiveEngine.active) LiveNative.nativePlay(rememberedRow) }
            }
            if (act == FocusPolicy.Act.STOP_FOR_GOOD || act == FocusPolicy.Act.STOP_AND_REMEMBER) LiveBridge.send("{\"t\":\"device\",\"reason\":\"focus\",\"stopped\":true,\"resume\":" + (act == FocusPolicy.Act.STOP_AND_REMEMBER) + "}")
            refresh()
        }
    }

    // ---- output devices ---------------------------------------------------------------------------------------------------------------------------------------------------------------------

    private fun onDevices(what: String, devs: Array<out AudioDeviceInfo>?) {
        val outs = devs?.filter { it.isSink } ?: return
        if (outs.isEmpty() || !LiveEngine.active) return
        lastEvent = "output $what: " + outs.joinToString { (it.productName?.toString() ?: "?") + "/" + it.type }
        Log.i(TAG, lastEvent)
        // nothing else here: the ticker sees a stream that stopped calling back; a stream Android re-routed keeps calling back and is left alone
    }

    private val ticker = object : Runnable {
        override fun run() {
            try { tick() } catch (e: Exception) { Log.i(TAG, "tick: $e") }
            if (service != null) main.postDelayed(this, 500)
        }
    }

    private fun tick() {
        if (!LiveEngine.active) { watch.reset(); return }
        val now = System.currentTimeMillis()
        val calls = FlatJson.num(LiveNative.nativeStreamState(), "calls")?.toLong() ?: -1L
        if (watch.stalled(calls, now)) {
            watch.reset()
            Log.i(TAG, "the stream stopped calling back: rebuilding it")
            exec.execute {
                val ok = LiveEngine.reopenStream()
                lastEvent = if (ok) "output stream rebuilt (stopped)" else "output stream could not be rebuilt: engine down"
                if (ok) LiveBridge.send("{\"t\":\"device\",\"reason\":\"stream\",\"stopped\":true,\"sr\":" + LiveEngine.sampleRate + "}")
                else LiveBridge.send("{\"t\":\"engine\",\"up\":false}")   // CORE-BRIDGE 7 G13: the page re-sends everything when the engine comes back up
                refresh()
            }
        }
        if (LiveEngine.gate is GateState.Running) exec.execute { LiveEngine.gateTick() }
        // focus is given up when the song is not playing and nothing is waiting to resume (3 s of grace after a Play request: the poller needs a moment to see PLAYING)
        val s = LiveEngine.status()
        val playing = s.state == LiveState.PLAYING || s.state == LiveState.HELD
        if (hasFocus && !playing && !armed && LiveEngine.gate !is GateState.Running && now - lastPlayReqMs > 3000) abandonFocus()
        val sig = "${s.state}|${LiveEngine.songTitle}|${LiveEngine.gate::class.simpleName}|$armed"
        if (sig != lastSig) refresh()
    }

    // ---- media session + notification --------------------------------------------------------------------------------------------------------------------------------------------

    /** Pushes the song name, the app icon and the transport state to the media session, then re-posts the notification. Any thread. */
    fun refresh() {
        val svc = service ?: return
        val s = LiveEngine.status()
        val playing = s.state == LiveState.PLAYING || s.state == LiveState.HELD
        lastSig = "${s.state}|${LiveEngine.songTitle}|${LiveEngine.gate::class.simpleName}|$armed"
        main.post {
            val ms = mediaSession ?: return@post
            val md = MediaMetadata.Builder().putString(MediaMetadata.METADATA_KEY_TITLE, LiveEngine.songTitle).putString(MediaMetadata.METADATA_KEY_ARTIST, "Thunder Plus")
            art?.let { md.putBitmap(MediaMetadata.METADATA_KEY_ALBUM_ART, it) }
            ms.setMetadata(md.build())
            val st = if (LiveEngine.active && playing) PlaybackState.STATE_PLAYING else PlaybackState.STATE_STOPPED
            ms.setPlaybackState(PlaybackState.Builder().setActions(PlaybackState.ACTION_PLAY or PlaybackState.ACTION_PAUSE or PlaybackState.ACTION_STOP or PlaybackState.ACTION_PLAY_PAUSE)
                .setState(st, PlaybackState.PLAYBACK_POSITION_UNKNOWN, if (playing) 1f else 0f).build())
            try { svc.postLive(svc.applicationContext) } catch (e: Exception) { Log.i(TAG, "post: $e") }
        }
    }

    fun largeIcon(): Bitmap? = art
}
