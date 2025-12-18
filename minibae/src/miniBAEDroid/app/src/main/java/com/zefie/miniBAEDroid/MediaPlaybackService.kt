package com.zefie.miniBAEDroid

import android.app.Service
import android.content.Intent
import android.os.Binder
import android.os.IBinder
import android.content.pm.ServiceInfo
import android.os.Build
import androidx.core.app.ServiceCompat

class MediaPlaybackService : Service() {

    private val binder = LocalBinder()
    private lateinit var notificationHelper: MusicNotificationHelper
    
    // Media state
    var currentSong: org.minibae.Song? = null
    var currentSound: org.minibae.Sound? = null

    inner class LocalBinder : Binder() {
        fun getService(): MediaPlaybackService = this@MediaPlaybackService
    }

    override fun onCreate() {
        super.onCreate()
        notificationHelper = MusicNotificationHelper(this)
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        // If the service is started, make sure it's in foreground
        // We might need to pass initial notification data here or show a placeholder
        // For now, we rely on the client (Activity) to call updateNotification
        return START_NOT_STICKY
    }

    override fun onBind(intent: Intent): IBinder {
        return binder
    }

    override fun onDestroy() {
        super.onDestroy()
        // Clean up resources if needed
        currentSong?.stop(true)
        currentSound?.stop(true)
    }
    
    fun updateNotification(
        title: String,
        artist: String,
        isPlaying: Boolean,
        hasNext: Boolean,
        hasPrevious: Boolean,
        currentPosition: Long = 0,
        duration: Long = 0
    ) {
        val notification = notificationHelper.buildNotification(
            title, artist, isPlaying, hasNext, hasPrevious, currentPosition, duration
        )
        
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            ServiceCompat.startForeground(
                this,
                MusicNotificationHelper.NOTIFICATION_ID,
                notification,
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                    ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PLAYBACK
                } else {
                    0
                }
            )
        } else {
            startForeground(MusicNotificationHelper.NOTIFICATION_ID, notification)
        }
    }
    
    fun stopForegroundService() {
        stopForeground(true)
        stopSelf()
    }
}
