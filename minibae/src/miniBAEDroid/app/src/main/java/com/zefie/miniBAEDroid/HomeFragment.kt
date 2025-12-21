package com.zefie.miniBAEDroid

import android.R
import android.os.Bundle
import android.content.Context
import android.content.Intent
import android.os.Build
import android.os.Handler
import android.os.Environment
import android.os.Looper
import android.os.storage.StorageManager
import android.os.storage.StorageVolume
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.Toast
import android.net.Uri
import android.app.Activity
import androidx.activity.result.contract.ActivityResultContracts
import androidx.documentfile.provider.DocumentFile
import androidx.fragment.app.Fragment
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.gestures.detectHorizontalDragGestures
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.background
import androidx.compose.material.*
import androidx.compose.material.pullrefresh.PullRefreshIndicator
import androidx.compose.material.pullrefresh.pullRefresh
import androidx.compose.material.pullrefresh.rememberPullRefreshState
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.*
import androidx.compose.material.icons.filled.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.ComposeView
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.foundation.horizontalScroll
import androidx.compose.ui.layout.onGloballyPositioned
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.zIndex
import androidx.core.view.WindowCompat
import androidx.compose.runtime.collectAsState
import android.content.res.Configuration
import java.io.File
import org.minibae.Mixer
import org.minibae.Song
import kotlinx.coroutines.delay
import kotlinx.coroutines.Job
import kotlinx.coroutines.launch
import androidx.lifecycle.lifecycleScope
import kotlin.math.roundToInt

class HomeFragment : Fragment() {

    companion object {
        // Default to the 2nd option ("Peaky S Curve" in the UI)
        var velocityCurve = mutableStateOf(1)

        private const val PREF_NAME = "miniBAE_prefs"
        private const val KEY_ENABLE_AUDIO_FILES = "enable_audio_files"
        private const val KEY_ENABLED_EXTENSIONS = "enabled_extensions"
        
        // Valid music file extensions
        private val MUSIC_EXTENSIONS_DEBUG = listOf("mid", "midi", "kar", "rmf", "xmf", "mxmf", "rmi")
        private val AUDIO_EXTENSIONS = listOf("wav", "ogg", "flac", "au", "mp2", "mp3", "aif", "aiff")
        private val MUSIC_EXTENSIONS_RELEASE = listOf("mid", "midi", "kar", "rmf", "xmf", "mxmf", "rmi")
        
        // Valid sound bank file extensions
        val BANK_EXTENSIONS = setOf("sf2", "hsb", "sf3", "sfo", "dls")
        
        // Get appropriate music extensions based on build type
        fun getMusicExtensions(context: Context): Set<String> {
            val base = (if (BuildConfig.DEBUG) MUSIC_EXTENSIONS_DEBUG else MUSIC_EXTENSIONS_RELEASE).toSet()
            val supported = base + AUDIO_EXTENSIONS
            val prefs = context.getSharedPreferences(PREF_NAME, Context.MODE_PRIVATE)
            val saved = prefs.getStringSet(KEY_ENABLED_EXTENSIONS, null)
            if (saved != null) {
                return saved.map { it.lowercase() }.toSet().intersect(supported)
            }

            // Backwards-compat: fall back to the old audio toggle if the per-extension
            // list hasn't been set yet.
            val enableAudio = prefs.getBoolean(KEY_ENABLE_AUDIO_FILES, true)
            return if (enableAudio) base + AUDIO_EXTENSIONS else base
        }

        fun getSupportedExtensionsForSettings(context: Context): List<String> {
            val base = if (BuildConfig.DEBUG) MUSIC_EXTENSIONS_DEBUG else MUSIC_EXTENSIONS_RELEASE
            return (base + AUDIO_EXTENSIONS).distinct()
        }

        fun getEnabledExtensionsForSettings(context: Context): Set<String> {
            val prefs = context.getSharedPreferences(PREF_NAME, Context.MODE_PRIVATE)
            val supported = getSupportedExtensionsForSettings(context).toSet()
            val saved = prefs.getStringSet(KEY_ENABLED_EXTENSIONS, null)
            return if (saved != null) {
                saved.map { it.lowercase() }.toSet().intersect(supported)
            } else {
                // Fallback to legacy toggle
                val base = (if (BuildConfig.DEBUG) MUSIC_EXTENSIONS_DEBUG else MUSIC_EXTENSIONS_RELEASE).toSet()
                val enableAudio = prefs.getBoolean(KEY_ENABLE_AUDIO_FILES, true)
                if (enableAudio) base + AUDIO_EXTENSIONS else base
            }
        }

        fun isSoundExtension(extension: String): Boolean {
            return AUDIO_EXTENSIONS.contains(extension.lowercase())
        }
    }

    private var mixerIdleJob: Job? = null
    private var pickedFolderUri: Uri? = null
    private lateinit var viewModel: MusicPlayerViewModel
    private var isAppInForeground = mutableStateOf(true)
    private var pendingExternalUri: Uri? = null

    private val mainHandler = Handler(Looper.getMainLooper())

    private fun postToMain(block: () -> Unit) {
        mainHandler.post(block)
    }

    private fun <T> runOnMainSync(block: () -> T): T {
        if (Looper.myLooper() == Looper.getMainLooper()) {
            return block()
        }
        val latch = java.util.concurrent.CountDownLatch(1)
        val result = java.util.concurrent.atomic.AtomicReference<T>()
        val error = java.util.concurrent.atomic.AtomicReference<Throwable?>()
        postToMain {
            try {
                result.set(block())
            } catch (t: Throwable) {
                error.set(t)
            } finally {
                latch.countDown()
            }
        }
        latch.await()
        error.get()?.let { throw it }
        return result.get()
    }
    
    private val currentSong: Song?
        get() = (activity as? MainActivity)?.currentSong
        
    private fun setCurrentSong(song: Song?) {
        karaokeHandler.reset()
        val ext = viewModel.getCurrentItem()?.file?.extension?.lowercase() ?: ""
        karaokeHandler.setFileExtension(ext)
        song?.setMetaEventListener(karaokeHandler)
        (activity as? MainActivity)?.currentSong = song
    }
    
    private val currentSound: org.minibae.Sound?
        get() = (activity as? MainActivity)?.currentSound
        
    private fun setCurrentSound(sound: org.minibae.Sound?) {
        (activity as? MainActivity)?.currentSound = sound
    }
    
    // Sound bank settings
    private var currentBankName = mutableStateOf("Loading...")
    private var isLoadingBank = mutableStateOf(false)
    private var isExporting = mutableStateOf(false)
    private var exportStatus = mutableStateOf("")
    private var reverbType = mutableStateOf(1)
    // velocityCurve is now in companion object
    private var exportCodec = mutableStateOf(2) // Default to OGG
    private var searchResultLimit = mutableStateOf(1000) // Default to 1000
    private var enabledExtensions = mutableStateOf<Set<String>>(emptySet())
    
    // Bank browser state (completely separate from main browser)
    private var showBankBrowser = mutableStateOf(false)
    private var bankBrowserPath = mutableStateOf("/sdcard") // Will be loaded from prefs
    private var bankBrowserFiles = mutableStateListOf<PlaylistItem>()
    private var bankBrowserLoading = mutableStateOf(false)
    
    private val karaokeHandler = KaraokeHandler()

    // Prevent overlapping bank swap operations (mixer teardown/recreate is not re-entrant).
    private val bankSwapInProgress = java.util.concurrent.atomic.AtomicBoolean(false)

    private inner class KaraokeHandler : Song.MetaEventListener {
        private val currentLine = StringBuilder()
        private var lastFragment = ""
        private var haveMetaLyrics = false
        private var currentExtension = ""

        fun setFileExtension(ext: String) {
            currentExtension = ext
        }

        override fun onMetaEvent(markerType: Int, data: ByteArray) {
            if (isExporting.value) return
            
            if (markerType == 0x05) {
                haveMetaLyrics = true
            }

            // Use ISO-8859-1 to avoid replacement chars for 8-bit data
            var text = String(data, java.nio.charset.StandardCharsets.ISO_8859_1).replace("\u0000", "")
            if (text.isEmpty()) return

            if (markerType == 0x05) {
                processFragment(text)
            } else if (markerType == 0x01) {
                val isKaraokeMarker = text.startsWith("@") || text.startsWith("/") || text.startsWith("\\")
                
                if (isKaraokeMarker) {
                     if (text.startsWith("@")) {
                         commitLine()
                     } else {
                         processFragment(text)
                     }
                } else if (!haveMetaLyrics) {
                    // Plain text fallback - only if .kar file
                    if (currentExtension == "kar") {
                        processFragment(text)
                    }
                }
            }
        }

        private fun processFragment(frag: String) {
            var text = frag
            var newlineBefore = false
            var newlineAfter = false

            if (text.startsWith("/") || text.startsWith("\\")) {
                newlineBefore = true
                text = text.substring(1)
            }
            
            if (text.endsWith("\r") || text.endsWith("\n")) {
                newlineAfter = true
                text = text.replace("\r", "").replace("\n", "")
            }

            if (newlineBefore) {
                commitLine()
            }

            if (text.isNotEmpty()) {
                if (lastFragment.isNotEmpty() && text.length > lastFragment.length && text.startsWith(lastFragment)) {
                    val lenToRemove = lastFragment.length
                    if (currentLine.length >= lenToRemove) {
                        currentLine.setLength(currentLine.length - lenToRemove)
                    }
                    currentLine.append(text)
                } else {
                    // Don't auto-add spaces for karaoke
                    currentLine.append(text)
                }
                lastFragment = text
                
                activity?.runOnUiThread {
                    viewModel.currentLyric = currentLine.toString()
                }
            }

            if (newlineAfter) {
                commitLine()
            }
        }

        private fun commitLine() {
            currentLine.setLength(0)
            lastFragment = ""
            activity?.runOnUiThread {
                viewModel.currentLyric = ""
            }
        }
        
        fun reset() {
            currentLine.setLength(0)
            lastFragment = ""
            haveMetaLyrics = false
            activity?.runOnUiThread {
                viewModel.currentLyric = ""
            }
        }
    }

    private val saveFilePicker = registerForActivityResult(ActivityResultContracts.StartActivityForResult()) { result ->
        if (result.resultCode == Activity.RESULT_OK) {
            val data: Intent? = result.data
            data?.data?.let { uri ->
                exportToFile(uri)
            }
        }
    }

    fun onFolderPicked(uri: Uri) {
        pickedFolderUri = uri
        Toast.makeText(this.requireContext(), "Folder selected: $uri", Toast.LENGTH_SHORT).show()
        try {
            val takeFlags = (android.content.Intent.FLAG_GRANT_READ_URI_PERMISSION or android.content.Intent.FLAG_GRANT_WRITE_URI_PERMISSION)
            requireContext().contentResolver.takePersistableUriPermission(uri, takeFlags)
        } catch (_: Exception) { }
        try {
            val prefs = requireContext().getSharedPreferences("miniBAE_prefs", Context.MODE_PRIVATE)
            prefs.edit().putString("lastFolderUri", uri.toString()).apply()
        } catch (_: Exception) { }
    }
    
    fun onFilePicked(uri: Uri) {
        try {
            var displayName: String? = null
            requireContext().contentResolver.query(uri, null, null, null, null)?.use { cursor ->
                if (cursor.moveToFirst()) {
                    val nameIndex = cursor.getColumnIndex(android.provider.OpenableColumns.DISPLAY_NAME)
                    if (nameIndex >= 0) {
                        displayName = cursor.getString(nameIndex)
                    }
                }
            }
            
            val originalName = displayName ?: uri.lastPathSegment?.substringAfterLast('/') ?: "song.mid"
            val file = File(requireContext().cacheDir, originalName)
            requireContext().contentResolver.openInputStream(uri)?.use { input ->
                file.outputStream().use { output ->
                    input.copyTo(output)
                }
            }
            val item = PlaylistItem(file)
            viewModel.addToPlaylist(item)
            viewModel.playAtIndex(viewModel.playlist.size - 1)
            startPlayback(file)
            saveFavorites()
        } catch (ex: Exception) {
            Toast.makeText(requireContext(), "Failed to load file: ${ex.message}", Toast.LENGTH_SHORT).show()
        }
    }

    fun reloadCurrentSongForBankSwap() {
        // Ensure the reload (which may open the mixer) happens on the main thread.
        postToMain {
            (activity as? MainActivity)?.reloadCurrentSongForBankSwap()
        }
    }
    
