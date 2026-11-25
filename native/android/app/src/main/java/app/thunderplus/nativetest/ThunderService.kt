package app.thunderplus.nativetest

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.os.IBinder

/** Foreground service of type mediaPlayback: keeps playback alive with the screen off or the app in the background. */
class ThunderService : Service() {
    override fun onBind(intent: Intent?): IBinder? = null

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (LiveEngine.active) return onLiveCommand(intent?.action)
        if (intent?.action == ACTION_STOP) {
            ThunderHost.stop(this)
            stopForeground(STOP_FOREGROUND_REMOVE)
            stopSelf()
            return START_NOT_STICKY
        }
        val nm = getSystemService(NotificationManager::class.java)
        nm.createNotificationChannel(NotificationChannel(CHANNEL, "Playback", NotificationManager.IMPORTANCE_LOW))
        val open = PendingIntent.getActivity(this, 0, Intent(this, MainActivity::class.java), PendingIntent.FLAG_IMMUTABLE)
        val stop = PendingIntent.getService(this, 1, Intent(this, ThunderService::class.java).setAction(ACTION_STOP), PendingIntent.FLAG_IMMUTABLE)
        val n = Notification.Builder(this, CHANNEL)
            .setSmallIcon(android.R.drawable.ic_media_play)
            .setContentTitle("Thunder Plus (native test)")
            .setContentText("Playing")
            .setContentIntent(open)
            .addAction(Notification.Action.Builder(android.R.drawable.ic_media_pause, "Stop", stop).build())
            .setOngoing(true)
            .build()
        startForeground(NOTIF_ID, n, ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PLAYBACK)
        return START_NOT_STICKY
    }

    /** N2 H1a: the live engine's notification. Play starts the loaded song, Stop stops the transport (the engine and this service stay until "Quit native" in the app). Engine calls block: off the main thread. */
    private fun onLiveCommand(action: String?): Int {
        val ctx = applicationContext
        postLive(ctx)   // startForegroundService needs startForeground soon: post first, update after the work
        LiveSession.attach(this)   // H1c: audio focus, headphones, device changes, media session, the 500 ms ticker (idempotent)
        LiveSession.refresh()
        when (action) {
            ACTION_PLAY -> LiveEngine.async { if (LiveButtons.canPlay(LiveEngine.status(), LiveEngine.loaded != null)) LiveEngine.play(); LiveSession.refresh() }
            ACTION_STOP -> LiveEngine.async { LiveEngine.stop(); LiveSession.refresh() }
            else -> {}
        }
        return START_NOT_STICKY
    }

    /** The live engine's notification (H1c): the song's name as the title, the app icon, Play and Stop, in the media style tied to the media session (lock screen and Android 13+ media controls). Main thread or any: it only posts. */
    fun postLive(ctx: Context) {
        val text = LiveButtons.notificationText(LiveEngine.status(), LiveEngine.loaded != null)
        val nm = getSystemService(NotificationManager::class.java)
        nm.createNotificationChannel(NotificationChannel(CHANNEL, "Playback", NotificationManager.IMPORTANCE_LOW))
        val open = PendingIntent.getActivity(this, 0, Intent(this, MainActivity::class.java), PendingIntent.FLAG_IMMUTABLE)
        val play = PendingIntent.getService(this, 2, Intent(this, ThunderService::class.java).setAction(ACTION_PLAY), PendingIntent.FLAG_IMMUTABLE)
        val stop = PendingIntent.getService(this, 1, Intent(this, ThunderService::class.java).setAction(ACTION_STOP), PendingIntent.FLAG_IMMUTABLE)
        val b = Notification.Builder(this, CHANNEL)
            .setSmallIcon(R.drawable.ic_stat_thunder)
            .setContentTitle(LiveEngine.songTitle)
            .setContentText(text)
            .setContentIntent(open)
            .addAction(Notification.Action.Builder(android.R.drawable.ic_media_play, "Play", play).build())
            .addAction(Notification.Action.Builder(android.R.drawable.ic_media_pause, "Stop", stop).build())
            .setOngoing(true)
            .setVisibility(Notification.VISIBILITY_PUBLIC)
        LiveSession.largeIcon()?.let { b.setLargeIcon(it) }
        LiveSession.session?.let { b.setStyle(Notification.MediaStyle().setMediaSession(it.sessionToken).setShowActionsInCompactView(0, 1)) }
        startForeground(NOTIF_ID, b.build(), ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PLAYBACK)
    }

    /**
     * H5-1 (owner decision 3, 25 Nov): the owner swiped the app away from recents. The page is gone and Android shows no dialog, so nothing is asked of it: the shell writes the last song the page sent as a recovery copy
     * when it was not saved (Recovery.kt), then STOPS playback and shuts the engine down, then ends this service. All of it off the main thread (the engine's shutdown can sit behind a project load: the ANR fix).
     */
    override fun onTaskRemoved(rootIntent: Intent?) {
        val ctx = applicationContext
        Thread({
            Recovery.saveForTaskRemoved(ctx)   // BEFORE the engine goes: nothing here depends on it
            try { if (LiveEngine.active) { LiveEngine.stop(); LiveEngine.shutdown() } else ThunderHost.stop(ctx) } catch (t: Throwable) { android.util.Log.i("ThunderService", "task removed: $t") }
            android.os.Handler(android.os.Looper.getMainLooper()).post { stopForeground(STOP_FOREGROUND_REMOVE); stopSelf() }
        }, "thunder-task-removed").start()
        super.onTaskRemoved(rootIntent)
    }

    override fun onDestroy() {
        LiveSession.detach()
        LiveEngine.shutdown()
        ThunderHost.shutdown()
        super.onDestroy()
    }

    companion object {
        const val CHANNEL = "playback"
        const val NOTIF_ID = 1
        const val ACTION_STOP = "app.thunderplus.nativetest.STOP"
        const val ACTION_PLAY = "app.thunderplus.nativetest.PLAY"
    }
}
