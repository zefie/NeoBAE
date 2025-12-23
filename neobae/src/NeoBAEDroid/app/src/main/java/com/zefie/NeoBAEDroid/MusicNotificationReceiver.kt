package com.zefie.NeoBAEDroid

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent

class MusicNotificationReceiver : BroadcastReceiver() {
    
    companion object {
        private var playPauseCallback: (() -> Unit)? = null
        private var nextCallback: (() -> Unit)? = null
        private var previousCallback: (() -> Unit)? = null
        private var closeCallback: (() -> Unit)? = null
        
        fun setCallbacks(
            onPlayPause: () -> Unit,
            onNext: () -> Unit,
            onPrevious: () -> Unit,
            onClose: () -> Unit
        ) {
            playPauseCallback = onPlayPause
            nextCallback = onNext
            previousCallback = onPrevious
            closeCallback = onClose
        }
    }
    
    override fun onReceive(context: Context?, intent: Intent?) {
        when (intent?.action) {
            MusicNotificationHelper.ACTION_PLAY_PAUSE -> playPauseCallback?.invoke()
            MusicNotificationHelper.ACTION_NEXT -> nextCallback?.invoke()
            MusicNotificationHelper.ACTION_PREVIOUS -> previousCallback?.invoke()
            MusicNotificationHelper.ACTION_CLOSE -> closeCallback?.invoke()
        }
    }
}
