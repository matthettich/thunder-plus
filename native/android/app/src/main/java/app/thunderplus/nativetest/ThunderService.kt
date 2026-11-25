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
        when (action) {
            ACTION_PLAY -> LiveEngine.async { if (LiveButtons.canPlay(LiveEngine.status(), LiveEngine.loaded != null)) LiveEngine.play(); postLive(ctx) }
            ACTION_STOP -> LiveEngine.async { LiveEngine.stop(); postLive(ctx) }
            else -> {}
        }
        return START_NOT_STICKY
    }

    private fun postLive(ctx: Context) {
        val text = LiveButtons.notificationText(LiveEngine.status(), LiveEngine.loaded != null)
        val nm = getSystemService(NotificationManager::class.java)
        nm.createNotificationChannel(NotificationChannel(CHANNEL, "Playback", NotificationManager.IMPORTANCE_LOW))
        val open = PendingIntent.getActivity(this, 0, Intent(this, MainActivity::class.java), PendingIntent.FLAG_IMMUTABLE)
        val play = PendingIntent.getService(this, 2, Intent(this, ThunderService::class.java).setAction(ACTION_PLAY), PendingIntent.FLAG_IMMUTABLE)
        val stop = PendingIntent.getService(this, 1, Intent(this, ThunderService::class.java).setAction(ACTION_STOP), PendingIntent.FLAG_IMMUTABLE)
        val n = Notification.Builder(this, CHANNEL)
            .setSmallIcon(android.R.drawable.ic_media_play)
            .setContentTitle("Thunder Plus (native live engine)")
            .setContentText(if (LiveEngine.projectName.isEmpty()) text else LiveEngine.projectName + ": " + text)
            .setContentIntent(open)
            .addAction(Notification.Action.Builder(android.R.drawable.ic_media_play, "Play", play).build())
            .addAction(Notification.Action.Builder(android.R.drawable.ic_media_pause, "Stop", stop).build())
            .setOngoing(true)
            .build()
        startForeground(NOTIF_ID, n, ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PLAYBACK)
    }

    override fun onDestroy() {
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