    private fun checkStoragePermissions() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            // Android 11+ - check for MANAGE_EXTERNAL_STORAGE permission
            if (!Environment.isExternalStorageManager()) {
                try {
                    val intent = Intent(android.provider.Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION)
                    intent.data = android.net.Uri.parse("package:${requireContext().packageName}")
                    startActivity(intent)
                    Toast.makeText(requireContext(), "Please enable 'All files access' permission to access external storage devices", Toast.LENGTH_LONG).show()
                } catch (e: Exception) {
                    val intent = Intent(android.provider.Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION)
                    startActivity(intent)
                }
            }
        } else if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            // Android 6+ - check for READ_EXTERNAL_STORAGE permission
            if (androidx.core.content.ContextCompat.checkSelfPermission(
                requireContext(), 
                android.Manifest.permission.READ_EXTERNAL_STORAGE
            ) != android.content.pm.PackageManager.PERMISSION_GRANTED) {
                androidx.core.app.ActivityCompat.requestPermissions(
                    requireActivity(),
                    arrayOf(android.Manifest.permission.READ_EXTERNAL_STORAGE),
                    1001
                )
            }
        }
    }

    fun refreshFolderAfterPermission() {
        // Refresh the current folder now that permissions are granted
        if (::viewModel.isInitialized) {
            viewModel.currentFolderPath?.let { path ->
                loadFolderContents(path)
            }
        }
    }

    private var loadingState: MutableState<Boolean>? = null
    private var lastFolderPath: String? = null
    
    override fun onResume() {
        super.onResume()
        isAppInForeground.value = true
        
        // Check if we need to refresh folder after permission grant
        // If the list is empty or only contains the refresh button, try to reload
        if (::viewModel.isInitialized) {
            val isEmpty = viewModel.folderFiles.isEmpty() || 
                         (viewModel.folderFiles.size == 1 && viewModel.folderFiles[0].title.contains("Refresh"))
            
            if (isEmpty) {
                 var hasPerm = true
                 if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                     if (!Environment.isExternalStorageManager()) hasPerm = false
                 } else if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
                     if (androidx.core.content.ContextCompat.checkSelfPermission(
                         requireContext(), 
                         android.Manifest.permission.READ_EXTERNAL_STORAGE
                     ) != android.content.pm.PackageManager.PERMISSION_GRANTED) hasPerm = false
                 }
                 
                 if (hasPerm) {
                     viewModel.currentFolderPath?.let { path ->
                         loadFolderContents(path)
                     }
                 }
            }
        }
        
        val mainActivity = activity as? MainActivity
        android.util.Log.d("HomeFragment", "onResume: pendingBankReload=${mainActivity?.pendingBankReload}")
        if (mainActivity?.pendingBankReload == true) {
            mainActivity.pendingBankReload = false
            android.util.Log.d("HomeFragment", "Calling reloadCurrentSongForBankSwap")
            reloadCurrentSongForBankSwap()
        }
        
        // Check for pending file URI from external intent
        mainActivity?.consumePendingFileUri()?.let { uri ->
            android.util.Log.d("HomeFragment", "Found pending file URI: $uri")
            if (::viewModel.isInitialized) {
                handleExternalFile(uri)
            } else {
                // Store for later when viewModel is ready
                android.util.Log.d("HomeFragment", "ViewModel not ready, storing pending URI")
                pendingExternalUri = uri
            }
        }
    }
    
    override fun onPause() {
        super.onPause()
        isAppInForeground.value = false
    }
    
    override fun onDestroyView() {
        super.onDestroyView()
        // Don't hide notification on destroy - keep it if music is still playing
        // Only hide if user explicitly closes via notification action
    }
    
    override fun onDestroy() {
        super.onDestroy()
    }

    private fun saveFavorites() {
        try {
            val prefs = requireContext().getSharedPreferences("miniBAE_prefs", Context.MODE_PRIVATE)
            val json = viewModel.favorites.joinToString("|||")
            prefs.edit().putString("savedFavorites", json).apply()
        } catch (ex: Exception) {
            android.util.Log.e("HomeFragment", "Failed to save favorites: ${ex.message}")
        }
    }

    private fun syncVirtualPlaylistToFavoritesOrder() {
        if (!::viewModel.isInitialized) return
        if (viewModel.currentScreen != NavigationScreen.FAVORITES) return

        if (viewModel.playlist.isEmpty() || viewModel.favorites.isEmpty()) return

        val order = viewModel.favorites.withIndex().associate { it.value to it.index }
        val existing = viewModel.playlist.toList()
        val originalIndex = existing.withIndex().associate { it.value.path to it.index }

        // Only reorder if the playlist actually contains any favorites.
        if (existing.none { order.containsKey(it.path) }) return

        val reordered = existing.sortedWith(
            compareBy<PlaylistItem>({ order[it.path] ?: Int.MAX_VALUE }, { originalIndex[it.path] ?: Int.MAX_VALUE })
        )

        if (reordered == existing) return
        viewModel.replacePlaylistPreservingCurrent(reordered)
    }
    
    private fun loadFavorites() {
        try {
            val prefs = requireContext().getSharedPreferences("miniBAE_prefs", Context.MODE_PRIVATE)
            val json = prefs.getString("savedFavorites", null)
            if (json != null && json.isNotEmpty()) {
                val paths = json.split("|||")
                viewModel.favorites.clear()
                viewModel.favorites.addAll(paths.filter { File(it).exists() })
            }
        } catch (ex: Exception) {
            android.util.Log.e("HomeFragment", "Failed to load favorites: ${ex.message}")
        }
    }
    
    override fun onCreateView(inflater: LayoutInflater, container: ViewGroup?, savedInstanceState: Bundle?): View {
        if (pickedFolderUri == null) {
            try {
                val prefs = requireContext().getSharedPreferences("miniBAE_prefs", Context.MODE_PRIVATE)
                val last = prefs.getString("lastFolderUri", null)
                if (last != null) {
                    val uri = Uri.parse(last)
                    val hasPerm = requireContext().contentResolver.persistedUriPermissions.any { it.uri == uri }
                    if (hasPerm) {
                        pickedFolderUri = uri
                    }
                }
                // Don't set these on startup - they'll be set when mixer is created
            } catch (_: Exception) { }
        }
        
        return ComposeView(requireContext()).apply {
            setContent {
                viewModel = androidx.lifecycle.viewmodel.compose.viewModel(
                    viewModelStoreOwner = requireActivity()
                )
                val loading = remember { mutableStateOf(false) }
                loadingState = loading

                LaunchedEffect(Unit) {
                    val prefs = requireContext().getSharedPreferences("miniBAE_prefs", Context.MODE_PRIVATE)
                    viewModel.volumePercent = prefs.getInt("volume_percent", 75)
                    
                    // Load saved settings
                    reverbType.value = prefs.getInt("default_reverb", 1)
                    velocityCurve.value = prefs.getInt("velocity_curve", 1) // Default to 2nd option
                    exportCodec.value = prefs.getInt("export_codec", 2) // Default to OGG
                    enabledExtensions.value = getEnabledExtensionsForSettings(requireContext())
                    
                    // Initialize bank name
                    Thread {
                        val prefs = requireContext().getSharedPreferences("miniBAE_prefs", Context.MODE_PRIVATE)
                        val friendly = if (Mixer.getMixer() != null) {
                            Mixer.getBankFriendlyName()
                        } else {
                            // Mixer doesn't exist, show saved bank preference
                            val lastBankPath = prefs.getString("last_bank_path", "__builtin__")
                            when {
                                lastBankPath == "__builtin__" -> "Built-in patches"
                                !lastBankPath.isNullOrEmpty() -> {
                                    val file = java.io.File(lastBankPath)
                                    file.name
                                }
                                else -> null
                            }
                        }
                        currentBankName.value = friendly ?: "No Bank Loaded"
                    }.start()
                    
                    // Don't check permissions on startup - only when user tries to access folders
                    // This prevents the back button from returning to home screen
                    
                    // Set default folder to /sdcard if none exists
                    if (viewModel.currentFolderPath == null) {
                        val savedPath = prefs.getString("current_folder_path", "/sdcard")
                        viewModel.currentFolderPath = savedPath
                        loadFolderContents(savedPath ?: "/sdcard")
                    }
                    
                    if (viewModel.favorites.isEmpty()) {
                        loadFavorites()
                    }
                
                    // Handle pending external file URI after viewModel is ready
                    pendingExternalUri?.let { uri ->
                        android.util.Log.d("HomeFragment", "Processing pending external URI: $uri")
                        handleExternalFile(uri)
                        pendingExternalUri = null
                    }
                
                    // Register notification action callbacks
                    MusicNotificationReceiver.setCallbacks(
                        onPlayPause = { togglePlayPause() },
                        onNext = { playNext() },
                        onPrevious = { playPrevious() },
                        onClose = {
                            stopPlayback(true)
                            viewModel.isPlaying = false
                            (activity as? MainActivity)?.stopServiceNotification()
                            // User closed playback - cleanup is scheduled by stopPlayback(true)
                        }
                    )
                    val savedRepeatMode = prefs.getInt("repeat_mode", 0)
                    viewModel.repeatMode = RepeatMode.values().getOrNull(savedRepeatMode) ?: RepeatMode.NONE
                }

                LaunchedEffect(pickedFolderUri) {
                    if (pickedFolderUri != null) {
                        loadFolderIntoPlaylist()
                    }
                }

                // Track last position we pushed to the system notification. This is separate from
                // the in-app UI updates because the system needs an explicit PlaybackState update
                // when position jumps backwards (e.g. internal BAE looping / baeloop).
                var lastNotificationPositionMs by remember { mutableStateOf(0) }
                var lastNotificationUpdateTimeMs by remember { mutableStateOf(0L) }

                LaunchedEffect(viewModel.isPlaying, viewModel.isDraggingSeekBar) {
                    while (viewModel.isPlaying && !viewModel.isDraggingSeekBar) {
                        try {
                            val pos = getPlaybackPositionMs()
                            val len = getPlaybackLengthMs()
                            viewModel.currentPositionMs = pos
                            if (len > 0) viewModel.totalDurationMs = len

                            // Keep system notification progress in sync without spamming updates.
                            // Key case: when audio loops internally, position jumps back to ~0 but
                            // the system UI will keep showing the old (near-end) position unless we
                            // push a fresh PlaybackState.
                            val nowMs = android.os.SystemClock.elapsedRealtime()
                            val wrappedBackwards = pos + 500 < lastNotificationPositionMs
                            val periodicUpdate = nowMs - lastNotificationUpdateTimeMs >= 1000

                            if (wrappedBackwards || periodicUpdate) {
                                val currentItem = viewModel.getCurrentItem()
                                if (currentItem != null && (viewModel.isPlaying || viewModel.currentTitle != "No song loaded")) {
                                    val folderName = viewModel.currentFolderPath?.let { path ->
                                        File(path).name
                                    } ?: "Unknown Folder"

                                    (activity as? MainActivity)?.updateServiceNotification(
                                        title = viewModel.currentTitle,
                                        artist = folderName,
                                        isPlaying = viewModel.isPlaying,
                                        hasNext = viewModel.hasNext(),
                                        hasPrevious = viewModel.hasPrevious(),
                                        currentPosition = pos.toLong(),
                                        duration = viewModel.totalDurationMs.toLong(),
                                        fileExtension = currentItem.file.extension
                                    )
                                    lastNotificationPositionMs = pos
                                    lastNotificationUpdateTimeMs = nowMs
                                }
                            }
                            
                            // Handle playback completion
                            var playbackFinished = false
                            
                            if (currentSong != null) {
                                if (currentSong?.isDone() == true) {
                                    playbackFinished = true
                                }
                            } else if (currentSound != null) {
                                if (len > 0 && pos >= len - 50) {
                                    playbackFinished = true
                                }
                            }
                            
                            if (playbackFinished) {
                                delay(100)
                                when (viewModel.repeatMode) {
                                    RepeatMode.SONG -> {
                                        // BAE handles looping internally via setLoops()
                                        // No manual restart needed here - BAE will loop automatically
                                    }
                                    RepeatMode.PLAYLIST -> {
                                        // Play next song, or loop back to first
                                        if (viewModel.hasNext()) {
                                            playNext()
                                        } else if (viewModel.playlist.isNotEmpty()) {
                                            viewModel.playAtIndex(0)
                                            viewModel.getCurrentItem()?.let { startPlayback(it.file) }
                                        }
                                    }
                                    RepeatMode.NONE -> {
                                        // Just play next if available
                                        if (viewModel.hasNext()) {
                                            playNext()
                                        } else {
                                            viewModel.isPlaying = false
                                            // No more songs to play - schedule cleanup
                                            scheduleMixerCleanup()
                                        }
                                    }
                                }
                            }
                        } catch (_: Exception) {}
                        delay(250)
                    }
                }

                LaunchedEffect(viewModel.volumePercent) {
                    applyVolume()
                    val prefs = requireContext().getSharedPreferences("miniBAE_prefs", Context.MODE_PRIVATE)
                    prefs.edit().putInt("volume_percent", viewModel.volumePercent).apply()
                }
                
                LaunchedEffect(viewModel.repeatMode) {
                    val prefs = requireContext().getSharedPreferences("miniBAE_prefs", Context.MODE_PRIVATE)
                    prefs.edit().putInt("repeat_mode", viewModel.repeatMode.ordinal).apply()
                }
                
                // Update notification when playback state or track metadata changes
                // (Include duration so the system progress bar end time updates when length becomes known.)
                LaunchedEffect(viewModel.isPlaying, viewModel.currentTitle, viewModel.currentIndex, viewModel.totalDurationMs) {
                    val currentItem = viewModel.getCurrentItem()
                    if (currentItem != null && (viewModel.isPlaying || viewModel.currentTitle != "No song loaded")) {
                        val folderName = viewModel.currentFolderPath?.let { path ->
                            File(path).name
                        } ?: "Unknown Folder"
                        
                        val fileExt = currentItem.file.extension
                        
                        (activity as? MainActivity)?.updateServiceNotification(
                            title = viewModel.currentTitle,
                            artist = folderName,
                            isPlaying = viewModel.isPlaying,
                            hasNext = viewModel.hasNext(),
                            hasPrevious = viewModel.hasPrevious(),
                            currentPosition = viewModel.currentPositionMs.toLong(),
                            duration = viewModel.totalDurationMs.toLong(),
                            fileExtension = fileExt
                        )
                    } else {
                        (activity as? MainActivity)?.stopServiceNotification()
                    }
                }

                MaterialTheme(
                    colors = if (androidx.compose.foundation.isSystemInDarkTheme()) darkColors(
                        primary = Color(0xFFBB86FC),
                        secondary = Color(0xFFBB86FC),
                        background = Color(0xFF121212),
                        surface = Color(0xFF1E1E1E)
                    ) else lightColors(
                        primary = Color(0xFF6200EE),
                        secondary = Color(0xFF6200EE)
                    )
                ) {
                    // Set status bar color
                    val view = LocalView.current
                    val window = (view.context as? android.app.Activity)?.window
                    SideEffect {
                        window?.let {
                            WindowCompat.setDecorFitsSystemWindows(it, false)
                            val controller = WindowCompat.getInsetsController(it, view)
                            controller.isAppearanceLightStatusBars = false
                        }
                    }
                    
                    NewMusicPlayerScreen(
                        viewModel = viewModel,
                        loading = loading.value,
                        onPlayPause = { togglePlayPause() },
                        onNext = { playNext() },
                        onPrevious = { playPrevious() },
                        onSeek = { ms ->
                            viewModel.isDraggingSeekBar = false
                            seekPlaybackToMs(ms)
                            viewModel.currentPositionMs = ms
                        },
                        onStartDrag = { viewModel.isDraggingSeekBar = true },
                        onDrag = { ms -> viewModel.currentPositionMs = ms },
                        onVolumeChange = { viewModel.volumePercent = it },
                        onPlaylistItemClick = { file ->
                            playFileFromBrowser(file)
                        },
                        onToggleFavorite = { filePath ->
                            viewModel.toggleFavorite(filePath)
                            saveFavorites()
                        },
                        onAddFolder = {
                            (activity as? MainActivity)?.requestFolderPicker()
                        },
                        onAddFile = {
                            (activity as? MainActivity)?.requestFilePicker()
                        },
                        onNavigate = { screen ->
                            viewModel.currentScreen = screen
                        },
                        onShufflePlay = {
                            shuffleAndPlay()
                        },
                        onNavigateToFolder = { path ->
                            navigateToFolder(path)
                        },
                        onAddToPlaylist = { file ->
                            val item = PlaylistItem(file)
                            if (!viewModel.playlist.any { it.id == item.id }) {
                                viewModel.addToPlaylist(item)
                                Toast.makeText(requireContext(), "Added to playlist", Toast.LENGTH_SHORT).show()
                            }
                        },
                        bankName = currentBankName.value,
                        isLoadingBank = isLoadingBank.value,
                        isExporting = isExporting.value,
                        exportStatus = exportStatus.value,
                        reverbType = reverbType.value,
                        velocityCurve = velocityCurve.value,
                        exportCodec = exportCodec.value,
                        searchResultLimit = searchResultLimit.value,
                        enabledExtensions = enabledExtensions.value,
                        onLoadBuiltin = {
                            loadBuiltInPatches()
                        },
                        onReverbChange = { value ->
                            reverbType.value = value
                            if (Mixer.getMixer() != null) {
                                Mixer.setDefaultReverb(value)
                            }
                            val prefs = requireContext().getSharedPreferences("miniBAE_prefs", Context.MODE_PRIVATE)
                            prefs.edit().putInt("default_reverb", value).apply()
                        },
                        onCurveChange = { value ->
                            velocityCurve.value = value
                            if (Mixer.getMixer() != null) {
                                Mixer.setDefaultVelocityCurve(value)
                            }
                            val prefs = requireContext().getSharedPreferences("miniBAE_prefs", Context.MODE_PRIVATE)
                            prefs.edit().putInt("velocity_curve", value).apply()
                        },
                        onExportCodecChange = { value ->
                            exportCodec.value = value
                            val prefs = requireContext().getSharedPreferences("miniBAE_prefs", Context.MODE_PRIVATE)
                            prefs.edit().putInt("export_codec", value).apply()
                        },
                        onSearchLimitChange = { value ->
                            searchResultLimit.value = value
                            val prefs = requireContext().getSharedPreferences("miniBAE_prefs", Context.MODE_PRIVATE)
                            prefs.edit().putInt("search_result_limit", value).apply()
                        },
                        onExtensionEnabledChange = { ext, enabled ->
                            val normalized = ext.lowercase()
                            val next = enabledExtensions.value.toMutableSet()
                            if (enabled) {
                                next.add(normalized)
                            } else {
                                next.remove(normalized)
                            }

                            val prefs = requireContext().getSharedPreferences("miniBAE_prefs", Context.MODE_PRIVATE)
                            val anyAudioEnabled = next.any { AUDIO_EXTENSIONS.contains(it) }
                            prefs.edit()
                                .putStringSet(KEY_ENABLED_EXTENSIONS, next)
                                .putBoolean(KEY_ENABLE_AUDIO_FILES, anyAudioEnabled)
                                .apply()

                            enabledExtensions.value = next

                            // Refresh folder listing so Home updates immediately
                            viewModel.currentFolderPath?.let { path ->
                                loadFolderContents(path)
                            }
                        },
                        onExportRequest = { filename, codec ->
                            val intent = Intent(Intent.ACTION_CREATE_DOCUMENT).apply {
                                addCategory(Intent.CATEGORY_OPENABLE)
                                type = when (codec) {
                                    2 -> "audio/ogg"
                                    3 -> "audio/flac"
                                    else -> "audio/wav"
                                }
                                putExtra(Intent.EXTRA_TITLE, filename)
                            }
                            saveFilePicker.launch(intent)
                        },
                        onRefreshStorage = { 
                            checkStoragePermissions()
                            viewModel.currentFolderPath?.let { path ->
                                loadFolderContents(path)
                            } ?: loadFolderContents("/")
                        },
                        onRepeatModeChange = {
                            val loopCount = if (viewModel.repeatMode == RepeatMode.SONG) 32767 else 0
                            currentSong?.setLoops(loopCount)
                            currentSound?.setLoops(loopCount)
                        },
                        onAddAllMidi = {
                            addAllMidiInDirectory()
                        },
                        onAddAllMidiRecursive = {
                            addAllMidiRecursively()
                        },
                        showBankBrowser = showBankBrowser.value,
                        bankBrowserPath = bankBrowserPath.value,
                        bankBrowserFiles = bankBrowserFiles,
                        bankBrowserLoading = bankBrowserLoading.value,
                        onBrowseBanks = {
                            checkStoragePermissions()
                            // Load last bank browser path from preferences (separate from main browser)
                            val prefs = requireContext().getSharedPreferences("miniBAE_prefs", Context.MODE_PRIVATE)
                            val startPath = prefs.getString("last_bank_browser_path", "/") ?: "/"
                            bankBrowserPath.value = startPath
                            navigateToBankFolder(startPath)
                            showBankBrowser.value = true
                        },
                        onBankBrowserNavigate = { path ->
                            navigateToBankFolder(path)
                        },
                        onBankBrowserSelect = { file ->
                            loadBankFromFile(file)
                        },
                        onBankBrowserClose = {
                            showBankBrowser.value = false
                        }
                    )
                }
            }
        }
    }
    
    private fun loadFolderIntoPlaylist() {
        loadingState?.value = true
        Thread {
            try {
                val files = getMediaFiles()
                activity?.runOnUiThread {
                    val items = files.map { PlaylistItem(it) }
                    val newPath = getMusicDir()?.absolutePath
                    if (newPath != lastFolderPath) {
                        viewModel.folderFiles.clear()
                        viewModel.folderFiles.addAll(items)
                        viewModel.currentFolderPath = newPath
                        viewModel.invalidateSearchCache() // Clear search cache when folder changes
                        lastFolderPath = newPath
                    }
                    loadingState?.value = false
                }
            } catch (_: Exception) {
                activity?.runOnUiThread {
                    loadingState?.value = false
                }
            }
        }.start()
    }

    private fun togglePlayPause() {
        if (viewModel.isPlaying) {
            pausePlayback()
            viewModel.isPlaying = false
        } else {
            if (viewModel.getCurrentItem() != null) {
                if (hasActivePlayback() && isPlaybackPaused()) {
                    resumePlayback()
                    viewModel.isPlaying = true
                } else {
                    viewModel.getCurrentItem()?.let { startPlayback(it.file) }
                }
            }
        }
    }
    
    private fun playNext() {
        if (viewModel.hasNext()) {
            viewModel.playNext()
            viewModel.getCurrentItem()?.let { startPlayback(it.file) }
        }
    }
    
    private fun playPrevious() {
        viewModel.playPrevious()
        viewModel.getCurrentItem()?.let { startPlayback(it.file) }
    }
    
    private fun playFileFromBrowser(file: File) {
        // Use the current folder or search results as the playlist
        if (currentSong?.hasEmbeddedBank() == true) {
            android.util.Log.d("HomeFragment", "The previous song had an embedded bank, restoring last known bank")
            val prefs = requireContext().getSharedPreferences("miniBAE_prefs", Context.MODE_PRIVATE)
            val lastBankPath = prefs.getString("last_bank_path", "__builtin__")
            if (lastBankPath == "__builtin__") {
                loadBuiltInPatches()
            } else if (!lastBankPath.isNullOrEmpty()) {
                loadBankFromFile(java.io.File(lastBankPath))
            }
        }
        stopPlayback(true)
        viewModel.clearPlaylist()
        
        // Determine the source list based on current screen
        val sourceList = when (viewModel.currentScreen) {
            NavigationScreen.SEARCH -> {
                // For search, we need to get the current results
                val results = viewModel.searchResults.value
                results.filter { !it.isFolder }
            }
            NavigationScreen.FAVORITES -> {
                // Get favorites as PlaylistItems
                viewModel.favorites.mapNotNull { path ->
                    val f = File(path)
                    if (f.exists() && !f.isDirectory) {
                        PlaylistItem(f)
                    } else {
                        null
                    }
                }
            }
            else -> {
                // Use folder files as playlist (HOME screen)
                viewModel.folderFiles.filter { !it.isFolder }
            }
        }
        
        // Add all files from source list to playlist
        viewModel.addAllToPlaylist(sourceList)
        
        // Find the index of the clicked file in the playlist
        val index = viewModel.playlist.indexOfFirst { it.file.absolutePath == file.absolutePath }
        if (index >= 0) {
            viewModel.playAtIndex(index)
            startPlayback(file)
        } else {
            // Fallback: if file not found, just play it as a single item
            val item = PlaylistItem(file)
            viewModel.addToPlaylist(item)
            viewModel.playAtIndex(viewModel.playlist.size - 1)
            startPlayback(file)
        }
    }
    
    private fun shuffleAndPlay() {
        if (viewModel.playlist.isNotEmpty()) {
            val shuffledIndex = (0 until viewModel.playlist.size).random()
            viewModel.playAtIndex(shuffledIndex)
            viewModel.getCurrentItem()?.let { startPlayback(it.file) }
        }
    }

    private fun reloadSelectedHsbBankForNewSongIfNeeded(targetFile: File) {
        // Guard against intermittent bank unloads across track changes on Android.
        // Only do this for MIDI-ish song types (banks don't affect decoded audio files).
        val ext = targetFile.extension.lowercase()
        val isSongType = ext in setOf("mid", "midi", "kar", "rmf", "xmf", "mxmf", "rmi")
        if (!isSongType) return

        if (Mixer.getMixer() == null) return

        val prefs = requireContext().getSharedPreferences("miniBAE_prefs", Context.MODE_PRIVATE)
        val lastBankPath = prefs.getString("last_bank_path", null)

        // Treat missing preference as "builtin" (default bank is patches.hsb).
        val bankKey = if (lastBankPath.isNullOrBlank()) "__builtin__" else lastBankPath

        val r = when {
            bankKey == "__builtin__" -> loadBuiltInPatchesFromAssets(requireContext())
            bankKey.endsWith(".hsb", ignoreCase = true) -> {
                val bankFile = File(bankKey)
                if (bankFile.exists() && bankFile.isFile) {
                    Mixer.addBankFromFile(bankFile.absolutePath)
                } else {
                    // If the configured HSB disappeared, fall back to built-in patches.
                    loadBuiltInPatchesFromAssets(requireContext())
                }
            }
            else -> return
        }

        android.util.Log.d(
            "HomeFragment",
            "HSB bank refresh before song load: bank=$bankKey result=$r"
        )
    }
    
    private fun startPlayback(file: File) {
        try {
            stopPlayback(true)
            cancelMixerCleanup()
            viewModel.currentPositionMs = 0
            
            // Ensure mixer exists before trying to load
            if (!ensureMixerExists()) {
                viewModel.isPlaying = false
                Toast.makeText(requireContext(), "Mixer not available", Toast.LENGTH_SHORT).show()
                return
            }

            reloadSelectedHsbBankForNewSongIfNeeded(file)
            
            val bytes = file.readBytes()
            val loadResult = org.minibae.LoadResult()

            val status = Mixer.loadFromMemory(bytes, loadResult)
            
            if (status == 0) {
                android.util.Log.d("HomeFragment", "Loaded ${loadResult.fileTypeString} file: ${file.name}")
                if (loadResult.isSong) {
                    val song = loadResult.song
                    if (song != null) {
                        setCurrentSong(song)
                        setCurrentSound(null) // Clear sound reference
                        applyVolume()
                        
                        // Apply velocity curve
                        if (song.isSF2Song()) {
                            song.setVelocityCurve(0)
                        } else {
                            song.setVelocityCurve(velocityCurve.value)
                        }

                        val r = song.start()
                        if (r == 0) {
                            // Set loop count AFTER start (start clears songMaxLoopCount)
                            
                            
                            Mixer.setDefaultReverb(reverbType.value)
                            if (song.hasEmbeddedBank()) {
                                currentBankName.value = "Embedded Bank"
                            }
                            if (song.isSF2Song()) {
                                song.pause()
                                song.seekToMs(0)
                                // Workaround for Fluidsynth drop: call start() again after 250ms
                                android.os.Handler(android.os.Looper.getMainLooper()).postDelayed({
                                    try {
                                        song.resume()
                                    } catch (_: Exception) {}
                                }, 250)
                            }
                            val loopCount = if (viewModel.repeatMode == RepeatMode.SONG) 32767 else 0
                            song.setLoops(loopCount)                                          
                            viewModel.isPlaying = true
                            viewModel.currentTitle = file.nameWithoutExtension
                        } else {
                            viewModel.isPlaying = false
                            Toast.makeText(requireContext(), "Failed to start (err=$r)", Toast.LENGTH_SHORT).show()
                        }
                    } else {
                        viewModel.isPlaying = false
                        Toast.makeText(requireContext(), "Failed to get song object", Toast.LENGTH_SHORT).show()
                    }
                } else if (loadResult.isSound) {
                    // Audio files (WAV, MP3, FLAC, OGG, AIFF, AU) are loaded as Sound objects
                    val sound = loadResult.sound
                    if (sound != null) {
                        setCurrentSound(sound)
                        setCurrentSong(null) // Clear song reference
                        applyVolume()
                        val loopCount = if (viewModel.repeatMode == RepeatMode.SONG) 32767 else 0    
                        sound.setLoops(loopCount)
                        val r = sound.start()
                        if (r == 0) {
                            sound.setLoops(loopCount)
                            viewModel.isPlaying = true
                            viewModel.currentTitle = file.nameWithoutExtension
                            android.util.Log.d("HomeFragment", "Started ${loadResult.fileTypeString} sound: ${file.name}")
                        } else {
                            viewModel.isPlaying = false
                            Toast.makeText(requireContext(), "Failed to start sound (err=$r)", Toast.LENGTH_SHORT).show()
                        }
                    } else {
                        viewModel.isPlaying = false
                        Toast.makeText(requireContext(), "Failed to get sound object", Toast.LENGTH_SHORT).show()
                        loadResult.cleanup()
                    }
                } else {
                    viewModel.isPlaying = false
                    Toast.makeText(requireContext(), "Unknown file type", Toast.LENGTH_SHORT).show()
                }
            } else {
                viewModel.isPlaying = false
                Toast.makeText(requireContext(), "Failed to load (err=$status)", Toast.LENGTH_SHORT).show()
            }
        } catch (ex: Exception) {
            viewModel.isPlaying = false
            Toast.makeText(requireContext(), "Playback error: ${ex.localizedMessage}", Toast.LENGTH_SHORT).show()
        }
    }

    private fun applyVolume() {
        Mixer.setMasterVolumePercent(viewModel.volumePercent)
        currentSong?.setVolumePercent(viewModel.volumePercent)
        currentSound?.setVolumePercent(viewModel.volumePercent)
    }
    
    // Helper functions to handle both Song and Sound uniformly
    private fun isPlaybackPaused(): Boolean {
        if (!ensureMixerExists()) {
            return false
        }
        return currentSong?.isPaused() ?: currentSound?.isPaused() ?: false
    }
    
    private fun scheduleMixerCleanup() {
        mixerIdleJob?.cancel()

        // Suspend audio output without destroying the mixer.
        // This keeps the loaded bank(s) resident (fast track-to-track loads)
        // while stopping the hardware audio thread (low idle CPU).
        if (Mixer.getMixer() != null) {
            val r = Mixer.disengageAudio()
            android.util.Log.d("HomeFragment", "Mixer audio disengaged (r=$r)")
        }
    }

    private fun cancelMixerCleanup() {
        mixerIdleJob?.cancel()
        mixerIdleJob = null

        // If we previously suspended audio, resume it before starting playback.
        if (Mixer.getMixer() != null) {
            val r = Mixer.reengageAudio()
            android.util.Log.d("HomeFragment", "Mixer audio reengaged (r=$r)")
        }
    }

    private fun pausePlayback() {
        if (!ensureMixerExists()) {
            return
        }
        currentSong?.pause()
        currentSound?.pause()

        // Paused playback should not keep the audio thread running.
        scheduleMixerCleanup()
    }
    
    private fun resumePlayback() {
        if (!ensureMixerExists()) {
            return
        }
        cancelMixerCleanup()
        currentSong?.resume()
        currentSound?.resume()
    }
    
    private fun stopPlayback(delete: Boolean = true) {
        // Only try to stop if mixer exists
        if (Mixer.getMixer() != null) {
            currentSong?.stop(delete)
            currentSound?.stop(delete)
        }
        if (delete) {
            setCurrentSong(null)
            setCurrentSound(null)
            scheduleMixerCleanup()
        }
    }
    
    private fun getPlaybackPositionMs(): Int {
        if (!ensureMixerExists()) {
            return 0
        }
        return currentSong?.getPositionMs() ?: currentSound?.getPositionMs() ?: 0
    }
    
    private fun getPlaybackLengthMs(): Int {
        if (!ensureMixerExists()) {
            return 0
        }
        return currentSong?.getLengthMs() ?: currentSound?.getLengthMs() ?: 0
    }
    
    private fun seekPlaybackToMs(ms: Int) {
        if (!ensureMixerExists()) {
            return
        }
        if (hasActivePlayback() && currentSong?.isDone == false) {
            currentSong?.seekToMs(ms)
        } else if (hasActivePlayback() && currentSound?.isDone == false) {
            currentSound?.seekToMs(ms)
        }
    }
    
    // Public method to handle seeks from notification media controls
    fun handleSeekFromNotification(ms: Int) {
        seekPlaybackToMs(ms)
        viewModel.isDraggingSeekBar = false
        viewModel.currentPositionMs = ms

        // Push an immediate MediaSession/notification update so the system seek bar doesn't get stuck.
        val currentItem = viewModel.getCurrentItem()
        if (currentItem != null) {
            val folderName = viewModel.currentFolderPath?.let { path ->
                File(path).name
            } ?: "Unknown Folder"

            (activity as? MainActivity)?.updateServiceNotification(
                title = viewModel.currentTitle,
                artist = folderName,
                isPlaying = viewModel.isPlaying,
                hasNext = viewModel.hasNext(),
                hasPrevious = viewModel.hasPrevious(),
                currentPosition = ms.toLong(),
                duration = viewModel.totalDurationMs.toLong(),
                fileExtension = currentItem.file.extension
            )
        }
    }
    
    // Public methods for notification media controls
    fun handlePlayPauseFromNotification() {
        android.util.Log.d("HomeFragment", "handlePlayPauseFromNotification called")
        togglePlayPause()
    }
    
    fun handleNextFromNotification() {
        android.util.Log.d("HomeFragment", "handleNextFromNotification called")
        playNext()
    }
    
    fun handlePreviousFromNotification() {
        android.util.Log.d("HomeFragment", "handlePreviousFromNotification called")
        playPrevious()
    }
    
    fun handleCloseFromNotification() {
        android.util.Log.d("HomeFragment", "handleCloseFromNotification called")
        stopPlayback(delete = true)
        viewModel.isPlaying = false
        (activity as? MainActivity)?.playbackService?.stopForegroundService()
    }
    
    fun handleExternalFile(uri: Uri) {
        android.util.Log.d("HomeFragment", "handleExternalFile: $uri")
        lifecycleScope.launch {
            try {
                // Get file info from the URI
                val fileName = DocumentFile.fromSingleUri(requireContext(), uri)?.name ?: "Unknown"
                val extension = fileName.substringAfterLast('.', "").lowercase()
                
                // Check if it's a supported MIDI format
                val musicExtensions = getMusicExtensions(requireContext())
                if (!musicExtensions.contains(extension)) {
                    requireActivity().runOnUiThread {
                        Toast.makeText(requireContext(), "Unsupported file format: $extension", Toast.LENGTH_SHORT).show()
                    }
                    return@launch
                }
                
                // Try to determine the parent folder path from the URI
                val parentPath = when {
                    uri.scheme == "file" -> {
                        // Direct file:// URI - get parent directory
                        val file = File(uri.path ?: "")
                        file.parent
                    }
                    uri.scheme == "content" -> {
                        // Content URI - try to extract path from URI structure
                        // This is a best-effort approach for SAF URIs
                        val path = uri.path ?: ""
                        val uriString = uri.toString()
                        android.util.Log.d("HomeFragment", "Content URI path: $path, full URI: $uriString")
                        when {
                            path.contains("/document/primary:") -> {
                                // Primary storage - extract folder path
                                val afterPrimary = path.substringAfter("/document/primary:")
                                val folderPath = afterPrimary.substringBeforeLast('/', "")
                                if (folderPath.isNotEmpty()) {
                                    "/sdcard/$folderPath"
                                } else {
                                    "/sdcard"
                                }
                            }
                            path.contains("/storage/emulated/0/") -> {
                                // FX File Manager and similar - extract path directly
                                val extractedPath = path.substringAfter("/storage/emulated/0/")
                                val folderPath = extractedPath.substringBeforeLast('/', "")
                                if (folderPath.isNotEmpty()) {
                                    "/sdcard/$folderPath"
                                } else {
                                    "/sdcard"
                                }
                            }
                            else -> null
                        }
                    }
                    else -> null
                }
                
                requireActivity().runOnUiThread {
                    if (parentPath != null && File(parentPath).exists()) {
                        // Navigate to the parent folder first
                        android.util.Log.d("HomeFragment", "Loading folder: $parentPath")
                        viewModel.currentScreen = NavigationScreen.HOME
                        loadFolderContents(parentPath)
                        
                        // Wait a moment for folder to load, then find and play the file
                        lifecycleScope.launch {
                            delay(500) // Give time for folder to load
                            
                            // Find the file in the loaded folder
                            val matchingFile = viewModel.folderFiles.find { 
                                it.file.name == fileName
                            }
                            
                            if (matchingFile != null) {
                                android.util.Log.d("HomeFragment", "Found file in folder, playing: ${matchingFile.file.absolutePath}")
                                playFileFromBrowser(matchingFile.file)
                            } else {
                                // Fallback: create temp file and play directly
                                android.util.Log.d("HomeFragment", "File not found in folder, using temp file")
                                playExternalFileDirectly(uri, fileName)
                            }
                        }
                    } else {
                        // Can't determine parent folder, play file directly as temp
                        android.util.Log.d("HomeFragment", "Can't determine parent folder, playing directly")
                        playExternalFileDirectly(uri, fileName)
                    }
                }
            } catch (e: Exception) {
                android.util.Log.e("HomeFragment", "Error handling external file: ${e.message}")
                requireActivity().runOnUiThread {
                    Toast.makeText(requireContext(), "Error opening file: ${e.localizedMessage}", Toast.LENGTH_SHORT).show()
                }
            }
        }
    }
    
    private fun playExternalFileDirectly(uri: Uri, fileName: String) {
        lifecycleScope.launch {
            try {
                requireContext().contentResolver.openInputStream(uri)?.use { inputStream ->
                    // Read file into memory
                    val bytes = inputStream.readBytes()
                    val tempFile = File(requireContext().cacheDir, fileName)
                    tempFile.writeBytes(bytes)
                    
                    // Play the file directly
                    requireActivity().runOnUiThread {
                        stopPlayback(delete = true)
                        viewModel.clearPlaylist()
                        val item = PlaylistItem(tempFile)
                        viewModel.addToPlaylist(item)
                        viewModel.playAtIndex(0)
                        startPlayback(tempFile)
                    }
                }
            } catch (e: Exception) {
                android.util.Log.e("HomeFragment", "Error playing external file: ${e.message}")
                requireActivity().runOnUiThread {
                    Toast.makeText(requireContext(), "Error playing file: ${e.localizedMessage}", Toast.LENGTH_SHORT).show()
                }
            }
        }
    }
    
    private fun hasActivePlayback(): Boolean {
        if (!ensureMixerExists()) {
            return false
        }
        return currentSong != null || currentSound != null
    }
    
    private fun ensureMixerExists(): Boolean {
        // Check if mixer exists, recreate if needed
        if (Mixer.getMixer() == null) {
            val status = Mixer.create(requireActivity().assets, 44100, 2, 64, 8, 64)
            if (status != 0) {
                Toast.makeText(requireContext(), "Failed to recreate mixer: $status", Toast.LENGTH_SHORT).show()
                return false
            }
            
            // Set cache dir
            Mixer.setNativeCacheDir(requireContext().cacheDir.absolutePath)
            
            // Restore bank settings
            ensureBankIsLoaded()
            
            // Restore reverb and velocity curve settings
            try {
                val prefs = requireContext().getSharedPreferences("miniBAE_prefs", android.content.Context.MODE_PRIVATE)
                val reverbType = prefs.getInt("default_reverb", 1)
                val velocityCurve = prefs.getInt("velocity_curve", 1)
                Mixer.setDefaultReverb(reverbType)
                Mixer.setDefaultVelocityCurve(velocityCurve)
            } catch (_: Exception) {}
        }
        return true
    }

    private fun ensureBankIsLoaded() {
        var bankLoaded = false
        try {
            val prefs = requireContext().getSharedPreferences("miniBAE_prefs", android.content.Context.MODE_PRIVATE)
            val lastBankPath = prefs.getString("last_bank_path", null)
            
            if (!lastBankPath.isNullOrEmpty()) {
                if (lastBankPath == "__builtin__") {
                    if (loadBuiltInPatchesFromAssets(requireContext()) == 0) {
                        bankLoaded = true
                    }
                } else {
                    val bankFile = java.io.File(lastBankPath)
                    if (bankFile.exists()) {
                        // Avoid extremely large banks on Android (likely OOM / long stall).
                        if (bankFile.length() < BANK_SIZE_LIMIT_BYTES) {
                            // Avoid OOM on large SF2/DLS banks: load by path (native loads from disk)
                            if (Mixer.addBankFromFile(bankFile.absolutePath) == 0) {
                                bankLoaded = true
                            }
                        }
                    }
                }
            }
            
            // Fall back to built-in patches if no bank was loaded
            if (!bankLoaded) {
                loadBuiltInPatchesFromAssets(requireContext())
            }
        } catch (_: Exception) {
            // If restoration fails, use built-in patches
            try {
                loadBuiltInPatchesFromAssets(requireContext())
            } catch (_: Exception) {}
        }
    }

    private fun loadBuiltInPatchesFromAssets(ctx: Context): Int {
        return try {
            val data = ctx.assets.open("patches.hsb").use { it.readBytes() }
            Mixer.addBankFromMemory(data, "patches.hsb")
        } catch (_: Throwable) {
            -1
        }
    }
    
    private fun getMusicDir(): File? {
        return if (pickedFolderUri != null) {
            val docTree = DocumentFile.fromTreeUri(requireContext(), pickedFolderUri!!)
            if (docTree != null) {
                val targetDir = File(requireContext().cacheDir, "pickedFolder")
                if (!targetDir.exists()) targetDir.mkdirs()
                targetDir.listFiles()?.forEach { it.delete() }
                docTree.listFiles().forEach { docFile ->
                    val name = docFile.name ?: return@forEach
                    val targetFile = File(targetDir, name)
                    try {
                        requireContext().contentResolver.openInputStream(docFile.uri)?.use { input ->
                            targetFile.outputStream().use { output -> input.copyTo(output) }
                        }
                    } catch (_: Exception) { }
                }
                targetDir
            } else null
        } else null
    }

    private fun getMediaFiles(): List<File> {
        val musicDir = getMusicDir() ?: File("/sdcard/Music")
        val validExtensions = getMusicExtensions(requireContext())
        val map = LinkedHashMap<String, File>()
        if (musicDir.exists() && musicDir.isDirectory) {
            musicDir.listFiles { file -> file.isFile && file.extension.lowercase() in validExtensions }?.forEach { f ->
                map[f.absolutePath] = f
            }
        }
        return map.values.sortedBy { it.name.lowercase() }
    }
    
    private fun getBankFiles(folder: File): List<PlaylistItem> {
        val validExtensions = BANK_EXTENSIONS
        val allItems = folder.listFiles()?.let { allFiles ->
            val folders = allFiles.filter { it.isDirectory && it.canRead() }
                .sortedBy { it.name.lowercase() }
                .map { 
                    val item = PlaylistItem(it)
                    item.isFolder = true
                    item
                }
            
            val bankFiles = allFiles.filter { file -> 
                file.isFile && file.extension.lowercase() in validExtensions 
            }.sortedBy { it.name.lowercase() }
                .map { PlaylistItem(it) }
            
            folders + bankFiles
        } ?: emptyList()
        
        return allItems
    }
    
    private fun addAllMidiInDirectory() {
        val currentPath = viewModel.currentFolderPath ?: return
        val currentDir = File(currentPath)
        if (!currentDir.exists() || !currentDir.isDirectory) return
        
        val validExtensions = getMusicExtensions(requireContext())
        val midiFiles = currentDir.listFiles { file -> 
            file.isFile && file.extension.lowercase() in validExtensions 
        }?.sortedBy { it.name.lowercase() } ?: return
        
        if (midiFiles.isEmpty()) {
            Toast.makeText(requireContext(), "No MIDI files found in this directory", Toast.LENGTH_SHORT).show()
            return
        }
        
        midiFiles.forEach { file ->
            val item = PlaylistItem(file)
            if (!viewModel.playlist.any { it.file.absolutePath == file.absolutePath }) {
                viewModel.addToPlaylist(item)
            }
        }
        Toast.makeText(requireContext(), "Added ${midiFiles.size} files to playlist", Toast.LENGTH_SHORT).show()
    }
    
    private fun addAllMidiRecursively() {
        val currentPath = viewModel.currentFolderPath ?: return
        val currentDir = File(currentPath)
        if (!currentDir.exists() || !currentDir.isDirectory) return
        
        val validExtensions = getMusicExtensions(requireContext())
        val midiFiles = mutableListOf<File>()
        
        fun scanDirectory(dir: File) {
            dir.listFiles()?.forEach { file ->
                when {
                    file.isFile && file.extension.lowercase() in validExtensions -> {
                        midiFiles.add(file)
                    }
                    file.isDirectory -> {
                        scanDirectory(file)
                    }
                }
            }
        }
        
        scanDirectory(currentDir)
        midiFiles.sortBy { it.absolutePath.lowercase() }
        
        if (midiFiles.isEmpty()) {
            return
        }
        
        midiFiles.forEach { file ->
            val item = PlaylistItem(file)
            if (!viewModel.playlist.any { it.file.absolutePath == file.absolutePath }) {
                viewModel.addToPlaylist(item)
            }
        }
        Toast.makeText(requireContext(), "Added ${midiFiles.size} files to playlist (recursive scan)", Toast.LENGTH_SHORT).show()
    }
    
    private fun loadFolderContents(path: String) {
        // Check permissions before loading folder contents
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            if (!Environment.isExternalStorageManager()) {
                checkStoragePermissions()
                return
            }
        } else if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            if (androidx.core.content.ContextCompat.checkSelfPermission(
                requireContext(), 
                android.Manifest.permission.READ_EXTERNAL_STORAGE
            ) != android.content.pm.PackageManager.PERMISSION_GRANTED) {
                checkStoragePermissions()
                return
            }
        }
        
        loadingState?.value = true
        Thread {
            try {
                // Redirect /storage to / since /storage can't be listed
                val actualPath = if (path == "/storage") "/" else path
                
                // Special handling for root directory - show storage options
                if (actualPath == "/") {
                    activity?.runOnUiThread {
                        viewModel.folderFiles.clear()
                        
                        // Add Internal Storage (/sdcard)
                        val internalStorage = File("/sdcard")
                        if (internalStorage.exists() && internalStorage.isDirectory) {
                            val item = PlaylistItem(internalStorage)
                            item.isFolder = true
                            item.title = "Internal Storage"
                            viewModel.folderFiles.add(item)
                        }
                        
                        // Use Android's proper storage APIs to detect external storage
                        try {
                            val storageManager = requireContext().getSystemService(Context.STORAGE_SERVICE) as StorageManager
                            
                            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
                                // Use StorageVolume API for Android N+
                                val storageVolumes = storageManager.storageVolumes
                                storageVolumes.forEach { volume ->
                                    if (volume.isRemovable && volume.state == Environment.MEDIA_MOUNTED) {
                                        try {
                                            // Try to get the path using reflection for older Android versions
                                            val getPathMethod = volume.javaClass.getMethod("getPath")
                                            val volumePath = getPathMethod.invoke(volume) as String?
                                            
                                            if (volumePath != null) {
                                                val volumeFile = File(volumePath)
                                                if (volumeFile.exists() && volumeFile.canRead()) {
                                                    val item = PlaylistItem(volumeFile)
                                                    item.isFolder = true
                                                    item.title = volume.getDescription(requireContext())
                                                        ?: if (volume.isPrimary) "Primary Storage" else "External Storage"
                                                    viewModel.folderFiles.add(item)
                                                }
                                            }
                                        } catch (_: Exception) {
                                            // Reflection failed, skip this volume
                                        }
                                    }
                                }
                            }
                            
                            // Also try Environment.getExternalFilesDirs() approach
                            val externalDirs = requireContext().getExternalFilesDirs(null)
                            externalDirs?.forEachIndexed { index, dir ->
                                if (dir != null && index > 0) { // Skip index 0 (primary external storage)
                                    // Navigate up to get the root of the external storage
                                    var rootDir = dir
                                    while (rootDir.parentFile != null && rootDir.name != "Android") {
                                        rootDir = rootDir.parentFile!!
                                    }
                                    if (rootDir.parentFile != null) {
                                        rootDir = rootDir.parentFile!! // Go one level above Android folder
                                    }
                                    
                                    if (rootDir.exists() && rootDir.canRead() && 
                                        !viewModel.folderFiles.any { it.file.absolutePath == rootDir.absolutePath }) {
                                        try {
                                            val testFiles = rootDir.listFiles()
                                            if (testFiles != null && testFiles.isNotEmpty()) {
                                                val item = PlaylistItem(rootDir)
                                                item.isFolder = true
                                                item.title = when {
                                                    rootDir.absolutePath.contains("usb", ignoreCase = true) -> "USB Storage"
                                                    rootDir.absolutePath.matches(Regex(".*[0-9A-F]{4}-[0-9A-F]{4}.*")) -> "SD Card"
                                                    else -> "External Storage ${index}"
                                                }
                                                viewModel.folderFiles.add(item)
                                            }
                                        } catch (_: Exception) {
                                            // Skip inaccessible storage
                                        }
                                    }
                                }
                            }
                        } catch (_: Exception) {
                            android.util.Log.w("HomeFragment", "Failed to detect external storage using APIs")
                        }
                        
                        // Fallback: Try common external storage paths
                        val commonExternalPaths = listOf(
                            "/storage/sdcard1",
                            "/storage/extSdCard",
                            "/mnt/external_sd",
                            "/mnt/extSdCard"
                        )
                        
                        commonExternalPaths.forEach { path ->
                            val extStorage = File(path)
                            if (extStorage.exists() && extStorage.isDirectory && extStorage.canRead() &&
                                !viewModel.folderFiles.any { it.file.absolutePath == extStorage.absolutePath }) {
                                try {
                                    val testFiles = extStorage.listFiles()
                                    if (testFiles != null && testFiles.isNotEmpty()) {
                                        val item = PlaylistItem(extStorage)
                                        item.isFolder = true
                                        item.title = when {
                                            path.contains("sdcard1") || path.contains("extSdCard") || path.contains("external_sd") -> "SD Card"
                                            else -> "External Storage"
                                        }
                                        viewModel.folderFiles.add(item)
                                    }
                                } catch (_: Exception) {
                                    // Skip inaccessible storage
                                }
                            }
                        }
                        
                        // Add a refresh button at the top
                        val refreshItem = PlaylistItem(File("/"))
                        refreshItem.isFolder = false
                        refreshItem.title = "🔄 Refresh Storage List"
                        viewModel.folderFiles.add(0, refreshItem)
                        
                        viewModel.currentFolderPath = actualPath
                        viewModel.invalidateSearchCache() // Clear search cache when folder changes
                        
                        // Save current folder for next launch
                        val prefs = requireContext().getSharedPreferences("miniBAE_prefs", Context.MODE_PRIVATE)
                        prefs.edit().putString("current_folder_path", actualPath).apply()
                        
                        loadingState?.value = false
                    }
                    return@Thread
                }
                
                val folder = File(actualPath)
                if (!folder.exists() || !folder.isDirectory) {
                    activity?.runOnUiThread {
                        loadingState?.value = false
                        Toast.makeText(requireContext(), "Invalid folder: $actualPath", Toast.LENGTH_SHORT).show()
                    }
                    return@Thread
                }
                
                val validExtensions = getMusicExtensions(requireContext())
                val allItems = folder.listFiles()?.let { allFiles ->
                    val folders = allFiles.filter { it.isDirectory && it.canRead() }
                        .sortedBy { it.name.lowercase() }
                        .map { 
                            val item = PlaylistItem(it)
                            item.isFolder = true
                            item
                        }
                    
                    val musicFiles = allFiles.filter { file -> 
                        file.isFile && file.extension.lowercase() in validExtensions 
                    }.sortedBy { it.name.lowercase() }
                        .map { PlaylistItem(it) }
                    
                    folders + musicFiles
                } ?: emptyList()
                
                activity?.runOnUiThread {
                    // Update folder files list (separate from playlist)
                    viewModel.folderFiles.clear()
                    viewModel.folderFiles.addAll(allItems)
                    
                    viewModel.currentFolderPath = actualPath
                    viewModel.invalidateSearchCache() // Clear search cache when folder changes
                    
                    // Save current folder for next launch
                    val prefs = requireContext().getSharedPreferences("miniBAE_prefs", Context.MODE_PRIVATE)
                    prefs.edit().putString("current_folder_path", actualPath).apply()
                    
                    loadingState?.value = false
                }
            } catch (ex: Exception) {
                activity?.runOnUiThread {
                    loadingState?.value = false
                    Toast.makeText(requireContext(), "Error loading folder: ${ex.message}", Toast.LENGTH_SHORT).show()
                }
            }
        }.start()
    }
    
    private fun navigateToFolder(path: String) {
        // Handle special refresh button
        if (path == "/" && viewModel.currentFolderPath == "/") {
            // Refresh storage list
            checkStoragePermissions()
            loadFolderContents("/")
        } else {
            loadFolderContents(path)
        }
    }
    
    private fun navigateToBankFolder(path: String) {
        bankBrowserLoading.value = true
        Thread {
            try {
                // Redirect /storage to / since /storage can't be listed
                val actualPath = if (path == "/storage") "/" else path
                
                // Special handling for root directory - show storage options
                if (actualPath == "/") {
                    activity?.runOnUiThread {
                        bankBrowserFiles.clear()
                        
                        // Add Internal Storage (/sdcard)
                        val internalStorage = File("/sdcard")
                        if (internalStorage.exists() && internalStorage.isDirectory) {
                            val item = PlaylistItem(internalStorage)
                            item.isFolder = true
                            item.title = "Internal Storage"
                            bankBrowserFiles.add(item)
                        }
                        
                        // Use Android's proper storage APIs to detect external storage
                        try {
                            val storageManager = requireContext().getSystemService(Context.STORAGE_SERVICE) as StorageManager
                            
                            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
                                // Use StorageVolume API for Android N+
                                val storageVolumes = storageManager.storageVolumes
                                storageVolumes.forEach { volume ->
                                    if (volume.isRemovable && volume.state == Environment.MEDIA_MOUNTED) {
                                        try {
                                            // Try to get the path using reflection for older Android versions
                                            val getPathMethod = volume.javaClass.getMethod("getPath")
                                            val volumePath = getPathMethod.invoke(volume) as String?
                                            
                                            if (volumePath != null) {
                                                val volumeFile = File(volumePath)
                                                if (volumeFile.exists() && volumeFile.canRead()) {
                                                    val item = PlaylistItem(volumeFile)
                                                    item.isFolder = true
                                                    item.title = volume.getDescription(requireContext())
                                                        ?: if (volume.isPrimary) "Primary Storage" else "External Storage"
                                                    bankBrowserFiles.add(item)
                                                }
                                            }
                                        } catch (_: Exception) {
                                            // Reflection failed, skip this volume
                                        }
                                    }
                                }
                            }
                            
                            // Also try Environment.getExternalFilesDirs() approach
                            val externalDirs = requireContext().getExternalFilesDirs(null)
                            externalDirs?.forEachIndexed { index, dir ->
                                if (dir != null && index > 0) { // Skip index 0 (primary external storage)
                                    // Navigate up to get the root of the external storage
                                    var rootDir = dir
                                    while (rootDir.parentFile != null && rootDir.name != "Android") {
                                        rootDir = rootDir.parentFile!!
                                    }
                                    if (rootDir.parentFile != null) {
                                        rootDir = rootDir.parentFile!! // Go one level above Android folder
                                    }
                                    
                                    if (rootDir.exists() && rootDir.canRead() && 
                                        !bankBrowserFiles.any { it.file.absolutePath == rootDir.absolutePath }) {
                                        try {
                                            val testFiles = rootDir.listFiles()
                                            if (testFiles != null && testFiles.isNotEmpty()) {
                                                val item = PlaylistItem(rootDir)
                                                item.isFolder = true
                                                item.title = when {
                                                    rootDir.absolutePath.contains("usb", ignoreCase = true) -> "USB Storage"
                                                    rootDir.absolutePath.matches(Regex(".*[0-9A-F]{4}-[0-9A-F]{4}.*")) -> "SD Card"
                                                    else -> "External Storage ${index}"
                                                }
                                                bankBrowserFiles.add(item)
                                            }
                                        } catch (_: Exception) {
                                            // Skip inaccessible storage
                                        }
                                    }
                                }
                            }
                        } catch (_: Exception) {
                            android.util.Log.w("HomeFragment", "Failed to detect external storage using APIs")
                        }
                        
                        // Fallback: Try common external storage paths
                        val commonExternalPaths = listOf(
                            "/storage/sdcard1",
                            "/storage/extSdCard",
                            "/mnt/external_sd",
                            "/mnt/extSdCard"
                        )
                        
                        commonExternalPaths.forEach { path ->
                            val extStorage = File(path)
                            if (extStorage.exists() && extStorage.isDirectory && extStorage.canRead() &&
                                !bankBrowserFiles.any { it.file.absolutePath == extStorage.absolutePath }) {
                                try {
                                    val testFiles = extStorage.listFiles()
                                    if (testFiles != null && testFiles.isNotEmpty()) {
                                        val item = PlaylistItem(extStorage)
                                        item.isFolder = true
                                        item.title = when {
                                            path.contains("sdcard1") || path.contains("extSdCard") || path.contains("external_sd") -> "SD Card"
                                            else -> "External Storage"
                                        }
                                        bankBrowserFiles.add(item)
                                    }
                                } catch (_: Exception) {
                                    // Skip inaccessible storage
                                }
                            }
                        }
                        
                        bankBrowserPath.value = actualPath
                        bankBrowserLoading.value = false
                    }
                    return@Thread
                }
                
                val folder = File(actualPath)
                if (!folder.exists() || !folder.isDirectory) {
                    activity?.runOnUiThread {
                        bankBrowserLoading.value = false
                        Toast.makeText(requireContext(), "Invalid folder: $actualPath", Toast.LENGTH_SHORT).show()
                    }
                    return@Thread
                }
                
                val items = getBankFiles(folder)
                
                activity?.runOnUiThread {
                    bankBrowserFiles.clear()
                    bankBrowserFiles.addAll(items)
                    bankBrowserPath.value = actualPath
                    bankBrowserLoading.value = false
                }
            } catch (ex: Exception) {
                activity?.runOnUiThread {
                    bankBrowserLoading.value = false
                    Toast.makeText(requireContext(), "Error loading folder: ${ex.message}", Toast.LENGTH_SHORT).show()
                }
            }
        }.start()
    }
    
    private fun loadBankFromFile(file: File) {
        if (!bankSwapInProgress.compareAndSet(false, true)) {
            // Avoid re-entrant bank loads if the user taps too quickly.
            postToMain {
                context?.let { Toast.makeText(it, "Bank swap already in progress", Toast.LENGTH_SHORT).show() }
            }
            return
        }

        val bankBytes = runCatching { file.length() }.getOrDefault(0L)
        if (bankBytes >= BANK_SIZE_LIMIT_BYTES) {
            showBankBrowser.value = false
            postToMain {
                isLoadingBank.value = false
                context?.let {
                    Toast.makeText(it, "Bank too large to load (>= 4 GB)", Toast.LENGTH_SHORT).show()
                }
            }
            bankSwapInProgress.set(false)
            return
        }

        val wasPlaying = viewModel.isPlaying
        val hadSong = currentSong != null
        // Preserve the playback intent across mixer teardown/recreate.
        (activity as? MainActivity)?.pendingBankReloadResume = wasPlaying

        // Preserve the exact playback position across reloads.
        (activity as? MainActivity)?.pendingBankReloadPositionMs =
            try {
                currentSong?.getPositionMs() ?: currentSound?.getPositionMs() ?: viewModel.currentPositionMs
            } catch (_: Exception) {
                viewModel.currentPositionMs
            }
        // Pause before bank load to avoid glitchy audio and to keep state stable during reload.
        if (wasPlaying && Mixer.getMixer() != null) {
            postToMain {
                try {
                    pausePlayback()
                } catch (_: Exception) {
                }
            }
        }

        isLoadingBank.value = true
        showBankBrowser.value = false
        Thread {
            var loadStatus: Int? = null
            try {
                val originalName = file.name
                val prefs = requireContext().getSharedPreferences("miniBAE_prefs", Context.MODE_PRIVATE)
                
                // Save the parent directory for next time the bank browser is opened
                val parentPath = file.parent ?: "/sdcard"
                prefs.edit().putString("last_bank_browser_path", parentPath).apply()
                
                // If mixer doesn't exist, just save the path for lazy loading
                if (Mixer.getMixer() == null) {
                    prefs.edit().putString("last_bank_path", file.absolutePath).apply()
                    postToMain {
                        currentBankName.value = originalName
                        isLoadingBank.value = false
                    }
                    bankSwapInProgress.set(false)
                    return@Thread
                }
                
                // Mixer exists, load bank now
                val isHsbTarget = file.extension.equals("hsb", ignoreCase = true)

                // HSB bank swapping requires a full mixer teardown/reopen on Android.
                // Only do this when a Song is active (banks don't affect Sound playback).
                if (isHsbTarget && currentSong != null) {
                    postToMain {
                        try {
                            viewModel.isPlaying = false
                            currentSong?.close()
                            currentSound?.stop(true)
                            setCurrentSong(null)
                            setCurrentSound(null)
                        } catch (_: Exception) {
                        }
                    }

                    val status = try {
                        runOnMainSync {
                            try {
                                Mixer.delete()
                            } catch (_: Exception) {
                            }
                            val s = Mixer.create(requireActivity().assets, 44100, 2, 64, 8, 64)
                            if (s == 0) {
                                Mixer.setNativeCacheDir(requireContext().cacheDir.absolutePath)
                                try {
                                    Mixer.setDefaultReverb(reverbType.value)
                                    Mixer.setDefaultVelocityCurve(velocityCurve.value)
                                } catch (_: Exception) {
                                }
                                try {
                                    applyVolume()
                                } catch (_: Exception) {
                                }
                                try {
                                    Mixer.reengageAudio()
                                } catch (_: Exception) {
                                }
                            }
                            s
                        }
                    } catch (_: Exception) {
                        -1
                    }

                    if (status != 0) {
                        loadStatus = status
                        postToMain {
                            currentBankName.value = "Failed to recreate mixer"
                            context?.let { Toast.makeText(it, "Failed to recreate mixer (err=$status)", Toast.LENGTH_SHORT).show() }
                        }
                        return@Thread
                    }
                }

                // Avoid OOM on large SF2/DLS banks: load by path (native loads from disk)
                val r = Mixer.addBankFromFile(file.absolutePath)
                loadStatus = r
                
                if (r == 0) {
                    prefs.edit().putString("last_bank_path", file.absolutePath).apply()
                    
                    postToMain {
                        currentBankName.value = originalName
                        context?.let { Toast.makeText(it, "Loaded: $originalName", Toast.LENGTH_SHORT).show() }
                    }
                    
                        // Hot-swap: only Songs need reload (Sounds don't use banks)
                        // For HSB swaps we may have torn down the mixer and cleared currentSong,
                        // so use the pre-swap state.
                        if (hadSong) {
                            // If playback was active, force audio thread on before reload/resume.
                            if (wasPlaying) {
                                try {
                                    Mixer.reengageAudio()
                                } catch (_: Exception) {
                                }
                            }
                            reloadCurrentSongForBankSwap()
                        }
                } else {
                    postToMain {
                        currentBankName.value = "Failed to load: $originalName"
                        context?.let { Toast.makeText(it, "Failed to load bank (err=$r)", Toast.LENGTH_SHORT).show() }
                    }
                }
            } catch (ex: Exception) {
                postToMain {
                    context?.let { Toast.makeText(it, "Error: ${ex.message}", Toast.LENGTH_SHORT).show() }
                }
            } finally {
                postToMain {
                    // If bank load succeeded and we reloaded a MIDI Song, MainActivity decides whether to
                    // keep playing based on viewModel.isPlaying. For audio Sounds (or failures), resume here.
                    val shouldResumeHere = wasPlaying && (currentSound != null || (loadStatus != null && loadStatus != 0))
                    if (shouldResumeHere && Mixer.getMixer() != null && isAdded) {
                        try {
                            resumePlayback()
                        } catch (_: Exception) {
                        }
                    }
                    isLoadingBank.value = false
                }
                bankSwapInProgress.set(false)
            }
        }.start()
    }
    
    private fun loadBuiltInPatches() {
        if (!bankSwapInProgress.compareAndSet(false, true)) {
            postToMain {
                context?.let { Toast.makeText(it, "Bank swap already in progress", Toast.LENGTH_SHORT).show() }
            }
            return
        }

        val wasPlaying = viewModel.isPlaying
        val hadSong = currentSong != null
        // Built-in patches may trigger a mixer teardown; preserve whether we should resume.
        (activity as? MainActivity)?.pendingBankReloadResume = wasPlaying

        // Preserve the exact playback position across reloads.
        (activity as? MainActivity)?.pendingBankReloadPositionMs =
            try {
                currentSong?.getPositionMs() ?: currentSound?.getPositionMs() ?: viewModel.currentPositionMs
            } catch (_: Exception) {
                viewModel.currentPositionMs
            }
        if (wasPlaying && Mixer.getMixer() != null) {
            postToMain {
                try {
                    pausePlayback()
                } catch (_: Exception) {
                }
            }
        }

        isLoadingBank.value = true
        Thread {
            val prefs = requireContext().getSharedPreferences("miniBAE_prefs", Context.MODE_PRIVATE)
            var loadStatus: Int? = null
            
            // If mixer doesn't exist, just save the preference for lazy loading
            if (Mixer.getMixer() == null) {
                prefs.edit().putString("last_bank_path", "__builtin__").apply()
                postToMain {
                    currentBankName.value = "Built-in patches"
                    context?.let { Toast.makeText(it, "Built-in patches will load when playback starts", Toast.LENGTH_SHORT).show() }
                    isLoadingBank.value = false
                }
                bankSwapInProgress.set(false)
                return@Thread
            }
            
            // Mixer exists, load patches now

            // Built-in patches are an HSB bank; swapping to HSB needs a full mixer teardown/reopen.
            // Only do this when a Song is active (banks don't affect Sound playback).
            if (currentSong != null) {
                postToMain {
                    try {
                        viewModel.isPlaying = false
                        currentSong?.close()
                        currentSound?.stop(true)
                        setCurrentSong(null)
                        setCurrentSound(null)
                    } catch (_: Exception) {
                    }
                }

                val status = try {
                    runOnMainSync {
                        try {
                            Mixer.delete()
                        } catch (_: Exception) {
                        }
                        val s = Mixer.create(requireActivity().assets, 44100, 2, 64, 8, 64)
                        if (s == 0) {
                            Mixer.setNativeCacheDir(requireContext().cacheDir.absolutePath)
                            try {
                                Mixer.setDefaultReverb(reverbType.value)
                                Mixer.setDefaultVelocityCurve(velocityCurve.value)
                            } catch (_: Exception) {
                            }
                            try {
                                applyVolume()
                            } catch (_: Exception) {
                            }
                            try {
                                Mixer.reengageAudio()
                            } catch (_: Exception) {
                            }
                        }
                        s
                    }
                } catch (_: Exception) {
                    -1
                }

                if (status != 0) {
                    loadStatus = status
                    postToMain {
                        currentBankName.value = "Failed to recreate mixer"
                        context?.let { Toast.makeText(it, "Failed to recreate mixer (err=$status)", Toast.LENGTH_SHORT).show() }
                        isLoadingBank.value = false
                    }
                    return@Thread
                }
            }

            val r = loadBuiltInPatchesFromAssets(requireContext())
            loadStatus = r
            postToMain {
                if (r == 0) {
                    val friendly = Mixer.getBankFriendlyName()
                    currentBankName.value = friendly ?: "Built-in patches"
                    prefs.edit().putString("last_bank_path", "__builtin__").apply()
                    
                    // Hot-swap: only Songs need reload (Sounds don't use banks)
                    // Built-in patches are HSB; we may have cleared currentSong during teardown.
                    if (hadSong) {
                        if (wasPlaying) {
                            try {
                                Mixer.reengageAudio()
                            } catch (_: Exception) {
                            }
                        }
                        reloadCurrentSongForBankSwap()
                    }
                    
                    context?.let { Toast.makeText(it, "Loaded built-in patches", Toast.LENGTH_SHORT).show() }
                } else {
                    currentBankName.value = "Failed to load built-in"
                    context?.let { Toast.makeText(it, "Failed to load built-in patches (err=$r)", Toast.LENGTH_SHORT).show() }
                }

                val shouldResumeHere = wasPlaying && (currentSound != null || (loadStatus != null && loadStatus != 0))
                if (shouldResumeHere && Mixer.getMixer() != null && isAdded) {
                    try {
                        resumePlayback()
                    } catch (_: Exception) {
                    }
                }
                isLoadingBank.value = false
            }

            bankSwapInProgress.set(false)
        }.start()
    }
    
    private fun exportToFile(uri: Uri) {
        // Export is only for Songs (MIDI/RMF), not Sound files (which are already audio)
        if (currentSound != null) {
            Toast.makeText(requireContext(), "Export is for MIDI/RMF files only. Sound files are already audio.", Toast.LENGTH_SHORT).show()
            return
        }
        
        // Set exporting state on UI thread
        activity?.runOnUiThread {
            isExporting.value = true
            exportStatus.value = "Preparing export..."
        }
        
        Thread {
            try {
                val currentItem = viewModel.getCurrentItem()
                if (currentItem == null) {
                    activity?.runOnUiThread {
                        Toast.makeText(requireContext(), "No song to export", Toast.LENGTH_SHORT).show()
                    }
                    return@Thread
                }
                
                // Get export parameters
                val codec = exportCodec.value
                val fileType = when (codec) {
                    2 -> Mixer.BAE_VORBIS_TYPE
                    3 -> Mixer.BAE_FLAC_TYPE
                    else -> Mixer.BAE_WAVE_TYPE
                }
                val compressionType = when (codec) {
                    2 -> Mixer.BAE_COMPRESSION_VORBIS_128
                    3 -> Mixer.BAE_COMPRESSION_LOSSLESS
                    else -> Mixer.BAE_COMPRESSION_NONE
                }
                
                val ext = when (codec) {
                    2 -> "ogg"
                    3 -> "flac"
                    else -> "wav"
                }
                
                // Create a temporary file path for export
                val tempFile = File(requireContext().cacheDir, "export_temp.$ext")
                
                // Ensure the temp file can be created
                try {
                    if (tempFile.exists()) {
                        tempFile.delete()
                    }
                    tempFile.createNewFile()
                } catch (e: Exception) {
                    android.util.Log.e("HomeFragment", "Error creating temp file: ${e.message}")
                    activity?.runOnUiThread {
                        Toast.makeText(requireContext(), "Error creating temporary export file", Toast.LENGTH_SHORT).show()
                    }
                    return@Thread
                }
                
                android.util.Log.d("HomeFragment", "Starting export to: ${tempFile.absolutePath}, fileType: $fileType, compressionType: $compressionType")
                
                // We must use the existing mixer because creating a new mixer without audio 
                // engagement causes crashes in platform-specific code (BAE_GetAudioByteBufferSize)
                // that expects hardware to be initialized.
                
                // Ensure we have a valid song loaded
                if (currentSong == null) {
                    android.util.Log.e("HomeFragment", "No song currently loaded")
                    activity?.runOnUiThread {
                        Toast.makeText(requireContext(), "No song loaded for export", Toast.LENGTH_SHORT).show()
                    }
                    return@Thread
                }
                
                // Get song length BEFORE stopping (some implementations need the song to be active)
                val lengthMs = currentSong?.getLengthMs() ?: 0
                if (lengthMs <= 0) {
                    android.util.Log.e("HomeFragment", "Invalid song length: $lengthMs ms")
                    activity?.runOnUiThread {
                        Toast.makeText(requireContext(), "Cannot determine song length", Toast.LENGTH_SHORT).show()
                    }
                    return@Thread
                }
                
                android.util.Log.d("HomeFragment", "Song length: $lengthMs ms")
                
                // Save current playback state
                val wasPlaying = viewModel.isPlaying
                val savedPosition = currentSong?.getPositionMs() ?: 0
                
                android.util.Log.d("HomeFragment", "Saved playback state: wasPlaying=$wasPlaying, position=$savedPosition ms")
                
                // Stop current playback and seek to start for export
                currentSong?.stop(false)
                currentSong?.seekToMs(0)
                viewModel.isPlaying = false
                
                android.util.Log.d("HomeFragment", "Current song stopped and seeked to start for export")
                    
                try {
                    // CORRECT ORDER: Start export FIRST, then start song
                    // Start export to temporary file using the global mixer
                    val r = Mixer.getMixer()?.startOutputToFile(tempFile.absolutePath, fileType, compressionType) ?: -1
                    if (r == 0) {
                        android.util.Log.d("HomeFragment", "Export started successfully, length: $lengthMs ms")
                        
                        // Stop and rewind again to ensure clean state
                        currentSong?.stop(false)
                        currentSong?.seekToMs(0)
                        
                        // Preroll then start song playback for export
                        val prerollResult = currentSong?.preroll() ?: -1
                        if (prerollResult != 0) {
                            throw Exception("Failed to preroll song for export (err=$prerollResult)")
                        }
                        android.util.Log.d("HomeFragment", "Song prerolled for export")
                        
                        val startResult = currentSong?.start() ?: -1
                        if (startResult != 0) {
                            throw Exception("Failed to start song for export (err=$startResult)")
                        }
                        
                        android.util.Log.d("HomeFragment", "Song started, letting first audio callback settle...")
                        
                        // CRITICAL: Give the mixer/song a moment to actually start processing
                        // The first audio callback needs to happen before export will work
                        Thread.sleep(100) // 100ms should be enough for initial scheduling
                        
                        android.util.Log.d("HomeFragment", "Priming export pipeline...")
                        
                        // CRITICAL: Prime the export pipeline (matching gui_export.c behavior)
                        // Service several times to ensure audio engine starts processing
                        for (prime in 0 until 8) {
                            val primeResult = Mixer.getMixer()?.serviceOutputToFile() ?: -1
                            if (primeResult != 0) {
                                throw Exception("Export priming failed (err=$primeResult)")
                            }
                            Thread.sleep(1) // Small delay between primes
                        }
                        
                        // Keep priming while song reports done (hasn't started processing yet)
                        var primeCount = 0
                        val maxPrimes = 32
                        while (primeCount < maxPrimes) {
                            val stillDone = currentSong?.isDone() ?: false
                            if (!stillDone) break // Song is now active
                            
                            val primeResult = Mixer.getMixer()?.serviceOutputToFile() ?: -1
                            if (primeResult != 0) {
                                throw Exception("Export priming failed (err=$primeResult)")
                            }
                            Thread.sleep(2) // 2ms between priming attempts
                            primeCount++
                        }
                        
                        android.util.Log.d("HomeFragment", "Export pipeline primed after ${primeCount + 8} service calls")
                        
                        activity?.runOnUiThread {
                            exportStatus.value = "Exporting audio... 0%"
                        }
                        
                        // Service the export loop - check BAESong_IsDone(), not position
                        // Export runs faster than real-time, so position won't track normally
                        var isDone = false
                        var iterCount = 0
                        val maxIterations = lengthMs * 10 // Safety limit: assume ~100 iters per second of song
                        var lastProgressPercent = 0
                        
                        while (!isDone && iterCount < maxIterations) {
                            try {
                                // Service the export (processes audio as fast as possible)
                                val serviceResult = Mixer.getMixer()?.serviceOutputToFile() ?: -1
                                if (serviceResult != 0) {
                                    android.util.Log.e("HomeFragment", "serviceOutputToFile error: $serviceResult")
                                    break
                                }
                                
                                // Check if song is done (end of MIDI events reached)
                                isDone = currentSong?.isDone() ?: false
                                
                                // Update progress every 5% to avoid excessive UI updates
                                if (lengthMs > 0) {
                                    val positionMs = currentSong?.getPositionMs() ?: 0
                                    val progressPercent = ((positionMs * 100) / lengthMs).coerceIn(0, 100)
                                    
                                    if (progressPercent >= lastProgressPercent + 5) {
                                        lastProgressPercent = progressPercent
                                        activity?.runOnUiThread {
                                            exportStatus.value = "Exporting audio... $progressPercent%"
                                        }
                                    }
                                }
                                
                                iterCount++
                                
                                // Small yield to prevent tight loop from blocking everything
                                if (iterCount % 100 == 0) {
                                    Thread.sleep(1)
                                }
                            } catch (e: Exception) {
                                android.util.Log.e("HomeFragment", "Error during export service: ${e.message}")
                                break
                            }
                        }
                        
                        if (iterCount >= maxIterations) {
                            android.util.Log.w("HomeFragment", "Export hit iteration limit (possible stall)")
                        }
                        
                        android.util.Log.d("HomeFragment", "Export loop completed after $iterCount iterations, isDone=$isDone")
                        
                        // Drain period: service a few more times to capture trailing audio
                        android.util.Log.d("HomeFragment", "Draining export buffer...")
                        for (drain in 0 until 20) {
                            Mixer.getMixer()?.serviceOutputToFile()
                            Thread.sleep(5)
                        }
                        
                        android.util.Log.d("HomeFragment", "Export playback completed")
                        
                    } else {
                        throw Exception("Failed to start output to file (err=$r)")
                    }
                    
                } catch (e: Exception) {
                    android.util.Log.e("HomeFragment", "Error during export: ${e.message}")
                    throw e
                } finally {
                    // Clean up export
                    try {
                        Mixer.getMixer()?.stopOutputToFile()
                        android.util.Log.d("HomeFragment", "Export stopped")
                    } catch (e: Exception) {
                        android.util.Log.e("HomeFragment", "Error stopping export: ${e.message}")
                    }
                    
                    // Stop the song and restore position if needed
                    try {
                        currentSong?.stop(false)
                        android.util.Log.d("HomeFragment", "Song stopped after export")
                    } catch (e: Exception) {
                        android.util.Log.e("HomeFragment", "Error stopping song: ${e.message}")
                    }
                    
                    // Restore playback if it was playing before
                    if (wasPlaying) {
                        try {
                            android.util.Log.d("HomeFragment", "Restoring playback...")
                            activity?.runOnUiThread {
                                // Seek back to saved position
                                currentSong?.seekToMs(savedPosition)
                                viewModel.currentPositionMs = savedPosition
                                currentSong?.start()
                                viewModel.isPlaying = true
                            }
                        } catch (e: Exception) {
                            android.util.Log.e("HomeFragment", "Error restoring playback: ${e.message}")
                        }
                    }
                }
                
                // Check if export file was created and has content
                if (!tempFile.exists() || tempFile.length() == 0L) {
                    throw Exception("Export file was not created or is empty (size: ${tempFile.length()} bytes)")
                }
                
                android.util.Log.d("HomeFragment", "Export file created successfully, size: ${tempFile.length()} bytes")
                
                activity?.runOnUiThread {
                    exportStatus.value = "Finalizing export..."
                }
                
                // Copy temp file to user's chosen location
                var bytesCopied = 0L
                requireContext().contentResolver.openOutputStream(uri)?.use { outputStream ->
                    tempFile.inputStream().use { inputStream ->
                        bytesCopied = inputStream.copyTo(outputStream)
                    }
                }
                
                android.util.Log.d("HomeFragment", "Copied $bytesCopied bytes to final destination")
                
                // Clean up temp file
                tempFile.delete()
                
                activity?.runOnUiThread {
                    isExporting.value = false
                    exportStatus.value = ""
                    Toast.makeText(requireContext(), "Export completed successfully (${bytesCopied} bytes)", Toast.LENGTH_SHORT).show()
                }
                
            } catch (ex: Exception) {
                android.util.Log.e("HomeFragment", "Export error: ${ex.message}")
                activity?.runOnUiThread {
                    isExporting.value = false
                    exportStatus.value = ""
                    Toast.makeText(requireContext(), "Export error: ${ex.message}", Toast.LENGTH_SHORT).show()
                }
            }
        }.start()
    }
}

