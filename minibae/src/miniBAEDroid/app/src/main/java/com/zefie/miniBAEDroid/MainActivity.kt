package com.zefie.miniBAEDroid

import android.os.Bundle
import android.content.Context
import android.net.Uri
import android.view.View
import android.view.WindowInsets
import android.view.WindowInsetsController
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import android.widget.Button
import android.widget.Toast
import org.minibae.Mixer

class MainActivity : AppCompatActivity() {
    private lateinit var openFolderLauncher: androidx.activity.result.ActivityResultLauncher<Uri?>
    private lateinit var openFileLauncher: androidx.activity.result.ActivityResultLauncher<Array<String>>
    var pendingBankReload = false // Flag to indicate bank was changed in settings
    var currentSong: org.minibae.Song? = null // Current song instance that survives fragment transitions


    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        
        // Hide system navigation bar
        hideSystemUI()

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
            // Ensure built-in instrument patches are loaded so MIDI/RMF playback produces sound.
            // Many devices need at least one bank of patches; failure is non-fatal but will result in silent MIDI.
            val br = Mixer.addBuiltInPatches()
            if (br != 0) {
                Toast.makeText(this, "Failed to load built-in patches: $br", Toast.LENGTH_SHORT).show()
            }
            // Apply persisted reverb setting after song start so it affects playback
            try {
                val prefs = getSharedPreferences("miniBAE_prefs", Context.MODE_PRIVATE)
                val savedReverb = prefs.getInt("default_reverb", 1)
                val savedCurve = prefs.getInt("velocity_curve", 1)
                Mixer.setDefaultReverb(savedReverb)
                Mixer.setDefaultVelocityCurve(savedCurve)
            } catch (ex: Exception) { }            
        }

        // Setup fragments
        val homeTab = findViewById<Button>(R.id.tab_home)
        val settingsTab = findViewById<Button>(R.id.tab_settings)

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
        settingsTab.setOnClickListener {
            supportFragmentManager.beginTransaction().replace(R.id.fragment_container, SettingsFragment()).commit()
        }
    }
    
    fun requestFolderPicker() {
        openFolderLauncher.launch(null)
    }
    
    fun requestFilePicker() {
        openFileLauncher.launch(arrayOf("audio/midi", "audio/x-midi", "audio/*", "*/*"))
    }
    
    fun showSettings() {
        supportFragmentManager.beginTransaction().replace(R.id.fragment_container, SettingsFragment()).commit()
    }
    
    fun showHome() {
        supportFragmentManager.beginTransaction().replace(R.id.fragment_container, HomeFragment()).commit()
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
        currentSong?.stop()
        currentSong = null
        viewModel.isPlaying = false
        
        // Small delay to ensure the old song is fully stopped
        Thread.sleep(50)
        
        // Reload the song with the new bank
        try {
            val song = Mixer.createSong()
            if (song != null) {
                currentSong = song
                val bytes = currentItem.file.readBytes()
                val status = song.loadFromMemory(bytes)
                if (status == 0) {
                    // Apply volume
                    Mixer.setMasterVolumePercent(viewModel.volumePercent)
                    song.setVolumePercent(viewModel.volumePercent)
                    
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
            // Android 11 (API 30) and above
            window.setDecorFitsSystemWindows(false)
            window.insetsController?.let {
                it.hide(WindowInsets.Type.systemBars())
                it.systemBarsBehavior = WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
            }
        } else {
            // Android 10 and below
            @Suppress("DEPRECATION")
            window.decorView.systemUiVisibility = (
                View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                or View.SYSTEM_UI_FLAG_FULLSCREEN
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
    
    @Deprecated("Deprecated in Java")
    override fun onBackPressed() {
        val fragment = supportFragmentManager.findFragmentById(R.id.fragment_container)
        if (fragment is SettingsFragment) {
            // Return to player instead of exiting
            showHome()
        } else {
            // On player screen, exit app
            super.onBackPressed()
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        Mixer.delete()
    }
}
