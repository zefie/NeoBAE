package com.zefie.miniBAEDroid

import android.os.Bundle
import android.content.Context
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.view.View
import android.view.WindowInsets
import android.view.WindowInsetsController
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat
import android.widget.Button
import android.widget.Toast
import org.minibae.Mixer

class MainActivity : AppCompatActivity() {
    private lateinit var openFolderLauncher: androidx.activity.result.ActivityResultLauncher<Uri?>
    private lateinit var openFileLauncher: androidx.activity.result.ActivityResultLauncher<Array<String>>
    private lateinit var permissionLauncher: androidx.activity.result.ActivityResultLauncher<Array<String>>
    var pendingBankReload = false
    var pendingBankReloadResume = false
    var pendingBankReloadPositionMs = 0
    var currentSong: org.minibae.Song? = null
    var currentSound: org.minibae.Sound? = null

    // Bank swaps can schedule delayed resume/seek callbacks (SF2 workaround). If the user swaps banks
    // again before the callback fires, we must cancel/ignore the stale callback to avoid calling into
    // a Song that has been stopped/closed or whose mixer was recreated.
    private val bankSwapHandler = android.os.Handler(android.os.Looper.getMainLooper())
    @Volatile private var bankSwapGeneration: Long = 0
    private var pendingBankSwapRunnable: Runnable? = null
    
    // Service binding
    var playbackService: MediaPlaybackService? = null
    private var isBound = false
    
    private val connection = object : android.content.ServiceConnection {
        override fun onServiceConnected(className: android.content.ComponentName, service: android.os.IBinder) {
            val binder = service as MediaPlaybackService.LocalBinder
            playbackService = binder.getService()
            isBound = true
            
            // Set up media control callbacks
            playbackService?.seekCallback = { position ->
                // Forward seek request to HomeFragment
                val fragment = supportFragmentManager.findFragmentById(R.id.fragment_container)
                if (fragment is HomeFragment) {
                    fragment.handleSeekFromNotification(position)
                } else {
                    android.util.Log.e("MainActivity", "HomeFragment not found for seek!")
                }
            }
            
            playbackService?.playPauseCallback = {
                android.util.Log.d("MainActivity", "Play/Pause callback")
                val fragment = supportFragmentManager.findFragmentById(R.id.fragment_container)
                if (fragment is HomeFragment) {
                    fragment.handlePlayPauseFromNotification()
                } else {
                    android.util.Log.e("MainActivity", "HomeFragment not found for play/pause!")
                }
            }
            
            playbackService?.nextCallback = {
                android.util.Log.d("MainActivity", "Next callback")
                val fragment = supportFragmentManager.findFragmentById(R.id.fragment_container)
                if (fragment is HomeFragment) {
                    fragment.handleNextFromNotification()
                } else {
                    android.util.Log.e("MainActivity", "HomeFragment not found for next!")
                }
            }
            
            playbackService?.previousCallback = {
                android.util.Log.d("MainActivity", "Previous callback")
                val fragment = supportFragmentManager.findFragmentById(R.id.fragment_container)
                if (fragment is HomeFragment) {
                    fragment.handlePreviousFromNotification()
                } else {
                    android.util.Log.e("MainActivity", "HomeFragment not found for previous!")
                }
            }
            
            playbackService?.closeCallback = {
                android.util.Log.d("MainActivity", "Close callback")
                val fragment = supportFragmentManager.findFragmentById(R.id.fragment_container)
                if (fragment is HomeFragment) {
                    fragment.handleCloseFromNotification()
                } else {
                    android.util.Log.e("MainActivity", "HomeFragment not found for close!")
                }
            }
        }

        override fun onServiceDisconnected(arg0: android.content.ComponentName) {
            isBound = false
            playbackService = null
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        
        // Hide system navigation bar
        hideSystemUI()
        
        // Register permission launcher
        permissionLauncher = registerForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) { permissions ->
            // Check if essential storage/audio permissions are granted
            // We don't strictly require POST_NOTIFICATIONS for the app to function

            if (permissions[android.Manifest.permission.READ_EXTERNAL_STORAGE] != true) {
                Toast.makeText(this, "Storage permissions are required to access music files", Toast.LENGTH_LONG).show()
            } else {
                // Permissions granted - refresh the folder in HomeFragment
                val fragment = supportFragmentManager.findFragmentById(R.id.fragment_container)
                if (fragment is HomeFragment) {
                    fragment.refreshFolderAfterPermission()
                }
            }
        }
        
        // Request storage permissions
        requestStoragePermissions()
        
        // Bind to MediaPlaybackService
        android.content.Intent(this, MediaPlaybackService::class.java).also { intent ->
            bindService(intent, connection, Context.BIND_AUTO_CREATE)
        }

        // Load native library (built by the module 'miniBAE')
        // Load sqlite3 first since miniBAE depends on it
        System.loadLibrary("sqlite3")
        System.loadLibrary("miniBAE")

        // Set cache directory for native code (before any mixer creation)
        Mixer.setNativeCacheDir(cacheDir.absolutePath)
        
        // Don't initialize Mixer on launch - it will be created lazily when needed
        // This prevents the audio callback from running and generating empty samples when idle

        // Setup OnBackPressedDispatcher instead of deprecated onBackPressed()
        onBackPressedDispatcher.addCallback(this, object : androidx.activity.OnBackPressedCallback(true) {
            override fun handleOnBackPressed() {
                // Check if fragment manager has back stack entries
                if (supportFragmentManager.backStackEntryCount > 0) {
                    supportFragmentManager.popBackStack()
                } else {
                    // Only finish if we're at the root
                    finish()
                }
            }
        })

        // Setup fragments
        val homeTab = findViewById<Button>(R.id.tab_home)

        // Register SAF launchers
        openFolderLauncher = registerForActivityResult(ActivityResultContracts.OpenDocumentTree()) { uri: Uri? ->
            uri?.let {
                val fragment = supportFragmentManager.findFragmentById(R.id.fragment_container)
                if (fragment is HomeFragment) {
                    fragment.onFolderPicked(it)
                }
            }
        }
        
        openFileLauncher = registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri: Uri? ->
            uri?.let {
                val fragment = supportFragmentManager.findFragmentById(R.id.fragment_container)
                if (fragment is HomeFragment) {
                    fragment.onFilePicked(it)
                }
            }
        }