private fun saveFavorites(context: Context, favorites: List<String>) {
    try {
        val prefs = context.getSharedPreferences("miniBAE_prefs", Context.MODE_PRIVATE)
        val json = favorites.joinToString("|||")
        prefs.edit().putString("savedFavorites", json).apply()
    } catch (ex: Exception) {
        android.util.Log.e("HomeFragment", "Failed to save favorites: ${ex.message}")
    }
}

private fun syncVirtualPlaylistToFavoritesOrder(viewModel: MusicPlayerViewModel) {
    if (viewModel.currentScreen != NavigationScreen.FAVORITES) return

    if (viewModel.playlist.isEmpty() || viewModel.favorites.isEmpty()) return

    val order = viewModel.favorites.withIndex().associate { it.value to it.index }
    val existing = viewModel.playlist.toList()
    val originalIndex = existing.withIndex().associate { it.value.path to it.index }

    // Only reorder if the playlist actually contains any favorites.
    if (existing.none { order.containsKey(it.path) }) return

    val reordered = existing.sortedWith(
        compareBy<PlaylistItem>({ order[it.path] ?: Int.MAX_VALUE }, { originalIndex[it.path] ?: Int.MAX_VALUE })
    )

    if (reordered == existing) return
    viewModel.replacePlaylistPreservingCurrent(reordered)
}

