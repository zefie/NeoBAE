package com.zefie.miniBAEDroid

import android.os.Bundle
import android.content.Context
import android.content.Intent
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
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.background
import androidx.compose.material.*
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.ComposeView
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.LocalView
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.core.view.WindowCompat
import java.io.File
import org.minibae.Mixer
import org.minibae.Song
import kotlinx.coroutines.delay

class HomeFragment : Fragment() {

    private var pickedFolderUri: Uri? = null
    private lateinit var viewModel: MusicPlayerViewModel
    
    private val currentSong: Song?
        get() = (activity as? MainActivity)?.currentSong
        
    private fun setCurrentSong(song: Song?) {
        (activity as? MainActivity)?.currentSong = song
    }
    
    // Sound bank settings
    private var currentBankName = mutableStateOf("Loading...")
    private var isLoadingBank = mutableStateOf(false)
    private var reverbType = mutableStateOf(1)
    private var velocityCurve = mutableStateOf(0)
    
    private val openBankPicker = registerForActivityResult(ActivityResultContracts.StartActivityForResult()) { result ->
        if (result.resultCode == Activity.RESULT_OK) {
            val data: Intent? = result.data
            data?.data?.let { uri ->
                loadBankFromUri(uri)
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
            Toast.makeText(requireContext(), "Playing: ${file.name}", Toast.LENGTH_SHORT).show()
        } catch (ex: Exception) {
            Toast.makeText(requireContext(), "Failed to load file: ${ex.message}", Toast.LENGTH_SHORT).show()
        }
    }

    fun reloadCurrentSongForBankSwap() {
        (activity as? MainActivity)?.reloadCurrentSongForBankSwap()
    }

    private var loadingState: MutableState<Boolean>? = null
    private var lastFolderPath: String? = null

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
                    Toast.makeText(requireContext(), "Restored ${items.size} song(s)", Toast.LENGTH_SHORT).show()
                }
            }
        } catch (ex: Exception) {
            android.util.Log.e("HomeFragment", "Failed to load playlist: ${ex.message}")
        }
    }

    override fun onResume() {
        super.onResume()
        
        val mainActivity = activity as? MainActivity
        android.util.Log.d("HomeFragment", "onResume: pendingBankReload=${mainActivity?.pendingBankReload}")
        if (mainActivity?.pendingBankReload == true) {
            mainActivity.pendingBankReload = false
            android.util.Log.d("HomeFragment", "Calling reloadCurrentSongForBankSwap")
            reloadCurrentSongForBankSwap()
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
                Mixer.setDefaultVelocityCurve(prefs.getInt("velocity_curve", 0))
                Mixer.setDefaultReverb(prefs.getInt("default_reverb", 1))
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
                    
                    // Initialize bank name
                    Thread {
                        val friendly = Mixer.getBankFriendlyName()
                        currentBankName.value = friendly ?: "Unknown Bank"
                    }.start()
                    
                    // Set default folder to /sdcard if none exists
                    if (viewModel.currentFolderPath == null) {
                        val savedPath = prefs.getString("current_folder_path", "/sdcard")
                        viewModel.currentFolderPath = savedPath
                        loadFolderContents(savedPath ?: "/sdcard")
                    }
                    
                    if (viewModel.favorites.isEmpty()) {
                        loadFavorites()
                    }
                }

                LaunchedEffect(pickedFolderUri) {
                    if (pickedFolderUri != null) {
                        loadFolderIntoPlaylist()
                    }
                }

                LaunchedEffect(viewModel.isPlaying, viewModel.isDraggingSeekBar) {
                    while (viewModel.isPlaying && !viewModel.isDraggingSeekBar) {
                        try {
                            val pos = currentSong?.getPositionMs() ?: 0
                            val len = currentSong?.getLengthMs() ?: 0
                            viewModel.currentPositionMs = pos
                            if (len > 0) viewModel.totalDurationMs = len
                            
                            if (len > 0 && pos >= len - 500 && viewModel.hasNext()) {
                                delay(100)
                                playNext()
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
                            currentSong?.seekToMs(ms)
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
                        bankName = currentBankName.value,
                        isLoadingBank = isLoadingBank.value,
                        reverbType = reverbType.value,
                        velocityCurve = velocityCurve.value,
                        onLoadBank = {
                            val i = Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
                                addCategory(Intent.CATEGORY_OPENABLE)
                                type = "*/*"
                                putExtra(Intent.EXTRA_MIME_TYPES, arrayOf("application/octet-stream", "application/hsb", "application/x-hsb"))
                            }
                            openBankPicker.launch(i)
                        },
                        onLoadBuiltin = {
                            loadBuiltInPatches()
                        },
                        onReverbChange = { value ->
                            reverbType.value = value
                            Mixer.setDefaultReverb(value)
                            val prefs = requireContext().getSharedPreferences("miniBAE_prefs", Context.MODE_PRIVATE)
                            prefs.edit().putInt("default_reverb", value).apply()
                        },
                        onCurveChange = { value ->
                            velocityCurve.value = value
                            Mixer.setDefaultVelocityCurve(value)
                            val prefs = requireContext().getSharedPreferences("miniBAE_prefs", Context.MODE_PRIVATE)
                            prefs.edit().putInt("velocity_curve", value).apply()
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
                        currentSong?.stop()
                        setCurrentSong(null)
                        viewModel.currentPositionMs = 0
                        viewModel.clearPlaylist()
                        viewModel.addAllToPlaylist(items)
                        viewModel.currentFolderPath = newPath
                        lastFolderPath = newPath
                        savePlaylist()
                    }
                    loadingState?.value = false
                    Toast.makeText(requireContext(), "Loaded ${files.size} song(s)", Toast.LENGTH_SHORT).show()
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
            currentSong?.pause()
            viewModel.isPlaying = false
        } else {
            if (viewModel.getCurrentItem() != null) {
                if (currentSong != null && currentSong?.isPaused() == true) {
                    currentSong?.resume()
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
            currentSong?.seekToMs(0)
            viewModel.currentPositionMs = 0
        } else if (viewModel.hasPrevious()) {
            viewModel.playPrevious()
            viewModel.getCurrentItem()?.let { startPlayback(it.file) }
        }
    }
    
    private fun playFileFromBrowser(file: File) {
        val index = viewModel.playlist.indexOfFirst { it.file.absolutePath == file.absolutePath }
        if (index >= 0) {
            viewModel.playAtIndex(index)
            startPlayback(file)
        } else {
            val item = PlaylistItem(file)
            viewModel.addToPlaylist(item)
            viewModel.playAtIndex(viewModel.playlist.size - 1)
            startPlayback(file)
            savePlaylist()
        }
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
            currentSong?.stop()
            setCurrentSong(null)
            viewModel.currentPositionMs = 0
            
            val song = Mixer.createSong()
            if (song != null) {
                setCurrentSong(song)
                val bytes = file.readBytes()
                val status = song.loadFromMemory(bytes)
                if (status == 0) {
                    applyVolume()
                    val r = song.start()
                    if (r == 0) {
                        viewModel.isPlaying = true
                        viewModel.currentTitle = file.nameWithoutExtension
                        Toast.makeText(requireContext(), "Playing: ${file.nameWithoutExtension}", Toast.LENGTH_SHORT).show()
                    } else {
                        viewModel.isPlaying = false
                        Toast.makeText(requireContext(), "Failed to start (err=$r)", Toast.LENGTH_SHORT).show()
                    }
                } else {
                    viewModel.isPlaying = false
                    Toast.makeText(requireContext(), "Failed to load (err=$status)", Toast.LENGTH_SHORT).show()
                }
            } else {
                Toast.makeText(requireContext(), "Audio mixer not initialized", Toast.LENGTH_SHORT).show()
            }
        } catch (ex: Exception) {
            viewModel.isPlaying = false
            Toast.makeText(requireContext(), "Playback error: ${ex.localizedMessage}", Toast.LENGTH_SHORT).show()
        }
    }

    private fun applyVolume() {
        Mixer.setMasterVolumePercent(viewModel.volumePercent)
        currentSong?.setVolumePercent(viewModel.volumePercent)
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
        val validExtensions = setOf("mid", "midi", "kar", "rmf", "rmi")
        val map = LinkedHashMap<String, File>()
        if (musicDir.exists() && musicDir.isDirectory) {
            musicDir.listFiles { file -> file.isFile && file.extension.lowercase() in validExtensions }?.forEach { f ->
                map[f.absolutePath] = f
            }
        }
        return map.values.sortedBy { it.name.lowercase() }
    }
    
    private fun loadFolderContents(path: String) {
        loadingState?.value = true
        Thread {
            try {
                val folder = File(path)
                if (!folder.exists() || !folder.isDirectory) {
                    activity?.runOnUiThread {
                        loadingState?.value = false
                        Toast.makeText(requireContext(), "Invalid folder: $path", Toast.LENGTH_SHORT).show()
                    }
                    return@Thread
                }
                
                val validExtensions = setOf("mid", "midi", "kar", "rmf", "rmi")
                val files = folder.listFiles { file -> 
                    file.isFile && file.extension.lowercase() in validExtensions 
                }?.sortedBy { it.name.lowercase() } ?: emptyList()
                
                activity?.runOnUiThread {
                    viewModel.clearPlaylist()
                    val items = files.map { PlaylistItem(it) }
                    viewModel.addAllToPlaylist(items)
                    viewModel.currentFolderPath = path
                    
                    // Save current folder for next launch
                    val prefs = requireContext().getSharedPreferences("miniBAE_prefs", Context.MODE_PRIVATE)
                    prefs.edit().putString("current_folder_path", path).apply()
                    
                    loadingState?.value = false
                    Toast.makeText(requireContext(), "Loaded ${files.size} song(s)", Toast.LENGTH_SHORT).show()
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
        loadFolderContents(path)
    }
    
    private fun loadBankFromUri(uri: Uri) {
        isLoadingBank.value = true
        Thread {
            try {
                requireContext().contentResolver.takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION)
                
                var originalName = uri.lastPathSegment ?: "selected_bank.hsb"
                requireContext().contentResolver.query(uri, null, null, null, null)?.use { cursor ->
                    if (cursor.moveToFirst()) {
                        val nameIndex = cursor.getColumnIndex(android.provider.OpenableColumns.DISPLAY_NAME)
                        if (nameIndex >= 0) {
                            cursor.getString(nameIndex)?.let { originalName = it }
                        }
                    }
                }
                
                val cached = File(requireContext().cacheDir, originalName)
                requireContext().contentResolver.openInputStream(uri)?.use { input ->
                    cached.outputStream().use { output ->
                        input.copyTo(output)
                    }
                }
                
                val bytes = cached.readBytes()
                val r = Mixer.addBankFromMemory(bytes)
                
                activity?.runOnUiThread {
                    if (r == 0) {
                        val friendly = Mixer.getBankFriendlyName()
                        currentBankName.value = friendly ?: originalName
                        
                        val prefs = requireContext().getSharedPreferences("miniBAE_prefs", Context.MODE_PRIVATE)
                        prefs.edit().putString("last_bank_path", cached.absolutePath).apply()
                        
                        // Hot-swap: reload current song
                        reloadCurrentSongForBankSwap()
                        
                        Toast.makeText(requireContext(), "Bank loaded: ${friendly ?: originalName}", Toast.LENGTH_SHORT).show()
                    } else {
                        currentBankName.value = "Failed to load bank"
                        Toast.makeText(requireContext(), "Failed to load bank (err=$r)", Toast.LENGTH_SHORT).show()
                    }
                    isLoadingBank.value = false
                }
            } catch (ex: Exception) {
                activity?.runOnUiThread {
                    currentBankName.value = "Error: ${ex.message}"
                    isLoadingBank.value = false
                    Toast.makeText(requireContext(), "Error loading bank: ${ex.message}", Toast.LENGTH_SHORT).show()
                }
            }
        }.start()
    }
    
    private fun loadBuiltInPatches() {
        isLoadingBank.value = true
        Thread {
            val r = Mixer.addBuiltInPatches()
            activity?.runOnUiThread {
                val prefs = requireContext().getSharedPreferences("miniBAE_prefs", Context.MODE_PRIVATE)
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
    bankName: String,
    isLoadingBank: Boolean,
    reverbType: Int,
    velocityCurve: Int,
    onLoadBank: () -> Unit,
    onLoadBuiltin: () -> Unit,
    onReverbChange: (Int) -> Unit,
    onCurveChange: (Int) -> Unit
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
                // Mini player
                if (viewModel.getCurrentItem() != null) {
                    Surface(
                        modifier = Modifier
                            .fillMaxWidth()
                            .clickable { viewModel.showFullPlayer = true },
                        elevation = 8.dp,
                        color = Color(0xFF2a2a2a)
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
                                        color = Color.White,
                                        maxLines = 1,
                                        overflow = TextOverflow.Ellipsis
                                    )
                                    val folderName = viewModel.currentFolderPath?.let { path ->
                                        File(path).name
                                    } ?: "Unknown"
                                    Text(
                                        text = folderName,
                                        fontSize = 12.sp,
                                        color = Color.Gray
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
                                            tint = Color.White,
                                            modifier = Modifier.size(28.dp)
                                        )
                                    }
                                }
                                
                                IconButton(onClick = onNext, enabled = viewModel.hasNext()) {
                                    Icon(
                                        Icons.Filled.SkipNext,
                                        contentDescription = "Next",
                                        tint = if (viewModel.hasNext()) Color.White else Color.Gray,
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
                        selected = viewModel.currentScreen == NavigationScreen.HOME,
                        onClick = { onNavigate(NavigationScreen.HOME) },
                        selectedContentColor = MaterialTheme.colors.primary,
                        unselectedContentColor = Color.Gray
                    )
                    BottomNavigationItem(
                        icon = { Icon(Icons.Filled.Search, contentDescription = "Search") },
                        selected = viewModel.currentScreen == NavigationScreen.SEARCH,
                        onClick = { onNavigate(NavigationScreen.SEARCH) },
                        selectedContentColor = MaterialTheme.colors.primary,
                        unselectedContentColor = Color.Gray
                    )
                    BottomNavigationItem(
                        icon = { Icon(Icons.Filled.Favorite, contentDescription = "Favorites") },
                        selected = viewModel.currentScreen == NavigationScreen.FAVORITES,
                        onClick = { onNavigate(NavigationScreen.FAVORITES) },
                        selectedContentColor = MaterialTheme.colors.primary,
                        unselectedContentColor = Color.Gray
                    )
                    BottomNavigationItem(
                        icon = { Icon(Icons.Filled.Settings, contentDescription = "Settings") },
                        selected = viewModel.currentScreen == NavigationScreen.SETTINGS,
                        onClick = { onNavigate(NavigationScreen.SETTINGS) },
                        selectedContentColor = MaterialTheme.colors.primary,
                        unselectedContentColor = Color.Gray
                    )
                }
            }
        }
    ) { paddingValues ->
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
                onVolumeChange = onVolumeChange
            )
        } else {
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
                    onNavigateToFolder = onNavigateToFolder
                )
                NavigationScreen.SEARCH -> SearchScreenContent(
                    viewModel = viewModel,
                    onPlaylistItemClick = onPlaylistItemClick,
                    onToggleFavorite = onToggleFavorite
                )
                NavigationScreen.FAVORITES -> FavoritesScreenContent(
                    viewModel = viewModel,
                    onPlaylistItemClick = onPlaylistItemClick,
                    onToggleFavorite = onToggleFavorite
                )
                NavigationScreen.SETTINGS -> SettingsScreenContent(
                    bankName = bankName,
                    isLoadingBank = isLoadingBank,
                    reverbType = reverbType,
                    velocityCurve = velocityCurve,
                    onLoadBank = onLoadBank,
                    onLoadBuiltin = onLoadBuiltin,
                    onReverbChange = onReverbChange,
                    onCurveChange = onCurveChange,
                    onVolumeChange = onVolumeChange
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
    onVolumeChange: (Int) -> Unit
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
    ) {
        // Top bar with back button
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(16.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            IconButton(onClick = onClose) {
                Icon(
                    Icons.Filled.ArrowBack,
                    contentDescription = "Back",
                    tint = MaterialTheme.colors.onBackground
                )
            }
            Spacer(modifier = Modifier.weight(1f))
        }
        
        // Main content centered
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(horizontal = 32.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.Center
        ) {
            // Album art placeholder
            Box(
                modifier = Modifier
                    .size(280.dp)
                    .clip(RoundedCornerShape(16.dp))
                    .background(Color(0xFF3700B3)),
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
            
            Spacer(modifier = Modifier.height(48.dp))
            
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
            
            Spacer(modifier = Modifier.height(32.dp))
            
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
                
                // Play/Pause button with circular progress
                Box(
                    contentAlignment = Alignment.Center,
                    modifier = Modifier.size(72.dp)
                ) {
                    CircularProgressIndicator(
                        progress = if (totalDurationMs > 0) currentPositionMs.toFloat() / totalDurationMs.toFloat() else 0f,
                        modifier = Modifier.fillMaxSize(),
                        color = MaterialTheme.colors.primary,
                        strokeWidth = 4.dp
                    )
                    
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
    onNavigateToFolder: (String) -> Unit
) {
    Column(modifier = Modifier.fillMaxSize()) {
        // Play and Shuffle buttons
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(16.dp),
            horizontalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            Button(
                onClick = {
                    if (viewModel.playlist.isNotEmpty()) {
                        onPlaylistItemClick(viewModel.playlist[0].file)
                    }
                },
                modifier = Modifier.weight(1f).height(48.dp),
                colors = ButtonDefaults.buttonColors(backgroundColor = MaterialTheme.colors.primary),
                shape = RoundedCornerShape(24.dp)
            ) {
                Icon(Icons.Filled.PlayArrow, contentDescription = null, tint = Color.White)
                Spacer(modifier = Modifier.width(8.dp))
                Text("Play", color = Color.White, fontWeight = FontWeight.Bold)
            }
            
            OutlinedButton(
                onClick = onShufflePlay,
                modifier = Modifier.weight(1f).height(48.dp),
                shape = RoundedCornerShape(24.dp),
                colors = ButtonDefaults.outlinedButtonColors(contentColor = Color.White)
            ) {
                Icon(Icons.Filled.Shuffle, contentDescription = null)
                Spacer(modifier = Modifier.width(8.dp))
                Text("Shuffle", fontWeight = FontWeight.Bold)
            }
        }
        
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
                    // Show parent directory ".." option
                    viewModel.currentFolderPath?.let { currentPath ->
                        val file = File(currentPath)
                        if (file.parent != null) {
                            item {
                                Surface(
                                    modifier = Modifier
                                        .fillMaxWidth()
                                        .clickable { onNavigateToFolder(file.parent!!) },
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
                                            tint = Color.Gray,
                                            modifier = Modifier.size(40.dp)
                                        )
                                        Spacer(modifier = Modifier.width(12.dp))
                                        Text(
                                            text = "..",
                                            fontSize = 14.sp,
                                            fontWeight = FontWeight.Bold,
                                            color = Color.Gray
                                        )
                                    }
                                }
                                Divider(color = Color.Gray.copy(alpha = 0.2f))
                            }
                        }
                    }
                    
                    // Show subfolders
                    viewModel.currentFolderPath?.let { currentPath ->
                        val folder = File(currentPath)
                        val subfolders = folder.listFiles { f -> f.isDirectory }?.sortedBy { it.name.lowercase() } ?: emptyList()
                        
                        itemsIndexed(subfolders) { index, subfolder ->
                            Surface(
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .clickable { onNavigateToFolder(subfolder.absolutePath) },
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
                                        text = subfolder.name,
                                        fontSize = 14.sp,
                                        fontWeight = FontWeight.Normal,
                                        color = Color.White
                                    )
                                }
                            }
                            if (index < subfolders.size - 1 || viewModel.playlist.isNotEmpty()) {
                                Divider(color = Color.Gray.copy(alpha = 0.2f))
                            }
                        }
                    }
                    
                    // Show songs
                    if (viewModel.playlist.isEmpty()) {
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
                        itemsIndexed(viewModel.playlist) { index, item ->
                            SongListItem(
                                item = item,
                                isFavorite = viewModel.isFavorite(item.path),
                                onClick = { onPlaylistItemClick(item.file) },
                                onToggleFavorite = { onToggleFavorite(item.path) }
                            )
                            if (index < viewModel.playlist.size - 1) {
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
fun SearchScreenContent(
    viewModel: MusicPlayerViewModel,
    onPlaylistItemClick: (File) -> Unit,
    onToggleFavorite: (String) -> Unit
) {
    Column(modifier = Modifier.fillMaxSize()) {
        // Search bar
        TextField(
            value = viewModel.searchQuery,
            onValueChange = { viewModel.searchQuery = it },
            modifier = Modifier
                .fillMaxWidth()
                .padding(16.dp),
            placeholder = { Text("Search songs...") },
            leadingIcon = { Icon(Icons.Filled.Search, contentDescription = null) },
            colors = TextFieldDefaults.textFieldColors(
                backgroundColor = Color(0xFF2a2a2a),
                textColor = Color.White
            ),
            shape = RoundedCornerShape(24.dp)
        )
        
        // Filtered results
        val filteredSongs = viewModel.playlist.filter {
            it.title.contains(viewModel.searchQuery, ignoreCase = true)
        }
        
        if (filteredSongs.isEmpty()) {
            Box(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                Column(horizontalAlignment = Alignment.CenterHorizontally) {
                    Icon(Icons.Filled.SearchOff, contentDescription = null, modifier = Modifier.size(64.dp), tint = Color.Gray)
                    Spacer(modifier = Modifier.height(16.dp))
                    Text("No results found", color = Color.Gray)
                }
            }
        } else {
            LazyColumn(modifier = Modifier.fillMaxSize()) {
                itemsIndexed(filteredSongs) { index, item ->
                    SongListItem(
                        item = item,
                        isFavorite = viewModel.isFavorite(item.path),
                        onClick = { onPlaylistItemClick(item.file) },
                        onToggleFavorite = { onToggleFavorite(item.path) }
                    )
                    if (index < filteredSongs.size - 1) {
                        Divider(color = Color.Gray.copy(alpha = 0.2f))
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
    onToggleFavorite: (String) -> Unit
) {
    val favoriteSongs = viewModel.playlist.filter { viewModel.isFavorite(it.path) }
    
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
                    SongListItem(
                        item = item,
                        isFavorite = true,
                        onClick = { onPlaylistItemClick(item.file) },
                        onToggleFavorite = { onToggleFavorite(item.path) }
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
                    color = Color.White,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis
                )
            }
            
            // Duration
            Text(
                text = "-:--",
                fontSize = 12.sp,
                color = Color.Gray,
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
    onLoadBank: () -> Unit,
    onLoadBuiltin: () -> Unit,
    onReverbChange: (Int) -> Unit,
    onCurveChange: (Int) -> Unit,
    onVolumeChange: (Int) -> Unit
) {
    val reverbOptions = listOf(
        "None", "Igor's Closet", "Igor's Garage", "Igor's Acoustic Lab",
        "Igor's Cavern", "Igor's Dungeon", "Small Reflections",
        "Early Reflections", "Basement", "Banquet Hall", "Catacombs"
    )
    
    val curveOptions = listOf("Default", "Peaky", "WebTV", "Expo", "Linear")
    
    var reverbExpanded by remember { mutableStateOf(false) }
    var curveExpanded by remember { mutableStateOf(false) }
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
                        onClick = onLoadBank,
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
                        Icons.Filled.TrendingUp,
                        contentDescription = null,
                        tint = MaterialTheme.colors.primary,
                        modifier = Modifier.size(24.dp)
                    )
                    Spacer(modifier = Modifier.width(8.dp))
                    Text(
                        text = "Velocity Curve",
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
