package com.zefie.miniBAEDroid

import android.R
import android.os.Bundle
import android.content.Context
import android.content.Intent
import android.os.Build
import android.os.Environment
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
import androidx.compose.foundation.gestures.detectHorizontalDragGestures
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.background
import androidx.compose.material.*
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
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.core.view.WindowCompat
import androidx.compose.runtime.collectAsState
import java.io.File
import org.minibae.Mixer
import org.minibae.Song
import kotlinx.coroutines.delay
import kotlinx.coroutines.Job
import kotlinx.coroutines.launch
import androidx.lifecycle.lifecycleScope

class HomeFragment : Fragment() {

    companion object {
        var velocityCurve = mutableStateOf(0)
    }

    private var mixerIdleJob: Job? = null
    private var pickedFolderUri: Uri? = null
    private lateinit var viewModel: MusicPlayerViewModel
    private lateinit var notificationHelper: MusicNotificationHelper
    private var isAppInForeground = mutableStateOf(true)
    
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
    
    // Bank browser state (completely separate from main browser)
    private var showBankBrowser = mutableStateOf(false)
    private var bankBrowserPath = mutableStateOf("/sdcard") // Will be loaded from prefs
    private var bankBrowserFiles = mutableStateListOf<PlaylistItem>()
    private var bankBrowserLoading = mutableStateOf(false)
    