@Composable
fun NewMusicPlayerScreen(
    viewModel: MusicPlayerViewModel,
    loading: Boolean,
    onPlayPause: () -> Unit,
    onNext: () -> Unit,
    onPrevious: () -> Unit,
    onSeek: (Int) -> Unit,
    onStartDrag: () -> Unit,
    onDrag: (Int) -> Unit,
    onVolumeChange: (Int) -> Unit,
    onPlaylistItemClick: (File) -> Unit,
    onToggleFavorite: (String) -> Unit,
    onAddFolder: () -> Unit,
    onAddFile: () -> Unit,
    onNavigate: (NavigationScreen) -> Unit,
    onShufflePlay: () -> Unit,
    onNavigateToFolder: (String) -> Unit,
    onAddToPlaylist: (File) -> Unit,
    bankName: String,
    isLoadingBank: Boolean,
    isExporting: Boolean,
    exportStatus: String,
    reverbType: Int,
    velocityCurve: Int,
    exportCodec: Int,
    searchResultLimit: Int,
    enabledExtensions: Set<String>,
    onLoadBuiltin: () -> Unit,
    onReverbChange: (Int) -> Unit,
    onCurveChange: (Int) -> Unit,
    onExportCodecChange: (Int) -> Unit,
    onSearchLimitChange: (Int) -> Unit,
    onExtensionEnabledChange: (String, Boolean) -> Unit,
    onExportRequest: (String, Int) -> Unit,
    onRefreshStorage: () -> Unit,
    onRepeatModeChange: () -> Unit,
    onAddAllMidi: () -> Unit,
    onAddAllMidiRecursive: () -> Unit,
    showBankBrowser: Boolean,
    bankBrowserPath: String,
    bankBrowserFiles: List<PlaylistItem>,
    bankBrowserLoading: Boolean,
    onBrowseBanks: () -> Unit,
    onBankBrowserNavigate: (String) -> Unit,
    onBankBrowserSelect: (File) -> Unit,
    onBankBrowserClose: () -> Unit
) {
    // State for delete confirmation dialog
    var showDeleteDialog by remember { mutableStateOf(false) }
    val context = LocalContext.current

    Box(modifier = Modifier.fillMaxSize()) {
        Scaffold(
            modifier = Modifier.systemBarsPadding(),
            topBar = {
            // Header with folder navigation
            TopAppBar(
                title = {
                    Column {
                        // Dynamic title based on current screen or bank browser
                        val titleText = if (showBankBrowser) {
                            "Bank Select"
                        } else if (viewModel.showFullPlayer) {
                            "Now Playing"
                        } else {
                            when (viewModel.currentScreen) {
                                NavigationScreen.HOME -> "Home"
                                NavigationScreen.SEARCH -> "Search"
                                NavigationScreen.FAVORITES -> "Favorites"
                                NavigationScreen.SETTINGS -> "Settings"
                                NavigationScreen.FILE_TYPES -> "Settings"
                            }
                        }
                        Text(
                            text = titleText,
                            fontSize = 20.sp,
                            fontWeight = FontWeight.Bold
                        )
                        
                        // Dynamic subtitle based on current screen or bank browser
                        val subtitleText = if (showBankBrowser) {
                            "Choose a sound bank"
                        } else if (viewModel.showFullPlayer) {
                            viewModel.getCurrentItem()?.title ?: ""
                        } else {
                            when (viewModel.currentScreen) {
                                NavigationScreen.HOME -> {
                                    viewModel.currentFolderPath ?: "No folder selected"
                                }
                                NavigationScreen.SEARCH -> {
                                    val searchResults by viewModel.searchResults.collectAsState()
                                    val resultCount = searchResults.size
                                    val totalResults = viewModel.indexedFileCount
                                    val limitText = if (searchResultLimit == -1) "all" else "$searchResultLimit"
                                    if (resultCount == 0) {
                                        "No results"
                                    } else {
                                        "Showing $resultCount of $totalResults result${if (totalResults != 1) "s" else ""} (max $limitText)"
                                    }
                                }
                                NavigationScreen.FAVORITES -> {
                                    val count = viewModel.favorites.size
                                    "$count favorite${if (count != 1) "s" else ""}"
                                }
                                NavigationScreen.SETTINGS -> "Configure miniBAE"
                                NavigationScreen.FILE_TYPES -> "Choose file types to enable"
                            }
                        }
                        Text(
                            text = subtitleText,
                            fontSize = 12.sp,
                            maxLines = 1,
                            overflow = TextOverflow.Ellipsis,
                            color = Color.Gray
                        )
                    }
                },
                actions = {
                    // Close button for Bank Browser
                    if (showBankBrowser) {
                        IconButton(onClick = onBankBrowserClose, enabled = !isLoadingBank) {
                            Icon(
                                Icons.Filled.Close,
                                contentDescription = "Close",
                                modifier = Modifier.size(24.dp)
                            )
                        }
                    }
                    // Close button for File Types page
                    else if (!viewModel.showFullPlayer && viewModel.currentScreen == NavigationScreen.FILE_TYPES) {
                        IconButton(onClick = { onNavigate(NavigationScreen.SETTINGS) }, enabled = !isLoadingBank) {
                            Icon(
                                Icons.Filled.Close,
                                contentDescription = "Close",
                                modifier = Modifier.size(24.dp)
                            )
                        }
                    }
                    // Build Index button for Search screen
                    else if (!viewModel.showFullPlayer && viewModel.currentScreen == NavigationScreen.SEARCH) {
                        val indexingProgress by viewModel.getIndexingProgress()?.collectAsState() ?: remember { mutableStateOf(IndexingProgress()) }
                        
                        // Delete database icon (trash can) - only enabled when current folder exactly matches a database
                        // Read directly from observable state in ViewModel
                        val hasExactDatabase = viewModel.hasExactDatabase
                        IconButton(
                            onClick = { showDeleteDialog = true },
                            enabled = hasExactDatabase && !isLoadingBank
                        ) {
                            Icon(
                                Icons.Filled.Delete,
                                contentDescription = "Delete Database",
                                tint = if (hasExactDatabase) MaterialTheme.colors.onSurface else MaterialTheme.colors.onSurface.copy(alpha = 0.38f)
                            )
                        }
                        
                        // Build/Stop Index icon (refresh)
                        IconButton(
                            onClick = {
                                if (indexingProgress.isIndexing) {
                                    viewModel.stopIndexing()
                                } else {
                                    val rootPath = viewModel.currentFolderPath ?: "/sdcard"
                                    viewModel.rebuildIndex(rootPath) { files, folders, size ->
                                        Toast.makeText(
                                            context,
                                            "Indexed $files files in $folders folders (${size / 1024 / 1024} MB)",
                                            Toast.LENGTH_LONG
                                        ).show()
                                        // Load the search results after indexing completes
                                        if (viewModel.searchQuery.isNotEmpty()) {
                                            viewModel.searchFilesInDatabase(viewModel.searchQuery, viewModel.currentFolderPath, searchResultLimit)
                                        } else {
                                            viewModel.getAllFilesInDatabase(viewModel.currentFolderPath, searchResultLimit)
                                        }
                                    }
                                }
                            },
                            enabled = !isLoadingBank && viewModel.currentFolderPath != null && viewModel.currentFolderPath != "/"
                        ) {
                            Icon(
                                if (indexingProgress.isIndexing) Icons.Filled.Stop else Icons.Filled.Refresh,
                                contentDescription = if (indexingProgress.isIndexing) "Stop Indexing" else "Build Index",
                                tint = if (indexingProgress.isIndexing) MaterialTheme.colors.error else MaterialTheme.colors.onSurface
                            )
                        }
                    }
                },
                backgroundColor = MaterialTheme.colors.surface,
                elevation = 4.dp
            )
        },
            bottomBar = {
            Column {
                // Mini player (hidden when full player is shown)
                if (!viewModel.showFullPlayer && viewModel.getCurrentItem() != null) {
                    val currentItem = viewModel.getCurrentItem()!!
                    Surface(
                        modifier = Modifier
                            .fillMaxWidth()
                            .clickable(enabled = !isLoadingBank) { viewModel.showFullPlayer = true },
                        elevation = 8.dp,
                        color = MaterialTheme.colors.surface
                    ) {
                        Column {
                            Row(
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .padding(horizontal = 16.dp, vertical = 8.dp),
                                verticalAlignment = Alignment.CenterVertically
                            ) {
                                // File type badge (same style as folder browser)
                                Box(
                                    modifier = Modifier.size(width = 50.dp, height = 40.dp),
                                    contentAlignment = Alignment.Center
                                ) {
                                    Surface(
                                        modifier = Modifier
                                            .wrapContentSize()
                                            .padding(4.dp),
                                        shape = RoundedCornerShape(4.dp),
                                        color = MaterialTheme.colors.primary.copy(alpha = 0.15f)
                                    ) {
                                        val fileExt = currentItem.file.extension.uppercase()
                                        val displayText = when (fileExt) {
                                            "MID" -> "MIDI"
                                            "MIDI" -> "MIDI"
                                            "RMI" -> "RMI"
                                            "RMF" -> "RMF"
                                            "XMF" -> "XMF"
                                            "MXMF" -> "MXMF"
                                            "KAR" -> "KAR"
                                            else -> fileExt
                                        }
                                        Text(
                                            text = displayText,
                                            fontSize = 9.sp,
                                            fontWeight = FontWeight.Bold,
                                            color = MaterialTheme.colors.primary,
                                            modifier = Modifier.padding(horizontal = 4.dp, vertical = 2.dp)
                                        )
                                    }
                                }
                                
                                Spacer(modifier = Modifier.width(12.dp))
                                
                                Column(modifier = Modifier.weight(1f)) {
                                    Text(
                                        text = viewModel.currentTitle,
                                        fontSize = 14.sp,
                                        fontWeight = FontWeight.Bold,
                                        color = MaterialTheme.colors.onSurface,
                                        maxLines = 1,
                                        overflow = TextOverflow.Ellipsis
                                    )
                                    val folderName = viewModel.currentFolderPath?.let { path ->
                                        File(path).name
                                    } ?: "Unknown"
                                    Text(
                                        text = folderName,
                                        fontSize = 12.sp,
                                        color = MaterialTheme.colors.onSurface.copy(alpha = 0.6f)
                                    )
                                }
                                
                                // Play/Pause button with circular progress
                                Box(
                                    modifier = Modifier.size(48.dp),
                                    contentAlignment = Alignment.Center
                                ) {
                                    // Circular progress indicator
                                    val progress = if (viewModel.totalDurationMs > 0) {
                                        viewModel.currentPositionMs.toFloat() / viewModel.totalDurationMs.toFloat()
                                    } else 0f
                                    
                                    CircularProgressIndicator(
                                        progress = progress.coerceIn(0f, 1f),
                                        modifier = Modifier.size(48.dp),
                                        color = MaterialTheme.colors.primary,
                                        strokeWidth = 3.dp
                                    )
                                    
                                    IconButton(onClick = onPlayPause, enabled = !isLoadingBank) {
                                        Icon(
                                            if (viewModel.isPlaying) Icons.Filled.Pause else Icons.Filled.PlayArrow,
                                            contentDescription = "Play/Pause",
                                            tint = MaterialTheme.colors.onSurface,
                                            modifier = Modifier.size(28.dp)
                                        )
                                    }
                                }
                                
                                IconButton(onClick = onNext, enabled = !isLoadingBank && viewModel.hasNext()) {
                                    Icon(
                                        Icons.Filled.SkipNext,
                                        contentDescription = "Next",
                                        tint = if (viewModel.hasNext()) MaterialTheme.colors.onSurface else MaterialTheme.colors.onSurface.copy(alpha = 0.3f),
                                        modifier = Modifier.size(28.dp)
                                    )
                                }
                            }
                        }
                    }
                }
                
                // Bottom navigation - hide when bank browser is open
                if (!showBankBrowser) {
                BottomNavigation(
                    backgroundColor = MaterialTheme.colors.surface,
                    elevation = 8.dp
                ) {
                    BottomNavigationItem(
                        icon = { Icon(Icons.Filled.Home, contentDescription = "Home") },
                        selected = !viewModel.showFullPlayer && viewModel.currentScreen == NavigationScreen.HOME,
                        onClick = { 
                            viewModel.showFullPlayer = false
                            onNavigate(NavigationScreen.HOME)
                        },
                        enabled = !isLoadingBank,
                        selectedContentColor = MaterialTheme.colors.primary,
                        unselectedContentColor = Color.Gray
                    )
                    BottomNavigationItem(
                        icon = { Icon(Icons.Filled.Search, contentDescription = "Search") },
                        selected = !viewModel.showFullPlayer && viewModel.currentScreen == NavigationScreen.SEARCH,
                        onClick = {
                            viewModel.showFullPlayer = false
                            onNavigate(NavigationScreen.SEARCH)
                        },
                        enabled = !isLoadingBank,
                        selectedContentColor = MaterialTheme.colors.primary,
                        unselectedContentColor = Color.Gray
                    )
                    BottomNavigationItem(
                        icon = { Icon(Icons.Filled.Favorite, contentDescription = "Favorites") },
                        selected = !viewModel.showFullPlayer && viewModel.currentScreen == NavigationScreen.FAVORITES,
                        onClick = {
                            viewModel.showFullPlayer = false
                            onNavigate(NavigationScreen.FAVORITES)
                        },
                        enabled = !isLoadingBank,
                        selectedContentColor = MaterialTheme.colors.primary,
                        unselectedContentColor = Color.Gray
                    )
                    BottomNavigationItem(
                        icon = { Icon(Icons.Filled.Settings, contentDescription = "Settings") },
                        selected = !viewModel.showFullPlayer && (viewModel.currentScreen == NavigationScreen.SETTINGS || viewModel.currentScreen == NavigationScreen.FILE_TYPES),
                        onClick = {
                            viewModel.showFullPlayer = false
                            onNavigate(NavigationScreen.SETTINGS)
                        },
                        enabled = !isLoadingBank,
                        selectedContentColor = MaterialTheme.colors.primary,
                        unselectedContentColor = Color.Gray
                    )
                }
                }
            }
        }
            ) { paddingValues ->
        Box(modifier = Modifier.fillMaxSize()) {
            Column(
                modifier = Modifier
                    .fillMaxSize()
                    .padding(paddingValues)
                    .background(MaterialTheme.colors.background)
            ) {
                when (viewModel.currentScreen) {
                NavigationScreen.HOME -> HomeScreenContent(
                    viewModel = viewModel,
                    loading = loading,
                    onPlaylistItemClick = onPlaylistItemClick,
                    onToggleFavorite = onToggleFavorite,
                    onAddFolder = onAddFolder,
                    onShufflePlay = onShufflePlay,
                    onNavigateToFolder = onNavigateToFolder,
                    onAddToPlaylist = onAddToPlaylist,
                    onRefreshStorage = onRefreshStorage,
                    onAddAllMidi = onAddAllMidi,
                    onAddAllMidiRecursive = onAddAllMidiRecursive
                )
                NavigationScreen.SEARCH -> SearchScreenContent(
                    viewModel = viewModel,
                    onPlaylistItemClick = onPlaylistItemClick,
                    onToggleFavorite = onToggleFavorite,
                    onAddToPlaylist = onAddToPlaylist,
                    searchResultLimit = searchResultLimit
                )
                NavigationScreen.FAVORITES -> FavoritesScreenContent(
                    viewModel = viewModel,
                    onPlaylistItemClick = onPlaylistItemClick,
                    onToggleFavorite = onToggleFavorite,
                    onAddToPlaylist = onAddToPlaylist,
                    onMoveFavorite = { from, to ->
                        if (from == to) return@FavoritesScreenContent
                        if (from !in viewModel.favorites.indices) return@FavoritesScreenContent
                        if (to !in viewModel.favorites.indices) return@FavoritesScreenContent

                        val moved = viewModel.favorites.removeAt(from)
                        viewModel.favorites.add(to, moved)
                    },
                    onReorderFinished = {
                        saveFavorites(context, viewModel.favorites)
                        syncVirtualPlaylistToFavoritesOrder(viewModel)
                    }
                )
                NavigationScreen.SETTINGS -> SettingsScreenContent(
                    bankName = bankName,
                    isLoadingBank = isLoadingBank,
                    reverbType = reverbType,
                    velocityCurve = velocityCurve,
                    exportCodec = exportCodec,
                    searchResultLimit = searchResultLimit,
                    onLoadBuiltin = onLoadBuiltin,
                    onReverbChange = onReverbChange,
                    onCurveChange = onCurveChange,
                    onVolumeChange = onVolumeChange,
                    onExportCodecChange = onExportCodecChange,
                    onSearchLimitChange = onSearchLimitChange,
                    onOpenFileTypes = { onNavigate(NavigationScreen.FILE_TYPES) },
                    onBrowseBanks = onBrowseBanks
                )
                NavigationScreen.FILE_TYPES -> FileTypesScreenContent(
                    enabledExtensions = enabledExtensions,
                    onExtensionEnabledChange = onExtensionEnabledChange
                )
            }
        }
        
        // Show full player overlay when requested
        if (viewModel.showFullPlayer) {
            FullPlayerScreen(
                viewModel = viewModel,
                onClose = { viewModel.showFullPlayer = false },
                onPlayPause = onPlayPause,
                onNext = onNext,
                onPrevious = onPrevious,
                onSeek = onSeek,
                onStartDrag = onStartDrag,
                onDrag = onDrag,
                onVolumeChange = onVolumeChange,
                onToggleFavorite = onToggleFavorite,
                exportCodec = exportCodec,
                onExportRequest = onExportRequest,
                onRepeatModeChange = onRepeatModeChange
            )
        }
        
        // Show export overlay when exporting
        if (isExporting) {
            Box(
                modifier = Modifier
                    .fillMaxSize()
                    .background(Color.Black.copy(alpha = 0.7f)),
                contentAlignment = Alignment.Center
            ) {
                Column(
                    horizontalAlignment = Alignment.CenterHorizontally,
                    verticalArrangement = Arrangement.Center
                ) {
                    CircularProgressIndicator(
                        modifier = Modifier.size(48.dp),
                        color = MaterialTheme.colors.primary
                    )
                    if (exportStatus.isNotEmpty()) {
                        Spacer(modifier = Modifier.height(16.dp))
                        Text(
                            text = exportStatus,
                            style = MaterialTheme.typography.body1,
                            color = Color.White
                        )
                    }
                }
            }
        }
        
        // Show bank browser overlay when active
        if (showBankBrowser) {
            Box(
                modifier = Modifier
                    .fillMaxSize()
                    .padding(paddingValues)
                    .background(MaterialTheme.colors.background)
            ) {
                BankBrowserScreen(
                    currentPath = bankBrowserPath,
                    files = bankBrowserFiles,
                    isLoading = bankBrowserLoading,
                    onNavigate = onBankBrowserNavigate,
                    onSelectBank = onBankBrowserSelect,
                    onLoadBuiltin = onLoadBuiltin,
                    onClose = onBankBrowserClose
                )
            }
        }
            }
        }

        if (isLoadingBank) {
            Box(
                modifier = Modifier
                    .fillMaxSize()
                    .background(Color.Black.copy(alpha = 0.7f))
                    .clickable(
                        indication = null,
                        interactionSource = remember { androidx.compose.foundation.interaction.MutableInteractionSource() }
                    ) { },
                contentAlignment = Alignment.Center
            ) {
                Column(
                    horizontalAlignment = Alignment.CenterHorizontally,
                    verticalArrangement = Arrangement.Center
                ) {
                    CircularProgressIndicator(
                        modifier = Modifier.size(48.dp),
                        color = MaterialTheme.colors.primary
                    )
                    Spacer(modifier = Modifier.height(16.dp))
                    Text(
                        text = "Loading bank...",
                        style = MaterialTheme.typography.body1,
                        color = Color.White
                    )
                }
            }
        }
    }
    
    // Delete database confirmation dialog
    if (showDeleteDialog) {
        androidx.compose.material.AlertDialog(
            onDismissRequest = { showDeleteDialog = false },
            title = { Text("Delete Database") },
            text = {
                Text("Are you sure you want to delete the search database for \"${viewModel.currentFolderPath}\"? This action cannot be undone.")
            },
            confirmButton = {
                androidx.compose.material.TextButton(
                    onClick = {
                        showDeleteDialog = false
                        viewModel.deleteCurrentDatabase { success ->
                            if (success) {
                                Toast.makeText(
                                    context,
                                    "Database deleted successfully",
                                    Toast.LENGTH_SHORT
                                ).show()
                            } else {
                                Toast.makeText(
                                    context,
                                    "Failed to delete database",
                                    Toast.LENGTH_SHORT
                                ).show()
                            }
                        }
                    }
                ) {
                    Text("Delete", color = MaterialTheme.colors.error)
                }
            },
            dismissButton = {
                androidx.compose.material.TextButton(
                    onClick = { showDeleteDialog = false }
                ) {
                    Text("Cancel")
                }
            }
        )
    }
}

