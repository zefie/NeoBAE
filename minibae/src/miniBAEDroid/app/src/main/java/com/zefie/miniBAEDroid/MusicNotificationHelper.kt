package com.zefie.miniBAEDroid

import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.RectF
import android.os.Build
import android.os.SystemClock
import android.support.v4.media.session.MediaSessionCompat
import androidx.core.app.NotificationCompat
import androidx.core.app.NotificationManagerCompat
import androidx.media.app.NotificationCompat as MediaNotificationCompat

class MusicNotificationHelper(private val context: Context) {
    
    companion object {
        const val CHANNEL_ID = "minibae_playback"
        const val NOTIFICATION_ID = 1
        
        const val ACTION_PLAY_PAUSE = "com.zefie.miniBAEDroid.PLAY_PAUSE"
        const val ACTION_NEXT = "com.zefie.miniBAEDroid.NEXT"
        const val ACTION_PREVIOUS = "com.zefie.miniBAEDroid.PREVIOUS"
        const val ACTION_CLOSE = "com.zefie.miniBAEDroid.CLOSE"
    }
    
    interface PlaybackCallback {
        fun onSeek(position: Long)
        fun onPlay()
        fun onPause()
        fun onNext()
        fun onPrevious()
        fun onStop()
    }
    
    private var mediaSession: MediaSessionCompat? = null
    private var playbackCallback: PlaybackCallback? = null
    private var currentIsPlaying: Boolean = false
    private var currentPosition: Long = 0
    
    init {
        createNotificationChannel()
        setupMediaSession()
    }
    
    fun setPlaybackCallback(callback: PlaybackCallback) {
        this.playbackCallback = callback
    }
    
    private fun createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val name = "Music Playback"
            val descriptionText = "Shows currently playing music"
            val importance = NotificationManager.IMPORTANCE_LOW
            val channel = NotificationChannel(CHANNEL_ID, name, importance).apply {
                description = descriptionText
                setShowBadge(false)
                setSound(null, null) // No notification sound
                enableLights(false)
                enableVibration(false)
            }
            