        supportFragmentManager.beginTransaction().replace(R.id.fragment_container, HomeFragment()).commit()

        homeTab.setOnClickListener {
            supportFragmentManager.beginTransaction().replace(R.id.fragment_container, HomeFragment()).commit()
        }
        
        // Handle file opened from external app
        handleIntent(intent)
    }
    
    override fun onNewIntent(intent: android.content.Intent) {
        super.onNewIntent(intent)
        setIntent(intent)
        handleIntent(intent)
    }
    
    private fun handleIntent(intent: android.content.Intent?) {
        if (intent?.action == android.content.Intent.ACTION_VIEW) {
            intent.data?.let { uri ->
                android.util.Log.d("MainActivity", "File opened from external app: $uri")
                // Pass the URI to HomeFragment for playback
                val fragment = supportFragmentManager.findFragmentById(R.id.fragment_container)
                if (fragment is HomeFragment) {
                    fragment.handleExternalFile(uri)
                } else {
                    // Store URI for later if fragment isn't ready
                    pendingFileUri = uri
                }
            }
        }
    }
    
    var pendingFileUri: Uri? = null
    
    fun consumePendingFileUri(): Uri? {
        val uri = pendingFileUri
        pendingFileUri = null
        return uri
    }
    
    fun requestFolderPicker() {
        openFolderLauncher.launch(null)
    }
    
    fun requestFilePicker() {
        openFileLauncher.launch(arrayOf("audio/midi", "audio/x-midi", "audio/*", "*/*"))
    }
    
    fun requestBankReload() {
        android.util.Log.d("MainActivity", "requestBankReload called, setting flag to true")
        // Capture current playback intent so we can resume after bank swap.
        try {
            val viewModel = androidx.lifecycle.ViewModelProvider(this)[MusicPlayerViewModel::class.java]
            pendingBankReloadResume = viewModel.isPlaying
        } catch (_: Exception) {
        }
        pendingBankReload = true
    }
    
    fun reloadCurrentSongForBankSwap() {
        // Mixer open/close and Song (re)load must be serialized on the main thread.
        // Rapid bank swaps can otherwise race native audio device acquisition.
        if (android.os.Looper.myLooper() != android.os.Looper.getMainLooper()) {
            runOnUiThread { reloadCurrentSongForBankSwap() }
            return
        }

        android.util.Log.d("MainActivity", "reloadCurrentSongForBankSwap called")

        // Invalidate any pending delayed callbacks from a previous swap.
        bankSwapGeneration += 1
        pendingBankSwapRunnable?.let { bankSwapHandler.removeCallbacks(it) }
        pendingBankSwapRunnable = null
        
        // Get the ViewModel from the ViewModelStore
        val viewModel = androidx.lifecycle.ViewModelProvider(this)[MusicPlayerViewModel::class.java]
        val currentItem = viewModel.getCurrentItem()
        if (currentItem == null) {
            android.util.Log.d("MainActivity", "No current item to reload")
            return
        }

        val wasPlaying = pendingBankReloadResume || viewModel.isPlaying
        pendingBankReloadResume = false
        val savedPosition = (if (pendingBankReloadPositionMs > 0) pendingBankReloadPositionMs else viewModel.currentPositionMs)
        pendingBankReloadPositionMs = 0
        android.util.Log.d(
            "MainActivity",
            "Reloading current item after bank swap: ${currentItem.title}, wasPlaying=$wasPlaying, position=$savedPosition"
        )

        try {
            // Tear down any existing wrappers first (mixer may have been recreated).
            try {
                currentSong?.close()
            } catch (_: Exception) {
            }
            try {
                currentSound?.stop(true)
            } catch (_: Exception) {
            }
            currentSong = null
            currentSound = null

            // If bank swap recreated the mixer, we must ensure it's present before reload.
            if (Mixer.getMixer() == null) {
                val status = Mixer.create(assets, 44100, 2, 64, 8, 64)
                if (status != 0) {
                    android.util.Log.e("MainActivity", "Failed to recreate mixer for reload: $status")
                    runOnUiThread {
                        Toast.makeText(this, "Failed to recreate mixer (err=$status)", Toast.LENGTH_SHORT).show()
                    }
                    return
                }
                Mixer.setNativeCacheDir(cacheDir.absolutePath)
            }

            // During bank swaps, HomeFragment pauses playback and suspends the audio output thread
            // via Mixer.disengageAudio(). If we intend to resume after reload, we must re-enable it
            // here or the Song/Sound may "play" silently.
            if (wasPlaying) {
                try {
                    Mixer.reengageAudio()
                } catch (_: Exception) {
                }
            }

            val bytes = currentItem.file.readBytes()
            val loadResult = org.minibae.LoadResult()
            val status = Mixer.loadFromMemory(bytes, loadResult)

            if (status != 0) {
                android.util.Log.e("MainActivity", "Failed to reload item: $status")
                runOnUiThread {
                    Toast.makeText(this, "Failed to reload (err=$status)", Toast.LENGTH_SHORT).show()
                }
                return
            }

            // Restore user settings (prefs are authoritative here)
            val prefs = getSharedPreferences("miniBAE_prefs", Context.MODE_PRIVATE)
            val reverbType = prefs.getInt("default_reverb", 1)
            val velocityCurve = prefs.getInt("velocity_curve", 1)
            val volumePercent = prefs.getInt("volume_percent", viewModel.volumePercent).coerceIn(0, 100)
            val lastBankPath = prefs.getString("last_bank_path", "__builtin__")
            val isHsbBank = lastBankPath == "__builtin__" || (lastBankPath?.endsWith(".hsb", ignoreCase = true) == true)

            // Android-only: boost output 2x in HSB mode (HSB banks are quieter).
            // Keep SF2 (fluidsynth) behavior unchanged.
            val shouldBoostHsb = isHsbBank && loadResult.isSong && (loadResult.song?.isSF2Song() == false)
            Mixer.setAndroidHsbBoostEnabled(shouldBoostHsb)

            Mixer.setMasterVolumePercent(volumePercent)
            Mixer.setDefaultReverb(reverbType)

            viewModel.isPlaying = false

            if (loadResult.isSong) {
                val song = loadResult.song
                if (song == null) {
                    runOnUiThread {
                        Toast.makeText(this, "Failed to reload song", Toast.LENGTH_SHORT).show()
                    }
                    return
                }

                currentSong = song
                currentSound = null

                // Match HomeFragment behavior: SF2 songs force curve 0
                if (song.isSF2Song()) {
                    song.setVelocityCurve(0)
                } else {
                    song.setVelocityCurve(velocityCurve)
                }
                song.setVolumePercent(volumePercent)

                // Start from 0 to re-init controllers cleanly
                song.seekToMs(0)
                song.preroll()
                song.seekToMs(0)

                val startResult = song.start()
                if (startResult != 0) {
                    android.util.Log.e("MainActivity", "Failed to start reloaded song: $startResult")
                    runOnUiThread {
                        Toast.makeText(this, "Failed to start song (err=$startResult)", Toast.LENGTH_SHORT).show()
                    }
                    return
                }

                // Must set loops AFTER start (start clears songMaxLoopCount).
                val loopCount = if (viewModel.repeatMode == RepeatMode.SONG) 32767 else 0
                song.setLoops(loopCount)

                if (song.isSF2Song()) {
                    // Workaround for Fluidsynth drop: pause + delayed resume.
                    song.pause()
                    song.seekToMs(0)
                    if (wasPlaying) {
                        val localGen = bankSwapGeneration
                        val runnable = Runnable {
                            // Ignore if another bank swap happened.
                            if (localGen != bankSwapGeneration) return@Runnable
                            // Ignore if the current Song changed or was torn down.
                            if (currentSong !== song) return@Runnable
                            if (Mixer.getMixer() == null) return@Runnable

                            try {
                                try {
                                    Mixer.reengageAudio()
                                } catch (_: Exception) {
                                }
                                song.resume()
                            } catch (_: Exception) {
                                return@Runnable
                            }

                            // Restore position *after* the SF2 resume workaround.
                            if (savedPosition > 0) {
                                bankSwapHandler.postDelayed({
                                    if (localGen != bankSwapGeneration) return@postDelayed
                                    if (currentSong !== song) return@postDelayed
                                    if (Mixer.getMixer() == null) return@postDelayed
                                    try {
                                        song.seekToMs(savedPosition)
                                        viewModel.currentPositionMs = savedPosition
                                    } catch (_: Exception) {
                                    }
                                }, 50)
                            }
                        }

                        pendingBankSwapRunnable = runnable
                        bankSwapHandler.postDelayed(runnable, 250)
                        viewModel.isPlaying = true
                    } else {
                        // Keep paused, but restore the user's previous position.
                        if (savedPosition > 0) {
                            try {
                                song.seekToMs(savedPosition)
                                viewModel.currentPositionMs = savedPosition
                            } catch (_: Exception) {
                            }
                        }
                        viewModel.isPlaying = false
                    }
                } else {
                    // Non-SF2: seek is stable without the pause/resume workaround.
                    if (savedPosition > 0) {
                        Thread.sleep(100)
                        song.seekToMs(savedPosition)
                        viewModel.currentPositionMs = savedPosition
                    }
                    if (wasPlaying) {
                        viewModel.isPlaying = true
                    } else {
                        song.pause()
                        viewModel.isPlaying = false
                    }
                }
            } else if (loadResult.isSound) {
                // Should be uncommon for a bank swap, but handle gracefully.
                val sound = loadResult.sound
                if (sound == null) {
                    runOnUiThread {
                        Toast.makeText(this, "Failed to reload sound", Toast.LENGTH_SHORT).show()
                    }
                    return
                }

                currentSound = sound
                currentSong = null

                sound.setVolumePercent(volumePercent)
                val loopCount = if (viewModel.repeatMode == RepeatMode.SONG) 32767 else 0
                sound.setLoops(loopCount)

                val startResult = sound.start()
                if (startResult != 0) {
                    runOnUiThread {
                        Toast.makeText(this, "Failed to start sound (err=$startResult)", Toast.LENGTH_SHORT).show()
                    }
                    return
                }

                if (savedPosition > 0) {
                    Thread.sleep(50)
                    sound.seekToMs(savedPosition)
                    viewModel.currentPositionMs = savedPosition
                }

                if (wasPlaying) {
                    viewModel.isPlaying = true
                } else {
                    sound.pause()
                    viewModel.isPlaying = false
                }
            } else {
                runOnUiThread {
                    Toast.makeText(this, "Reload failed: unknown type", Toast.LENGTH_SHORT).show()
                }
            }
        } catch (ex: Exception) {
            viewModel.isPlaying = false
            android.util.Log.e("MainActivity", "Reload error: ${ex.message}")
            runOnUiThread {
                Toast.makeText(this, "Reload error: ${ex.localizedMessage}", Toast.LENGTH_SHORT).show()
            }
        }
    }
    
    private fun hideSystemUI() {
        if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.R) {
            // Android 11 (API 30) and above - only hide navigation bar, keep status bar
            androidx.core.view.WindowCompat.setDecorFitsSystemWindows(window, false)
            window.insetsController?.let {
                it.hide(WindowInsets.Type.navigationBars())
                it.systemBarsBehavior = WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
            }
        } else {
            // Android 10 and below - only hide navigation bar, keep status bar
            @Suppress("DEPRECATION")
            window.decorView.systemUiVisibility = (
                View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                or View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                or View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                or View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                or View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
            )
        }
    }
    
    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) {
            hideSystemUI()
        }
    }
    
    private fun requestStoragePermissions() {
        val permissions = mutableListOf<String>()
        
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            permissions.add(android.Manifest.permission.POST_NOTIFICATIONS)
        } else {
            permissions.add(android.Manifest.permission.READ_EXTERNAL_STORAGE)
        }
        
        val needsPermission = permissions.any {
            ContextCompat.checkSelfPermission(this, it) != PackageManager.PERMISSION_GRANTED
        }
        
        if (needsPermission) {
            permissionLauncher.launch(permissions.toTypedArray())
        }
    }

    override fun onPause() {
        super.onPause()

        // If the UI is going to background while nothing is playing, suspend the
        // native audio output thread to avoid idle CPU burn. Do NOT do this while
        // actively playing, or we'd break background playback via the service.
        try {
            val viewModel = androidx.lifecycle.ViewModelProvider(this)[MusicPlayerViewModel::class.java]
            if (!viewModel.isPlaying && Mixer.getMixer() != null) {
                val r = Mixer.disengageAudio()
                android.util.Log.d("MainActivity", "Mixer audio disengaged onPause (r=$r)")
            }
        } catch (_: Exception) {
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        if (isBound) {
            unbindService(connection)
            isBound = false
        }
        Mixer.delete()
    }
    
    fun updateServiceNotification(
        title: String,
        artist: String,
        isPlaying: Boolean,
        hasNext: Boolean,
        hasPrevious: Boolean,
        currentPosition: Long = 0,
        duration: Long = 0,
        fileExtension: String = ""
    ) {
        if (isBound && playbackService != null) {
            // Start the service if it's not already running in foreground
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                startForegroundService(android.content.Intent(this, MediaPlaybackService::class.java))
            } else {
                startService(android.content.Intent(this, MediaPlaybackService::class.java))
            }
            playbackService?.updateNotification(title, artist, isPlaying, hasNext, hasPrevious, currentPosition, duration, fileExtension)
        }
    }
    
    fun stopServiceNotification() {
        if (isBound && playbackService != null) {
            playbackService?.stopForegroundService()
        }
    }
}