@Composable
fun FullPlayerScreen(
    viewModel: MusicPlayerViewModel,
    onClose: () -> Unit,
    onPlayPause: () -> Unit,
    onNext: () -> Unit,
    onPrevious: () -> Unit,
    onSeek: (Int) -> Unit,
    onStartDrag: () -> Unit,
    onDrag: (Int) -> Unit,
    onVolumeChange: (Int) -> Unit,
    onToggleFavorite: (String) -> Unit,
    exportCodec: Int,
    onExportRequest: (String, Int) -> Unit,
    onRepeatModeChange: () -> Unit
) {
    val currentPositionMs = viewModel.currentPositionMs
    val totalDurationMs = viewModel.totalDurationMs
    val currentItem = viewModel.getCurrentItem()
    val isPlaying = viewModel.isPlaying
    val configuration = LocalConfiguration.current
    val isLandscape = configuration.orientation == Configuration.ORIENTATION_LANDSCAPE
    
    // Update position in real-time
    LaunchedEffect(isPlaying) {
        while (isPlaying) {
            kotlinx.coroutines.delay(250)
            viewModel.currentPositionMs = viewModel.currentPositionMs
        }
    }
    
    if (isLandscape) {
        LandscapePlayerLayout(
            viewModel = viewModel,
            currentPositionMs = currentPositionMs,
            totalDurationMs = totalDurationMs,
            currentItem = currentItem,
            isPlaying = isPlaying,
            onClose = onClose,
            onPlayPause = onPlayPause,
            onNext = onNext,
            onPrevious = onPrevious,
            onSeek = onSeek,
            onStartDrag = onStartDrag,
            onDrag = onDrag,
            onVolumeChange = onVolumeChange,
            onToggleFavorite = onToggleFavorite,
            exportCodec = exportCodec,
            onExportRequest = onExportRequest,
            onRepeatModeChange = onRepeatModeChange
        )
    } else {
        PortraitPlayerLayout(
            viewModel = viewModel,
            currentPositionMs = currentPositionMs,
            totalDurationMs = totalDurationMs,
            currentItem = currentItem,
            isPlaying = isPlaying,
            onClose = onClose,
            onPlayPause = onPlayPause,
            onNext = onNext,
            onPrevious = onPrevious,
            onSeek = onSeek,
            onStartDrag = onStartDrag,
            onDrag = onDrag,
            onVolumeChange = onVolumeChange,
            onToggleFavorite = onToggleFavorite,
            exportCodec = exportCodec,
            onExportRequest = onExportRequest,
            onRepeatModeChange = onRepeatModeChange
        )
    }
}

@Composable
private fun PortraitPlayerLayout(
    viewModel: MusicPlayerViewModel,
    currentPositionMs: Int,
    totalDurationMs: Int,
    currentItem: PlaylistItem?,
    isPlaying: Boolean,
    onClose: () -> Unit,
    onPlayPause: () -> Unit,
    onNext: () -> Unit,
    onPrevious: () -> Unit,
    onSeek: (Int) -> Unit,
    onStartDrag: () -> Unit,
    onDrag: (Int) -> Unit,
    onVolumeChange: (Int) -> Unit,
    onToggleFavorite: (String) -> Unit,
    exportCodec: Int,
    onExportRequest: (String, Int) -> Unit,
    onRepeatModeChange: () -> Unit
) {
    Column(
        modifier = Modifier
            .fillMaxSize()
            .background(MaterialTheme.colors.background)
            .verticalScroll(rememberScrollState())
    ) {
        // Top bar with back button and favorite
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(16.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            IconButton(onClick = onClose) {
                Icon(
                    Icons.AutoMirrored.Filled.ArrowBack,
                    contentDescription = "Back",
                    tint = MaterialTheme.colors.onBackground
                )
            }
            Spacer(modifier = Modifier.weight(1f))
            
            // Export button (disabled when repeat mode is SONG to prevent infinite looping)
            currentItem?.let { item ->
                val isSoundFile = HomeFragment.isSoundExtension(item.file.extension)
                val isExportEnabled = viewModel.repeatMode != RepeatMode.SONG && !isSoundFile
                IconButton(
                    onClick = {
                        val extension = when (exportCodec) {
                            2 -> ".ogg"
                            3 -> ".flac"
                            else -> ".wav"
                        }
                        val defaultName = item.file.nameWithoutExtension + extension
                        onExportRequest(defaultName, exportCodec)
                    },
                    enabled = isExportEnabled
                ) {
                    Icon(
                        Icons.Filled.GetApp,
                        contentDescription = "Export",
                        tint = if (isExportEnabled) MaterialTheme.colors.onBackground else Color.Gray
                    )
                }
            }
            
            // Favorite button
            currentItem?.let { item ->
                val isFavorite = viewModel.isFavorite(item.path)
                IconButton(onClick = { onToggleFavorite(item.path) }) {
                    Icon(
                        if (isFavorite) Icons.Filled.Favorite else Icons.Filled.FavoriteBorder,
                        contentDescription = if (isFavorite) "Remove from favorites" else "Add to favorites",
                        tint = if (isFavorite) MaterialTheme.colors.primary else MaterialTheme.colors.onBackground
                    )
                }
            }
        }
        
        // Main content centered
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(horizontal = 32.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.Center
        ) {
            // Album art placeholder with swipe gesture
            var offsetX by remember { mutableStateOf(0f) }
            
            Box(
                modifier = Modifier
                    .size(120.dp)
                    .offset(x = (offsetX / 5).dp) // Visual feedback when swiping
                    .clip(RoundedCornerShape(16.dp))
                    .background(Color(0xFF3700B3))
                    .pointerInput(Unit) {
                        detectHorizontalDragGestures(
                            onDragEnd = {
                                if (offsetX > 100) {
                                    // Swipe right - previous
                                    if (viewModel.hasPrevious()) {
                                        onPrevious()
                                    }
                                } else if (offsetX < -100) {
                                    // Swipe left - next
                                    if (viewModel.hasNext()) {
                                        onNext()
                                    }
                                }
                                offsetX = 0f
                            },
                            onHorizontalDrag = { _, dragAmount ->
                                offsetX = (offsetX + dragAmount).coerceIn(-200f, 200f)
                            }
                        )
                    },
                contentAlignment = Alignment.Center
            ) {
                Icon(
                    Icons.Filled.MusicNote,
                    contentDescription = null,
                    modifier = Modifier.size(120.dp),
                    tint = Color.White.copy(alpha = 0.5f)
                )
            }
            
            Spacer(modifier = Modifier.height(32.dp))
            
            // Song title
            Text(
                text = currentItem?.title ?: "No song playing",
                style = MaterialTheme.typography.h5,
                color = MaterialTheme.colors.onBackground,
                maxLines = 2,
                overflow = TextOverflow.Ellipsis,
                textAlign = TextAlign.Center
            )
            
            Spacer(modifier = Modifier.height(8.dp))
            
            // Lyrics area
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .height(48.dp),
                contentAlignment = Alignment.Center
            ) {
                if (viewModel.currentLyric.isNotEmpty()) {
                    Text(
                        text = viewModel.currentLyric,
                        style = MaterialTheme.typography.body1.copy(fontWeight = FontWeight.Bold),
                        color = MaterialTheme.colors.primary,
                        textAlign = TextAlign.Center,
                        maxLines = 2,
                        overflow = TextOverflow.Ellipsis
                    )
                }
            }
            
            Spacer(modifier = Modifier.height(8.dp))
            
            // Seek bar
            Column(modifier = Modifier.fillMaxWidth()) {
                var isDragging by remember { mutableStateOf(false) }
                var dragPosition by remember { mutableStateOf(0f) }
                
                Slider(
                    value = if (isDragging) dragPosition else currentPositionMs.toFloat(),
                    onValueChange = {
                        if (!isDragging) {
                            isDragging = true
                            onStartDrag()
                        }
                        dragPosition = it
                        onDrag(it.toInt())
                    },
                    onValueChangeFinished = {
                        isDragging = false
                        onSeek(dragPosition.toInt())
                    },
                    valueRange = 0f..totalDurationMs.toFloat().coerceAtLeast(1f),
                    colors = SliderDefaults.colors(
                        thumbColor = MaterialTheme.colors.primary,
                        activeTrackColor = MaterialTheme.colors.primary,
                        inactiveTrackColor = Color.Gray.copy(alpha = 0.3f)
                    ),
                    modifier = Modifier.fillMaxWidth()
                )
                
                // Time labels
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween
                ) {
                    Text(
                        text = formatTime(currentPositionMs),
                        style = MaterialTheme.typography.caption,
                        color = MaterialTheme.colors.onBackground.copy(alpha = 0.7f),
                        modifier = Modifier.clickable { onSeek(0) }
                    )
                    Text(
                        text = formatTime(totalDurationMs),
                        style = MaterialTheme.typography.caption,
                        color = MaterialTheme.colors.onBackground.copy(alpha = 0.7f)
                    )
                }
            }
            
            Spacer(modifier = Modifier.height(24.dp))
            
            // Volume control
            Column(modifier = Modifier.fillMaxWidth()) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Icon(
                        Icons.AutoMirrored.Filled.VolumeUp,
                        contentDescription = "Volume",
                        tint = MaterialTheme.colors.onBackground.copy(alpha = 0.7f),
                        modifier = Modifier.size(20.dp)
                    )
                    Spacer(modifier = Modifier.width(8.dp))
                    Slider(
                        value = viewModel.volumePercent.toFloat(),
                        onValueChange = { onVolumeChange(it.toInt()) },
                        valueRange = 0f..100f,
                        colors = SliderDefaults.colors(
                            thumbColor = MaterialTheme.colors.primary,
                            activeTrackColor = MaterialTheme.colors.primary,
                            inactiveTrackColor = Color.Gray.copy(alpha = 0.3f)
                        ),
                        modifier = Modifier.weight(1f)
                    )
                    Spacer(modifier = Modifier.width(8.dp))
                    Text(
                        text = "${viewModel.volumePercent}%",
                        style = MaterialTheme.typography.caption,
                        color = MaterialTheme.colors.onBackground.copy(alpha = 0.7f),
                        modifier = Modifier.width(40.dp)
                    )
                }
            }
            
            Spacer(modifier = Modifier.height(24.dp))
            
            // Playback controls
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceEvenly,
                verticalAlignment = Alignment.CenterVertically
            ) {
                IconButton(
                    onClick = onPrevious,
                    modifier = Modifier.size(56.dp)
                ) {
                    Icon(
                        Icons.Filled.SkipPrevious,
                        contentDescription = "Previous",
                        tint = MaterialTheme.colors.onBackground,
                        modifier = Modifier.size(48.dp)
                    )
                }
                
                // Play/Pause button
                FloatingActionButton(
                    onClick = onPlayPause,
                    backgroundColor = MaterialTheme.colors.primary,
                    modifier = Modifier.size(64.dp)
                ) {
                    Icon(
                        if (isPlaying) Icons.Filled.Pause else Icons.Filled.PlayArrow,
                        contentDescription = if (isPlaying) "Pause" else "Play",
                        modifier = Modifier.size(32.dp),
                        tint = Color.White
                    )
                }
                
                IconButton(
                    onClick = onNext,
                    modifier = Modifier.size(56.dp)
                ) {
                    Icon(
                        Icons.Filled.SkipNext,
                        contentDescription = "Next",
                        tint = MaterialTheme.colors.onBackground,
                        modifier = Modifier.size(48.dp)
                    )
                }
            }
            
            Spacer(modifier = Modifier.height(16.dp))
            
            // Repeat and Shuffle buttons
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.Center,
                verticalAlignment = Alignment.CenterVertically
            ) {
                IconButton(
                    onClick = {
                        viewModel.repeatMode = when (viewModel.repeatMode) {
                            RepeatMode.NONE -> RepeatMode.SONG
                            RepeatMode.SONG -> RepeatMode.PLAYLIST
                            RepeatMode.PLAYLIST -> RepeatMode.NONE
                        }
                        // Update loop count for currently playing song
                        onRepeatModeChange()
                    }
                ) {
                    Icon(
                        when (viewModel.repeatMode) {
                            RepeatMode.NONE -> Icons.Filled.Repeat
                            RepeatMode.SONG -> Icons.Filled.RepeatOne
                            RepeatMode.PLAYLIST -> Icons.Filled.Repeat
                        },
                        contentDescription = "Repeat: ${viewModel.repeatMode.name}",
                        tint = if (viewModel.repeatMode == RepeatMode.NONE) {
                            Color.Gray
                        } else {
                            MaterialTheme.colors.primary
                        },
                        modifier = Modifier.size(32.dp)
                    )
                }
                
                Spacer(modifier = Modifier.width(24.dp))
                
                IconButton(
                    onClick = {
                        viewModel.toggleShuffle()
                    }
                ) {
                    Icon(
                        Icons.Filled.Shuffle,
                        contentDescription = "Shuffle: ${if (viewModel.isShuffled) "On" else "Off"}",
                        tint = if (viewModel.isShuffled) {
                            MaterialTheme.colors.primary
                        } else {
                            Color.Gray
                        },
                        modifier = Modifier.size(32.dp)
                    )
                }
            }
            
            Spacer(modifier = Modifier.height(24.dp))
        }
    }
}