            val notificationManager = context.getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
            notificationManager.createNotificationChannel(channel)
        }
    }
    
    private fun setupMediaSession() {
        mediaSession = MediaSessionCompat(context, "miniBAE_session").apply {
            isActive = true
            // Set supported actions for media controls
            setFlags(MediaSessionCompat.FLAG_HANDLES_MEDIA_BUTTONS or 
                    MediaSessionCompat.FLAG_HANDLES_TRANSPORT_CONTROLS)
            
            // Set callback to handle media session events (like seeking)
            setCallback(object : MediaSessionCompat.Callback() {
                override fun onSeekTo(pos: Long) {
                    super.onSeekTo(pos)
                    android.util.Log.d("MusicNotificationHelper", "onSeekTo: $pos")
                    playbackCallback?.onSeek(pos)
                }
                
                override fun onPlay() {
                    super.onPlay()
                    android.util.Log.d("MusicNotificationHelper", "onPlay")
                    playbackCallback?.onPlay()
                }
                
                override fun onPause() {
                    super.onPause()
                    android.util.Log.d("MusicNotificationHelper", "onPause")
                    playbackCallback?.onPause()
                }
                
                override fun onSkipToNext() {
                    super.onSkipToNext()
                    android.util.Log.d("MusicNotificationHelper", "onSkipToNext")
                    playbackCallback?.onNext()
                }
                
                override fun onSkipToPrevious() {
                    super.onSkipToPrevious()
                    android.util.Log.d("MusicNotificationHelper", "onSkipToPrevious")
                    playbackCallback?.onPrevious()
                }
                
                override fun onStop() {
                    super.onStop()
                    android.util.Log.d("MusicNotificationHelper", "onStop")
                    playbackCallback?.onStop()
                }
            })
        }
    }
    
    private fun updatePlaybackState(isPlaying: Boolean, currentPosition: Long) {
        // Store current state for seek operations
        this.currentIsPlaying = isPlaying
        this.currentPosition = currentPosition
        
        val state = if (isPlaying) {
            android.support.v4.media.session.PlaybackStateCompat.STATE_PLAYING
        } else {
            android.support.v4.media.session.PlaybackStateCompat.STATE_PAUSED
        }
        
        val playbackState = android.support.v4.media.session.PlaybackStateCompat.Builder()
            .setState(
                state, 
                currentPosition, 
                if (isPlaying) 1.0f else 0.0f, // Use 0.0f speed when paused
                SystemClock.elapsedRealtime() // Provide explicit update time for smooth progress
            )
            .setActions(
                android.support.v4.media.session.PlaybackStateCompat.ACTION_PLAY or
                android.support.v4.media.session.PlaybackStateCompat.ACTION_PAUSE or
                android.support.v4.media.session.PlaybackStateCompat.ACTION_SKIP_TO_NEXT or
                android.support.v4.media.session.PlaybackStateCompat.ACTION_SKIP_TO_PREVIOUS or
                android.support.v4.media.session.PlaybackStateCompat.ACTION_SEEK_TO
            )
            .build()
        
        mediaSession?.setPlaybackState(playbackState)
    }
    
    private fun updateMetadata(title: String, artist: String, duration: Long) {
        val metadata = android.support.v4.media.MediaMetadataCompat.Builder()
            .putString(android.support.v4.media.MediaMetadataCompat.METADATA_KEY_TITLE, title)
            .putString(android.support.v4.media.MediaMetadataCompat.METADATA_KEY_ARTIST, artist)
            .putLong(android.support.v4.media.MediaMetadataCompat.METADATA_KEY_DURATION, duration)
            .build()
        
        mediaSession?.setMetadata(metadata)
    }
    
    private fun createAlbumArt(): Bitmap {
        val size = 256 // Smaller size for more compact notification
        val bitmap = Bitmap.createBitmap(size, size, Bitmap.Config.ARGB_8888)
        val canvas = Canvas(bitmap)
        
        // Create dark purple to black gradient
        val gradient = android.graphics.LinearGradient(
            0f, 0f, 0f, size.toFloat(),
            intArrayOf(
                0xFF4A148C.toInt(), // Dark purple
                0xFF000000.toInt()  // Black
            ),
            null,
            android.graphics.Shader.TileMode.CLAMP
        )
        
        val paint = Paint().apply {
            isAntiAlias = true
            shader = gradient
        }
        
        val rect = RectF(0f, 0f, size.toFloat(), size.toFloat())
        canvas.drawRoundRect(rect, 16f, 16f, paint)
        
        return bitmap
    }
    
    fun buildNotification(
        title: String,
        artist: String,
        isPlaying: Boolean,
        hasNext: Boolean,
        hasPrevious: Boolean,
        currentPosition: Long = 0,
        duration: Long = 0
    ): android.app.Notification {
        // Update MediaSession with playback state and metadata for progress bar
        updatePlaybackState(isPlaying, currentPosition)
        updateMetadata(title, artist, duration)
        
        val notificationIntent = Intent(context, MainActivity::class.java).apply {
            flags = Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_SINGLE_TOP
        }
        val pendingIntent = PendingIntent.getActivity(
            context, 0, notificationIntent,
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT
        )
        
        // Create pending intents for media controls
        val playPausePendingIntent = PendingIntent.getBroadcast(
            context, 0, Intent(ACTION_PLAY_PAUSE).apply { setPackage(context.packageName) },
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT
        )
        
        val previousPendingIntent = PendingIntent.getBroadcast(
            context, 1, Intent(ACTION_PREVIOUS).apply { setPackage(context.packageName) },
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT
        )
        
        val nextPendingIntent = PendingIntent.getBroadcast(
            context, 2, Intent(ACTION_NEXT).apply { setPackage(context.packageName) },
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT
        )
        
        val closePendingIntent = PendingIntent.getBroadcast(
            context, 3, Intent(ACTION_CLOSE).apply { setPackage(context.packageName) },
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT
        )
        
        // Create album art
        val albumArt = createAlbumArt()
        
        // Build the notification with MediaStyle
        val builder = NotificationCompat.Builder(context, CHANNEL_ID)
            .setContentTitle(title)
            .setContentText(artist)
            .setSubText("miniBAE") // App name as subtext, similar to YouTube Music
            .setLargeIcon(albumArt)
            .setSmallIcon(android.R.drawable.ic_media_play) // Music play icon for status bar
            .setContentIntent(pendingIntent)
            .setOngoing(true)
            .setShowWhen(false)
            .setVisibility(NotificationCompat.VISIBILITY_PUBLIC)
            .setPriority(NotificationCompat.PRIORITY_HIGH) // Higher priority for better visibility
            .setCategory(NotificationCompat.CATEGORY_TRANSPORT)
            .setColorized(true)
            .setColor(0xFF6200EA.toInt()) // Purple accent color
        
        // Always add previous button (will be disabled if not available)
        builder.addAction(
            NotificationCompat.Action.Builder(
                android.R.drawable.ic_media_previous,
                "Previous",
                if (hasPrevious) previousPendingIntent else null
            ).build()
        )
        
        // Add play/pause button (always available)
        builder.addAction(
            NotificationCompat.Action.Builder(
                if (isPlaying) android.R.drawable.ic_media_pause else android.R.drawable.ic_media_play,
                if (isPlaying) "Pause" else "Play",
                playPausePendingIntent
            ).build()
        )
        
        // Always add next button (will be disabled if not available)
        builder.addAction(
            NotificationCompat.Action.Builder(
                android.R.drawable.ic_media_next,
                "Next",
                if (hasNext) nextPendingIntent else null
            ).build()
        )
        
        // Add close button
        builder.addAction(
            NotificationCompat.Action.Builder(
                android.R.drawable.ic_menu_close_clear_cancel,
                "Close",
                closePendingIntent
            ).build()
        )
        
        // Apply MediaStyle with proper action indices
        mediaSession?.let { session ->
            val mediaStyle = MediaNotificationCompat.MediaStyle()
                .setMediaSession(session.sessionToken)
                .setShowActionsInCompactView(0, 1, 2) // Show prev, play/pause, next in compact view
                .setShowCancelButton(false)
            
            builder.setStyle(mediaStyle)
        }
        
        // Progress bar is automatically handled by MediaSession PlaybackState and Metadata
        // No need to call builder.setProgress() - it's managed by the system via MediaSession
        
        return builder.build()
    }

    fun showNotification(
        title: String,
        artist: String,
        isPlaying: Boolean,
        hasNext: Boolean,
        hasPrevious: Boolean,
        currentPosition: Long = 0,
        duration: Long = 0
    ) {
        val notification = buildNotification(title, artist, isPlaying, hasNext, hasPrevious, currentPosition, duration)
        try {
            NotificationManagerCompat.from(context).notify(NOTIFICATION_ID, notification)
        } catch (e: SecurityException) {
            // Permission not granted, silently fail
        }
    }
    
    fun hideNotification() {
        NotificationManagerCompat.from(context).cancel(NOTIFICATION_ID)
    }
    
    fun cleanup() {
        mediaSession?.release()
        mediaSession = null
    }
}
