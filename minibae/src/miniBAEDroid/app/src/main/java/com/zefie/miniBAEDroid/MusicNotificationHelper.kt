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
    
    private var mediaSession: MediaSessionCompat? = null
    
    init {
        createNotificationChannel()
        setupMediaSession()
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
        }
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
    
    fun showNotification(
        title: String,
        artist: String,
        isPlaying: Boolean,
        hasNext: Boolean,
        hasPrevious: Boolean,
        currentPosition: Long = 0,
        duration: Long = 0
    ) {
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
        
        // Add progress information if available
        if (duration > 0) {
            builder.setProgress(duration.toInt(), currentPosition.toInt(), false)
        }
        
        try {
            NotificationManagerCompat.from(context).notify(NOTIFICATION_ID, builder.build())
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