@Composable
private fun LandscapePlayerLayout(
    viewModel: MusicPlayerViewModel,
    currentPositionMs: Int,
    totalDurationMs: Int,
    currentItem: PlaylistItem?,
    isPlaying: Boolean,
    onClose: () -> Unit,
    onPlayPause: () -> Unit,
    onNext: () -> Unit,
    onPrevious: () -> Unit,
    onSeek: (Int) -> Unit,
    onStartDrag: () -> Unit,
    onDrag: (Int) -> Unit,
    onVolumeChange: (Int) -> Unit,
    onToggleFavorite: (String) -> Unit,
    exportCodec: Int,
    onExportRequest: (String, Int) -> Unit,
    onRepeatModeChange: () -> Unit
) {
    Column(
        modifier = Modifier
            .fillMaxSize()
            .background(MaterialTheme.colors.background)
    ) {
        // Top bar with back button and actions
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 16.dp, vertical = 8.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            IconButton(onClick = onClose) {
                Icon(
                    Icons.AutoMirrored.Filled.ArrowBack,
                    contentDescription = "Back",
                    tint = MaterialTheme.colors.onBackground
                )
            }
            Spacer(modifier = Modifier.weight(1f))
            
            // Export button
            currentItem?.let { item ->
                val isSoundFile = HomeFragment.isSoundExtension(item.file.extension)
                val isExportEnabled = viewModel.repeatMode != RepeatMode.SONG && !isSoundFile
                IconButton(
                    onClick = {
                        val extension = when (exportCodec) {
                            2 -> ".ogg"
                            3 -> ".flac"
                            else -> ".wav"
                        }
                        val defaultName = item.file.nameWithoutExtension + extension
                        onExportRequest(defaultName, exportCodec)
                    },
                    enabled = isExportEnabled
                ) {
                    Icon(
                        Icons.Filled.GetApp,
                        contentDescription = "Export",
                        tint = if (isExportEnabled) MaterialTheme.colors.onBackground else Color.Gray
                    )
                }
            }
            
            // Favorite button
            currentItem?.let { item ->
                val isFavorite = viewModel.isFavorite(item.path)
                IconButton(onClick = { onToggleFavorite(item.path) }) {
                    Icon(
                        if (isFavorite) Icons.Filled.Favorite else Icons.Filled.FavoriteBorder,
                        contentDescription = if (isFavorite) "Remove from favorites" else "Add to favorites",
                        tint = if (isFavorite) MaterialTheme.colors.primary else MaterialTheme.colors.onBackground
                    )
                }
            }
        }
        
        // Main content in horizontal layout
        Row(
            modifier = Modifier
                .fillMaxSize()
                .padding(horizontal = 24.dp, vertical = 8.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            // Left side: Album art and title
            Column(
                modifier = Modifier
                    .width(200.dp)
                    .padding(end = 24.dp),
                horizontalAlignment = Alignment.CenterHorizontally,
                verticalArrangement = Arrangement.Center
            ) {
                // Album art with swipe gesture
                var offsetX by remember { mutableStateOf(0f) }
                
                Box(
                    modifier = Modifier
                        .size(160.dp)
                        .offset(x = (offsetX / 5).dp)
                        .clip(RoundedCornerShape(16.dp))
                        .background(Color(0xFF3700B3))
                        .pointerInput(Unit) {
                            detectHorizontalDragGestures(
                                onDragEnd = {
                                    if (offsetX > 100) {
                                        if (viewModel.hasPrevious()) {
                                            onPrevious()
                                        }
                                    } else if (offsetX < -100) {
                                        if (viewModel.hasNext()) {
                                            onNext()
                                        }
                                    }
                                    offsetX = 0f
                                },
                                onHorizontalDrag = { _, dragAmount ->
                                    offsetX = (offsetX + dragAmount).coerceIn(-200f, 200f)
                                }
                            )
                        },
                    contentAlignment = Alignment.Center
                ) {
                    Icon(
                        Icons.Filled.MusicNote,
                        contentDescription = null,
                        modifier = Modifier.size(120.dp),
                        tint = Color.White.copy(alpha = 0.5f)
                    )
                }
                
                Spacer(modifier = Modifier.height(16.dp))
                
                // Song title
                Text(
                    text = currentItem?.title ?: "No song playing",
                    style = MaterialTheme.typography.subtitle1,
                    color = MaterialTheme.colors.onBackground,
                    maxLines = 2,
                    overflow = TextOverflow.Ellipsis,
                    textAlign = TextAlign.Center
                )
            }
            
            // Right side: Controls and seek bar
            Column(
                modifier = Modifier
                    .weight(1f)
                    .fillMaxHeight(),
                verticalArrangement = Arrangement.Center
            ) {
                // Lyrics area (compact)
                if (viewModel.currentLyric.isNotEmpty()) {
                    Text(
                        text = viewModel.currentLyric,
                        style = MaterialTheme.typography.body2.copy(fontWeight = FontWeight.Bold),
                        color = MaterialTheme.colors.primary,
                        textAlign = TextAlign.Center,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                        modifier = Modifier.fillMaxWidth()
                    )
                    Spacer(modifier = Modifier.height(8.dp))
                }
                
                // Playback controls
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.Center,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    IconButton(
                        onClick = onPrevious,
                        modifier = Modifier.size(48.dp)
                    ) {
                        Icon(
                            Icons.Filled.SkipPrevious,
                            contentDescription = "Previous",
                            tint = MaterialTheme.colors.onBackground,
                            modifier = Modifier.size(40.dp)
                        )
                    }
                    
                    Spacer(modifier = Modifier.width(16.dp))
                    
                    // Play/Pause button
                    FloatingActionButton(
                        onClick = onPlayPause,
                        backgroundColor = MaterialTheme.colors.primary,
                        modifier = Modifier.size(56.dp)
                    ) {
                        Icon(
                            if (isPlaying) Icons.Filled.Pause else Icons.Filled.PlayArrow,
                            contentDescription = if (isPlaying) "Pause" else "Play",
                            modifier = Modifier.size(28.dp),
                            tint = Color.White
                        )
                    }
                    
                    Spacer(modifier = Modifier.width(16.dp))
                    
                    IconButton(
                        onClick = onNext,
                        modifier = Modifier.size(48.dp)
                    ) {
                        Icon(
                            Icons.Filled.SkipNext,
                            contentDescription = "Next",
                            tint = MaterialTheme.colors.onBackground,
                            modifier = Modifier.size(40.dp)
                        )
                    }
                }
                
                Spacer(modifier = Modifier.height(12.dp))
                
                // Repeat and Shuffle buttons
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.Center,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    IconButton(
                        onClick = {
                            viewModel.repeatMode = when (viewModel.repeatMode) {
                                RepeatMode.NONE -> RepeatMode.SONG
                                RepeatMode.SONG -> RepeatMode.PLAYLIST
                                RepeatMode.PLAYLIST -> RepeatMode.NONE
                            }
                            onRepeatModeChange()
                        }
                    ) {
                        Icon(
                            when (viewModel.repeatMode) {
                                RepeatMode.NONE -> Icons.Filled.Repeat
                                RepeatMode.SONG -> Icons.Filled.RepeatOne
                                RepeatMode.PLAYLIST -> Icons.Filled.Repeat
                            },
                            contentDescription = "Repeat: ${viewModel.repeatMode.name}",
                            tint = if (viewModel.repeatMode == RepeatMode.NONE) {
                                Color.Gray
                            } else {
                                MaterialTheme.colors.primary
                            },
                            modifier = Modifier.size(28.dp)
                        )
                    }
                    
                    Spacer(modifier = Modifier.width(16.dp))
                    
                    IconButton(
                        onClick = {
                            viewModel.toggleShuffle()
                        }
                    ) {
                        Icon(
                            Icons.Filled.Shuffle,
                            contentDescription = "Shuffle: ${if (viewModel.isShuffled) "On" else "Off"}",
                            tint = if (viewModel.isShuffled) {
                                MaterialTheme.colors.primary
                            } else {
                                Color.Gray
                            },
                            modifier = Modifier.size(28.dp)
                        )
                    }
                }
                
                Spacer(modifier = Modifier.height(12.dp))
                
                // Seek bar
                Column(modifier = Modifier.fillMaxWidth()) {
                    var isDragging by remember { mutableStateOf(false) }
                    var dragPosition by remember { mutableStateOf(0f) }
                    
                    Slider(
                        value = if (isDragging) dragPosition else currentPositionMs.toFloat(),
                        onValueChange = {
                            if (!isDragging) {
                                isDragging = true
                                onStartDrag()
                            }
                            dragPosition = it
                            onDrag(it.toInt())
                        },
                        onValueChangeFinished = {
                            isDragging = false
                            onSeek(dragPosition.toInt())
                        },
                        valueRange = 0f..totalDurationMs.toFloat().coerceAtLeast(1f),
                        colors = SliderDefaults.colors(
                            thumbColor = MaterialTheme.colors.primary,
                            activeTrackColor = MaterialTheme.colors.primary,
                            inactiveTrackColor = Color.Gray.copy(alpha = 0.3f)
                        ),
                        modifier = Modifier.fillMaxWidth()
                    )
                    
                    // Time labels
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween
                    ) {
                        Text(
                            text = formatTime(currentPositionMs),
                            style = MaterialTheme.typography.caption,
                            color = MaterialTheme.colors.onBackground.copy(alpha = 0.7f),
                            modifier = Modifier.clickable { onSeek(0) }
                        )
                        Text(
                            text = formatTime(totalDurationMs),
                            style = MaterialTheme.typography.caption,
                            color = MaterialTheme.colors.onBackground.copy(alpha = 0.7f)
                        )
                    }
                }
                
                Spacer(modifier = Modifier.height(12.dp))
                
                // Volume control
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Icon(
                        Icons.AutoMirrored.Filled.VolumeUp,
                        contentDescription = "Volume",
                        tint = MaterialTheme.colors.onBackground.copy(alpha = 0.7f),
                        modifier = Modifier.size(20.dp)
                    )
                    Spacer(modifier = Modifier.width(8.dp))
                    Slider(
                        value = viewModel.volumePercent.toFloat(),
                        onValueChange = { onVolumeChange(it.toInt()) },
                        valueRange = 0f..100f,
                        colors = SliderDefaults.colors(
                            thumbColor = MaterialTheme.colors.primary,
                            activeTrackColor = MaterialTheme.colors.primary,
                            inactiveTrackColor = Color.Gray.copy(alpha = 0.3f)
                        ),
                        modifier = Modifier.weight(1f)
                    )
                    Spacer(modifier = Modifier.width(8.dp))
                    Text(
                        text = "${viewModel.volumePercent}%",
                        style = MaterialTheme.typography.caption,
                        color = MaterialTheme.colors.onBackground.copy(alpha = 0.7f),
                        modifier = Modifier.width(40.dp)
                    )
                }
            }
        }
    }
}

@OptIn(ExperimentalMaterialApi::class)
@Composable
fun HomeScreenContent(
    viewModel: MusicPlayerViewModel,
    loading: Boolean,
    onPlaylistItemClick: (File) -> Unit,
    onToggleFavorite: (String) -> Unit,
    onAddFolder: () -> Unit,
    onShufflePlay: () -> Unit,
    onNavigateToFolder: (String) -> Unit,
    onAddToPlaylist: (File) -> Unit,
    onRefreshStorage: () -> Unit,
    onAddAllMidi: () -> Unit,
    onAddAllMidiRecursive: () -> Unit
) {
    var refreshing by remember { mutableStateOf(false) }
    val refreshScope = rememberCoroutineScope()
    val pullRefreshState = rememberPullRefreshState(
        refreshing = refreshing,
        onRefresh = {
            refreshing = true
            onRefreshStorage()
            // Reset refreshing after a short delay
            refreshScope.launch {
                kotlinx.coroutines.delay(500)
                refreshing = false
            }
        }
    )

    // Avoid re-filtering big lists on every unrelated recomposition (e.g. playback position ticks).
    // These recompute only when the underlying SnapshotStateList content changes.
    val folderFilesSnapshot by remember {
        derivedStateOf { viewModel.folderFiles.toList() }
    }
    val folderAndSpecialItems by remember {
        derivedStateOf { folderFilesSnapshot.filter { it.isFolder || it.title.startsWith("🔄") } }
    }
    val songFiles by remember {
        derivedStateOf { folderFilesSnapshot.filter { !it.isFolder && !it.title.startsWith("🔄") } }
    }
    val favoritesSet by remember {
        derivedStateOf { viewModel.favorites.toSet() }
    }
    
    Column(modifier = Modifier.fillMaxSize()) {
        // File list
        when {
            loading -> {
                Box(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                    CircularProgressIndicator()
                }
            }
            viewModel.currentFolderPath == null -> {
                Box(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                    Column(horizontalAlignment = Alignment.CenterHorizontally) {
                        Icon(
                            Icons.Filled.Folder,
                            contentDescription = null,
                            modifier = Modifier.size(64.dp),
                            tint = Color.Gray
                        )
                        Spacer(modifier = Modifier.height(16.dp))
                        Text("No folder selected", color = Color.Gray)
                        Spacer(modifier = Modifier.height(8.dp))
                        Button(onClick = onAddFolder) {
                            Text("Select Folder")
                        }
                    }
                }
            }
            else -> {
                Box(modifier = Modifier.fillMaxSize()) {
                    LazyColumn(
                        modifier = Modifier
                            .fillMaxSize()
                            .pullRefresh(pullRefreshState)
                    ) {
                    // Show parent directory ".." option
                    viewModel.currentFolderPath?.let { currentPath ->
                        val file = File(currentPath)
                        val parentPath = file.parent
                        // Show parent unless we're already at root or parent is null
                        if (parentPath != null && currentPath != "/") {
                            item(key = "parent:$parentPath") {
                                Surface(
                                    modifier = Modifier
                                        .fillMaxWidth()
                                        .clickable { onNavigateToFolder(parentPath) },
                                    color = Color.Transparent
                                ) {
                                    Row(
                                        modifier = Modifier
                                            .fillMaxWidth()
                                            .padding(horizontal = 16.dp, vertical = 12.dp),
                                        verticalAlignment = Alignment.CenterVertically
                                    ) {
                                        Icon(
                                            Icons.Filled.Folder,
                                            contentDescription = null,
                                            tint = MaterialTheme.colors.onBackground.copy(alpha = 0.6f),
                                            modifier = Modifier.size(40.dp)
                                        )
                                        Spacer(modifier = Modifier.width(12.dp))
                                        Text(
                                            text = "..",
                                            fontSize = 14.sp,
                                            fontWeight = FontWeight.Bold,
                                            color = MaterialTheme.colors.onBackground.copy(alpha = 0.6f)
                                        )
                                    }
                                }
                                Divider(color = Color.Gray.copy(alpha = 0.2f))
                            }
                        }
                    }
                    
                    // Show folders and special items (refresh button and storage items)
                    itemsIndexed(
                        folderAndSpecialItems,
                        key = { _, item -> item.id }
                    ) { _, item ->
                        Surface(
                            modifier = Modifier
                                .fillMaxWidth()
                                .clickable { 
                                    if (item.title.startsWith("🔄")) {
                                        // Handle refresh button
                                        onRefreshStorage()
                                    } else {
                                        onNavigateToFolder(item.path)
                                    }
                                },
                            color = Color.Transparent
                        ) {
                            Row(
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .padding(horizontal = 16.dp, vertical = 12.dp),
                                verticalAlignment = Alignment.CenterVertically
                            ) {
                                Icon(
                                    if (item.title.startsWith("🔄")) Icons.Filled.Refresh 
                                    else Icons.Filled.Folder,
                                    contentDescription = null,
                                    tint = if (item.title.startsWith("🔄")) Color.Green else MaterialTheme.colors.primary,
                                    modifier = Modifier.size(40.dp)
                                )
                                Spacer(modifier = Modifier.width(12.dp))
                                Text(
                                    text = item.title,
                                    fontSize = 14.sp,
                                    fontWeight = FontWeight.Bold,
                                    color = MaterialTheme.colors.onBackground
                                )
                            }
                        }
                        Divider(color = Color.Gray.copy(alpha = 0.2f))
                    }
                    
                    // Show songs
                    if (songFiles.isEmpty()) {
                        item(key = "empty_songs") {
                            Box(
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .padding(32.dp),
                                contentAlignment = Alignment.Center
                            ) {
                                Text(
                                    "No songs in this folder",
                                    color = Color.Gray,
                                    fontSize = 14.sp
                                )
                            }
                        }
                    } else {
                        itemsIndexed(
                            songFiles,
                            key = { _, item -> item.id }
                        ) { index, item ->
                            FolderSongListItem(
                                item = item,
                                isFavorite = favoritesSet.contains(item.path),
                                onClick = { onPlaylistItemClick(item.file) },
                                onToggleFavorite = { onToggleFavorite(item.path) },
                                onAddToPlaylist = { onAddToPlaylist(item.file) }
                            )
                            if (index < songFiles.size - 1) {
                                Divider(color = Color.Gray.copy(alpha = 0.2f))
                            }
                        }
                    }
                    }
                    
                    PullRefreshIndicator(
                        refreshing = refreshing,
                        state = pullRefreshState,
                        modifier = Modifier.align(Alignment.TopCenter)
                    )
                }
            }
        }
    }
}

@Composable
fun SearchScreenContent(
    viewModel: MusicPlayerViewModel,
    onPlaylistItemClick: (File) -> Unit,
    onToggleFavorite: (String) -> Unit,
    onAddToPlaylist: (File) -> Unit,
    searchResultLimit: Int
) {
    val context = LocalContext.current
    val indexingProgress by viewModel.getIndexingProgress()?.collectAsState() ?: remember { mutableStateOf(IndexingProgress()) }
    val searchResults by viewModel.searchResults.collectAsState()
    val configuration = LocalConfiguration.current
    val isLandscape = configuration.orientation == Configuration.ORIENTATION_LANDSCAPE
    
    // Initialize database on first composition
    LaunchedEffect(Unit) {
        viewModel.initializeDatabase(context)
    }
    
    // Trigger search when query changes or when showing all results
    // IMPORTANT: Wait for database to be ready before searching
    LaunchedEffect(viewModel.searchQuery, searchResultLimit, viewModel.isDatabaseReady) {
        // Only search if database is initialized
        if (!viewModel.isDatabaseReady) {
            return@LaunchedEffect
        }
        
        if (viewModel.searchQuery.isNotEmpty()) {
            viewModel.searchFilesInDatabase(viewModel.searchQuery, viewModel.currentFolderPath, searchResultLimit)
        } else {
            // Show all results when search is empty
            viewModel.getAllFilesInDatabase(viewModel.currentFolderPath, searchResultLimit)
        }
    }
    
    Column(modifier = Modifier.fillMaxSize()) {
        // Check if current path is indexed
        val isCurrentPathIndexed = viewModel.isCurrentPathIndexed
        
        if (isLandscape) {
            // Landscape: horizontal layout
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = 16.dp, vertical = 8.dp),
                horizontalArrangement = Arrangement.spacedBy(12.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                // Search bar
                TextField(
                    value = viewModel.searchQuery,
                    onValueChange = { viewModel.searchQuery = it },
                    modifier = Modifier.weight(1f),
                    placeholder = { Text("Search songs...") },
                    leadingIcon = { Icon(Icons.Filled.Search, contentDescription = null) },
                    trailingIcon = {
                        if (viewModel.searchQuery.isNotEmpty()) {
                            IconButton(onClick = { viewModel.searchQuery = "" }) {
                                Icon(Icons.Filled.Clear, contentDescription = "Clear")
                            }
                        }
                    },
                    colors = TextFieldDefaults.textFieldColors(
                        backgroundColor = MaterialTheme.colors.surface,
                        textColor = MaterialTheme.colors.onSurface,
                        focusedIndicatorColor = Color.Transparent,
                        unfocusedIndicatorColor = Color.Transparent,
                        disabledIndicatorColor = Color.Transparent
                    ),
                    shape = RoundedCornerShape(24.dp),
                    enabled = !indexingProgress.isIndexing,
                    singleLine = true
                )
                
                // Show indexing status in landscape
                if (indexingProgress.isIndexing) {
                    CircularProgressIndicator(
                        modifier = Modifier.size(20.dp),
                        strokeWidth = 2.dp
                    )
                    Text(
                        "${indexingProgress.filesIndexed}",
                        fontSize = 12.sp,
                        color = MaterialTheme.colors.primary
                    )
                }
            }
            
            Divider(color = Color.Gray.copy(alpha = 0.2f))
        } else {
            // Portrait: vertical layout
            TextField(
                value = viewModel.searchQuery,
                onValueChange = { viewModel.searchQuery = it },
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(16.dp),
                placeholder = { Text("Search songs...") },
                leadingIcon = { Icon(Icons.Filled.Search, contentDescription = null) },
                trailingIcon = {
                    if (viewModel.searchQuery.isNotEmpty()) {
                        IconButton(onClick = { viewModel.searchQuery = "" }) {
                            Icon(Icons.Filled.Clear, contentDescription = "Clear")
                        }
                    }
                },
                colors = TextFieldDefaults.textFieldColors(
                    backgroundColor = MaterialTheme.colors.surface,
                    textColor = MaterialTheme.colors.onSurface,
                    focusedIndicatorColor = Color.Transparent,
                    unfocusedIndicatorColor = Color.Transparent,
                    disabledIndicatorColor = Color.Transparent
                ),
                shape = RoundedCornerShape(24.dp),
                enabled = !indexingProgress.isIndexing
            )
            
            Divider(color = Color.Gray.copy(alpha = 0.2f))
            
            // Show message if current directory is not indexed
            if (!isCurrentPathIndexed && viewModel.currentFolderPath != null && viewModel.currentFolderPath != "/") {
                Card(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(horizontal = 16.dp, vertical = 8.dp),
                    backgroundColor = MaterialTheme.colors.surface.copy(alpha = 0.7f)
                ) {
                    Row(
                        modifier = Modifier.padding(16.dp),
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Icon(
                            Icons.Filled.Info,
                            contentDescription = null,
                            tint = Color.Gray,
                            modifier = Modifier.padding(end = 12.dp)
                        )
                        Text(
                            "This directory is not indexed. Use the refresh button above to build an index.",
                            fontSize = 14.sp,
                            color = Color.Gray
                        )
                    }
                }
            }
        }
        
        // Search results
        when {
            indexingProgress.isIndexing -> {
                // Show indexing progress
                Box(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                    Column(horizontalAlignment = Alignment.CenterHorizontally) {
                        CircularProgressIndicator(modifier = Modifier.size(48.dp))
                        Spacer(modifier = Modifier.height(16.dp))
                        Text("Building file index...", color = Color.Gray)
                        Spacer(modifier = Modifier.height(8.dp))
                        Text(
                            indexingProgress.currentPath.takeLast(50),
                            fontSize = 10.sp,
                            color = Color.Gray,
                            maxLines = 2
                        )
                    }
                }
            }
            viewModel.indexedFileCount == 0 -> {
                // No index built yet
                Box(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                    Column(horizontalAlignment = Alignment.CenterHorizontally, modifier = Modifier.padding(32.dp)) {
                        Icon(Icons.Filled.Storage, contentDescription = null, modifier = Modifier.size(64.dp), tint = Color.Gray)
                        Spacer(modifier = Modifier.height(16.dp))
                        Text("No search index", color = Color.Gray, fontSize = 16.sp)
                        Spacer(modifier = Modifier.height(8.dp))
                        Text(
                            "Click 'Build Index' to create a searchable database of your music files. This may take a few minutes for large collections.",
                            fontSize = 12.sp,
                            color = Color.Gray,
                            textAlign = androidx.compose.ui.text.style.TextAlign.Center
                        )
                    }
                }
            }
            viewModel.searchQuery.isEmpty() -> {
                // Show all files when nothing typed
                if (searchResults.isEmpty()) {
                    Box(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                        Column(horizontalAlignment = Alignment.CenterHorizontally) {
                            Icon(Icons.Filled.Search, contentDescription = null, modifier = Modifier.size(64.dp), tint = Color.Gray)
                            Spacer(modifier = Modifier.height(16.dp))
                            Text("Showing all indexed files", color = Color.Gray, textAlign = androidx.compose.ui.text.style.TextAlign.Center)
                            Spacer(modifier = Modifier.height(8.dp))
                            Text("Type to search", fontSize = 12.sp, color = Color.Gray)
                        }
                    }
                } else {
                    // Show all results
                    LazyColumn(modifier = Modifier.fillMaxSize()) {
                        itemsIndexed(searchResults) { index, item ->
                            FolderSongListItem(
                                item = item,
                                isFavorite = viewModel.isFavorite(item.path),
                                onClick = { onPlaylistItemClick(item.file) },
                                onToggleFavorite = { onToggleFavorite(item.path) },
                                onAddToPlaylist = { onAddToPlaylist(item.file) }
                            )
                            if (index < searchResults.size - 1) {
                                Divider(color = Color.Gray.copy(alpha = 0.2f))
                            }
                        }
                    }
                }
            }
            viewModel.isSearching -> {
                // Searching
                Box(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                    CircularProgressIndicator()
                }
            }
            searchResults.isEmpty() -> {
                // No results found
                Box(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                    Column(horizontalAlignment = Alignment.CenterHorizontally) {
                        Icon(Icons.Filled.SearchOff, contentDescription = null, modifier = Modifier.size(64.dp), tint = Color.Gray)
                        Spacer(modifier = Modifier.height(16.dp))
                        Text("No results found", color = Color.Gray)
                        Spacer(modifier = Modifier.height(8.dp))
                        Text("Try a different search term", fontSize = 12.sp, color = Color.Gray)
                    }
                }
            }
            else -> {
                // Show results with count
                LazyColumn(modifier = Modifier.fillMaxSize()) {
                    itemsIndexed(searchResults) { index, item ->
                        FolderSongListItem(
                            item = item,
                            isFavorite = viewModel.isFavorite(item.path),
                            onClick = { onPlaylistItemClick(item.file) },
                            onToggleFavorite = { onToggleFavorite(item.path) },
                            onAddToPlaylist = { onAddToPlaylist(item.file) }
                        )
                        if (index < searchResults.size - 1) {
                            Divider(color = Color.Gray.copy(alpha = 0.2f))
                        }
                    }
                }
            }
        }
    }
}

