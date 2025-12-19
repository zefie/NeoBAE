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
        
        // Set up callback to handle seek operations from notification
        notificationHelper.setPlaybackCallback(object : MusicNotificationHelper.PlaybackCallback {
            override fun onSeek(position: Long) {
                // Notify bound activity/fragment to seek to position (in milliseconds)
                seekCallback?.invoke(position.toInt())
            }
            
            override fun onPlay() {
                android.util.Log.d("MediaPlaybackService", "onPlay")
                playPauseCallback?.invoke()
            }
            
            override fun onPause() {
                android.util.Log.d("MediaPlaybackService", "onPause")
                playPauseCallback?.invoke()
            }
            
            override fun onNext() {
                android.util.Log.d("MediaPlaybackService", "onNext")
                nextCallback?.invoke()
            }
            
            override fun onPrevious() {
                android.util.Log.d("MediaPlaybackService", "onPrevious")
                previousCallback?.invoke()
            }
            
            override fun onStop() {
                android.util.Log.d("MediaPlaybackService", "onStop")
                closeCallback?.invoke()
            }
        })
    }
    
    // Callbacks for media operations
    var seekCallback: ((Int) -> Unit)? = null
    var playPauseCallback: (() -> Unit)? = null
    var nextCallback: (() -> Unit)? = null
    var previousCallback: (() -> Unit)? = null
    var closeCallback: (() -> Unit)? = null

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
        duration: Long = 0,
        fileExtension: String = ""
    ) {
        val notification = notificationHelper.buildNotification(
            title, artist, isPlaying, hasNext, hasPrevious, currentPosition, duration, fileExtension
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
        ServiceCompat.stopForeground(this, ServiceCompat.STOP_FOREGROUND_REMOVE)
        stopSelf()
    }
}
