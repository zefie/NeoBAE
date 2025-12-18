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
    var currentSound: org.minibae.Sound? = null

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
        
        // Only reload if it's a Song (Sound doesn't use banks)
        if (currentSound != null) {
            android.util.Log.d("MainActivity", "Current item is a Sound, bank change doesn't affect it")
            return
        }
        
        val song = currentSong
        if (song == null) {
            android.util.Log.d("MainActivity", "No song loaded")
            return
        }
        
        val wasPlaying = viewModel.isPlaying
        val savedPosition = viewModel.currentPositionMs
        android.util.Log.d("MainActivity", "Restarting song with new bank: ${currentItem.title}, wasPlaying=$wasPlaying, position=$savedPosition")
        
        try {
            // Stop current playback
            song.stop(true)
            val bytes = currentItem.file.readBytes()
            val loadResult = org.minibae.LoadResult()

            val status = Mixer.loadFromMemory(bytes, loadResult)

            viewModel.isPlaying = false
            
            // Bank has already been loaded by the caller
            // Just restart the song from the beginning to let it initialize controllers
            song.seekToMs(0)
            song.preroll()
            song.seekToMs(0)

            val startResult = song.start()
            if (startResult != 0) {
                android.util.Log.e("MainActivity", "Failed to start song: $startResult")
                runOnUiThread {
                    Toast.makeText(this, "Failed to start song (err=$startResult)", Toast.LENGTH_SHORT).show()
                }
                return
            }
            
            // Give the MIDI a moment to initialize controllers before seeking
            if (savedPosition > 0) {
                Thread.sleep(100)
                song.seekToMs(savedPosition)
                viewModel.currentPositionMs = savedPosition
            }
            
            // Handle playback state
            if (wasPlaying) {
                // Song was playing, keep it playing
                viewModel.isPlaying = true
                android.util.Log.d("MainActivity", "Song restarted and playing")
            } else {
                // Song was stopped or paused, pause it now
                song.pause()
                viewModel.isPlaying = false
                android.util.Log.d("MainActivity", "Song restarted and paused")
            }
        } catch (ex: Exception) {
            viewModel.isPlaying = false
            android.util.Log.e("MainActivity", "Restart error: ${ex.message}")
            runOnUiThread {
                Toast.makeText(this, "Restart error: ${ex.localizedMessage}", Toast.LENGTH_SHORT).show()
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
    }

    override fun onDestroy() {
        super.onDestroy()
        Mixer.delete()
    }
}