@Composable
fun FavoritesScreenContent(
    viewModel: MusicPlayerViewModel,
    onPlaylistItemClick: (File) -> Unit,
    onToggleFavorite: (String) -> Unit,
    onAddToPlaylist: (File) -> Unit,
    onMoveFavorite: (from: Int, to: Int) -> Unit,
    onReorderFinished: () -> Unit
) {
    // Keep the list clean so indices match and drag-reorder stays consistent.
    LaunchedEffect(Unit) {
        val missing = viewModel.favorites.filter { path ->
            val f = File(path)
            !f.exists() || f.isDirectory
        }
        if (missing.isNotEmpty()) {
            viewModel.favorites.removeAll(missing)
        }
    }

    val itemHeights = remember { mutableStateMapOf<String, Int>() }
    var draggingIndex by remember { mutableStateOf<Int?>(null) }
    var dragOffsetY by remember { mutableStateOf(0f) }
    var didReorder by remember { mutableStateOf(false) }
    
    Column(modifier = Modifier.fillMaxSize()) {
        if (viewModel.favorites.isEmpty()) {
            Box(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                Column(horizontalAlignment = Alignment.CenterHorizontally) {
                    Icon(Icons.Filled.FavoriteBorder, contentDescription = null, modifier = Modifier.size(64.dp), tint = Color.Gray)
                    Spacer(modifier = Modifier.height(16.dp))
                    Text("No favorite songs", color = Color.Gray)
                    Spacer(modifier = Modifier.height(8.dp))
                    Text("Tap the heart icon to add favorites", fontSize = 12.sp, color = Color.Gray)
                }
            }
        } else {
            LazyColumn(modifier = Modifier.fillMaxSize()) {
                itemsIndexed(
                    items = viewModel.favorites,
                    key = { _, path -> path }
                ) { index, path ->
                    val file = remember(path) { File(path) }
                    val item = remember(path) { PlaylistItem(file) }
                    val isDragging = draggingIndex == index

                    Box(
                        modifier = Modifier
                            .onGloballyPositioned { coords ->
                                itemHeights[path] = coords.size.height
                            }
                            .zIndex(if (isDragging) 1f else 0f)
                            .offset { IntOffset(0, if (isDragging) dragOffsetY.roundToInt() else 0) }
                    ) {
                        Surface(
                            modifier = Modifier.fillMaxWidth(),
                            color = Color.Transparent
                        ) {
                            Row(
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .padding(horizontal = 16.dp, vertical = 12.dp),
                                verticalAlignment = Alignment.CenterVertically
                            ) {
                                // File type badge (clickable to play)
                                Box(
                                    modifier = Modifier
                                        .size(width = 50.dp, height = 40.dp)
                                        .clickable { onPlaylistItemClick(item.file) },
                                    contentAlignment = Alignment.Center
                                ) {
                                    Surface(
                                        modifier = Modifier
                                            .wrapContentSize()
                                            .padding(4.dp),
                                        shape = RoundedCornerShape(4.dp),
                                        color = MaterialTheme.colors.primary.copy(alpha = 0.15f)
                                    ) {
                                        val fileExt = item.file.extension.uppercase()
                                        val displayText = when (fileExt) {
                                            "MID" -> "MIDI"
                                            "MIDI" -> "MIDI"
                                            "RMI" -> "RMI"
                                            "RMF" -> "RMF"
                                            "XMF" -> "XMF"
                                            "MXMF" -> "MXMF"
                                            "KAR" -> "KAR"
                                            else -> fileExt
                                        }
                                        Text(
                                            text = displayText,
                                            fontSize = 9.sp,
                                            fontWeight = FontWeight.Bold,
                                            color = MaterialTheme.colors.primary,
                                            modifier = Modifier.padding(horizontal = 4.dp, vertical = 2.dp)
                                        )
                                    }
                                }

                                Spacer(modifier = Modifier.width(4.dp))

                                // Song info (clickable to play)
                                Column(
                                    modifier = Modifier
                                        .weight(1f)
                                        .clickable { onPlaylistItemClick(item.file) }
                                ) {
                                    Text(
                                        text = item.title,
                                        fontSize = 14.sp,
                                        fontWeight = FontWeight.Normal,
                                        color = MaterialTheme.colors.onBackground,
                                        maxLines = 1,
                                        overflow = TextOverflow.Ellipsis
                                    )
                                }

                                // Favorite button
                                IconButton(onClick = { onToggleFavorite(item.path) }) {
                                    Icon(
                                        Icons.Filled.Favorite,
                                        contentDescription = "Remove from favorites",
                                        tint = MaterialTheme.colors.primary
                                    )
                                }

                                // Drag handle: start reorder from here so vertical scrolling still works elsewhere.
                                Box(
                                    modifier = Modifier
                                        .padding(start = 4.dp)
                                        .pointerInput(path) {
                                            detectDragGestures(
                                                onDragStart = {
                                                    draggingIndex = index
                                                    dragOffsetY = 0f
                                                    didReorder = false
                                                },
                                                onDragCancel = {
                                                    val changed = didReorder
                                                    draggingIndex = null
                                                    dragOffsetY = 0f
                                                    didReorder = false
                                                    if (changed) onReorderFinished()
                                                },
                                                onDragEnd = {
                                                    val changed = didReorder
                                                    draggingIndex = null
                                                    dragOffsetY = 0f
                                                    didReorder = false
                                                    if (changed) onReorderFinished()
                                                },
                                                onDrag = { change, dragAmount ->
                                                    change.consume()

                                                    val currentIndex = draggingIndex ?: return@detectDragGestures
                                                    dragOffsetY += dragAmount.y

                                                    val currentPath = viewModel.favorites.getOrNull(currentIndex) ?: return@detectDragGestures
                                                    val currentHeight = (itemHeights[currentPath]?.toFloat() ?: 0f)
                                                    if (currentHeight <= 0f) return@detectDragGestures

                                                    // Swap with next/previous when dragged past half an item.
                                                    if (dragOffsetY > currentHeight / 2f && currentIndex < viewModel.favorites.lastIndex) {
                                                        onMoveFavorite(currentIndex, currentIndex + 1)
                                                        didReorder = true
                                                        val newIndex = currentIndex + 1
                                                        draggingIndex = newIndex

                                                        val nextPath = viewModel.favorites.getOrNull(newIndex) ?: return@detectDragGestures
                                                        val nextHeight = (itemHeights[nextPath]?.toFloat() ?: currentHeight)
                                                        dragOffsetY -= nextHeight
                                                    } else if (dragOffsetY < -currentHeight / 2f && currentIndex > 0) {
                                                        onMoveFavorite(currentIndex, currentIndex - 1)
                                                        didReorder = true
                                                        val newIndex = currentIndex - 1
                                                        draggingIndex = newIndex

                                                        val prevPath = viewModel.favorites.getOrNull(newIndex) ?: return@detectDragGestures
                                                        val prevHeight = (itemHeights[prevPath]?.toFloat() ?: currentHeight)
                                                        dragOffsetY += prevHeight
                                                    }
                                                }
                                            )
                                        }
                                ) {
                                    Icon(
                                        Icons.Filled.DragHandle,
                                        contentDescription = "Reorder",
                                        tint = MaterialTheme.colors.onBackground.copy(alpha = 0.6f)
                                    )
                                }
                            }
                        }
                    }

                    if (index < viewModel.favorites.size - 1) {
                        Divider(color = Color.Gray.copy(alpha = 0.2f))
                    }
                }
            }
        }
    }
}

@Composable
fun SongListItem(
    item: PlaylistItem,
    isFavorite: Boolean,
    onClick: () -> Unit,
    onToggleFavorite: () -> Unit
) {
    Surface(
        modifier = Modifier
            .fillMaxWidth()
            .clickable(onClick = onClick),
        color = Color.Transparent
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 16.dp, vertical = 12.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            // Music icon
            Icon(
                Icons.Filled.MusicNote,
                contentDescription = null,
                tint = MaterialTheme.colors.primary,
                modifier = Modifier.size(40.dp)
            )
            
            Spacer(modifier = Modifier.width(12.dp))
            
            // Song info
            Column(modifier = Modifier.weight(1f)) {
                Text(
                    text = item.title,
                    fontSize = 14.sp,
                    fontWeight = FontWeight.Normal,
                    color = MaterialTheme.colors.onBackground,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis
                )
            }
            
            // Duration
            Text(
                text = "-:--",
                fontSize = 12.sp,
                color = MaterialTheme.colors.onBackground.copy(alpha = 0.6f),
                modifier = Modifier.padding(end = 12.dp)
            )
            
            // Favorite button
            IconButton(onClick = onToggleFavorite) {
                Icon(
                    if (isFavorite) Icons.Filled.Favorite else Icons.Filled.FavoriteBorder,
                    contentDescription = "Favorite",
                    tint = if (isFavorite) MaterialTheme.colors.primary else Color.Gray
                )
            }
        }
    }
}

private fun formatFileSize(bytes: Long): String {
    if (bytes < 0) return "0 B"
    val units = arrayOf("B", "KB", "MB", "GB", "TB")
    var value = bytes.toDouble()
    var unitIndex = 0
    while (value >= 1024.0 && unitIndex < units.lastIndex) {
        value /= 1024.0
        unitIndex++
    }
    return if (unitIndex == 0) {
        "${bytes.toLong()} ${units[unitIndex]}"
    } else {
        String.format("%.1f %s", value, units[unitIndex])
    }
}

private const val BANK_SIZE_LIMIT_BYTES: Long = 4L * 1024L * 1024L * 1024L

@Composable
fun FolderSongListItem(
    item: PlaylistItem,
    isFavorite: Boolean,
    onClick: () -> Unit,
    onToggleFavorite: () -> Unit,
    onAddToPlaylist: () -> Unit
) {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        color = Color.Transparent
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 16.dp, vertical = 12.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            // File type badge (clickable to play)
            Box(
                modifier = Modifier
                    .size(width = 50.dp, height = 40.dp)
                    .clickable(onClick = onClick),
                contentAlignment = Alignment.Center
            ) {
                Surface(
                    modifier = Modifier
                        .wrapContentSize()
                        .padding(4.dp),
                    shape = RoundedCornerShape(4.dp),
                    color = MaterialTheme.colors.primary.copy(alpha = 0.15f)
                ) {
                    val fileExt = item.file.extension.uppercase()
                    val displayText = when (fileExt) {
                        "MID" -> "MIDI"
                        "MIDI" -> "MIDI"
                        "RMI" -> "RMI"
                        "RMF" -> "RMF"
                        "XMF" -> "XMF"
                        "MXMF" -> "MXMF"
                        "KAR" -> "KAR"
                        else -> fileExt
                    }
                    Text(
                        text = displayText,
                        fontSize = 9.sp,
                        fontWeight = FontWeight.Bold,
                        color = MaterialTheme.colors.primary,
                        modifier = Modifier.padding(horizontal = 4.dp, vertical = 2.dp)
                    )
                }
            }
            
            Spacer(modifier = Modifier.width(4.dp))
            
            // Song info (clickable to play)
            Column(
                modifier = Modifier
                    .weight(1f)
                    .clickable(onClick = onClick)
            ) {
                Text(
                    text = item.title,
                    fontSize = 14.sp,
                    fontWeight = FontWeight.Normal,
                    color = MaterialTheme.colors.onBackground,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis
                )
            }
            
            // Favorite button
            IconButton(onClick = onToggleFavorite) {
                Icon(
                    if (isFavorite) Icons.Filled.Favorite else Icons.Filled.FavoriteBorder,
                    contentDescription = "Favorite",
                    tint = if (isFavorite) MaterialTheme.colors.primary else Color.Gray
                )
            }
        }
    }
}

@Composable
private fun formatTime(ms: Int): String {
    if (ms <= 0) return "0:00"
    val totalSeconds = ms / 1000
    val minutes = totalSeconds / 60
    val seconds = totalSeconds % 60
    return String.format("%d:%02d", minutes, seconds)
}

@Composable
fun SettingsScreenContent(
    bankName: String,
    isLoadingBank: Boolean,
    reverbType: Int,
    velocityCurve: Int,
    exportCodec: Int,
    searchResultLimit: Int,
    onLoadBuiltin: () -> Unit,
    onReverbChange: (Int) -> Unit,
    onCurveChange: (Int) -> Unit,
    onVolumeChange: (Int) -> Unit,
    onExportCodecChange: (Int) -> Unit,
    onSearchLimitChange: (Int) -> Unit,
    onOpenFileTypes: () -> Unit,
    onBrowseBanks: () -> Unit
) {
    val reverbOptions = listOf(
        "None", "Igor's Closet", "Igor's Garage", "Igor's Acoustic Lab",
        "Igor's Cavern", "Igor's Dungeon", "Small Reflections",
        "Early Reflections", "Basement", "Banquet Hall", "Catacombs"
    )
    
    val curveOptions = listOf("Beatnik Default", "Peaky S Curve", "WebTV Curve", "2x Exponential", "2x Linear")
    val exportCodecOptions = listOf("WAV", "OGG", "FLAC")
    val searchLimitOptions = listOf(
        250 to "250",
        500 to "500",
        750 to "750",
        1000 to "1,000",
        2500 to "2,500",
        5000 to "5,000",
        10000 to "10,000",
        25000 to "25,000",
        50000 to "50,000",
        -1 to "Unlimited"
    )
    
    var reverbExpanded by remember { mutableStateOf(false) }
    var curveExpanded by remember { mutableStateOf(false) }
    var exportCodecExpanded by remember { mutableStateOf(false) }
    var searchLimitExpanded by remember { mutableStateOf(false) }
    var showUnlimitedWarning by remember { mutableStateOf(false) }
    val scrollState = rememberScrollState()
    val configuration = LocalConfiguration.current
    val isLandscape = configuration.orientation == Configuration.ORIENTATION_LANDSCAPE
    
    Column(
        modifier = Modifier
            .fillMaxSize()
            .verticalScroll(scrollState)
            .padding(16.dp)
    ) {
        if (isLandscape) {
            // Landscape: 2-column layout
            // Sound Bank Button
            Card(
                modifier = Modifier.fillMaxWidth(),
                elevation = 4.dp,
                shape = RoundedCornerShape(12.dp)
            ) {
                Column(modifier = Modifier.padding(16.dp)) {
                    Row(
                        verticalAlignment = Alignment.CenterVertically,
                        modifier = Modifier.padding(bottom = 12.dp)
                    ) {
                        Icon(
                            Icons.Filled.LibraryMusic,
                            contentDescription = null,
                            tint = MaterialTheme.colors.primary,
                            modifier = Modifier.size(24.dp)
                        )
                        Spacer(modifier = Modifier.width(8.dp))
                        Text(
                            text = "Sound Bank",
                            style = MaterialTheme.typography.h6,
                            fontWeight = FontWeight.Bold,
                            color = MaterialTheme.colors.primary
                        )
                    }
                    
                    if (isLoadingBank) {
                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.Center,
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            CircularProgressIndicator(modifier = Modifier.size(24.dp))
                            Spacer(modifier = Modifier.width(12.dp))
                            Text("Loading bank...", style = MaterialTheme.typography.body2)
                        }
                    } else {
                        Surface(
                            modifier = Modifier.fillMaxWidth(),
                            shape = RoundedCornerShape(8.dp),
                            color = MaterialTheme.colors.primary.copy(alpha = 0.1f)
                        ) {
                            Row(
                                modifier = Modifier.padding(12.dp),
                                verticalAlignment = Alignment.CenterVertically
                            ) {
                                Icon(
                                    Icons.Filled.MusicNote,
                                    contentDescription = null,
                                    tint = MaterialTheme.colors.primary,
                                    modifier = Modifier.size(20.dp)
                                )
                                Spacer(modifier = Modifier.width(8.dp))
                                Text(
                                    text = bankName,
                                    style = MaterialTheme.typography.body1,
                                    fontWeight = FontWeight.SemiBold,
                                    color = MaterialTheme.colors.onSurface
                                )
                            }
                        }
                    }
                    
                    Spacer(modifier = Modifier.height(12.dp))
                    
                    Button(
                        onClick = onBrowseBanks,
                        modifier = Modifier.fillMaxWidth(),
                        enabled = !isLoadingBank
                    ) {
                        Icon(Icons.Filled.LibraryMusic, contentDescription = null, modifier = Modifier.size(18.dp))
                        Spacer(modifier = Modifier.width(8.dp))
                        Text("Change Sound Bank")
                    }
                    
                    Text(
                        text = "Supports HSB, SF2, SF3, DLS formats • Hot-swap: reloads current song automatically",
                        style = MaterialTheme.typography.caption,
                        color = MaterialTheme.colors.onSurface.copy(alpha = 0.6f),
                        modifier = Modifier.padding(top = 8.dp)
                    )
                }
            }
            
            Spacer(modifier = Modifier.height(16.dp))
            
            // Row 1: Reverb and Velocity Curve
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(16.dp)
            ) {
                // Reverb Section
                Card(
                    modifier = Modifier.weight(1f),
                    elevation = 4.dp,
                    shape = RoundedCornerShape(12.dp)
                ) {
            Column(modifier = Modifier.padding(16.dp)) {
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    modifier = Modifier.padding(bottom = 12.dp)
                ) {
                    Icon(
                        Icons.Filled.GraphicEq,
                        contentDescription = null,
                        tint = MaterialTheme.colors.primary,
                        modifier = Modifier.size(24.dp)
                    )
                    Spacer(modifier = Modifier.width(8.dp))
                    Text(
                        text = "Reverb",
                        style = MaterialTheme.typography.h6,
                        fontWeight = FontWeight.Bold,
                        color = MaterialTheme.colors.primary
                    )
                }
                
                Box {
                    OutlinedButton(
                        onClick = { reverbExpanded = true },
                        modifier = Modifier.fillMaxWidth()
                    ) {
                        Text(
                            text = reverbOptions.getOrNull(reverbType - 1) ?: "None",
                            modifier = Modifier.weight(1f)
                        )
                        Icon(Icons.Filled.ArrowDropDown, contentDescription = null)
                    }
                    DropdownMenu(
                        expanded = reverbExpanded,
                        onDismissRequest = { reverbExpanded = false }
                    ) {
                        reverbOptions.forEachIndexed { index, option ->
                            DropdownMenuItem(onClick = {
                                onReverbChange(index + 1)
                                reverbExpanded = false
                            }) {
                                Text(option)
                            }
                        }
                    }
                }
            }
        }
            }
            
            Spacer(modifier = Modifier.height(16.dp))
            
            // Row 2: Velocity Curve and Export Codec
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(16.dp)
            ) {
        // Velocity Curve Section
        Card(
            modifier = Modifier.weight(1f),
            elevation = 4.dp,
            shape = RoundedCornerShape(12.dp)
        ) {
            Column(modifier = Modifier.padding(16.dp)) {
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    modifier = Modifier.padding(bottom = 12.dp)
                ) {
                    Icon(
                        Icons.AutoMirrored.Filled.TrendingUp,
                        contentDescription = null,
                        tint = MaterialTheme.colors.primary,
                        modifier = Modifier.size(24.dp)
                    )
                    Spacer(modifier = Modifier.width(8.dp))
                    Text(
                        text = "Velocity Curve (HSB Only)",
                        style = MaterialTheme.typography.h6,
                        fontWeight = FontWeight.Bold,
                        color = MaterialTheme.colors.primary
                    )
                }
                
                Box {
                    OutlinedButton(
                        onClick = { curveExpanded = true },
                        modifier = Modifier.fillMaxWidth()
                    ) {
                        Text(
                            text = curveOptions.getOrNull(velocityCurve) ?: "Default",
                            modifier = Modifier.weight(1f)
                        )
                        Icon(Icons.Filled.ArrowDropDown, contentDescription = null)
                    }
                    DropdownMenu(
                        expanded = curveExpanded,
                        onDismissRequest = { curveExpanded = false }
                    ) {
                        curveOptions.forEachIndexed { index, option ->
                            DropdownMenuItem(onClick = {
                                onCurveChange(index)
                                curveExpanded = false
                            }) {
                                Text(option)
                            }
                        }
                    }
                }
            }
        }
        
                // Export Codec Section
                Card(
                    modifier = Modifier.weight(1f),
                    elevation = 4.dp,
                    shape = RoundedCornerShape(12.dp)
                ) {
            Column(modifier = Modifier.padding(16.dp)) {
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    modifier = Modifier.padding(bottom = 12.dp)
                ) {
                    Icon(
                        Icons.Filled.GetApp,
                        contentDescription = null,
                        tint = MaterialTheme.colors.primary,
                        modifier = Modifier.size(24.dp)
                    )
                    Spacer(modifier = Modifier.width(8.dp))
                    Text(
                        text = "Export Codec",
                        style = MaterialTheme.typography.h6,
                        fontWeight = FontWeight.Bold,
                        color = MaterialTheme.colors.primary
                    )
                }
                
                Box {
                    OutlinedButton(
                        onClick = { exportCodecExpanded = true },
                        modifier = Modifier.fillMaxWidth()
                    ) {
                        Text(
                            text = exportCodecOptions.getOrNull(exportCodec - 1) ?: "OGG",
                            modifier = Modifier.weight(1f)
                        )
                        Icon(Icons.Filled.ArrowDropDown, contentDescription = null)
                    }
                    DropdownMenu(
                        expanded = exportCodecExpanded,
                        onDismissRequest = { exportCodecExpanded = false }
                    ) {
                        exportCodecOptions.forEachIndexed { index, option ->
                            DropdownMenuItem(onClick = {
                                onExportCodecChange(index + 1)
                                exportCodecExpanded = false
                            }) {
                                Text(option)
                            }
                        }
                    }
                }
            }
        }
            }
            
            Spacer(modifier = Modifier.height(16.dp))

        // File Types entry (full width in landscape)
        Card(
            modifier = Modifier.fillMaxWidth(),
            elevation = 4.dp,
            shape = RoundedCornerShape(12.dp)
        ) {
            Column(modifier = Modifier.padding(16.dp)) {
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    modifier = Modifier.padding(bottom = 12.dp)
                ) {
                    Icon(
                        Icons.Filled.Audiotrack,
                        contentDescription = null,
                        tint = MaterialTheme.colors.primary,
                        modifier = Modifier.size(24.dp)
                    )
                    Spacer(modifier = Modifier.width(8.dp))
                    Text(
                        text = "File Types",
                        style = MaterialTheme.typography.h6,
                        fontWeight = FontWeight.Bold,
                        color = MaterialTheme.colors.primary
                    )
                }

                Text(
                    text = "Choose which file types appear in Home and Search",
                    style = MaterialTheme.typography.caption,
                    color = MaterialTheme.colors.onSurface.copy(alpha = 0.6f)
                )

                Spacer(modifier = Modifier.height(12.dp))

                Button(
                    onClick = onOpenFileTypes,
                    modifier = Modifier.fillMaxWidth()
                ) {
                    Icon(Icons.Filled.Audiotrack, contentDescription = null, modifier = Modifier.size(18.dp))
                    Spacer(modifier = Modifier.width(8.dp))
                    Text("Choose File Types")
                }
            }
        }

        Spacer(modifier = Modifier.height(16.dp))
        
        // Search Result Limit Section (full width in landscape)
        Card(
            modifier = Modifier.fillMaxWidth(),
            elevation = 4.dp,
            shape = RoundedCornerShape(12.dp)
        ) {
            Column(modifier = Modifier.padding(16.dp)) {
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    modifier = Modifier.padding(bottom = 12.dp)
                ) {
                    Icon(
                        Icons.Filled.Search,
                        contentDescription = null,
                        tint = MaterialTheme.colors.primary,
                        modifier = Modifier.size(24.dp)
                    )
                    Spacer(modifier = Modifier.width(8.dp))
                    Text(
                        text = "Search Result Limit",
                        style = MaterialTheme.typography.h6,
                        fontWeight = FontWeight.Bold,
                        color = MaterialTheme.colors.primary
                    )
                }
                
                Box {
                    OutlinedButton(
                        onClick = { searchLimitExpanded = true },
                        modifier = Modifier.fillMaxWidth()
                    ) {
                        Text(
                            text = searchLimitOptions.find { it.first == searchResultLimit }?.second ?: "1,000",
                            modifier = Modifier.weight(1f)
                        )
                        Icon(Icons.Filled.ArrowDropDown, contentDescription = null)
                    }
                    DropdownMenu(
                        expanded = searchLimitExpanded,
                        onDismissRequest = { searchLimitExpanded = false }
                    ) {
                        searchLimitOptions.forEach { (value, label) ->
                            DropdownMenuItem(onClick = {
                                if (value == -1) {
                                    // Show warning for unlimited
                                    showUnlimitedWarning = true
                                    searchLimitExpanded = false
                                } else {
                                    onSearchLimitChange(value)
                                    searchLimitExpanded = false
                                }
                            }) {
                                Text(label)
                            }
                        }
                    }
                }
                
                Text(
                    text = "Maximum number of search results to display. Lower limits improve performance.",
                    style = MaterialTheme.typography.caption,
                    color = MaterialTheme.colors.onSurface.copy(alpha = 0.6f),
                    modifier = Modifier.padding(top = 8.dp)
                )
            }
        }
        
        // Warning dialog for unlimited option
        if (showUnlimitedWarning) {
            androidx.compose.material.AlertDialog(
                onDismissRequest = { showUnlimitedWarning = false },
                title = { Text("Warning") },
                text = {
                    Text("Setting the search limit to Unlimited may cause slowdowns and increased memory usage with large databases. Are you sure you want to continue?")
                },
                confirmButton = {
                    androidx.compose.material.TextButton(
                        onClick = {
                            onSearchLimitChange(-1)
                            showUnlimitedWarning = false
                        }
                    ) {
                        Text("Yes, Unlimited")
                    }
                },
                dismissButton = {
                    androidx.compose.material.TextButton(
                        onClick = { showUnlimitedWarning = false }
                    ) {
                        Text("Cancel")
                    }
                }
            )
        }
        } else {
            // Portrait: vertical layout (original)
            // Sound Bank Section
            Card(
                modifier = Modifier.fillMaxWidth(),
                elevation = 4.dp,
                shape = RoundedCornerShape(12.dp)
            ) {
                Column(modifier = Modifier.padding(16.dp)) {
                    Row(
                        verticalAlignment = Alignment.CenterVertically,
                        modifier = Modifier.padding(bottom = 12.dp)
                    ) {
                        Icon(
                            Icons.Filled.LibraryMusic,
                            contentDescription = null,
                            tint = MaterialTheme.colors.primary,
                            modifier = Modifier.size(24.dp)
                        )
                        Spacer(modifier = Modifier.width(8.dp))
                        Text(
                            text = "Sound Bank",
                            style = MaterialTheme.typography.h6,
                            fontWeight = FontWeight.Bold,
                            color = MaterialTheme.colors.primary
                        )
                    }
                    
                    if (isLoadingBank) {
                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.Center,
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            CircularProgressIndicator(modifier = Modifier.size(24.dp))
                            Spacer(modifier = Modifier.width(12.dp))
                            Text("Loading bank...", style = MaterialTheme.typography.body2)
                        }
                    } else {
                        Surface(
                            modifier = Modifier.fillMaxWidth(),
                            shape = RoundedCornerShape(8.dp),
                            color = MaterialTheme.colors.primary.copy(alpha = 0.1f)
                        ) {
                            Row(
                                modifier = Modifier.padding(12.dp),
                                verticalAlignment = Alignment.CenterVertically
                            ) {
                                Icon(
                                    Icons.Filled.MusicNote,
                                    contentDescription = null,
                                    tint = MaterialTheme.colors.primary,
                                    modifier = Modifier.size(20.dp)
                                )
                                Spacer(modifier = Modifier.width(8.dp))
                                Text(
                                    text = bankName,
                                    style = MaterialTheme.typography.body1,
                                    fontWeight = FontWeight.SemiBold,
                                    color = MaterialTheme.colors.onSurface
                                )
                            }
                        }
                    }
                    
                    Spacer(modifier = Modifier.height(12.dp))
                    
                    Button(
                        onClick = onBrowseBanks,
                        modifier = Modifier.fillMaxWidth(),
                        enabled = !isLoadingBank
                    ) {
                        Icon(Icons.Filled.LibraryMusic, contentDescription = null, modifier = Modifier.size(18.dp))
                        Spacer(modifier = Modifier.width(8.dp))
                        Text("Change Sound Bank")
                    }
                    
                    Text(
                        text = "Supports HSB, SF2, SF3, DLS formats • Hot-swap: reloads current song automatically",
                        style = MaterialTheme.typography.caption,
                        color = MaterialTheme.colors.onSurface.copy(alpha = 0.6f),
                        modifier = Modifier.padding(top = 8.dp)
                    )
                }
            }
            
            Spacer(modifier = Modifier.height(16.dp))
            
            // Reverb Section
            Card(
                modifier = Modifier.fillMaxWidth(),
                elevation = 4.dp,
                shape = RoundedCornerShape(12.dp)
            ) {
                Column(modifier = Modifier.padding(16.dp)) {
                    Row(
                        verticalAlignment = Alignment.CenterVertically,
                        modifier = Modifier.padding(bottom = 12.dp)
                    ) {
                        Icon(
                            Icons.Filled.GraphicEq,
                            contentDescription = null,
                            tint = MaterialTheme.colors.primary,
                            modifier = Modifier.size(24.dp)
                        )
                        Spacer(modifier = Modifier.width(8.dp))
                        Text(
                            text = "Reverb",
                            style = MaterialTheme.typography.h6,
                            fontWeight = FontWeight.Bold,
                            color = MaterialTheme.colors.primary
                        )
                    }
                    
                    Box {
                        OutlinedButton(
                            onClick = { reverbExpanded = true },
                            modifier = Modifier.fillMaxWidth()
                        ) {
                            Text(
                                text = reverbOptions.getOrNull(reverbType - 1) ?: "None",
                                modifier = Modifier.weight(1f)
                            )
                            Icon(Icons.Filled.ArrowDropDown, contentDescription = null)
                        }
                        DropdownMenu(
                            expanded = reverbExpanded,
                            onDismissRequest = { reverbExpanded = false }
                        ) {
                            reverbOptions.forEachIndexed { index, option ->
                                DropdownMenuItem(onClick = {
                                    onReverbChange(index + 1)
                                    reverbExpanded = false
                                }) {
                                    Text(option)
                                }
                            }
                        }
                    }
                }
            }
            
            Spacer(modifier = Modifier.height(16.dp))
            
            // Velocity Curve Section
            Card(
                modifier = Modifier.fillMaxWidth(),
                elevation = 4.dp,
                shape = RoundedCornerShape(12.dp)
            ) {
                Column(modifier = Modifier.padding(16.dp)) {
                    Row(
                        verticalAlignment = Alignment.CenterVertically,
                        modifier = Modifier.padding(bottom = 12.dp)
                    ) {
                        Icon(
                            Icons.AutoMirrored.Filled.TrendingUp,
                            contentDescription = null,
                            tint = MaterialTheme.colors.primary,
                            modifier = Modifier.size(24.dp)
                        )
                        Spacer(modifier = Modifier.width(8.dp))
                        Text(
                            text = "Velocity Curve (HSB Only)",
                            style = MaterialTheme.typography.h6,
                            fontWeight = FontWeight.Bold,
                            color = MaterialTheme.colors.primary
                        )
                    }
                    
                    Box {
                        OutlinedButton(
                            onClick = { curveExpanded = true },
                            modifier = Modifier.fillMaxWidth()
                        ) {
                            Text(
                                text = curveOptions.getOrNull(velocityCurve) ?: "Default",
                                modifier = Modifier.weight(1f)
                            )
                            Icon(Icons.Filled.ArrowDropDown, contentDescription = null)
                        }
                        DropdownMenu(
                            expanded = curveExpanded,
                            onDismissRequest = { curveExpanded = false }
                        ) {
                            curveOptions.forEachIndexed { index, option ->
                                DropdownMenuItem(onClick = {
                                    onCurveChange(index)
                                    curveExpanded = false
                                }) {
                                    Text(option)
                                }
                            }
                        }
                    }
                }
            }
            
            Spacer(modifier = Modifier.height(16.dp))
            
            // Export Codec Section
            Card(
                modifier = Modifier.fillMaxWidth(),
                elevation = 4.dp,
                shape = RoundedCornerShape(12.dp)
            ) {
                Column(modifier = Modifier.padding(16.dp)) {
                    Row(
                        verticalAlignment = Alignment.CenterVertically,
                        modifier = Modifier.padding(bottom = 12.dp)
                    ) {
                        Icon(
                            Icons.Filled.GetApp,
                            contentDescription = null,
                            tint = MaterialTheme.colors.primary,
                            modifier = Modifier.size(24.dp)
                        )
                        Spacer(modifier = Modifier.width(8.dp))
                        Text(
                            text = "Export Codec",
                            style = MaterialTheme.typography.h6,
                            fontWeight = FontWeight.Bold,
                            color = MaterialTheme.colors.primary
                        )
                    }
                    
                    Box {
                        OutlinedButton(
                            onClick = { exportCodecExpanded = true },
                            modifier = Modifier.fillMaxWidth()
                        ) {
                            Text(
                                text = exportCodecOptions.getOrNull(exportCodec - 1) ?: "OGG",
                                modifier = Modifier.weight(1f)
                            )
                            Icon(Icons.Filled.ArrowDropDown, contentDescription = null)
                        }
                        DropdownMenu(
                            expanded = exportCodecExpanded,
                            onDismissRequest = { exportCodecExpanded = false }
                        ) {
                            exportCodecOptions.forEachIndexed { index, option ->
                                DropdownMenuItem(onClick = {
                                    onExportCodecChange(index + 1)
                                    exportCodecExpanded = false
                                }) {
                                    Text(option)
                                }
                            }
                        }
                    }
                }
            }
            
            Spacer(modifier = Modifier.height(16.dp))

            // File Types entry
            Card(
                modifier = Modifier.fillMaxWidth(),
                elevation = 4.dp,
                shape = RoundedCornerShape(12.dp)
            ) {
                Column(modifier = Modifier.padding(16.dp)) {
                    Row(
                        verticalAlignment = Alignment.CenterVertically,
                        modifier = Modifier.padding(bottom = 12.dp)
                    ) {
                        Icon(
                            Icons.Filled.Audiotrack,
                            contentDescription = null,
                            tint = MaterialTheme.colors.primary,
                            modifier = Modifier.size(24.dp)
                        )
                        Spacer(modifier = Modifier.width(8.dp))
                        Text(
                            text = "File Types",
                            style = MaterialTheme.typography.h6,
                            fontWeight = FontWeight.Bold,
                            color = MaterialTheme.colors.primary
                        )
                    }

                    Text(
                        text = "Choose which file types appear in Home",
                        style = MaterialTheme.typography.caption,
                        color = MaterialTheme.colors.onSurface.copy(alpha = 0.6f)
                    )

                    Spacer(modifier = Modifier.height(12.dp))

                    Button(
                        onClick = onOpenFileTypes,
                        modifier = Modifier.fillMaxWidth()
                    ) {
                        Icon(Icons.Filled.Audiotrack, contentDescription = null, modifier = Modifier.size(18.dp))
                        Spacer(modifier = Modifier.width(8.dp))
                        Text("Choose File Types")
                    }
                }
            }

            Spacer(modifier = Modifier.height(16.dp))
            
            // Search Result Limit Section
            Card(
                modifier = Modifier.fillMaxWidth(),
                elevation = 4.dp,
                shape = RoundedCornerShape(12.dp)
            ) {
                Column(modifier = Modifier.padding(16.dp)) {
                    Row(
                        verticalAlignment = Alignment.CenterVertically,
                        modifier = Modifier.padding(bottom = 12.dp)
                    ) {
                        Icon(
                            Icons.Filled.Search,
                            contentDescription = null,
                            tint = MaterialTheme.colors.primary,
                            modifier = Modifier.size(24.dp)
                        )
                        Spacer(modifier = Modifier.width(8.dp))
                        Text(
                            text = "Search Result Limit",
                            style = MaterialTheme.typography.h6,
                            fontWeight = FontWeight.Bold,
                            color = MaterialTheme.colors.primary
                        )
                    }
                    
                    Box {
                        OutlinedButton(
                            onClick = { searchLimitExpanded = true },
                            modifier = Modifier.fillMaxWidth()
                        ) {
                            Text(
                                text = searchLimitOptions.find { it.first == searchResultLimit }?.second ?: "1,000",
                                modifier = Modifier.weight(1f)
                            )
                            Icon(Icons.Filled.ArrowDropDown, contentDescription = null)
                        }
                        DropdownMenu(
                            expanded = searchLimitExpanded,
                            onDismissRequest = { searchLimitExpanded = false }
                        ) {
                            searchLimitOptions.forEach { (value, label) ->
                                DropdownMenuItem(onClick = {
                                    if (value == -1) {
                                        // Show warning for unlimited
                                        showUnlimitedWarning = true
                                        searchLimitExpanded = false
                                    } else {
                                        onSearchLimitChange(value)
                                        searchLimitExpanded = false
                                    }
                                }) {
                                    Text(label)
                                }
                            }
                        }
                    }
                    
                    Text(
                        text = "Maximum number of search results to display. Lower limits improve performance.",
                        style = MaterialTheme.typography.caption,
                        color = MaterialTheme.colors.onSurface.copy(alpha = 0.6f),
                        modifier = Modifier.padding(top = 8.dp)
                    )
                }
            }
        }
        
        // Warning dialog for unlimited option (outside if/else)
        if (showUnlimitedWarning) {
            androidx.compose.material.AlertDialog(
                onDismissRequest = { showUnlimitedWarning = false },
                title = { Text("Warning") },
                text = {
                    Text("Setting the search limit to Unlimited may cause slowdowns and increased memory usage with large databases. Are you sure you want to continue?")
                },
                confirmButton = {
                    androidx.compose.material.TextButton(
                        onClick = {
                            onSearchLimitChange(-1)
                            showUnlimitedWarning = false
                        }
                    ) {
                        Text("Yes, Unlimited")
                    }
                },
                dismissButton = {
                    androidx.compose.material.TextButton(
                        onClick = { showUnlimitedWarning = false }
                    ) {
                        Text("Cancel")
                    }
                }
            )
        }
        
        Spacer(modifier = Modifier.height(16.dp))
        
        // About Section (same for both orientations)
        Card(
            modifier = Modifier.fillMaxWidth(),
            elevation = 4.dp,
            shape = RoundedCornerShape(12.dp)
        ) {
            Column(modifier = Modifier.padding(16.dp)) {
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    modifier = Modifier.padding(bottom = 12.dp)
                ) {
                    Icon(
                        Icons.Filled.Info,
                        contentDescription = null,
                        tint = MaterialTheme.colors.primary,
                        modifier = Modifier.size(24.dp)
                    )
                    Spacer(modifier = Modifier.width(8.dp))
                    Text(
                        text = "About",
                        style = MaterialTheme.typography.h6,
                        fontWeight = FontWeight.Bold,
                        color = MaterialTheme.colors.primary
                    )
                }
                
                val baeVersion = remember { Mixer.getVersion() ?: "Unknown" }
                val baeCompileInfo = remember { Mixer.getCompileInfo() ?: "Unknown" }
                val baeFeatures = remember { Mixer.getFeatureString() ?: "" }
                
                Text(
                    text = "miniBAE for Android",
                    style = MaterialTheme.typography.body1,
                    fontWeight = FontWeight.SemiBold
                )
                Spacer(modifier = Modifier.height(4.dp))
                Text(
                    text = "A cross-platform audio engine for MIDI playback",
                    style = MaterialTheme.typography.body2,
                    color = Color.Gray
                )
                
                if (baeVersion.isNotEmpty()) {
                    Spacer(modifier = Modifier.height(12.dp))
                    Text(
                        text = "Version: $baeVersion",
                        style = MaterialTheme.typography.body2,
                        color = MaterialTheme.colors.onSurface
                    )
                }
                
                if (baeCompileInfo.isNotEmpty()) {
                    Spacer(modifier = Modifier.height(4.dp))
                    Text(
                        text = "Compiled with: $baeCompileInfo",
                        style = MaterialTheme.typography.body2,
                        color = MaterialTheme.colors.onSurface
                    )
                }
                
                if (baeFeatures.isNotEmpty()) {
                    Spacer(modifier = Modifier.height(8.dp))
                    Text(
                        text = "Features:",
                        style = MaterialTheme.typography.caption,
                        fontWeight = FontWeight.SemiBold,
                        color = MaterialTheme.colors.onSurface.copy(alpha = 0.7f)
                    )
                    Spacer(modifier = Modifier.height(2.dp))
                    Text(
                        text = baeFeatures,
                        style = MaterialTheme.typography.caption,
                        color = MaterialTheme.colors.onSurface.copy(alpha = 0.6f)
                    )
                }
            }
        }
    }
}

