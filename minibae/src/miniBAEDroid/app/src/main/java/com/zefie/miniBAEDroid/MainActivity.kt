package com.zefie.miniBAEDroid

import android.os.Bundle
import android.content.Context
import android.net.Uri
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import android.widget.Button
import android.widget.Toast
import android.widget.ImageButton
import android.widget.SeekBar
import android.view.MotionEvent
import android.view.View
import org.minibae.Mixer
import org.minibae.Sound
import org.minibae.Song
import java.io.File
import java.io.FileOutputStream

class MainActivity : AppCompatActivity() {
    private var sound: Sound? = null
    private lateinit var openFolderLauncher: androidx.activity.result.ActivityResultLauncher<Uri?>
    // callable by fragments to request playback
    var playFile: ((String) -> Unit)? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        // Load native library (built by the module 'miniBAE')
        System.loadLibrary("miniBAE")

        // Initialize Mixer for the app so fragments can create Songs
        val status = Mixer.create(assets, 44100, 0, 16, 8, 100)
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
                val savedCurve = prefs.getInt("velocity_curve", 0)
                Mixer.setDefaultReverb(savedReverb)
                Mixer.setDefaultVelocityCurve(savedCurve)
            } catch (ex: Exception) { }            
        }

        // Setup fragments
        val homeTab = findViewById<Button>(R.id.tab_home)
        val settingsTab = findViewById<Button>(R.id.tab_settings)

        // Register SAF launcher
        openFolderLauncher = registerForActivityResult(ActivityResultContracts.OpenDocumentTree()) { uri: Uri? ->
            uri?.let {
                val fragment = supportFragmentManager.findFragmentById(R.id.fragment_container)
                if (fragment is HomeFragment) {
                    fragment.onFolderPicked(it)
                }
            }
        }

        supportFragmentManager.beginTransaction().replace(R.id.fragment_container, HomeFragment()).commit()

        homeTab.setOnClickListener {
            supportFragmentManager.beginTransaction().replace(R.id.fragment_container, HomeFragment()).commit()
            findViewById<Button>(R.id.browse_button).visibility = android.view.View.VISIBLE
        }
        settingsTab.setOnClickListener {
            supportFragmentManager.beginTransaction().replace(R.id.fragment_container, SettingsFragment()).commit()
            findViewById<Button>(R.id.browse_button).visibility = android.view.View.GONE
        }

        // Wire the activity-level Browse button (placed under the header) to the HomeFragment
    val browseBtn = findViewById<Button>(R.id.browse_button)
    // Ensure initial visibility matches starting fragment (Home)
    browseBtn.visibility = android.view.View.VISIBLE
        // Launch SAF folder picker when button is pressed
        browseBtn.setOnClickListener {
            openFolderLauncher.launch(null)
        }

        // Refresh button: re-scan current folder contents in HomeFragment
        val refreshBtn = findViewById<ImageButton>(R.id.refresh_button)
        refreshBtn.setOnClickListener {
            val fragment = supportFragmentManager.findFragmentById(R.id.fragment_container)
            if (fragment is HomeFragment) {
                fragment.refreshFiles()
                Toast.makeText(this, "Folder refreshed", Toast.LENGTH_SHORT).show()
            }
        }

        // Player UI
        val playBtn = findViewById<ImageButton>(R.id.play_pause_button)
        val seekBar = findViewById<SeekBar>(R.id.player_seek)

        var currentSong: Song? = null
        var isPlaying = false

        // Periodic progress updater
        val handler = android.os.Handler(mainLooper)
        val updateRunnable = object: Runnable {
            override fun run() {
                // TODO: query native song position/duration if exposed. Here we just advance UI when playing.
                if (isPlaying) {
                    val p = seekBar.progress
                    if (p < seekBar.max) seekBar.progress = p + 1
                }
                handler.postDelayed(this, 250)
            }
        }
        handler.post(updateRunnable)

        playBtn.setOnClickListener {
            if (!isPlaying) {
                // start or resume
                currentSong?.let { s ->
                    val r = s.start()
                    if (r == 0) {
                        isPlaying = true
                        playBtn.setImageResource(android.R.drawable.ic_media_pause)
                    }
                }
            } else {
                // stop
                currentSong?.stop()
                isPlaying = false
                playBtn.setImageResource(android.R.drawable.ic_media_play)
            }
        }

        // Fine scrubbing: reduce sensitivity when user drags finger away vertically
        var initialY = 0f
        seekBar.setOnTouchListener { v, event ->
            when (event.action) {
                MotionEvent.ACTION_DOWN -> {
                    initialY = event.y
                    // pause UI updates while dragging
                    true
                }
                MotionEvent.ACTION_MOVE -> {
                    val dy = Math.abs(event.y - initialY)
                    val fineFactor = 1f / (1f + dy / 100f) // larger vertical distance => smaller step
                    // compute new progress based on touch x
                    val sb = v as SeekBar
                    val width = sb.width
                    val x = event.x.coerceIn(0f, width.toFloat())
                    val rawProgress = (x / width * sb.max).toInt()
                    val current = sb.progress
                    val newProg = current + ((rawProgress - current) * fineFactor).toInt()
                    sb.progress = newProg.coerceIn(0, sb.max)
                    true
                }
                MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                    // Seek native song if available; for now we just set progress
                    true
                }
                else -> false
            }
        }

        // expose play functionality to fragments via activity method
        this.playFile = { path: String ->
            // stop existing
            currentSong?.stop()
            currentSong = Mixer.createSong()
            currentSong?.let { s ->
                try {
                    val f = java.io.File(path)
                    val bytes = f.readBytes()
                    val status = s.loadFromMemory(bytes)
                    if (status == 0) {
                        val r = s.start()
                        if (r == 0) {
                            isPlaying = true
                            playBtn.setImageResource(android.R.drawable.ic_media_pause)
                        }
                    } else {
                        Toast.makeText(this, "Failed to load: $status", Toast.LENGTH_SHORT).show()
                    }
                } catch (ex: Exception) {
                    Toast.makeText(this, "Failed to read file: ${ex.localizedMessage}", Toast.LENGTH_SHORT).show()
                }
            }
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        Mixer.delete()
    }
}
