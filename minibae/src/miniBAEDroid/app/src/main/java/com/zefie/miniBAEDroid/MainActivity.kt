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
    var currentSong: org.minibae.Song? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        
        // Hide system navigation bar
        hideSystemUI()
        
        // Register permission launcher
        permissionLauncher = registerForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) { permissions ->
            val allGranted = permissions.entries.all { it.value }
            if (!allGranted) {
                Toast.makeText(this, "Storage permissions are required to access music files", Toast.LENGTH_LONG).show()
            }
        }
        
        // Request storage permissions
        requestStoragePermissions()

        // Load native library (built by the module 'miniBAE')
        System.loadLibrary("miniBAE")

        // Initialize Mixer for the app so fragments can create Songs
        val status = Mixer.create(assets, 44100, 2, 64, 8, 64)
        if (status != 0) {
            Toast.makeText(this, "Mixer init failed: $status", Toast.LENGTH_SHORT).show()
        }
        else {
            // Provide native code with a writable cache directory path to avoid permission issues when creating temp files
            Mixer.setNativeCacheDir(cacheDir.absolutePath)
            
            // Restore saved bank or use built-in patches
            var bankLoaded = false
            try {
                val prefs = getSharedPreferences("miniBAE_prefs", Context.MODE_PRIVATE)
                val lastBankPath = prefs.getString("last_bank_path", null)
                
                if (!lastBankPath.isNullOrEmpty() && lastBankPath != "__builtin__") {
                    // Try to restore saved bank file
                    val bankFile = java.io.File(lastBankPath)
                    if (bankFile.exists()) {
                        try {
                            val bytes = bankFile.readBytes()
                            val br = Mixer.addBankFromMemory(bytes, bankFile.name)
                            if (br == 0) {
                                bankLoaded = true
                                Toast.makeText(this, "Restored bank: ${Mixer.getBankFriendlyName() ?: bankFile.name}", Toast.LENGTH_SHORT).show()
                            }
                        } catch (ex: Exception) {
                            android.util.Log.e("MainActivity", "Failed to restore saved bank: ${ex.message}")
                        }
                    } else {
                        android.util.Log.w("MainActivity", "Saved bank file not found: $lastBankPath")
                    }
                }
            } catch (ex: Exception) {
                android.util.Log.e("MainActivity", "Error restoring bank: ${ex.message}")
            }
            
            // Fall back to built-in patches if no bank was loaded
            if (!bankLoaded) {
                val br = Mixer.addBuiltInPatches()
                if (br != 0) {
                    Toast.makeText(this, "Failed to load built-in patches: $br", Toast.LENGTH_SHORT).show()
                } else {
                    Toast.makeText(this, "Using built-in patches", Toast.LENGTH_SHORT).show()
                }
            }
            
            // Apply persisted reverb and velocity curve settings
            try {
                val prefs = getSharedPreferences("miniBAE_prefs", Context.MODE_PRIVATE)
                val savedReverb = prefs.getInt("default_reverb", 1)
                val savedCurve = prefs.getInt("velocity_curve", 1)
                Mixer.setDefaultReverb(savedReverb)
                Mixer.setDefaultVelocityCurve(savedCurve)
            } catch (ex: Exception) { }            
        }

        // Setup OnBackPressedDispatcher instead of deprecated onBackPressed()
        onBackPressedDispatcher.addCallback(this, object : androidx.activity.OnBackPressedCallback(true) {
            override fun handleOnBackPressed() {
                // Exit app on back pressed
                finish()
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
    }
    
    fun requestFolderPicker() {
        openFolderLauncher.launch(null)
    }
    
    fun requestFilePicker() {
        openFileLauncher.launch(arrayOf("audio/midi", "audio/x-midi", "audio/*", "*/*"))
    }
    
    fun requestBankReload() {
        android.util.Log.d("MainActivity", "requestBankReload called, setting flag to true")
        pendingBankReload = true
    }
    
    fun reloadCurrentSongForBankSwap() {
        android.util.Log.d("MainActivity", "reloadCurrentSongForBankSwap called")
        
        // Get the ViewModel from the ViewModelStore
        val viewModel = androidx.lifecycle.ViewModelProvider(this)[MusicPlayerViewModel::class.java]
        val currentItem = viewModel.getCurrentItem()
        if (currentItem == null) {
            android.util.Log.d("MainActivity", "No current item to reload")
            return
        }
        
        val wasPlaying = viewModel.isPlaying
        val wasPaused = currentSong?.isPaused() == true
        val savedPosition = viewModel.currentPositionMs
        android.util.Log.d("MainActivity", "Reloading song: ${currentItem.title}, wasPlaying=$wasPlaying, wasPaused=$wasPaused, position=$savedPosition")
        
        // Stop current playback and ensure it fully stops
        currentSong?.stop(true)
        currentSong = null
        viewModel.isPlaying = false
        
        // Small delay to ensure the old song is fully stopped
        Thread.sleep(50)
        
        // Reload the song with the new bank
        try {
            val bytes = currentItem.file.readBytes()
            val loadResult = org.minibae.LoadResult()
            
            val status = Mixer.loadFromMemory(bytes, loadResult)
            
            if (status == 0 && loadResult.isSong) {
                val song = loadResult.song
                if (song != null) {
                    currentSong = song
                    android.util.Log.d("MainActivity", "Reloaded ${loadResult.fileTypeString} file")
                    
                    // Apply volume
                    Mixer.setMasterVolumePercent(viewModel.volumePercent)
                    song.setVolumePercent(viewModel.volumePercent)
                    
                    // Set loop count based on repeat mode
                    val loopCount = if (viewModel.repeatMode == RepeatMode.SONG) 32768 else 0
                    song.setLoops(loopCount)
                    
                    // Start the song first (required before seeking)
                    val startResult = song.start()
                    if (startResult != 0) {
                        android.util.Log.e("MainActivity", "Failed to start song: $startResult")
                        runOnUiThread {
                            Toast.makeText(this, "Failed to start song (err=$startResult)", Toast.LENGTH_SHORT).show()
                        }
                        return
                    }
                    
                    // Seek to saved position
                    if (savedPosition > 0) {
                        song.seekToMs(savedPosition)
                        viewModel.currentPositionMs = savedPosition
                    }
                    
                    // Handle playback state
                    if (wasPlaying) {
                        // Song was playing, keep it playing
                        viewModel.isPlaying = true
                        android.util.Log.d("MainActivity", "Song reloaded and playing")
                    } else {
                        // Song was stopped or paused, pause it now
                        song.pause()
                        viewModel.isPlaying = false
                        android.util.Log.d("MainActivity", "Song reloaded and paused")
                    }
                    
                    runOnUiThread {
                        Toast.makeText(this, "Bank applied to current song", Toast.LENGTH_SHORT).show()
                    }
                } else {
                    viewModel.isPlaying = false
                    runOnUiThread {
                        Toast.makeText(this, "Failed to reload song (err=$status)", Toast.LENGTH_SHORT).show()
                    }
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
            permissions.add(android.Manifest.permission.READ_MEDIA_AUDIO)
            permissions.add(android.Manifest.permission.POST_NOTIFICATIONS)
        } else {
            permissions.add(android.Manifest.permission.READ_EXTERNAL_STORAGE)
            permissions.add(android.Manifest.permission.WRITE_EXTERNAL_STORAGE)
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
        // If no song is loaded when going to background, exit the app
        if (currentSong == null) {
            finish()
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        Mixer.delete()
    }
}