@Composable
fun FileTypesScreenContent(
    enabledExtensions: Set<String>,
    onExtensionEnabledChange: (String, Boolean) -> Unit
) {
    val scrollState = rememberScrollState()
    val context = LocalContext.current
    val supportedExtensions = remember { HomeFragment.getSupportedExtensionsForSettings(context) }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .verticalScroll(scrollState)
            .padding(16.dp)
    ) {
        Card(
            modifier = Modifier.fillMaxWidth(),
            elevation = 4.dp,
            shape = RoundedCornerShape(12.dp)
        ) {
            Column(modifier = Modifier.padding(16.dp)) {
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    modifier = Modifier.padding(bottom = 12.dp)
                ) {
                    Icon(
                        Icons.Filled.Audiotrack,
                        contentDescription = null,
                        tint = MaterialTheme.colors.primary,
                        modifier = Modifier.size(24.dp)
                    )
                    Spacer(modifier = Modifier.width(8.dp))
                    Text(
                        text = "File Types",
                        style = MaterialTheme.typography.h6,
                        fontWeight = FontWeight.Bold,
                        color = MaterialTheme.colors.primary
                    )
                }

                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(bottom = 12.dp),
                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    OutlinedButton(
                        onClick = {
                            supportedExtensions.forEach { ext ->
                                if (!enabledExtensions.contains(ext)) {
                                    onExtensionEnabledChange(ext, true)
                                }
                            }
                        }
                    ) {
                        Text("Check All")
                    }

                    OutlinedButton(
                        onClick = {
                            supportedExtensions.forEach { ext ->
                                val shouldEnable = !HomeFragment.isSoundExtension(ext)
                                if (enabledExtensions.contains(ext) != shouldEnable) {
                                    onExtensionEnabledChange(ext, shouldEnable)
                                }
                            }
                        }
                    ) {
                        Text("MIDI")
                    }

                    OutlinedButton(
                        onClick = {
                            supportedExtensions.forEach { ext ->
                                val shouldEnable = HomeFragment.isSoundExtension(ext)
                                if (enabledExtensions.contains(ext) != shouldEnable) {
                                    onExtensionEnabledChange(ext, shouldEnable)
                                }
                            }
                        }
                    ) {
                        Text("Audio")
                    }
                }

                supportedExtensions.forEach { ext ->
                    val checked = enabledExtensions.contains(ext)
                    Row(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(vertical = 4.dp)
                            .clickable { onExtensionEnabledChange(ext, !checked) },
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Checkbox(
                            checked = checked,
                            onCheckedChange = { onExtensionEnabledChange(ext, it) }
                        )
                        Spacer(modifier = Modifier.width(8.dp))
                        Text(
                            text = ".$ext",
                            style = MaterialTheme.typography.body1
                        )
                    }
                }
            }
        }
    }
}

@Composable
fun BankBrowserScreen(
    currentPath: String,
    files: List<PlaylistItem>,
    isLoading: Boolean,
    onNavigate: (String) -> Unit,
    onSelectBank: (File) -> Unit,
    onLoadBuiltin: () -> Unit,
    onClose: () -> Unit
) {
    Column(modifier = Modifier.fillMaxSize()) {
        // Current path bar
        Surface(
            modifier = Modifier.fillMaxWidth(),
            color = MaterialTheme.colors.surface,
            elevation = 2.dp
        ) {
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = 16.dp, vertical = 12.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                Icon(
                    Icons.Filled.Folder,
                    contentDescription = null,
                    tint = MaterialTheme.colors.onSurface.copy(alpha = 0.6f),
                    modifier = Modifier.size(16.dp)
                )
                Spacer(modifier = Modifier.width(8.dp))
                Text(
                    text = currentPath,
                    style = MaterialTheme.typography.caption,
                    color = MaterialTheme.colors.onSurface.copy(alpha = 0.7f),
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis
                )
            }
        }
        
        // File list
        when {
            isLoading -> {
                Box(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                    CircularProgressIndicator(color = MaterialTheme.colors.primary)
                }
            }
            files.isEmpty() -> {
                LazyColumn(modifier = Modifier.fillMaxSize()) {
                    // Built-in Patches option at the top
                    item {
                        Surface(
                            modifier = Modifier
                                .fillMaxWidth()
                                .clickable { 
                                    onLoadBuiltin()
                                    onClose()
                                },
                            color = MaterialTheme.colors.primary.copy(alpha = 0.05f)
                        ) {
                            Row(
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .padding(horizontal = 16.dp, vertical = 14.dp),
                                verticalAlignment = Alignment.CenterVertically
                            ) {
                                Icon(
                                    Icons.Filled.GetApp,
                                    contentDescription = null,
                                    tint = MaterialTheme.colors.secondary,
                                    modifier = Modifier.size(40.dp)
                                )
                                Spacer(modifier = Modifier.width(12.dp))
                                Column(modifier = Modifier.weight(1f)) {
                                    Text(
                                        text = "Built-in Patches",
                                        fontSize = 14.sp,
                                        fontWeight = FontWeight.Bold,
                                        color = MaterialTheme.colors.onBackground
                                    )
                                    Text(
                                        text = "Beatnik Standard sound bank",
                                        fontSize = 12.sp,
                                        color = MaterialTheme.colors.onBackground.copy(alpha = 0.6f)
                                    )
                                }
                                Icon(
                                    Icons.AutoMirrored.Filled.ArrowForward,
                                    contentDescription = null,
                                    tint = MaterialTheme.colors.onBackground.copy(alpha = 0.4f)
                                )
                            }
                        }
                        Divider(color = Color.Gray.copy(alpha = 0.2f))
                    }
                    
                    // Show parent directory ".." if not at root
                    val file = File(currentPath)
                    val parentPath = file.parent
                    if (parentPath != null && currentPath != "/") {
                        item {
                            Surface(
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .clickable { onNavigate(parentPath) },
                                color = Color.Transparent
                            ) {
                                Row(
                                    modifier = Modifier
                                        .fillMaxWidth()
                                        .padding(horizontal = 16.dp, vertical = 12.dp),
                                    verticalAlignment = Alignment.CenterVertically
                                ) {
                                    Icon(
                                        Icons.Filled.Folder,
                                        contentDescription = null,
                                        tint = MaterialTheme.colors.onBackground.copy(alpha = 0.6f),
                                        modifier = Modifier.size(40.dp)
                                    )
                                    Spacer(modifier = Modifier.width(12.dp))
                                    Text(
                                        text = "..",
                                        fontSize = 14.sp,
                                        fontWeight = FontWeight.Bold,
                                        color = MaterialTheme.colors.onBackground.copy(alpha = 0.6f)
                                    )
                                }
                            }
                            Divider(color = Color.Gray.copy(alpha = 0.2f))
                        }
                    }
                    
                    // Empty folder message
                    item {
                        Box(modifier = Modifier.fillMaxWidth().padding(vertical = 32.dp), contentAlignment = Alignment.Center) {
                            Column(horizontalAlignment = Alignment.CenterHorizontally) {
                                Icon(
                                    Icons.Filled.FolderOpen,
                                    contentDescription = null,
                                    modifier = Modifier.size(64.dp),
                                    tint = Color.Gray
                                )
                                Spacer(modifier = Modifier.height(16.dp))
                                Text("No bank files in this folder", color = Color.Gray)
                                Spacer(modifier = Modifier.height(8.dp))
                                Text(
                                    "Supported: .sf2, .sf3, .sfo, .hsb, .dls",
                                    fontSize = 12.sp,
                                    color = Color.Gray
                                )
                            }
                        }
                    }
                }
            }
            else -> {
                LazyColumn(modifier = Modifier.fillMaxSize()) {
                    // Built-in Patches option at the top
                    item {
                        Surface(
                            modifier = Modifier
                                .fillMaxWidth()
                                .clickable { 
                                    onLoadBuiltin()
                                    onClose()
                                },
                            color = MaterialTheme.colors.primary.copy(alpha = 0.05f)
                        ) {
                            Row(
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .padding(horizontal = 16.dp, vertical = 14.dp),
                                verticalAlignment = Alignment.CenterVertically
                            ) {
                                Icon(
                                    Icons.Filled.GetApp,
                                    contentDescription = null,
                                    tint = MaterialTheme.colors.secondary,
                                    modifier = Modifier.size(40.dp)
                                )
                                Spacer(modifier = Modifier.width(12.dp))
                                Column(modifier = Modifier.weight(1f)) {
                                    Text(
                                        text = "Built-in Patches",
                                        fontSize = 14.sp,
                                        fontWeight = FontWeight.Bold,
                                        color = MaterialTheme.colors.onBackground
                                    )
                                    Text(
                                        text = "Beatnik Standard sound bank",
                                        fontSize = 12.sp,
                                        color = MaterialTheme.colors.onBackground.copy(alpha = 0.6f)
                                    )
                                }
                                Icon(
                                    Icons.AutoMirrored.Filled.ArrowForward,
                                    contentDescription = null,
                                    tint = MaterialTheme.colors.onBackground.copy(alpha = 0.4f)
                                )
                            }
                        }
                        Divider(color = Color.Gray.copy(alpha = 0.2f))
                    }
                    
                    // Show parent directory ".." if not at root
                    val file = File(currentPath)
                    val parentPath = file.parent
                    if (parentPath != null && currentPath != "/") {
                        item {
                            Surface(
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .clickable { onNavigate(parentPath) },
                                color = Color.Transparent
                            ) {
                                Row(
                                    modifier = Modifier
                                        .fillMaxWidth()
                                        .padding(horizontal = 16.dp, vertical = 12.dp),
                                    verticalAlignment = Alignment.CenterVertically
                                ) {
                                    Icon(
                                        Icons.Filled.Folder,
                                        contentDescription = null,
                                        tint = MaterialTheme.colors.onBackground.copy(alpha = 0.6f),
                                        modifier = Modifier.size(40.dp)
                                    )
                                    Spacer(modifier = Modifier.width(12.dp))
                                    Text(
                                        text = "..",
                                        fontSize = 14.sp,
                                        fontWeight = FontWeight.Bold,
                                        color = MaterialTheme.colors.onBackground.copy(alpha = 0.6f)
                                    )
                                }
                            }
                            Divider(color = Color.Gray.copy(alpha = 0.2f))
                        }
                    }
                    
                    // Show folders
                    itemsIndexed(files.filter { it.isFolder }) { _, item ->
                        Surface(
                            modifier = Modifier
                                .fillMaxWidth()
                                .clickable { onNavigate(item.path) },
                            color = Color.Transparent
                        ) {
                            Row(
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .padding(horizontal = 16.dp, vertical = 12.dp),
                                verticalAlignment = Alignment.CenterVertically
                            ) {
                                Icon(
                                    Icons.Filled.Folder,
                                    contentDescription = null,
                                    tint = MaterialTheme.colors.primary,
                                    modifier = Modifier.size(40.dp)
                                )
                                Spacer(modifier = Modifier.width(12.dp))
                                Text(
                                    text = item.title,
                                    fontSize = 14.sp,
                                    fontWeight = FontWeight.Bold,
                                    color = MaterialTheme.colors.onBackground
                                )
                            }
                        }
                        Divider(color = Color.Gray.copy(alpha = 0.2f))
                    }
                    
                    // Show bank files
                    itemsIndexed(files.filter { !it.isFolder }) { _, item ->
                        val fileSizeBytes = runCatching { item.file.length() }.getOrDefault(0L)
                        val isTooLarge = fileSizeBytes >= BANK_SIZE_LIMIT_BYTES
                        val disabledColor = MaterialTheme.colors.onBackground.copy(alpha = 0.38f)
                        val primaryTextColor = if (isTooLarge) disabledColor else MaterialTheme.colors.onBackground
                        val secondaryTextColor = if (isTooLarge) disabledColor else MaterialTheme.colors.onBackground.copy(alpha = 0.6f)
                        Surface(
                            modifier = Modifier
                                .fillMaxWidth()
                                .clickable(enabled = !isTooLarge) { onSelectBank(item.file) },
                            color = Color.Transparent
                        ) {
                            Row(
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .padding(horizontal = 16.dp, vertical = 12.dp),
                                verticalAlignment = Alignment.CenterVertically
                            ) {
                                Icon(
                                    Icons.Filled.LibraryMusic,
                                    contentDescription = null,
                                    tint = if (isTooLarge) disabledColor else MaterialTheme.colors.secondary,
                                    modifier = Modifier.size(40.dp)
                                )
                                Spacer(modifier = Modifier.width(12.dp))
                                Column(modifier = Modifier.weight(1f)) {
                                    Text(
                                        text = item.title,
                                        fontSize = 14.sp,
                                        fontWeight = FontWeight.Normal,
                                        color = primaryTextColor,
                                        maxLines = 1,
                                        overflow = TextOverflow.Ellipsis
                                    )
                                    Text(
                                        text = run {
                                            val ext = item.file.extension.uppercase()
                                            if (fileSizeBytes > 0) "$ext • ${formatFileSize(fileSizeBytes)}" else ext
                                        },
                                        fontSize = 12.sp,
                                        color = secondaryTextColor
                                    )
                                }
                                Icon(
                                    Icons.AutoMirrored.Filled.ArrowForward,
                                    contentDescription = null,
                                    tint = if (isTooLarge) disabledColor else MaterialTheme.colors.onBackground.copy(alpha = 0.4f)
                                )
                            }
                        }
                        Divider(color = Color.Gray.copy(alpha = 0.2f))
                    }
                }
            }
        }
    }
}