    private val karaokeHandler = KaraokeHandler()

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
            savePlaylist()
            saveFavorites()
        } catch (ex: Exception) {
            Toast.makeText(requireContext(), "Failed to load file: ${ex.message}", Toast.LENGTH_SHORT).show()
        }
    }

    fun reloadCurrentSongForBankSwap() {
        (activity as? MainActivity)?.reloadCurrentSongForBankSwap()
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
        if (::notificationHelper.isInitialized) {
            notificationHelper.cleanup()
        }
    }

    private fun savePlaylist() {
        try {
            val prefs = requireContext().getSharedPreferences("miniBAE_prefs", Context.MODE_PRIVATE)
            val paths = viewModel.playlist.map { it.file.absolutePath }
            val json = paths.joinToString("|||")
            prefs.edit()
                .putString("savedPlaylist", json)
                .putInt("savedCurrentIndex", viewModel.currentIndex)
                .apply()
        } catch (ex: Exception) {
            android.util.Log.e("HomeFragment", "Failed to save playlist: ${ex.message}")
        }
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
    
    private fun loadPlaylist() {
        try {
            val prefs = requireContext().getSharedPreferences("miniBAE_prefs", Context.MODE_PRIVATE)
            val json = prefs.getString("savedPlaylist", null)
            if (json != null && json.isNotEmpty()) {
                val paths = json.split("|||")
                val items = paths.mapNotNull { path ->
                    val file = File(path)
                    if (file.exists()) PlaylistItem(file) else null
                }
                if (items.isNotEmpty()) {
                    viewModel.addAllToPlaylist(items)
                    val savedIndex = prefs.getInt("savedCurrentIndex", 0)
                    if (savedIndex in viewModel.playlist.indices) {
                        viewModel.currentIndex = savedIndex
                        viewModel.getCurrentItem()?.let { item ->
                            viewModel.currentTitle = item.title
                        }
                    }
                }
            }
        } catch (ex: Exception) {
            android.util.Log.e("HomeFragment", "Failed to load playlist: ${ex.message}")
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
                    velocityCurve.value = prefs.getInt("velocity_curve", 0)
                    exportCodec.value = prefs.getInt("export_codec", 2) // Default to OGG
                    
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
                
                    // Initialize notification helper
                    notificationHelper = MusicNotificationHelper(requireContext())
                    
                    // Register notification action callbacks
                    MusicNotificationReceiver.setCallbacks(
                        onPlayPause = { togglePlayPause() },
                        onNext = { playNext() },
                        onPrevious = { playPrevious() },
                        onClose = {
                            stopPlayback(true)
                            viewModel.isPlaying = false
                            if (::notificationHelper.isInitialized) {
                                notificationHelper.hideNotification()
                            }
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

                LaunchedEffect(viewModel.isPlaying, viewModel.isDraggingSeekBar) {
                    while (viewModel.isPlaying && !viewModel.isDraggingSeekBar) {
                        try {
                            val pos = getPlaybackPositionMs()
                            val len = getPlaybackLengthMs()
                            viewModel.currentPositionMs = pos
                            if (len > 0) viewModel.totalDurationMs = len
                            
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
                                        // Repeat current song
                                        seekPlaybackToMs(0)
                                        viewModel.currentPositionMs = 0
                                        currentSong?.start()
                                        currentSound?.start()
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
                
                // Update notification when playback state changes (only when backgrounded)
                LaunchedEffect(viewModel.isPlaying, viewModel.currentTitle, viewModel.currentIndex, isAppInForeground.value) {
                    if (!::notificationHelper.isInitialized) return@LaunchedEffect
                    
                    // Only show notification when app is in background
                    if (!isAppInForeground.value) {
                        val currentItem = viewModel.getCurrentItem()
                        if (currentItem != null && (viewModel.isPlaying || viewModel.currentTitle != "No song loaded")) {
                            val folderName = viewModel.currentFolderPath?.let { path ->
                                File(path).name
                            } ?: "Unknown Folder"
                            
                            notificationHelper.showNotification(
                                title = viewModel.currentTitle,
                                artist = folderName,
                                isPlaying = viewModel.isPlaying,
                                hasNext = viewModel.hasNext(),
                                hasPrevious = viewModel.hasPrevious(),
                                currentPosition = viewModel.currentPositionMs.toLong(),
                                duration = viewModel.totalDurationMs.toLong()
                            )
                        } else {
                            notificationHelper.hideNotification()
                        }
                    } else {
                        // Hide notification when app is in foreground
                        notificationHelper.hideNotification()
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
                            it.statusBarColor = android.graphics.Color.parseColor("#1E1E1E")
                            WindowCompat.getInsetsController(it, view)?.let { controller ->
                                controller.isAppearanceLightStatusBars = false
                            }
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
                                savePlaylist()
                                Toast.makeText(requireContext(), "Added to playlist", Toast.LENGTH_SHORT).show()
                            }
                        },
                        onRemoveFromPlaylist = { index ->
                            viewModel.removeFromPlaylist(index)
                            savePlaylist()
                        },
                        onClearPlaylist = {
                            viewModel.clearPlaylist()
                            savePlaylist()
                        },
                        onMoveItem = { from, to ->
                            viewModel.moveItem(from, to)
                            savePlaylist()
                        },
                        bankName = currentBankName.value,
                        isLoadingBank = isLoadingBank.value,
                        isExporting = isExporting.value,
                        exportStatus = exportStatus.value,
                        reverbType = reverbType.value,
                        velocityCurve = velocityCurve.value,
                        exportCodec = exportCodec.value,
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
                            loadFolderContents("/")
                        },
                        onRepeatModeChange = {
                            currentSong?.setLoops(if (viewModel.repeatMode == RepeatMode.SONG) 32768 else 0)
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
        if (viewModel.currentPositionMs > 3000) {
            seekPlaybackToMs(0)
            viewModel.currentPositionMs = 0
        } else if (viewModel.hasPrevious()) {
            viewModel.playPrevious()
            viewModel.getCurrentItem()?.let { startPlayback(it.file) }
        }
    }
    
    private fun playFileFromBrowser(file: File) {
        // Create a single-file playlist and play it
        if (currentSong?.hasEmbeddedBank() == true) {
            android.util.Log.d("HomeFragment", "The previous song had an embedded bank, restoring last known bank")
            val prefs = requireContext().getSharedPreferences("miniBAE_prefs", Context.MODE_PRIVATE)
            val lastBankPath = prefs.getString("last_bank_path", "__builtin__")
            loadBankFromFile(java.io.File(lastBankPath))
        }
        stopPlayback(false)
        viewModel.clearPlaylist()
        val item = PlaylistItem(file)
        viewModel.addToPlaylist(item)
        viewModel.playAtIndex(0)
        startPlayback(file)
        savePlaylist()
    }
    
    private fun shuffleAndPlay() {
        if (viewModel.playlist.isNotEmpty()) {
            val shuffledIndex = (0 until viewModel.playlist.size).random()
            viewModel.playAtIndex(shuffledIndex)
            viewModel.getCurrentItem()?.let { startPlayback(it.file) }
        }
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
                        
                        // Set loop count based on repeat mode
                        val loopCount = if (viewModel.repeatMode == RepeatMode.SONG) 32768 else 0
                        song.setLoops(loopCount)

                        // Apply velocity curve
                        if (song.isSF2Song()) {
                            song.setVelocityCurve(0)
                        } else {
                            song.setVelocityCurve(velocityCurve.value)
                        }

                        song.seekToMs(0)
                        song.preroll()
                        song.seekToMs(0)
                        val r = song.start()
                        if (r == 0) {
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
                        
                        val r = sound.start()
                        if (r == 0) {
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
        return currentSong?.isPaused() ?: currentSound?.isPaused() ?: false
    }
    
    private fun scheduleMixerCleanup() {
        mixerIdleJob?.cancel()
        mixerIdleJob = lifecycleScope.launch {
            delay(60000) // 1 minute
            Mixer.delete()
            android.util.Log.d("HomeFragment", "Mixer deleted due to inactivity")
        }
    }

    private fun cancelMixerCleanup() {
        mixerIdleJob?.cancel()
        mixerIdleJob = null
    }

    private fun pausePlayback() {
        currentSong?.pause()
        currentSound?.pause()
    }
    
    private fun resumePlayback() {
        cancelMixerCleanup()
        currentSong?.resume()
        currentSound?.resume()
    }
    
    private fun stopPlayback(delete: Boolean = true) {
        currentSong?.stop(delete)
        currentSound?.stop(delete)
        if (delete) {
            setCurrentSong(null)
            setCurrentSound(null)
            scheduleMixerCleanup()
        }
    }
    
    private fun getPlaybackPositionMs(): Int {
        return currentSong?.getPositionMs() ?: currentSound?.getPositionMs() ?: 0
    }
    
    private fun getPlaybackLengthMs(): Int {
        return currentSong?.getLengthMs() ?: currentSound?.getLengthMs() ?: 0
    }
    
    private fun seekPlaybackToMs(ms: Int) {
        // Sound doesn't support seeking yet
        currentSong?.seekToMs(ms)
    }
    
    private fun hasActivePlayback(): Boolean {
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
            var bankLoaded = false
            try {
                val prefs = requireContext().getSharedPreferences("miniBAE_prefs", android.content.Context.MODE_PRIVATE)
                val lastBankPath = prefs.getString("last_bank_path", null)
                
                if (!lastBankPath.isNullOrEmpty()) {
                    if (lastBankPath == "__builtin__") {
                        if (Mixer.addBuiltInPatches() == 0) {
                            bankLoaded = true
                        }
                    } else {
                        val bankFile = java.io.File(lastBankPath)
                        if (bankFile.exists()) {
                            val bytes = bankFile.readBytes()
                            if (Mixer.addBankFromMemory(bytes, bankFile.name) == 0) {
                                bankLoaded = true
                            }
                        }
                    }
                }
                
                // Fall back to built-in patches if no bank was loaded
                if (!bankLoaded) {
                    Mixer.addBuiltInPatches()
                }
                
                // Restore reverb and velocity curve settings
                val reverbType = prefs.getInt("default_reverb", 1)
                val velocityCurve = prefs.getInt("velocity_curve", 1)
                Mixer.setDefaultReverb(reverbType)
                Mixer.setDefaultVelocityCurve(velocityCurve)
            } catch (_: Exception) {
                // If restoration fails, use built-in patches
                try {
                    Mixer.addBuiltInPatches()
                } catch (_: Exception) {}
            }
        }
        return true
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
        val validExtensions = setOf("mid", "midi", "kar", "rmf", "xmf", "mxmf", "rmi")
        val map = LinkedHashMap<String, File>()
        if (musicDir.exists() && musicDir.isDirectory) {
            musicDir.listFiles { file -> file.isFile && file.extension.lowercase() in validExtensions }?.forEach { f ->
                map[f.absolutePath] = f
            }
        }
        return map.values.sortedBy { it.name.lowercase() }
    }
    
    private fun getBankFiles(folder: File): List<PlaylistItem> {
        val validExtensions = setOf("sf2", "hsb", "sf3", "sfo", "dls")
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
        
        val validExtensions = setOf("mid", "midi", "kar", "rmf", "rmi", "xmf", "mxmf")
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
        savePlaylist()
        Toast.makeText(requireContext(), "Added ${midiFiles.size} files to playlist", Toast.LENGTH_SHORT).show()
    }
    
    private fun addAllMidiRecursively() {
        val currentPath = viewModel.currentFolderPath ?: return
        val currentDir = File(currentPath)
        if (!currentDir.exists() || !currentDir.isDirectory) return
        
        val validExtensions = setOf("mid", "midi", "kar", "rmf", "rmi", "xmf", "mxmf")
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
        savePlaylist()
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
                
                val validExtensions = setOf("mid", "midi", "kar", "rmf", "rmi", "xmf", "mxmf")
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
        isLoadingBank.value = true
        showBankBrowser.value = false
        Thread {
            try {
                val bytes = file.readBytes()
                val originalName = file.name
                val prefs = requireContext().getSharedPreferences("miniBAE_prefs", Context.MODE_PRIVATE)
                
                // Save the parent directory for next time the bank browser is opened
                val parentPath = file.parent ?: "/sdcard"
                prefs.edit().putString("last_bank_browser_path", parentPath).apply()
                
                // If mixer doesn't exist, just save the path for lazy loading
                if (Mixer.getMixer() == null) {
                    prefs.edit().putString("last_bank_path", file.absolutePath).apply()
                    activity?.runOnUiThread {
                        currentBankName.value = originalName
                        isLoadingBank.value = false
                    }
                    return@Thread
                }
                
                // Mixer exists, load bank now
                val r = Mixer.addBankFromMemory(bytes, originalName)
                
                if (r == 0) {
                    prefs.edit().putString("last_bank_path", file.absolutePath).apply()
                    
                    activity?.runOnUiThread {
                        currentBankName.value = originalName
                        Toast.makeText(requireContext(), "Loaded: $originalName", Toast.LENGTH_SHORT).show()
                    }
                    
                    // Hot-swap: reload current song
                    reloadCurrentSongForBankSwap()
                } else {
                    activity?.runOnUiThread {
                        currentBankName.value = "Failed to load: $originalName"
                        Toast.makeText(requireContext(), "Failed to load bank (err=$r)", Toast.LENGTH_SHORT).show()
                    }
                }
                isLoadingBank.value = false
            } catch (ex: Exception) {
                activity?.runOnUiThread {
                    isLoadingBank.value = false
                    Toast.makeText(requireContext(), "Error: ${ex.message}", Toast.LENGTH_SHORT).show()
                }
            }
        }.start()
    }
    
    private fun loadBuiltInPatches() {
        isLoadingBank.value = true
        Thread {
            val prefs = requireContext().getSharedPreferences("miniBAE_prefs", Context.MODE_PRIVATE)
            
            // If mixer doesn't exist, just save the preference for lazy loading
            if (Mixer.getMixer() == null) {
                prefs.edit().putString("last_bank_path", "__builtin__").apply()
                activity?.runOnUiThread {
                    currentBankName.value = "Built-in patches"
                    Toast.makeText(requireContext(), "Built-in patches will load when playback starts", Toast.LENGTH_SHORT).show()
                    isLoadingBank.value = false
                }
                return@Thread
            }
            
            // Mixer exists, load patches now
            val r = Mixer.addBuiltInPatches()
            activity?.runOnUiThread {
                if (r == 0) {
                    val friendly = Mixer.getBankFriendlyName()
                    currentBankName.value = friendly ?: "Built-in patches"
                    prefs.edit().putString("last_bank_path", "__builtin__").apply()
                    
                    // Hot-swap: reload current song
                    reloadCurrentSongForBankSwap()
                    
                    Toast.makeText(requireContext(), "Loaded built-in patches", Toast.LENGTH_SHORT).show()
                } else {
                    currentBankName.value = "Failed to load built-in"
                    Toast.makeText(requireContext(), "Failed to load built-in patches (err=$r)", Toast.LENGTH_SHORT).show()
                }
                isLoadingBank.value = false
            }
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
    onRemoveFromPlaylist: (Int) -> Unit,
    onClearPlaylist: () -> Unit,
    onMoveItem: (Int, Int) -> Unit,
    bankName: String,
    isLoadingBank: Boolean,
    isExporting: Boolean,
    exportStatus: String,
    reverbType: Int,
    velocityCurve: Int,
    exportCodec: Int,
    onLoadBuiltin: () -> Unit,
    onReverbChange: (Int) -> Unit,
    onCurveChange: (Int) -> Unit,
    onExportCodecChange: (Int) -> Unit,
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
    Scaffold(
        modifier = Modifier.systemBarsPadding(),
        topBar = {
            // Header with folder navigation
            TopAppBar(
                title = {
                    Column {
                        Text(
                            text = "Home",
                            fontSize = 20.sp,
                            fontWeight = FontWeight.Bold
                        )
                        val folderName = viewModel.currentFolderPath?.let { path ->
                            File(path).name
                        } ?: "No folder selected"
                        Text(
                            text = folderName,
                            fontSize = 12.sp,
                            maxLines = 1,
                            overflow = TextOverflow.Ellipsis,
                            color = Color.Gray
                        )
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
                    Surface(
                        modifier = Modifier
                            .fillMaxWidth()
                            .clickable { viewModel.showFullPlayer = true },
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
                                Icon(
                                    Icons.Filled.MusicNote,
                                    contentDescription = null,
                                    tint = MaterialTheme.colors.primary,
                                    modifier = Modifier.size(40.dp)
                                )
                                
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
                                    
                                    IconButton(onClick = onPlayPause) {
                                        Icon(
                                            if (viewModel.isPlaying) Icons.Filled.Pause else Icons.Filled.PlayArrow,
                                            contentDescription = "Play/Pause",
                                            tint = MaterialTheme.colors.onSurface,
                                            modifier = Modifier.size(28.dp)
                                        )
                                    }
                                }
                                
                                IconButton(onClick = onNext, enabled = viewModel.hasNext()) {
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
                
                // Bottom navigation
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
                        selectedContentColor = MaterialTheme.colors.primary,
                        unselectedContentColor = Color.Gray
                    )
                    BottomNavigationItem(
                        icon = { Icon(Icons.AutoMirrored.Filled.QueueMusic, contentDescription = "Playlist") },
                        selected = !viewModel.showFullPlayer && viewModel.currentScreen == NavigationScreen.PLAYLIST,
                        onClick = {
                            viewModel.showFullPlayer = false
                            onNavigate(NavigationScreen.PLAYLIST)
                        },
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
                        selectedContentColor = MaterialTheme.colors.primary,
                        unselectedContentColor = Color.Gray
                    )
                    BottomNavigationItem(
                        icon = { Icon(Icons.Filled.Settings, contentDescription = "Settings") },
                        selected = !viewModel.showFullPlayer && viewModel.currentScreen == NavigationScreen.SETTINGS,
                        onClick = {
                            viewModel.showFullPlayer = false
                            onNavigate(NavigationScreen.SETTINGS)
                        },
                        selectedContentColor = MaterialTheme.colors.primary,
                        unselectedContentColor = Color.Gray
                    )
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
                    onAddToPlaylist = onAddToPlaylist
                )
                NavigationScreen.PLAYLIST -> PlaylistScreenContent(
                    viewModel = viewModel,
                    onPlaylistItemClick = onPlaylistItemClick,
                    onToggleFavorite = onToggleFavorite,
                    onRemoveFromPlaylist = onRemoveFromPlaylist,
                    onClearPlaylist = onClearPlaylist,
                    onMoveItem = onMoveItem
                )
                NavigationScreen.FAVORITES -> FavoritesScreenContent(
                    viewModel = viewModel,
                    onPlaylistItemClick = onPlaylistItemClick,
                    onToggleFavorite = onToggleFavorite,
                    onAddToPlaylist = onAddToPlaylist
                )
                NavigationScreen.SETTINGS -> SettingsScreenContent(
                    bankName = bankName,
                    isLoadingBank = isLoadingBank,
                    reverbType = reverbType,
                    velocityCurve = velocityCurve,
                    exportCodec = exportCodec,
                    onLoadBuiltin = onLoadBuiltin,
                    onReverbChange = onReverbChange,
                    onCurveChange = onCurveChange,
                    onVolumeChange = onVolumeChange,
                    onExportCodecChange = onExportCodecChange,
                    onBrowseBanks = onBrowseBanks
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
                    .background(MaterialTheme.colors.background)
            ) {
                BankBrowserScreen(
                    currentPath = bankBrowserPath,
                    files = bankBrowserFiles,
                    isLoading = bankBrowserLoading,
                    onNavigate = onBankBrowserNavigate,
                    onSelectBank = onBankBrowserSelect,
                    onClose = onBankBrowserClose
                )
            }
        }
    }
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
    
    // Update position in real-time
    LaunchedEffect(isPlaying) {
        while (isPlaying) {
            kotlinx.coroutines.delay(250)
            viewModel.currentPositionMs = viewModel.currentPositionMs
        }
    }
    
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
                val isExportEnabled = viewModel.repeatMode != RepeatMode.SONG
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
                LazyColumn(modifier = Modifier.fillMaxSize()) {
                    // Add all MIDI buttons (only show if there are songs in the folder)
                    val songFiles = viewModel.folderFiles.filter { !it.isFolder && !it.title.startsWith("🔄") }
                    if (songFiles.isNotEmpty()) {
                        item {
                            Row(
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .padding(horizontal = 16.dp, vertical = 8.dp),
                                horizontalArrangement = Arrangement.spacedBy(8.dp)
                            ) {
                                Button(
                                    onClick = onAddAllMidi,
                                    modifier = Modifier.weight(1f),
                                    colors = ButtonDefaults.buttonColors(
                                        backgroundColor = MaterialTheme.colors.primary.copy(alpha = 0.1f),
                                        contentColor = MaterialTheme.colors.primary
                                    )
                                ) {
                                    Icon(
                                        Icons.AutoMirrored.Filled.PlaylistAdd,
                                        contentDescription = null,
                                        modifier = Modifier.size(18.dp)
                                    )
                                    Spacer(modifier = Modifier.width(4.dp))
                                    Text("Add All", fontSize = 12.sp)
                                }
                                Button(
                                    onClick = onAddAllMidiRecursive,
                                    modifier = Modifier.weight(1f),
                                    colors = ButtonDefaults.buttonColors(
                                        backgroundColor = MaterialTheme.colors.primary.copy(alpha = 0.1f),
                                        contentColor = MaterialTheme.colors.primary
                                    )
                                ) {
                                    Icon(
                                        Icons.AutoMirrored.Filled.PlaylistAdd,
                                        contentDescription = null,
                                        modifier = Modifier.size(18.dp)
                                    )
                                    Spacer(modifier = Modifier.width(4.dp))
                                    Text("+ Subfolders", fontSize = 12.sp)
                                }
                            }
                        }
                    }
                    
                    // Show parent directory ".." option
                    viewModel.currentFolderPath?.let { currentPath ->
                        val file = File(currentPath)
                        val parentPath = file.parent
                        // Show parent unless we're already at root or parent is null
                        if (parentPath != null && currentPath != "/") {
                            item {
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
                    itemsIndexed(viewModel.folderFiles.filter { it.isFolder || it.title.startsWith("🔄") }) { _, item ->
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
                        item {
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
                        itemsIndexed(songFiles) { index, item ->
                            FolderSongListItem(
                                item = item,
                                isFavorite = viewModel.isFavorite(item.path),
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
            }
        }
    }
}

@Composable
fun PlaylistScreenContent(
    viewModel: MusicPlayerViewModel,
    onPlaylistItemClick: (File) -> Unit,
    onToggleFavorite: (String) -> Unit,
    onRemoveFromPlaylist: (Int) -> Unit,
    onClearPlaylist: () -> Unit,
    onMoveItem: (Int, Int) -> Unit
) {
    Column(modifier = Modifier.fillMaxSize()) {
        if (viewModel.playlist.isEmpty()) {
            Box(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                Column(horizontalAlignment = Alignment.CenterHorizontally) {
                    Icon(Icons.AutoMirrored.Filled.QueueMusic, contentDescription = null, modifier = Modifier.size(64.dp), tint = Color.Gray)
                    Spacer(modifier = Modifier.height(16.dp))
                    Text("Playlist is empty", color = Color.Gray)
                    Spacer(modifier = Modifier.height(8.dp))
                    Text("Add songs from Home or Favorites", fontSize = 12.sp, color = Color.Gray)
                }
            }
        } else {
            // Header with clear button
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(16.dp),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    "${viewModel.playlist.size} songs",
                    style = MaterialTheme.typography.h6,
                    color = MaterialTheme.colors.onBackground
                )
                OutlinedButton(onClick = onClearPlaylist) {
                    Text("Clear All")
                }
            }
            
            LazyColumn(modifier = Modifier.fillMaxSize()) {
                itemsIndexed(viewModel.playlist) { index, item ->
                    PlaylistSongListItem(
                        item = item,
                        isCurrentlyPlaying = index == viewModel.currentIndex,
                        isFavorite = viewModel.isFavorite(item.path),
                        onClick = { 
                            viewModel.playAtIndex(index)
                            onPlaylistItemClick(item.file)
                        },
                        onToggleFavorite = { onToggleFavorite(item.path) },
                        onRemove = { onRemoveFromPlaylist(index) }
                    )
                    if (index < viewModel.playlist.size - 1) {
                        Divider(color = Color.Gray.copy(alpha = 0.2f))
                    }
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
    onAddToPlaylist: (File) -> Unit
) {
    val context = LocalContext.current
    val indexingProgress by viewModel.getIndexingProgress()?.collectAsState() ?: remember { mutableStateOf(IndexingProgress()) }
    val searchResults by viewModel.searchResults.collectAsState()
    
    // Initialize database on first composition
    LaunchedEffect(Unit) {
        viewModel.initializeDatabase(context)
    }
    
    // Trigger search when query changes
    LaunchedEffect(viewModel.searchQuery) {
        if (viewModel.searchQuery.length >= 3) {
            viewModel.searchFilesInDatabase(viewModel.searchQuery, viewModel.currentFolderPath)
        }
    }
    
    Column(modifier = Modifier.fillMaxSize()) {
        // Search bar
        TextField(
            value = viewModel.searchQuery,
            onValueChange = { viewModel.searchQuery = it },
            modifier = Modifier
                .fillMaxWidth()
                .padding(16.dp),
            placeholder = { Text("Search songs... (min 3 chars)") },
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
                textColor = MaterialTheme.colors.onSurface
            ),
            shape = RoundedCornerShape(24.dp),
            enabled = !indexingProgress.isIndexing
        )
        
        // Check if current path is indexed
        val isCurrentPathIndexed = viewModel.isCurrentPathIndexed
        
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
                        "This directory is not indexed. Build an index to enable search.",
                        fontSize = 14.sp,
                        color = Color.Gray
                    )
                }
            }
        }
        
        // Index status and rebuild button
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 16.dp, vertical = 8.dp),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Column(modifier = Modifier.weight(1f)) {
                // Only show count if current path is indexed or we're at root
                val displayCount = if (isCurrentPathIndexed || viewModel.currentFolderPath == null || viewModel.currentFolderPath == "/") {
                    viewModel.indexedFileCount
                } else {
                    0
                }
                
                Text(
                    "Indexed: $displayCount files",
                    fontSize = 12.sp,
                    color = if (displayCount > 0) Color.Gray else Color.Gray.copy(alpha = 0.5f)
                )
                if (indexingProgress.isIndexing) {
                    Text(
                        "Indexing: ${indexingProgress.filesIndexed} files, ${indexingProgress.foldersScanned} folders",
                        fontSize = 10.sp,
                        color = MaterialTheme.colors.primary
                    )
                    LinearProgressIndicator(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(top = 4.dp)
                    )
                }
            }
            
            Button(
                onClick = {
                    if (indexingProgress.isIndexing) {
                        // Stop indexing
                        viewModel.stopIndexing()
                    } else {
                        // Start indexing current directory and all subdirectories
                        val rootPath = viewModel.currentFolderPath ?: "/sdcard"
                        viewModel.rebuildIndex(rootPath) { files, folders, size ->
                            // Show completion toast
                            Toast.makeText(
                                context,
                                "Indexed $files files in $folders folders (${size / 1024 / 1024} MB)",
                                Toast.LENGTH_LONG
                            ).show()
                        }
                    }
                },
                enabled = viewModel.currentFolderPath != null && viewModel.currentFolderPath != "/",
                colors = if (indexingProgress.isIndexing) {
                    ButtonDefaults.buttonColors(backgroundColor = MaterialTheme.colors.error)
                } else {
                    ButtonDefaults.buttonColors()
                },
                modifier = Modifier.padding(start = 8.dp)
            ) {
                Icon(
                    if (indexingProgress.isIndexing) Icons.Filled.Stop else Icons.Filled.Refresh,
                    contentDescription = null,
                    modifier = Modifier.size(16.dp)
                )
                Spacer(modifier = Modifier.width(4.dp))
                Text(if (indexingProgress.isIndexing) "Stop" else "Build Index")
            }
        }
        
        Divider(color = Color.Gray.copy(alpha = 0.2f))
        
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
                // Show hint when nothing typed
                Box(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                    Column(horizontalAlignment = Alignment.CenterHorizontally) {
                        Icon(Icons.Filled.Search, contentDescription = null, modifier = Modifier.size(64.dp), tint = Color.Gray)
                        Spacer(modifier = Modifier.height(16.dp))
                        Text("Enter at least 3 characters to search", color = Color.Gray, textAlign = androidx.compose.ui.text.style.TextAlign.Center)
                        Spacer(modifier = Modifier.height(8.dp))
                        Text("Instant search across all indexed files", fontSize = 12.sp, color = Color.Gray)
                    }
                }
            }
            viewModel.searchQuery.length < 3 -> {
                // Show hint for more characters
                Box(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                    Column(horizontalAlignment = Alignment.CenterHorizontally) {
                        Icon(Icons.Filled.Search, contentDescription = null, modifier = Modifier.size(64.dp), tint = Color.Gray)
                        Spacer(modifier = Modifier.height(16.dp))
                        Text("Type ${3 - viewModel.searchQuery.length} more character(s)...", color = Color.Gray)
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
                    item {
                        Text(
                            "${searchResults.size} file(s) found",
                            modifier = Modifier.padding(horizontal = 16.dp, vertical = 8.dp),
                            fontSize = 12.sp,
                            color = Color.Gray
                        )
                    }
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
    onAddToPlaylist: (File) -> Unit
) {
    val favoriteSongs = viewModel.favorites.mapNotNull { path ->
        val file = File(path)
        if (file.exists() && !file.isDirectory) {
            PlaylistItem(file)
        } else {
            null
        }
    }
    
    Column(modifier = Modifier.fillMaxSize()) {
        if (favoriteSongs.isEmpty()) {
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
                itemsIndexed(favoriteSongs) { index, item ->
                    FolderSongListItem(
                        item = item,
                        isFavorite = true,
                        onClick = { onPlaylistItemClick(item.file) },
                        onToggleFavorite = { onToggleFavorite(item.path) },
                        onAddToPlaylist = { onAddToPlaylist(item.file) }
                    )
                    if (index < favoriteSongs.size - 1) {
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
            // Music icon (clickable to play)
            IconButton(onClick = onClick) {
                Icon(
                    Icons.Filled.MusicNote,
                    contentDescription = null,
                    tint = MaterialTheme.colors.primary,
                    modifier = Modifier.size(40.dp)
                )
            }
            
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
            
            // Add to playlist button
            IconButton(onClick = onAddToPlaylist) {
                Icon(
                    Icons.Filled.Add,
                    contentDescription = "Add to playlist",
                    tint = Color.Gray
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
fun PlaylistSongListItem(
    item: PlaylistItem,
    isCurrentlyPlaying: Boolean,
    isFavorite: Boolean,
    onClick: () -> Unit,
    onToggleFavorite: () -> Unit,
    onRemove: () -> Unit
) {
    Surface(
        modifier = Modifier
            .fillMaxWidth()
            .clickable(onClick = onClick),
        color = if (isCurrentlyPlaying) MaterialTheme.colors.primary.copy(alpha = 0.1f) else Color.Transparent
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 16.dp, vertical = 12.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            // Music icon
            Icon(
                if (isCurrentlyPlaying) Icons.Filled.PlayArrow else Icons.Filled.MusicNote,
                contentDescription = null,
                tint = if (isCurrentlyPlaying) MaterialTheme.colors.primary else MaterialTheme.colors.onBackground.copy(alpha = 0.6f),
                modifier = Modifier.size(40.dp)
            )
            
            Spacer(modifier = Modifier.width(12.dp))
            
            // Song info
            Column(modifier = Modifier.weight(1f)) {
                Text(
                    text = item.title,
                    fontSize = 14.sp,
                    fontWeight = if (isCurrentlyPlaying) FontWeight.Bold else FontWeight.Normal,
                    color = if (isCurrentlyPlaying) MaterialTheme.colors.primary else MaterialTheme.colors.onBackground,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis
                )
            }
            
            // Favorite button
            IconButton(onClick = onToggleFavorite) {
                Icon(
                    if (isFavorite) Icons.Filled.Favorite else Icons.Filled.FavoriteBorder,
                    contentDescription = "Favorite",
                    tint = if (isFavorite) MaterialTheme.colors.primary else MaterialTheme.colors.onBackground.copy(alpha = 0.6f)
                )
            }
            
            // Remove button
            IconButton(onClick = onRemove) {
                Icon(
                    Icons.Filled.Close,
                    contentDescription = "Remove",
                    tint = MaterialTheme.colors.onBackground.copy(alpha = 0.6f)
                )
            }
        }
    }
}

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
    onLoadBuiltin: () -> Unit,
    onReverbChange: (Int) -> Unit,
    onCurveChange: (Int) -> Unit,
    onVolumeChange: (Int) -> Unit,
    onExportCodecChange: (Int) -> Unit,
    onBrowseBanks: () -> Unit
) {
    val reverbOptions = listOf(
        "None", "Igor's Closet", "Igor's Garage", "Igor's Acoustic Lab",
        "Igor's Cavern", "Igor's Dungeon", "Small Reflections",
        "Early Reflections", "Basement", "Banquet Hall", "Catacombs"
    )
    
    val curveOptions = listOf("Beatnik Default", "Peaky S Curve", "WebTV Curve", "2x Exponential", "2x Linear")
    val exportCodecOptions = listOf("WAV", "OGG", "FLAC")
    
    var reverbExpanded by remember { mutableStateOf(false) }
    var curveExpanded by remember { mutableStateOf(false) }
    var exportCodecExpanded by remember { mutableStateOf(false) }
    val scrollState = rememberScrollState()
    
    Column(
        modifier = Modifier
            .fillMaxSize()
            .verticalScroll(scrollState)
            .padding(16.dp)
    ) {
        // Bank Section
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
                
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    OutlinedButton(
                        onClick = onBrowseBanks,
                        modifier = Modifier.weight(1f),
                        enabled = !isLoadingBank
                    ) {
                        Icon(Icons.Filled.FolderOpen, contentDescription = null, modifier = Modifier.size(18.dp))
                        Spacer(modifier = Modifier.width(4.dp))
                        Text("Load Bank")
                    }
                    OutlinedButton(
                        onClick = onLoadBuiltin,
                        modifier = Modifier.weight(1f),
                        enabled = !isLoadingBank
                    ) {
                        Icon(Icons.Filled.GetApp, contentDescription = null, modifier = Modifier.size(18.dp))
                        Spacer(modifier = Modifier.width(4.dp))
                        Text("Built-in")
                    }
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
        
        // About Section
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
fun BankBrowserScreen(
    currentPath: String,
    files: List<PlaylistItem>,
    isLoading: Boolean,
    onNavigate: (String) -> Unit,
    onSelectBank: (File) -> Unit,
    onClose: () -> Unit
) {
    Column(modifier = Modifier.fillMaxSize()) {
        // Header with path and close button
        Surface(
            modifier = Modifier.fillMaxWidth(),
            color = MaterialTheme.colors.primary,
            elevation = 4.dp
        ) {
            Column {
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(8.dp),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    IconButton(onClick = onClose) {
                        Icon(
                            Icons.Filled.Close,
                            contentDescription = "Close",
                            tint = Color.White
                        )
                    }
                    Text(
                        text = "Select Sound Bank",
                        style = MaterialTheme.typography.h6,
                        color = Color.White,
                        modifier = Modifier.weight(1f)
                    )
                }
                
                // Current path
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(horizontal = 16.dp, vertical = 8.dp),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Icon(
                        Icons.Filled.Folder,
                        contentDescription = null,
                        tint = Color.White.copy(alpha = 0.7f),
                        modifier = Modifier.size(16.dp)
                    )
                    Spacer(modifier = Modifier.width(8.dp))
                    Text(
                        text = currentPath,
                        style = MaterialTheme.typography.caption,
                        color = Color.White.copy(alpha = 0.9f),
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis
                    )
                }
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
                Box(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
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
            else -> {
                LazyColumn(modifier = Modifier.fillMaxSize()) {
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
                        Surface(
                            modifier = Modifier
                                .fillMaxWidth()
                                .clickable { onSelectBank(item.file) },
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
                                    tint = MaterialTheme.colors.secondary,
                                    modifier = Modifier.size(40.dp)
                                )
                                Spacer(modifier = Modifier.width(12.dp))
                                Column(modifier = Modifier.weight(1f)) {
                                    Text(
                                        text = item.title,
                                        fontSize = 14.sp,
                                        fontWeight = FontWeight.Normal,
                                        color = MaterialTheme.colors.onBackground,
                                        maxLines = 1,
                                        overflow = TextOverflow.Ellipsis
                                    )
                                    Text(
                                        text = item.file.extension.uppercase(),
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
                }
            }
        }
    }
}
