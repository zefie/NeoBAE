package com.zefie.NeoBAEDroid

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
import androidx.activity.compose.rememberLauncherForActivityResult
import android.provider.DocumentsContract
import androidx.documentfile.provider.DocumentFile
import androidx.fragment.app.Fragment
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.gestures.detectHorizontalDragGestures
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.lazy.rememberLazyListState
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
import androidx.compose.ui.text.style.TextDecoration
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.rotate
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
import com.zefie.NeoBAE.Mixer
import com.zefie.NeoBAE.Song
import kotlinx.coroutines.delay
import kotlinx.coroutines.Job
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import androidx.lifecycle.lifecycleScope
import kotlin.math.roundToInt
import java.io.IOException

class HomeFragment : Fragment() {


    companion object {
        val velocityCurve get() = NeoBAEPrefs.velocityCurve
        val fixPanLfoBias get() = NeoBAEPrefs.fixPanLfoBias
        val classicChorus get() = NeoBAEPrefs.classicChorus
        val webtvMode get() = NeoBAEPrefs.webtvMode
        val dlsCompatibilityMode get() = NeoBAEPrefs.dlsCompatibilityMode
        val normalizePlayback get() = NeoBAEPrefs.normalizePlayback

        val BANK_EXTENSIONS get() = NeoBAEPrefs.BANK_EXTENSIONS

        fun getMusicExtensions(context: Context): Set<String> =
            NeoBAEPrefs.getMusicExtensions(context)

        fun getSupportedExtensionsForSettings(context: Context): List<String> =
            NeoBAEPrefs.getSupportedExtensionsForSettings(context)

        fun isSoundExtension(extension: String): Boolean =
            NeoBAEPrefs.isSoundExtension(extension)
    }


    private var mixerIdleJob: Job? = null
    private var pickedFolderUri: Uri? = null
    private var pickedBankFolderUri: Uri? = null
    private val safUriMap = mutableMapOf<String, Uri>()
    private lateinit var viewModel: MusicPlayerViewModel
    private var isAppInForeground = mutableStateOf(true)
    private var pendingExternalUri: Uri? = null

    private val mainHandler = Handler(Looper.getMainLooper())

    // Track which file produced the currently loaded Song/Sound so we can tell when we're
    // transitioning away from a track that used an embedded bank.
    private var currentLoadedFilePath: String? = null
    private var restoreAfterEmbeddedBankInProgress: Boolean = false

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
        val previousSong = (activity as? MainActivity)?.currentSong
        if (previousSong !== song && baeScriptBoundSong === previousSong) {
            baeScriptBoundSong = null
        }
        karaokeHandler.reset()
        val ext = viewModel.getCurrentItem()?.file?.extension?.lowercase() ?: ""
        karaokeHandler.setFileExtension(ext)
        song?.setMetaEventListener(karaokeHandler)
        (activity as? MainActivity)?.currentSong = song
    }
    
    private val currentSound: com.zefie.NeoBAE.Sound?
        get() = (activity as? MainActivity)?.currentSound
        
    private fun setCurrentSound(sound: com.zefie.NeoBAE.Sound?) {
        (activity as? MainActivity)?.currentSound = sound
    }
    
    // Sound bank settings
    private var currentBankName = mutableStateOf("Loading...")
    private var currentBankPath = mutableStateOf("__builtin__")
    private var isLoadingBank = mutableStateOf(false)
    private var hasEggsBank = mutableStateOf(false)
    private var hasMobileBAEBank = mutableStateOf(false)
    private var dlsBankLevel = mutableStateOf(0)
    private var hasXmfOverlay = mutableStateOf(false)
    private var hasRmiEmbedded = mutableStateOf(false)
    private var rmiUsesSf2 = mutableStateOf(false)

    private fun refreshBankBadges(bankPathOverride: String? = null) {
        try {
            if (bankPathOverride != null) {
                currentBankPath.value = bankPathOverride
            } else if (isAdded) {
                val prefs = requireContext().getSharedPreferences("NeoBAE_prefs", Context.MODE_PRIVATE)
                currentBankPath.value = prefs.getString("last_bank_path", "__builtin__") ?: "__builtin__"
            }
            if (Mixer.exists()) {
                hasEggsBank.value = Mixer.hasEggsDLSBank()
                /* Main bank only — XMF overlay is always MobileBAE and would steal the host chip. */
                hasMobileBAEBank.value = Mixer.hasMobileBAEMainBank()
                dlsBankLevel.value = Mixer.getDLSBankLevel()
                hasXmfOverlay.value = Mixer.hasXMFDLSOverlayBank()
                val song = currentSong
                val embed = song?.hasEmbeddedBank() == true
                hasRmiEmbedded.value = embed && !hasXmfOverlay.value
                rmiUsesSf2.value = hasRmiEmbedded.value && (song?.isSF2Song() == true)
            } else {
                hasEggsBank.value = false
                hasMobileBAEBank.value = false
                dlsBankLevel.value = 0
                hasXmfOverlay.value = false
                hasRmiEmbedded.value = false
                rmiUsesSf2.value = false
            }
        } catch (_: Exception) {
            hasEggsBank.value = false
            hasMobileBAEBank.value = false
            dlsBankLevel.value = 0
            hasXmfOverlay.value = false
            hasRmiEmbedded.value = false
            rmiUsesSf2.value = false
        }
    }

    /** Match zefidi: RMI replaces host title; XMF overlays as "Host + Embedded Bank". */
    private fun applyEmbeddedBankUi(song: Song) {
        if (!song.hasEmbeddedBank()) return
        val isXmf = Mixer.hasXMFDLSOverlayBank()
        if (isXmf) {
            val base = currentBankName.value
                .removeSuffix(" + Embedded Bank")
                .removeSuffix(" + Embedded")
                .takeIf {
                    it.isNotBlank() &&
                        !it.equals("Embedded Bank", ignoreCase = true) &&
                        it != "Loading..." &&
                        it != "No Bank Loaded"
                }
                ?: run {
                    val path = if (isAdded) {
                        requireContext()
                            .getSharedPreferences("NeoBAE_prefs", Context.MODE_PRIVATE)
                            .getString("last_bank_path", "__builtin__")
                    } else {
                        "__builtin__"
                    }
                    when {
                        path.isNullOrBlank() || path == "__builtin__" -> "Built-in patches"
                        else -> File(path).name
                    }
                }
            currentBankName.value = "$base + Embedded Bank"
        } else {
            /* RMI: full bank in-file — title is just the embed. */
            val friendly = try {
                Mixer.getBankFriendlyName()
            } catch (_: Exception) {
                null
            }
            currentBankName.value = when {
                !friendly.isNullOrBlank() &&
                    !friendly.equals("DLS Bank", ignoreCase = true) -> friendly
                else -> "Embedded Bank"
            }
        }
    }

    /** Called after bank-swap song reload (MainActivity path skips finishStartNormalizedSong). */
    fun syncBankUiAfterSongLoad(song: Song?) {
        if (song == null) return
        postToMain {
            applyEmbeddedBankUi(song)
            refreshBankBadges()
        }
    }
    private var isExporting = mutableStateOf(false)
    private var exportStatus = mutableStateOf("")
    private var reverbType = mutableStateOf(1)
    // velocityCurve is now in companion object
    private var exportCodec = mutableStateOf(2) // Default to OGG
    private var baeScriptEnabled = mutableStateOf(false)
    private var baeScriptSource = mutableStateOf("")
    private var baeScriptNeedsReload = true
    private var baeScriptBoundSong: Song? = null
    private var searchResultLimit = mutableStateOf(1000) // Default to 1000
    private var enabledExtensions = mutableStateOf<Set<String>>(emptySet())
    
    // Bank browser state (completely separate from main browser)
    private var showBankBrowser = mutableStateOf(false)
    private var bankBrowserPath = mutableStateOf("/sdcard") // Will be loaded from prefs
    private var bankBrowserFiles = mutableStateListOf<PlaylistItem>()
    private var bankBrowserLoading = mutableStateOf(false)
    
    private val karaokeHandler = KaraokeHandler()
    private val normalizeGainCache = java.util.concurrent.ConcurrentHashMap<String, Int>()

    /** Bank swap changes patch loudness — drop cached gains so the next play re-estimates. */
    private fun invalidateNormalizeCacheForBankChange() {
        normalizeGainCache.clear()
        try {
            if (Mixer.getMixer() != null) {
                Mixer.setSongNormalizeGain(100)
            }
        } catch (_: Exception) {
        }
    }

    /** Used by MainActivity after bank-swap reload re-estimates normalize gain. */
    fun rememberNormalizeGain(path: String, gainPct: Int) {
        if (path.isNotEmpty() && gainPct > 0) {
            normalizeGainCache[path] = gainPct
        }
    }

    /** Apply mixer normalize for the current MIDI song (cache or estimate). */
    private fun applyNormalizeGainForSong(song: Song, pathKey: String, rePreroll: Boolean = true): Int {
        if (!normalizePlayback.value) {
            try {
                Mixer.setSongNormalizeGain(100)
            } catch (_: Exception) {
            }
            return 100
        }
        val cached = normalizeGainCache[pathKey]
        if (cached != null && cached > 0) {
            try {
                Mixer.setSongNormalizeGain(cached)
            } catch (_: Exception) {
            }
            android.util.Log.d("HomeFragment", "Normalize cache hit: $cached%")
            return cached
        }
        var gainPct = 100
        try {
            gainPct = song.normalizeFromMidiEstimate()
        } catch (e: Exception) {
            android.util.Log.w("HomeFragment", "Normalize estimate failed: ${e.message}")
            gainPct = -1
        }
        if (gainPct > 0) {
            normalizeGainCache[pathKey] = gainPct
            android.util.Log.d("HomeFragment", "Normalize estimate gain: $gainPct%")
        } else {
            try {
                Mixer.setSongNormalizeGain(100)
            } catch (_: Exception) {
            }
            gainPct = 100
        }
        if (rePreroll) {
            try {
                song.seekToMs(0)
                song.preroll()
                song.setVelocityCurve(velocityCurve.value)
            } catch (_: Exception) {
            }
        }
        return gainPct
    }

    // Prevent overlapping bank swap operations (mixer teardown/recreate is not re-entrant).
    private val bankSwapInProgress = java.util.concurrent.atomic.AtomicBoolean(false)

    private inner class KaraokeHandler : Song.MetaEventListener {
        private val currentLine = StringBuilder()
        private var lastFragment = ""
        private var haveMetaLyrics = false
        private var seenGenericTextLyric = false
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
                // "@" is a common control/info prefix in KAR-style text meta events.
                // Treat it as a line reset like the native lyric callback does.
                if (text.startsWith("@")) {
                    commitLine()
                    return
                }

                // Some MIDI files (not just .kar) encode lyrics as generic text (0x01),
                // using a leading '\\' on the first fragment to indicate "this is lyric text".
                // Subsequent fragments often omit the prefix. Track that and keep consuming 0x01.
                if (text.startsWith("\\")) {
                    seenGenericTextLyric = true
                }

                val isKaraokeDelimiter = text.startsWith("/") || text.startsWith("\\")
                if (isKaraokeDelimiter) {
                    processFragment(text)
                } else if (!haveMetaLyrics) {
                    // Plain text fallback:
                    // - Always accept for .kar
                    // - Also accept for .mid/.midi once we've seen a '\\' lyric indicator
                    if (currentExtension == "kar" || seenGenericTextLyric) {
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
            seenGenericTextLyric = false
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
            val prefs = requireContext().getSharedPreferences("NeoBAE_prefs", Context.MODE_PRIVATE)
            prefs.edit().putString("lastFolderUri", uri.toString()).apply()
        } catch (_: Exception) { }
        // Load the selected SAF folder immediately.
        viewModel.currentFolderPath = uri.toString()
        safUriMap.clear()
        loadFolderContentsSaf(uri.toString())
    }

    fun onBankFolderPicked(uri: Uri) {
        pickedBankFolderUri = uri
        Toast.makeText(this.requireContext(), "Bank folder selected: $uri", Toast.LENGTH_SHORT).show()
        try {
            val takeFlags = (android.content.Intent.FLAG_GRANT_READ_URI_PERMISSION or android.content.Intent.FLAG_GRANT_WRITE_URI_PERMISSION)
            requireContext().contentResolver.takePersistableUriPermission(uri, takeFlags)
        } catch (_: Exception) { }
        try {
            val prefs = requireContext().getSharedPreferences("NeoBAE_prefs", Context.MODE_PRIVATE)
            prefs.edit().putString("last_bank_browser_path", uri.toString()).apply()
        } catch (_: Exception) { }
        bankBrowserPath.value = uri.toString()
        bankBrowserLoading.value = false
        showBankBrowser.value = true
        loadBankFolderContentsSaf(uri.toString())
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

        if (isPlaySafVariant() && ::viewModel.isInitialized) {
            pruneUnavailableFavorites(requireContext(), viewModel.favorites)
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
            val prefs = requireContext().getSharedPreferences("NeoBAE_prefs", Context.MODE_PRIVATE)
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
            val prefs = requireContext().getSharedPreferences("NeoBAE_prefs", Context.MODE_PRIVATE)
            val json = prefs.getString("savedFavorites", null)
            if (json != null && json.isNotEmpty()) {
                val paths = json.split("|||")
                viewModel.favorites.clear()
                viewModel.favorites.addAll(paths)
                pruneUnavailableFavorites(requireContext(), viewModel.favorites)
                saveFavorites()
            }
        } catch (ex: Exception) {
            android.util.Log.e("HomeFragment", "Failed to load favorites: ${ex.message}")
        }
    }

    fun importFavoritesFromMbaeUri(uri: Uri, navigateToFavorites: Boolean) {
        android.util.Log.d("HomeFragment", "Importing favorites from: $uri")
        lifecycleScope.launch {
            try {
                val name = DocumentFile.fromSingleUri(requireContext(), uri)?.name
                val ext = name?.substringAfterLast('.', "")?.lowercase()
                if (ext != "mbae") {
                    requireActivity().runOnUiThread {
                        Toast.makeText(requireContext(), "Not a .mbae file", Toast.LENGTH_SHORT).show()
                    }
                    return@launch
                }

                val (rawPaths, pathType) = requireContext().contentResolver.openInputStream(uri)?.use { input ->
                    withContext(kotlinx.coroutines.Dispatchers.IO) {
                        FavoritesMbaeXml.readFromWithType(input)
                    }
                } ?: Pair(emptyList<String>(), null)

                // Determine if conversion is needed.
                // pathType "saf" → file was created by the Lite/SAF build (content:// URIs).
                // pathType "raw" or null → file was created by the Full build (filesystem paths).
                val isSafBuild = isPlaySafVariant()
                val fileIsSafFormat = pathType == "saf"

                val converted: List<String> = withContext(kotlinx.coroutines.Dispatchers.IO) {
                    when {
                        isSafBuild && !fileIsSafFormat -> {
                            // Full → Lite: try to resolve each filesystem path to a SAF content:// URI.
                            convertRawPathsToSaf(requireContext(), rawPaths)
                        }
                        !isSafBuild && fileIsSafFormat -> {
                            // Lite → Full: try to resolve each content:// URI to a real filesystem path.
                            convertSafPathsToRaw(rawPaths)
                        }
                        else -> rawPaths // Same format - no conversion needed.
                    }
                }

                val convertedCount = converted.size
                val originalCount = rawPaths.size

                requireActivity().runOnUiThread {
                    viewModel.favorites.clear()
                    viewModel.favorites.addAll(converted)
                    pruneUnavailableFavorites(requireContext(), viewModel.favorites)
                    saveFavorites()
                    syncVirtualPlaylistToFavoritesOrder()
                    if (navigateToFavorites) {
                        viewModel.showFullPlayer = false
                        viewModel.currentScreen = NavigationScreen.FAVORITES
                    }
                    val msg = when {
                        convertedCount == 0 && originalCount > 0 ->
                            "Imported 0 of $originalCount favorites (none could be resolved in this build)"
                        convertedCount < originalCount ->
                            "Imported ${viewModel.favorites.size} favorites (${ originalCount - convertedCount} could not be resolved)"
                        else ->
                            "Imported ${viewModel.favorites.size} favorites"
                    }
                    Toast.makeText(requireContext(), msg, Toast.LENGTH_LONG).show()
                }
            } catch (e: Exception) {
                android.util.Log.e("HomeFragment", "Failed to import favorites: ${e.message}")
                requireActivity().runOnUiThread {
                    Toast.makeText(requireContext(), "Import failed: ${e.localizedMessage}", Toast.LENGTH_SHORT).show()
                }
            }
        }
    }

    fun exportFavoritesToMbaeUri(uri: Uri) {
        android.util.Log.d("HomeFragment", "Exporting favorites to: $uri")
        lifecycleScope.launch {
            try {
                // Tag with the path type so the other build can detect and convert on import.
                val pathType = if (isPlaySafVariant()) "saf" else "raw"
                requireContext().contentResolver.openOutputStream(uri)?.use { output ->
                    FavoritesMbaeXml.writeCompressedTo(output, viewModel.favorites.toList(), pathType)
                } ?: throw IOException("Unable to open output stream")

                requireActivity().runOnUiThread {
                    Toast.makeText(requireContext(), "Exported ${viewModel.favorites.size} favorites", Toast.LENGTH_SHORT).show()
                }
            } catch (e: Exception) {
                android.util.Log.e("HomeFragment", "Failed to export favorites: ${e.message}")
                requireActivity().runOnUiThread {
                    Toast.makeText(requireContext(), "Export failed: ${e.localizedMessage}", Toast.LENGTH_SHORT).show()
                }
            }
        }
    }
    
    override fun onCreateView(inflater: LayoutInflater, container: ViewGroup?, savedInstanceState: Bundle?): View {
        if (pickedFolderUri == null) {
            try {
                val prefs = requireContext().getSharedPreferences("NeoBAE_prefs", Context.MODE_PRIVATE)
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

        if (!BuildConfig.USE_MANAGE_EXTERNAL_STORAGE && pickedBankFolderUri == null) {
            try {
                val prefs = requireContext().getSharedPreferences("NeoBAE_prefs", Context.MODE_PRIVATE)
                val lastBankUriString = prefs.getString("last_bank_browser_path", null)
                if (!lastBankUriString.isNullOrBlank() && lastBankUriString.startsWith("content://")) {
                    val lastBankUri = Uri.parse(lastBankUriString)
                    val savedTreeRoot = getSafTreeRootUriString(lastBankUriString)
                    val hasPerm = requireContext().contentResolver.persistedUriPermissions.any { perm ->
                        val permUri = perm.uri.toString().trimEnd('/')
                        val permTreeRoot = getSafTreeRootUriString(permUri)
                        (permUri == lastBankUriString.trimEnd('/')) ||
                            (savedTreeRoot != null && permTreeRoot != null && savedTreeRoot == permTreeRoot)
                    }
                    if (hasPerm) {
                        pickedBankFolderUri = lastBankUri
                        bankBrowserPath.value = lastBankUri.toString()
                    }
                }
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
                    val prefs = requireContext().getSharedPreferences("NeoBAE_prefs", Context.MODE_PRIVATE)
                    viewModel.volumePercent = prefs.getInt("volume_percent", 75)

                    // Restore sort modes
                    viewModel.homeSortMode = runCatching {
                        SortMode.valueOf(prefs.getString(NeoBAEPrefs.KEY_HOME_SORT_MODE, SortMode.NAME_ASC.name) ?: SortMode.NAME_ASC.name)
                    }.getOrDefault(SortMode.NAME_ASC)
                    viewModel.searchSortMode = runCatching {
                        SortMode.valueOf(prefs.getString(NeoBAEPrefs.KEY_SEARCH_SORT_MODE, SortMode.NAME_ASC.name) ?: SortMode.NAME_ASC.name)
                    }.getOrDefault(SortMode.NAME_ASC)
                    
                    // Load saved settings
                    reverbType.value = prefs.getInt("default_reverb", 1)
                    velocityCurve.value = prefs.getInt("velocity_curve", 1) // Default to 2nd option
                    fixPanLfoBias.value = prefs.getBoolean("fix_pan_lfo_bias", true)
                    classicChorus.value = prefs.getBoolean("classic_chorus", false)
                    webtvMode.value = prefs.getBoolean("webtv_mode", false)
                    dlsCompatibilityMode.value = prefs.getBoolean("dls_compatibility_mode", true)
                    normalizePlayback.value = prefs.getBoolean("normalize_playback", false)
                    exportCodec.value = prefs.getInt("export_codec", 2) // Default to OGG
                    baeScriptEnabled.value = prefs.getBoolean("baescript_enabled", false)
                    baeScriptSource.value = prefs.getString("baescript_source", "") ?: ""
                    baeScriptNeedsReload = true
                    enabledExtensions.value = getMusicExtensions(requireContext())
                    
                    // Initialize bank name
                    Thread {
                        val prefs = requireContext().getSharedPreferences("NeoBAE_prefs", Context.MODE_PRIVATE)
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
                        refreshBankBadges()
                    }.start()
                    
                    // Don't check permissions on startup - only when user tries to access folders
                    // This prevents the back button from returning to home screen
                    
                    // Set default folder to /sdcard if none exists
                    if (viewModel.currentFolderPath == null) {
                        val rawSavedPath = prefs.getString("current_folder_path", "/sdcard")
                        val savedPath = sanitizeFolderPathForCurrentVariant(rawSavedPath)
                        viewModel.currentFolderPath = savedPath
                        if (savedPath != rawSavedPath) {
                            prefs.edit().putString("current_folder_path", savedPath).apply()
                        }
                        loadFolderContents(savedPath)
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
                        if (!BuildConfig.USE_MANAGE_EXTERNAL_STORAGE) {
                            loadFolderContentsSaf(pickedFolderUri.toString())
                        } else {
                            loadFolderIntoPlaylist()
                        }
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

                            tickSongScript(currentSong, pos, len, false)

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
                                    val folderName = getDisplayFolderName(viewModel.currentFolderPath)

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
                                            // No more songs to play.
                                            // If the finished song used an embedded bank, restore the previous bank now.
                                            if (currentSong?.hasEmbeddedBank() == true) {
                                                viewModel.isPlaying = false
                                                restoreLastKnownBankAfterEmbeddedSong {
                                                    // stopPlayback() inside restore already schedules cleanup.
                                                }
                                            } else {
                                                viewModel.isPlaying = false
                                                // No more songs to play - schedule cleanup
                                                scheduleMixerCleanup()
                                            }
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
                    val prefs = requireContext().getSharedPreferences("NeoBAE_prefs", Context.MODE_PRIVATE)
                    prefs.edit().putInt("volume_percent", viewModel.volumePercent).apply()
                }
                
                LaunchedEffect(viewModel.repeatMode) {
                    val prefs = requireContext().getSharedPreferences("NeoBAE_prefs", Context.MODE_PRIVATE)
                    prefs.edit().putInt("repeat_mode", viewModel.repeatMode.ordinal).apply()
                }
                
                // Update notification when playback state or track metadata changes
                // (Include duration so the system progress bar end time updates when length becomes known.)
                LaunchedEffect(viewModel.isPlaying, viewModel.currentTitle, viewModel.currentIndex, viewModel.totalDurationMs) {
                    val currentItem = viewModel.getCurrentItem()
                    if (currentItem != null && (viewModel.isPlaying || viewModel.currentTitle != "No song loaded")) {
                        val folderName = getDisplayFolderName(viewModel.currentFolderPath)
                        
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
                        onImportFavorites = {
                            (activity as? MainActivity)?.requestFavoritesImport()
                        },
                        onExportFavorites = {
                            (activity as? MainActivity)?.requestFavoritesExport()
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
                        bankPath = currentBankPath.value,
                        hasEggsBank = hasEggsBank.value,
                        hasMobileBAEBank = hasMobileBAEBank.value,
                        dlsBankLevel = dlsBankLevel.value,
                        hasXmfOverlay = hasXmfOverlay.value,
                        hasRmiEmbedded = hasRmiEmbedded.value,
                        rmiUsesSf2 = rmiUsesSf2.value,
                        isLoadingBank = isLoadingBank.value,
                        isExporting = isExporting.value,
                        exportStatus = exportStatus.value,
                        reverbType = reverbType.value,
                        velocityCurve = velocityCurve.value,
                        fixPanLfoBias = fixPanLfoBias.value,
                        classicChorus = classicChorus.value,
                        webtvMode = webtvMode.value,
                        dlsCompatibilityMode = dlsCompatibilityMode.value,
                        normalizePlayback = normalizePlayback.value,
                        exportCodec = exportCodec.value,
                        baeScriptEnabled = baeScriptEnabled.value,
                        baeScriptSource = baeScriptSource.value,
                        searchResultLimit = searchResultLimit.value,
                        enabledExtensions = enabledExtensions.value,
                        onLoadBuiltin = {
                            loadBuiltInPatches()
                        },
                        onReverbChange = { value ->
                            reverbType.value = value
                            val prefs = requireContext().getSharedPreferences("NeoBAE_prefs", Context.MODE_PRIVATE)
                            if (Mixer.getMixer() != null) {
                                Mixer.setDefaultReverb(value)

                                if (value == CUSTOM_REVERB_TYPE) {
                                    val active = getActiveCustomReverbPresetName(requireContext())
                                    if (!active.isNullOrEmpty()) {
                                        loadCustomReverbPreset(requireContext(), active)?.let { preset ->
                                            applyCustomReverbPresetToEngine(requireContext(), preset)
                                            viewModel.bumpCustomReverbSync()
                                        }
                                    } else {
                                        // Keep lowpass consistent even when not using a named preset.
                                        val lp = prefs.getInt("custom_reverb_lowpass", 64).coerceIn(0, 127)
                                        Mixer.setNeoCustomReverbLowpass(lp)
                                    }
                                }
                            }
                            prefs.edit().putInt("default_reverb", value).apply()
                        },
                        onCurveChange = { value ->
                            velocityCurve.value = value
                            // Velocity curve is per-song (BAESong_SetVelocityCurve).
                            // Persist the preference and apply it to the active song if present.
                            try {
                                currentSong?.let { song ->
                                    song.setVelocityCurve(value)
                                }
                            } catch (_: Exception) {}
                            val prefs = requireContext().getSharedPreferences("NeoBAE_prefs", Context.MODE_PRIVATE)
                            prefs.edit().putInt("velocity_curve", value).apply()
                        },
                        onFixPanLfoChange = { enabled ->
                            fixPanLfoBias.value = enabled
                            Mixer.setSpanDCFix(enabled)
                            val prefs = requireContext().getSharedPreferences("NeoBAE_prefs", Context.MODE_PRIVATE)
                            prefs.edit().putBoolean("fix_pan_lfo_bias", enabled).apply()
                        },
                        onClassicChorusChange = { enabled ->
                            classicChorus.value = enabled
                            Mixer.setClassicChorus(enabled)
                            val prefs = requireContext().getSharedPreferences("NeoBAE_prefs", Context.MODE_PRIVATE)
                            prefs.edit().putBoolean("classic_chorus", enabled).apply()
                        },
                        onWebTVModeChange = { enabled ->
                            webtvMode.value = enabled
                            val prefs = requireContext().getSharedPreferences("NeoBAE_prefs", Context.MODE_PRIVATE)
                            prefs.edit().putBoolean("webtv_mode", enabled).apply()
                            Mixer.setWebTVMode(enabled)
                            // Curve, default pans and voice state are fixed at song start:
                            // reload the current song at its position so the change applies now.
                            if (currentSong != null) {
                                reloadCurrentSongForBankSwap()
                            }
                        },
                        onDLSCompatibilityModeChange = { enabled ->
                            dlsCompatibilityMode.value = enabled
                            Mixer.setDLSCompatibilityMode(enabled)
                            val prefs = requireContext().getSharedPreferences("NeoBAE_prefs", Context.MODE_PRIVATE)
                            prefs.edit().putBoolean("dls_compatibility_mode", enabled).apply()
                        },
                        onNormalizePlaybackChange = { enabled ->
                            normalizePlayback.value = enabled
                            val prefs = requireContext().getSharedPreferences("NeoBAE_prefs", Context.MODE_PRIVATE)
                            prefs.edit().putBoolean("normalize_playback", enabled).apply()
                            if (!enabled) {
                                Mixer.setSongNormalizeGain(100)
                            }
                            // Refresh Android HSB post-mix boost (off while normalize is on).
                            applyVolume()
                        },
                        onExportCodecChange = { value ->
                            exportCodec.value = value
                            val prefs = requireContext().getSharedPreferences("NeoBAE_prefs", Context.MODE_PRIVATE)
                            prefs.edit().putInt("export_codec", value).apply()
                        },
                        onBaeScriptEnabledChange = { enabled ->
                            baeScriptEnabled.value = enabled
                            val prefs = requireContext().getSharedPreferences("NeoBAE_prefs", Context.MODE_PRIVATE)
                            prefs.edit().putBoolean("baescript_enabled", enabled).apply()
                            if (!enabled) {
                                currentSong?.clearScript()
                                baeScriptBoundSong = null
                                baeScriptNeedsReload = true
                            } else {
                                baeScriptNeedsReload = true
                            }
                        },
                        onBaeScriptSourceChange = { source ->
                            baeScriptSource.value = source
                            baeScriptNeedsReload = true
                            val prefs = requireContext().getSharedPreferences("NeoBAE_prefs", Context.MODE_PRIVATE)
                            prefs.edit().putString("baescript_source", source).apply()
                        },
                        onSearchLimitChange = { value ->
                            searchResultLimit.value = value
                            val prefs = requireContext().getSharedPreferences("NeoBAE_prefs", Context.MODE_PRIVATE)
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

                            val prefs = requireContext().getSharedPreferences("NeoBAE_prefs", Context.MODE_PRIVATE)
                            val anyAudioEnabled = next.any { NeoBAEPrefs.AUDIO_EXTENSIONS.contains(it) }
                            prefs.edit()
                                .putStringSet(NeoBAEPrefs.KEY_ENABLED_EXTENSIONS, next)
                                .putBoolean(NeoBAEPrefs.KEY_ENABLE_AUDIO_FILES, anyAudioEnabled)
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
                        getMidiChannelMuteStatus = {
                            try {
                                currentSong?.getChannelMuteStatus() ?: viewModel.getMidiChannelMuteStatus()
                            } catch (_: Exception) {
                                viewModel.getMidiChannelMuteStatus()
                            }
                        },
                        onSetMidiChannelMuted = { channel, muted ->
                            try {
                                viewModel.setMidiChannelMuted(channel, muted)
                                currentSong?.let { song ->
                                    if (muted) {
                                        song.muteChannel(channel)
                                    } else {
                                        song.unmuteChannel(channel)
                                    }
                                }
                            } catch (_: Exception) {}
                        },
                        onRefreshStorage = {
                            ensureStorageAccess()
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
                            if (!BuildConfig.USE_MANAGE_EXTERNAL_STORAGE) {
                                if (pickedBankFolderUri == null) {
                                    (activity as? MainActivity)?.requestBankFolderPicker()
                                } else {
                                    val startPath = bankBrowserPath.value.ifBlank { pickedBankFolderUri!!.toString() }
                                    bankBrowserPath.value = startPath
                                    navigateToBankFolder(startPath)
                                    showBankBrowser.value = true
                                }
                            } else {
                                val prefs = requireContext().getSharedPreferences("NeoBAE_prefs", Context.MODE_PRIVATE)
                                val startPath = prefs.getString("last_bank_browser_path", "/") ?: "/"
                                bankBrowserPath.value = startPath
                                navigateToBankFolder(startPath)
                                showBankBrowser.value = true
                            }
                        },
                        onPickBankFolder = {
                            (activity as? MainActivity)?.requestBankFolderPicker()
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
        if (!BuildConfig.USE_MANAGE_EXTERNAL_STORAGE) {
            pickedFolderUri?.let { uri ->
                loadFolderContentsSaf(uri.toString())
            }
            return
        }

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
            viewModel.getCurrentItem()?.let { startPlayback(resolveSafFileIfNeeded(it.file)) }
        }
    }
    
    private fun playPrevious() {
        viewModel.playPrevious()
        viewModel.getCurrentItem()?.let { startPlayback(resolveSafFileIfNeeded(it.file)) }
    }
    
    private fun playFileFromBrowser(file: File) {
        val resolvedFile = resolveSafFileIfNeeded(file)

        // Determine the source list based on current screen.
        // This is our "virtual playlist" for Next/Previous.
        val sourceList = when (viewModel.currentScreen) {
            NavigationScreen.SEARCH -> {
                viewModel.playlistModeLabel = "Search"
                val results = viewModel.searchResults.value
                results.filter { !it.isFolder }
            }
            NavigationScreen.FAVORITES -> {
                viewModel.playlistModeLabel = "Favorites"
                viewModel.favorites.mapNotNull { path ->
                    buildFavoritePlaylistItem(requireContext(), path)
                }
            }
            else -> {
                val folderPath = viewModel.currentFolderPath ?: "/"
                val folderName = getDisplayFolderName(folderPath)
                val displayName = if (folderName.isNotBlank()) folderName else folderPath
                viewModel.playlistModeLabel = "Folder ($displayName)"
                // HOME: exclude folders and any special (non-song) items.
                viewModel.folderFiles.filter { !it.isFolder && !it.title.startsWith("🔄") }
            }
        }

        // If the user taps the currently loaded file, don't reload it.
        // Still update the virtual playlist to match where they tapped from,
        // then bring them to the full page player.
        val currentPath = viewModel.getCurrentItem()?.file?.absolutePath
        if (currentPath != null && currentPath == resolvedFile.absolutePath) {
            val nextItems = sourceList.toMutableList()
            if (nextItems.none { it.file.absolutePath == resolvedFile.absolutePath }) {
                nextItems.add(PlaylistItem(resolvedFile))
            }
            viewModel.replacePlaylistPreservingCurrent(nextItems)

            // Ensure title/index reflect the currently loaded file.
            val idx = viewModel.playlist.indexOfFirst { it.file.absolutePath == resolvedFile.absolutePath }
            if (idx >= 0) {
                viewModel.playAtIndex(idx)
            }

            viewModel.showFullPlayer = true
            return
        }

        // Playlist/file switching: bank restoration (if needed) is handled inside startPlayback().
        viewModel.clearPlaylist()
        
        // Add all files from source list to playlist
        viewModel.addAllToPlaylist(sourceList)
        
        // Find the index of the clicked file in the playlist
        val index = viewModel.playlist.indexOfFirst { it.file.absolutePath == resolvedFile.absolutePath }
        if (index >= 0) {
            viewModel.playAtIndex(index)
            startPlayback(resolvedFile)
        } else {
            // Fallback: if file not found, just play it as a single item
            val item = PlaylistItem(resolvedFile)
            viewModel.addToPlaylist(item)
            viewModel.playAtIndex(viewModel.playlist.size - 1)
            startPlayback(resolvedFile)
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
        val isSongType = ext in setOf("mid", "midi", "kar", "rmf", "zmf", "xmf", "mxmf", "rmi")
        if (!isSongType) return

        if (Mixer.getMixer() == null) return

        val prefs = requireContext().getSharedPreferences("NeoBAE_prefs", Context.MODE_PRIVATE)
        val lastBankPath = prefs.getString("last_bank_path", null)

        // Treat missing preference as "builtin" (embedded patches111 bank).
        val bankKey = if (lastBankPath.isNullOrBlank()) "__builtin__" else lastBankPath

        val r = when {
            bankKey == "__builtin__" -> loadEmbeddedBuiltinBank()
            bankKey.endsWith(".hsb", ignoreCase = true) || bankKey.endsWith(".zsb", ignoreCase = true) -> {
                val bankFile = File(bankKey)
                if (bankFile.exists() && bankFile.isFile) {
                    Mixer.addBankFromFile(bankFile.absolutePath)
                } else {
                    // If the configured HSB disappeared, fall back to built-in patches.
                    loadEmbeddedBuiltinBank()
                }
            }
            else -> return
        }

        android.util.Log.d(
            "HomeFragment",
            "HSB bank refresh before song load: bank=$bankKey result=$r"
        )
    }

    private fun restoreLastKnownBankAfterEmbeddedSong(attempt: Int = 0, onComplete: () -> Unit) {
        if (!isAdded) {
            onComplete()
            return
        }

        val ctx = requireContext()
        val prefs = ctx.getSharedPreferences("NeoBAE_prefs", Context.MODE_PRIVATE)
        val lastBankPath = prefs.getString("last_bank_path", "__builtin__") ?: "__builtin__"
        val targetName = when {
            lastBankPath == "__builtin__" -> "Built-in patches"
            lastBankPath.isNotBlank() -> java.io.File(lastBankPath).name
            else -> "Built-in patches"
        }

        // If a manual bank swap is already underway, wait briefly rather than racing.
        if (!bankSwapInProgress.compareAndSet(false, true)) {
            if (attempt >= 20) {
                android.util.Log.w("HomeFragment", "Bank restore skipped: bank swap busy")
                onComplete()
                return
            }
            mainHandler.postDelayed({ restoreLastKnownBankAfterEmbeddedSong(attempt + 1, onComplete) }, 100)
            return
        }

        // Stop the embedded-bank song before restoring the user's bank.
        // Also cancel any pending mixer cleanup so we don't race bank load.
        runCatching {
            runOnMainSync {
                stopPlayback(delete = true)
                cancelMixerCleanup()
            }
        }

        isLoadingBank.value = true
        Thread {
            var status = 0
            try {
                if (Mixer.getMixer() == null) {
                    status = 0
                } else {
                    val wantsHsb = lastBankPath == "__builtin__" || lastBankPath.endsWith(".hsb", ignoreCase = true) || lastBankPath.endsWith(".zsb", ignoreCase = true)
                    if (wantsHsb) {
                        // HSB bank swapping on Android can require a full mixer teardown/recreate.
                        val recreateStatus = runCatching {
                            runOnMainSync {
                                try {
                                    Mixer.delete()
                                } catch (_: Exception) {
                                }
                                val s = Mixer.create(requireActivity().assets, 44100, 2, 64, 8, 64)
                                if (s == 0) {
                                    Mixer.setNativeCacheDir(ctx.cacheDir.absolutePath)
                                    try {
                                        Mixer.setDefaultReverb(reverbType.value)
                                        Mixer.setSpanDCFix(fixPanLfoBias.value)
                                        Mixer.setClassicChorus(classicChorus.value)
                                        Mixer.setWebTVMode(webtvMode.value)
                                        Mixer.setDLSCompatibilityMode(dlsCompatibilityMode.value)
                                        // Re-apply Neo Reverb custom parameters after mixer recreation.
                                        val prefs = ctx.getSharedPreferences("NeoBAE_prefs", Context.MODE_PRIVATE)
                                        val currentReverb = prefs.getInt("default_reverb", reverbType.value)
                                        if (currentReverb == CUSTOM_REVERB_TYPE) {
                                            val active = getActiveCustomReverbPresetName(ctx)
                                            if (!active.isNullOrEmpty()) {
                                                loadCustomReverbPreset(ctx, active)?.let { preset ->
                                                    applyCustomReverbPresetToEngine(ctx, preset)
                                                }
                                            } else {
                                                val lp = prefs.getInt("custom_reverb_lowpass", 64).coerceIn(0, 127)
                                                Mixer.setNeoCustomReverbLowpass(lp)
                                            }
                                        }
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
                        }.getOrDefault(-1)
                        if (recreateStatus != 0) {
                            status = recreateStatus
                        }
                    }

                    if (status == 0) {
                        status = if (lastBankPath == "__builtin__") {
                            loadEmbeddedBuiltinBank()
                        } else {
                            // Load by path to avoid OOM for large SF2/DLS; also works for HSB.
                            Mixer.addBankFromFile(lastBankPath)
                        }
                    }
                }
            } catch (_: Exception) {
                status = -1
            } finally {
                postToMain {
                    isLoadingBank.value = false
                    bankSwapInProgress.set(false)

                    if (status == 0) {
                        currentBankName.value = targetName
                        refreshBankBadges()
                        android.util.Log.d("HomeFragment", "Restored bank after embedded song: $targetName")
                    } else {
                        android.util.Log.w("HomeFragment", "Failed restoring bank after embedded song (err=$status)")
                    }

                    onComplete()
                }
            }
        }.start()
    }
    
    private fun startPlayback(file: File) {
        try {
            // If we're transitioning away from a Song that used an embedded bank, restore the
            // user's previously selected bank BEFORE loading the next track.
            val previousPath = currentLoadedFilePath
            val leavingEmbeddedBankSong = (currentSong?.hasEmbeddedBank() == true)
                && (previousPath != null)
                && (previousPath != file.absolutePath)

            if (leavingEmbeddedBankSong && !restoreAfterEmbeddedBankInProgress) {
                restoreAfterEmbeddedBankInProgress = true
                restoreLastKnownBankAfterEmbeddedSong {
                    restoreAfterEmbeddedBankInProgress = false
                    startPlayback(file)
                }
                return
            }

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
            val loadResult = com.zefie.NeoBAE.LoadResult()

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
                        song.setVelocityCurve(velocityCurve.value)

                        // Apply FX before optional normalize so the prerender matches playback.
                        Mixer.setDefaultReverb(reverbType.value)
                        if (reverbType.value == 18) {
                            val activeReverb = getActiveCustomReverbPresetName(requireContext())
                            if (!activeReverb.isNullOrEmpty()) {
                                loadCustomReverbPreset(requireContext(), activeReverb)?.let { preset ->
                                    applyCustomReverbPresetToEngine(requireContext(), preset)
                                }
                            } else {
                                val prefs = requireContext().getSharedPreferences("NeoBAE_prefs", Context.MODE_PRIVATE)
                                val lp = prefs.getInt("custom_reverb_lowpass", 64).coerceIn(0, 127)
                                Mixer.setNeoCustomReverbLowpass(lp)
                            }
                        }
                        val prefs = requireContext().getSharedPreferences("NeoBAE_prefs", Context.MODE_PRIVATE)
                        val eqEnabledPrefs = prefs.getBoolean("eq_enabled", false)
                        Mixer.setEQEnabled(eqEnabledPrefs)
                        if (eqEnabledPrefs) {
                            for (i in 0 until 5) {
                                Mixer.setEQGain(i, prefs.getFloat("eq_band_$i", 0f))
                            }
                        }

                        val pathKey = file.absolutePath
                        val cachedGain = if (normalizePlayback.value) normalizeGainCache[pathKey] else null
                        if (normalizePlayback.value && cachedGain == null) {
                            // Fast MIDI+patch estimate (sync); first play still gets a stable gain.
                            var gainPct = 100
                            try {
                                gainPct = song.normalizeFromMidiEstimate()
                            } catch (e: Exception) {
                                android.util.Log.w("HomeFragment", "Normalize failed: ${e.message}")
                                gainPct = -1
                            }
                            if (gainPct > 0) {
                                normalizeGainCache[pathKey] = gainPct
                                android.util.Log.d("HomeFragment", "Normalize estimate gain: $gainPct%")
                            } else {
                                Mixer.setSongNormalizeGain(100)
                                if (gainPct < 0) {
                                    Toast.makeText(requireContext(), "Normalize failed; playing unnormalized", Toast.LENGTH_SHORT).show()
                                }
                            }
                            finishStartNormalizedSong(song, file)
                        } else {
                            if (cachedGain != null) {
                                Mixer.setSongNormalizeGain(cachedGain)
                                android.util.Log.d("HomeFragment", "Normalize cache hit: $cachedGain%")
                            } else {
                                Mixer.setSongNormalizeGain(100)
                            }
                            finishStartNormalizedSong(song, file)
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
                        // Mixer-level song normalize must not boost PCM sample playback.
                        try {
                            Mixer.setSongNormalizeGain(100)
                        } catch (_: Exception) {
                        }
                        if (normalizePlayback.value) {
                            try {
                                val gainPct = sound.normalizeFromPeak()
                                if (gainPct > 0) {
                                    android.util.Log.d("HomeFragment", "Sound normalize gain: $gainPct%")
                                }
                            } catch (e: Exception) {
                                android.util.Log.w("HomeFragment", "Sound normalize failed: ${e.message}")
                            }
                        }
                        applyVolume()
                        val loopCount = if (viewModel.repeatMode == RepeatMode.SONG) 32767 else 0    
                        sound.setLoops(loopCount)
                        val r = sound.start()
                        if (r == 0) {
                            sound.setLoops(loopCount)
                            viewModel.isPlaying = true
                            val currentItemTitle = viewModel.getCurrentItem()?.takeIf { it.file.absolutePath == file.absolutePath }?.title
                            viewModel.currentTitle = currentItemTitle ?: file.nameWithoutExtension
                            android.util.Log.d("HomeFragment", "Started ${loadResult.fileTypeString} sound: ${file.name}")
                            currentLoadedFilePath = file.absolutePath
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

    private fun ensureSongScriptLoaded(song: Song?): Boolean {
        if (song == null) return false
        if (!baeScriptEnabled.value) {
            if (baeScriptBoundSong === song) {
                baeScriptBoundSong = null
            }
            return false
        }

        val source = baeScriptSource.value
        if (source.isBlank()) {
            song.clearScript()
            if (baeScriptBoundSong === song) {
                baeScriptBoundSong = null
            }
            return false
        }

        if (!baeScriptNeedsReload && baeScriptBoundSong === song) {
            return true
        }

        val r = song.loadScriptFromString(source)
        if (r != 0) {
            android.util.Log.w("HomeFragment", "BAEScript load failed (err=$r)")
            return false
        }
        baeScriptNeedsReload = false
        baeScriptBoundSong = song
        return true
    }

    private fun tickSongScript(song: Song?, timestampMs: Int, lengthMs: Int, exporting: Boolean) {
        if (song == null) return
        if (!baeScriptEnabled.value || baeScriptSource.value.isBlank()) return
        song.tickScript(timestampMs, lengthMs, exporting)
    }

    private fun finishStartNormalizedSong(song: Song, file: File) {
        val r = song.start()
        if (r == 0) {
            if (song.hasEmbeddedBank()) {
                applyEmbeddedBankUi(song)
                refreshBankBadges()
            }

            if (baeScriptEnabled.value && ensureSongScriptLoaded(song)) {
                tickSongScript(song, 0, song.getLengthMs(), false)
            }

            val loopCount = if (viewModel.repeatMode == RepeatMode.SONG) 32767 else 0
            song.setLoops(loopCount)
            applyMidiChannelMuteState(song)

            viewModel.isPlaying = true
            val currentItemTitle = viewModel.getCurrentItem()?.takeIf { it.file.absolutePath == file.absolutePath }?.title
            viewModel.currentTitle = currentItemTitle ?: file.nameWithoutExtension
            currentLoadedFilePath = file.absolutePath
        } else {
            viewModel.isPlaying = false
            Toast.makeText(requireContext(), "Failed to start (err=$r)", Toast.LENGTH_SHORT).show()
        }
    }

    private fun applyVolume() {
        val basePercent = viewModel.volumePercent.coerceIn(0, 100)
        Mixer.setGlobalVolumePercent(basePercent)
    }

    private fun applyMidiChannelMuteState(song: Song) {
        try {
            for (channel in 0 until 16) {
                val muted = !viewModel.midiChannelEnabled[channel]
                if (muted) {
                    song.muteChannel(channel)
                } else {
                    song.unmuteChannel(channel)
                }
            }
        } catch (_: Exception) {}
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

        // Only disengage the audio thread once the engine reports no active voices.
        // This avoids cutting off release/reverb tails during rapid play/pause/stop.
        mixerIdleJob = lifecycleScope.launch {
            val r = Mixer.disengageAudio()
            android.util.Log.d("HomeFragment", "Mixer audio disengaged (r=$r)")
            return@launch            
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
        // Start a background job to wait for the audio tail to finish before
        // disengaging the audio thread. This prevents cutting off reverb/release
        // tails when the user pauses playback.
        mixerIdleJob?.cancel()
        mixerIdleJob = lifecycleScope.launch {
            try {
                val maxWaitMs = 10000L
                val start = System.currentTimeMillis()
                while (Mixer.isAudioTailActive()) {
                    if (System.currentTimeMillis() - start > maxWaitMs) {
                        android.util.Log.w("HomeFragment", "pausePlayback: audio tail timeout")
                        break
                    }
                    delay(50)
                }
                val r = Mixer.disengageAudio()
                android.util.Log.d("HomeFragment", "Mixer audio disengaged after pause (r=$r)")
            } catch (e: Exception) {
                android.util.Log.e("HomeFragment", "Error while waiting for audio tail: ${e.message}")
            } finally {
                mixerIdleJob = null
            }
        }
    }
    
    private fun resumePlayback() {
        if (!ensureMixerExists()) {
            return
        }
        cancelMixerCleanup()
        // Re-apply cached normalize only for MIDI/RMF songs — never for BAESound PCM.
        if (normalizePlayback.value && currentSong != null && currentSound == null) {
            val pathKey = currentLoadedFilePath
            val cached = pathKey?.let { normalizeGainCache[it] }
            if (cached != null && cached > 0) {
                try {
                    Mixer.setSongNormalizeGain(cached)
                } catch (_: Exception) {
                }
            }
        } else if (currentSound != null) {
            try {
                Mixer.setSongNormalizeGain(100)
            } catch (_: Exception) {
            }
        }
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
            currentLoadedFilePath = null
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
            val folderName = getDisplayFolderName(viewModel.currentFolderPath)

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

                // Favorites import file (.mbae)
                if (extension == "mbae") {
                    importFavoritesFromMbaeUri(uri, navigateToFavorites = true)
                    return@launch
                }
                
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
            val prefs = requireContext().getSharedPreferences("NeoBAE_prefs", android.content.Context.MODE_PRIVATE)
            val status = Mixer.create(requireActivity().assets, 44100, 2, 64, 8, 64)
            if (status != 0) {
                Toast.makeText(requireContext(), "Failed to recreate mixer: $status", Toast.LENGTH_SHORT).show()
                return false
            }
            
            // Set cache dir
            Mixer.setNativeCacheDir(requireContext().cacheDir.absolutePath)

            // Apply DLS backend preference before restoring any saved bank.
            val dlsCompatModePref = prefs.getBoolean("dls_compatibility_mode", true)
            HomeFragment.dlsCompatibilityMode.value = dlsCompatModePref
            Mixer.setDLSCompatibilityMode(dlsCompatModePref)
            
            // Restore bank settings
            ensureBankIsLoaded()
            refreshBankBadges()
            
            // Restore reverb and velocity curve settings
            try {
                val reverbType = prefs.getInt("default_reverb", 1)
                val velocityCurvePref = prefs.getInt("velocity_curve", 1)
                Mixer.setDefaultReverb(reverbType)

                if (reverbType == CUSTOM_REVERB_TYPE) {
                    val active = getActiveCustomReverbPresetName(requireContext())
                    if (!active.isNullOrEmpty()) {
                        loadCustomReverbPreset(requireContext(), active)?.let { preset ->
                            applyCustomReverbPresetToEngine(requireContext(), preset)
                        }
                    } else {
                        val lp = prefs.getInt("custom_reverb_lowpass", 64).coerceIn(0, 127)
                        Mixer.setNeoCustomReverbLowpass(lp)
                    }
                }

                // Restore EQ settings
                val eqEnabledPrefs = prefs.getBoolean("eq_enabled", false)
                Mixer.setEQEnabled(eqEnabledPrefs)
                if (eqEnabledPrefs) {
                    for (i in 0 until 5) {
                        Mixer.setEQGain(i, prefs.getFloat("eq_band_$i", 0f))
                    }
                }

                HomeFragment.velocityCurve.value = velocityCurvePref
                // Restore pan LFO bias fix setting
                val panFixPref = prefs.getBoolean("fix_pan_lfo_bias", true)
                HomeFragment.fixPanLfoBias.value = panFixPref
                Mixer.setSpanDCFix(panFixPref)
                // Restore classic chorus setting
                val classicChorusPref = prefs.getBoolean("classic_chorus", false)
                HomeFragment.classicChorus.value = classicChorusPref
                Mixer.setClassicChorus(classicChorusPref)
                // Restore WebTV Mode setting
                val webtvModePref = prefs.getBoolean("webtv_mode", false)
                HomeFragment.webtvMode.value = webtvModePref
                Mixer.setWebTVMode(webtvModePref)
                // If we have an active song, apply the curve immediately.
                try {
                    currentSong?.let { song ->
                        song.setVelocityCurve(velocityCurvePref)
                    }
                } catch (_: Exception) {}
            } catch (_: Exception) {}
        }
        return true
    }

    private fun ensureBankIsLoaded() {
        var bankLoaded = false
        try {
            val prefs = requireContext().getSharedPreferences("NeoBAE_prefs", android.content.Context.MODE_PRIVATE)
            val lastBankPath = prefs.getString("last_bank_path", null)
            
            if (!lastBankPath.isNullOrEmpty()) {
                if (lastBankPath == "__builtin__") {
                    if (loadEmbeddedBuiltinBank() == 0) {
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
                loadEmbeddedBuiltinBank()
            }
        } catch (_: Exception) {
            // If restoration fails, use built-in patches
            try {
                loadEmbeddedBuiltinBank()
            } catch (_: Exception) {}
        }
    }

    /** Load the compile-time embedded patch bank (patches111). Returns BAE error code. */
    private fun loadEmbeddedBuiltinBank(): Int {
        return try {
            Mixer.loadBuiltinBank()
        } catch (_: Throwable) {
            -1
        }
    }

    private fun hotSwapDlsBankEngineIfNeeded() {
        val prefs = requireContext().getSharedPreferences("NeoBAE_prefs", Context.MODE_PRIVATE)
        val lastBankPath = prefs.getString("last_bank_path", null) ?: return

        if (lastBankPath == "__builtin__") return
        if (!lastBankPath.endsWith(".dls", ignoreCase = true)) return
        if (Mixer.getMixer() == null) return

        val dlsFile = File(lastBankPath)
        if (!dlsFile.exists() || !dlsFile.isFile) return

        // Reuse the existing bank swap flow so playback pause/reload behavior matches manual bank loads.
        loadBankFromFile(dlsFile)
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
        if (!BuildConfig.USE_MANAGE_EXTERNAL_STORAGE) {
            addAllMidiInDirectorySaf()
            return
        }

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
        if (!BuildConfig.USE_MANAGE_EXTERNAL_STORAGE) {
            addAllMidiRecursivelySaf()
            return
        }

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
        if (!BuildConfig.USE_MANAGE_EXTERNAL_STORAGE) {
            loadFolderContentsSaf(path)
            return
        }

        val safePath = sanitizeFolderPathForCurrentVariant(path)
        if (safePath != path) {
            viewModel.currentFolderPath = safePath
            val prefs = requireContext().getSharedPreferences("NeoBAE_prefs", Context.MODE_PRIVATE)
            prefs.edit().putString("current_folder_path", safePath).apply()
        }

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
                val actualPath = if (safePath == "/storage") "/" else safePath
                
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
                        val prefs = requireContext().getSharedPreferences("NeoBAE_prefs", Context.MODE_PRIVATE)
                        prefs.edit().putString("current_folder_path", actualPath).apply()
                        
                        loadingState?.value = false
                    }
                    return@Thread
                }
                
                val folder = File(actualPath)
                if (!folder.exists() || !folder.isDirectory) {
                    // Try to find closest ancestor directory that exists
                    var resolvedPath = actualPath
                    while (resolvedPath != "/" && resolvedPath.isNotEmpty()) {
                        val f = File(resolvedPath)
                        if (f.exists() && f.isDirectory) {
                            break
                        }
                        resolvedPath = File(resolvedPath).parent ?: "/"
                    }

                    val contextRef = context
                    activity?.runOnUiThread {
                        loadingState?.value = false
                        if (contextRef != null) {
                            Toast.makeText(contextRef, "Folder not found: $actualPath. Falling back to: $resolvedPath", Toast.LENGTH_SHORT).show()
                            if (::viewModel.isInitialized) {
                                viewModel.currentFolderPath = resolvedPath
                                val prefs = contextRef.getSharedPreferences("NeoBAE_prefs", Context.MODE_PRIVATE)
                                prefs.edit().putString("current_folder_path", resolvedPath).apply()
                            }
                            loadFolderContents(resolvedPath)
                        }
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
                    val prefs = requireContext().getSharedPreferences("NeoBAE_prefs", Context.MODE_PRIVATE)
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
        if (!BuildConfig.USE_MANAGE_EXTERNAL_STORAGE) {
            // SAF: "/" is the saved-folders list; everything else is a content:// URI.
            loadFolderContentsSaf(path)
            return
        }
        // Handle special refresh button
        if (path == "/" && viewModel.currentFolderPath == "/") {
            // Refresh storage list
            ensureStorageAccess()
            loadFolderContents("/")
        } else {
            loadFolderContents(path)
        }
    }

    private fun ensureStorageAccess(loadIfReady: Boolean = false) {
        if (BuildConfig.USE_MANAGE_EXTERNAL_STORAGE) {
            checkStoragePermissions()
        } else {
            if (pickedFolderUri == null) {
                (activity as? MainActivity)?.requestFolderPicker()
            } else if (loadIfReady) {
                loadFolderContentsSaf(pickedFolderUri.toString())
            }
        }
    }

    /**
     * SAF root ("/"): list every folder the user has granted persisted access to,
     * so they can jump between saved folders without re-picking them.
     */
    private fun loadSafSavedFoldersRoot() {
        val ctx = context ?: return
        loadingState?.value = true
        Thread {
            val entries = try { getPersistedSafFolderEntries(ctx) } catch (_: Exception) { emptyList() }
            val labelCounts = entries.groupingBy { it.label }.eachCount()
            val items = entries.map { entry ->
                val title = if ((labelCounts[entry.label] ?: 0) > 1) getDisplayPath(entry.uri.toString()) else entry.label
                PlaylistItem(
                    file = File(ctx.cacheDir, "saf_folder_${entry.uri.hashCode()}"),
                    uri = entry.uri,
                    title = title,
                    path = entry.uri.toString(),
                    isFolder = true
                )
            }
            activity?.runOnUiThread {
                if (::viewModel.isInitialized) {
                    viewModel.folderFiles.clear()
                    viewModel.folderFiles.addAll(items)
                    viewModel.currentFolderPath = "/"
                    viewModel.invalidateSearchCache()
                    ctx.getSharedPreferences("NeoBAE_prefs", Context.MODE_PRIVATE)
                        .edit().putString("current_folder_path", "/").apply()
                }
                loadingState?.value = false
                if (items.isEmpty()) {
                    (activity as? MainActivity)?.requestFolderPicker()
                }
            }
        }.start()
    }

    private fun loadFolderContentsSaf(path: String) {
        if (!path.startsWith("content://")) {
            loadSafSavedFoldersRoot()
            return
        }
        val folderUriString = path

        val folderDoc = getSafDocumentFile(folderUriString)
        if (folderDoc == null || !folderDoc.isDirectory) {
            activity?.runOnUiThread {
                loadingState?.value = false
                val contextRef = context
                if (contextRef != null) {
                    val parentPath = getSafParentPath(folderUriString)
                    if (parentPath != null && parentPath != folderUriString && parentPath != "/") {
                        Toast.makeText(contextRef, "Folder not found. Falling back to parent.", Toast.LENGTH_SHORT).show()
                        if (::viewModel.isInitialized) {
                            viewModel.currentFolderPath = parentPath
                            val prefs = contextRef.getSharedPreferences("NeoBAE_prefs", Context.MODE_PRIVATE)
                            prefs.edit().putString("current_folder_path", parentPath).apply()
                        }
                        loadFolderContentsSaf(parentPath)
                    } else {
                        Toast.makeText(contextRef, "Unable to open folder.", Toast.LENGTH_SHORT).show()
                        if (pickedFolderUri?.toString()?.trimEnd('/') == folderUriString.trimEnd('/')) {
                            pickedFolderUri = null
                            contextRef.getSharedPreferences("NeoBAE_prefs", Context.MODE_PRIVATE)
                                .edit().remove("lastFolderUri").apply()
                        }
                        // Fall back to the saved-folders list.
                        loadSafSavedFoldersRoot()
                    }
                }
            }
            return
        }

        loadingState?.value = true
        Thread {
            try {
                val validExtensions = getMusicExtensions(requireContext())
                val items = listSafChildrenFast(requireContext(), folderUriString, validExtensions)
                    .sortedWith(compareBy({ !it.isFolder }, { it.title.lowercase() }))

                activity?.runOnUiThread {
                    if (::viewModel.isInitialized) {
                        viewModel.folderFiles.clear()
                        viewModel.folderFiles.addAll(items)
                        viewModel.currentFolderPath = folderUriString
                        viewModel.invalidateSearchCache()
                        val prefs = requireContext().getSharedPreferences("NeoBAE_prefs", Context.MODE_PRIVATE)
                        prefs.edit().putString("current_folder_path", folderUriString).apply()
                    }
                    loadingState?.value = false
                }
            } catch (ex: Exception) {
                activity?.runOnUiThread {
                    loadingState?.value = false
                    Toast.makeText(requireContext(), "Error loading SAF folder: ${ex.message}", Toast.LENGTH_SHORT).show()
                }
            }
        }.start()
    }

    /**
     * Fast SAF child listing using a single batched ContentResolver cursor query instead of
     * DocumentFile.listFiles() which makes one IPC call per child.
     * Falls back to DocumentFile enumeration if the cursor approach fails.
     */
    private fun listSafChildrenFast(
        context: Context,
        folderUriString: String,
        validExtensions: Set<String>
    ): List<PlaylistItem> {
        val folderUri = try { Uri.parse(folderUriString) } catch (_: Exception) { null }
            ?: return emptyList()

        try {
            val treeDocId = DocumentsContract.getTreeDocumentId(folderUri)
            val docId = if (DocumentsContract.isDocumentUri(context, folderUri))
                DocumentsContract.getDocumentId(folderUri)
            else
                treeDocId
            val treeUri = DocumentsContract.buildTreeDocumentUri(folderUri.authority!!, treeDocId)
            val childrenUri = DocumentsContract.buildChildDocumentsUriUsingTree(treeUri, docId)

            val projection = arrayOf(
                DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                DocumentsContract.Document.COLUMN_DISPLAY_NAME,
                DocumentsContract.Document.COLUMN_MIME_TYPE,
                DocumentsContract.Document.COLUMN_SIZE
            )

            val results = mutableListOf<PlaylistItem>()
            context.contentResolver.query(childrenUri, projection, null, null, null)?.use { cursor ->
                val idCol = cursor.getColumnIndexOrThrow(DocumentsContract.Document.COLUMN_DOCUMENT_ID)
                val nameCol = cursor.getColumnIndexOrThrow(DocumentsContract.Document.COLUMN_DISPLAY_NAME)
                val mimeCol = cursor.getColumnIndexOrThrow(DocumentsContract.Document.COLUMN_MIME_TYPE)
                val sizeCol = cursor.getColumnIndex(DocumentsContract.Document.COLUMN_SIZE)

                while (cursor.moveToNext()) {
                    val childDocId = cursor.getString(idCol) ?: continue
                    val name = cursor.getString(nameCol) ?: continue
                    val mimeType = cursor.getString(mimeCol) ?: ""
                    val isDir = mimeType == DocumentsContract.Document.MIME_TYPE_DIR
                    val sizeBytes = if (sizeCol >= 0 && !cursor.isNull(sizeCol)) cursor.getLong(sizeCol) else -1L
                    val childUri = DocumentsContract.buildDocumentUriUsingTree(treeUri, childDocId)

                    if (isDir) {
                        val placeholder = File(context.cacheDir, "saf_folder_${childUri.hashCode()}")
                        results.add(PlaylistItem(
                            file = placeholder,
                            uri = childUri,
                            title = name,
                            path = childUri.toString(),
                            isFolder = true
                        ))
                    } else {
                        val ext = name.substringAfterLast('.', "").lowercase()
                        if (ext !in validExtensions) continue
                        val cacheFile = safCacheFileForUri(childUri, name)
                        safUriMap[cacheFile.absolutePath] = childUri
                        results.add(PlaylistItem(
                            file = cacheFile,
                            uri = childUri,
                            title = name,
                            path = childUri.toString(),
                            isFolder = false,
                            sizeBytes = sizeBytes
                        ))
                    }
                }
            }
            return results
        } catch (_: Exception) {
            // Fall back to DocumentFile API if the fast path fails
            val folderDoc = getSafDocumentFile(folderUriString) ?: return emptyList()
            return folderDoc.listFiles().mapNotNull { buildPlaylistItemForDocumentFile(it, validExtensions) }
        }
    }

    private fun buildPlaylistItemForDocumentFile(doc: DocumentFile, validExtensions: Set<String>): PlaylistItem? {
        val name = doc.name ?: return null
        if (doc.isDirectory) {
            val placeholder = File(requireContext().cacheDir, "saf_folder_${doc.uri.hashCode()}")
            return PlaylistItem(
                file = placeholder,
                uri = doc.uri,
                title = name,
                path = doc.uri.toString(),
                isFolder = true
            )
        }

        val extension = name.substringAfterLast('.', "").lowercase()
        if (extension !in validExtensions) return null

        val cacheFile = safCacheFileForUri(doc.uri, name)
        safUriMap[cacheFile.absolutePath] = doc.uri
        return PlaylistItem(
            file = cacheFile,
            uri = doc.uri,
            title = name,
            path = doc.uri.toString(),
            isFolder = false,
            sizeBytes = doc.length()
        )
    }

    private fun normalizeSafUriString(uriString: String): String {
        return uriString.trim().trimEnd('/')
    }

    private fun getSafParentPath(uriString: String): String? {
        val normalized = normalizeSafUriString(uriString)
        val uri = try { Uri.parse(normalized) } catch (_: Exception) { null } ?: return null
        if (uri.scheme != "content") return null

        val segments = uri.pathSegments
        if (segments.size < 2) return null
        if (segments.size == 2 && segments[0] == "tree") return null

        val documentIndex = segments.indexOf("document")
        if (documentIndex < 0 || documentIndex + 1 >= segments.size) return null

        val treeId = Uri.decode(segments[1])
        val documentId = Uri.decode(segments[documentIndex + 1])
        val documentParts = documentId.split('/')
        if (documentParts.size <= 1) {
            return "content://${uri.authority}/tree/${Uri.encode(treeId)}"
        }

        val parentDocumentId = documentParts.dropLast(1).joinToString("/")
        return if (parentDocumentId == treeId) {
            "content://${uri.authority}/tree/${Uri.encode(treeId)}"
        } else {
            "content://${uri.authority}/tree/${Uri.encode(segments[1])}/document/${Uri.encode(parentDocumentId)}"
        }
    }

    private fun getSafDocumentFile(uriString: String): DocumentFile? {
        val uri = try { Uri.parse(normalizeSafUriString(uriString)) } catch (_: Exception) { null } ?: return null
        if (uri.scheme != "content") return null // e.g. the "/" saved-folders root
        return try {
            DocumentFile.fromTreeUri(requireContext(), uri)
                ?: DocumentFile.fromSingleUri(requireContext(), uri)
        } catch (_: IllegalArgumentException) {
            null
        }
    }

    private fun safCacheFileForUri(uri: Uri, displayName: String): File {
        val safeName = sanitizeFilename(displayName)
        val cacheFolder = File(requireContext().cacheDir, "safcache")
        if (!cacheFolder.exists()) cacheFolder.mkdirs()
        val perUriFolder = File(cacheFolder, uri.hashCode().toString())
        if (!perUriFolder.exists()) perUriFolder.mkdirs()
        return File(perUriFolder, safeName)
    }

    private fun sanitizeFilename(name: String): String {
        return name.replace(Regex("[^A-Za-z0-9._-]"), "_")
    }

    private fun resolveSafFileIfNeeded(file: File): File {
        if (file.exists()) return file
        val uri = safUriMap[file.absolutePath] ?: SafUriRegistry.getUri(file.absolutePath) ?: return file
        return copyUriToCache(uri, file) ?: file
    }

    private fun copyUriToCache(uri: Uri, targetFile: File): File? {
        return try {
            targetFile.parentFile?.let { if (!it.exists()) it.mkdirs() }
            requireContext().contentResolver.openInputStream(uri)?.use { input ->
                targetFile.outputStream().use { output ->
                    input.copyTo(output)
                }
            }
            targetFile
        } catch (_: Exception) {
            null
        }
    }

    /**
     * Convert Full-build filesystem paths to SAF content:// URIs by searching the SAF-granted trees.
     * Each path is matched against all files found under persisted-permission folder trees.
     * Only paths whose filename is found inside a granted tree are kept.
     */
    private fun convertRawPathsToSaf(context: Context, rawPaths: List<String>): List<String> {
        if (rawPaths.isEmpty()) return emptyList()
        val results = mutableListOf<String>()
        val validExtensions = getMusicExtensions(context)

        // Build a map of filename → content URI by scanning all granted trees.
        val nameToUri = mutableMapOf<String, Uri>()
        context.contentResolver.persistedUriPermissions
            .filter { it.isReadPermission && it.uri.scheme == "content" }
            .forEach { perm ->
                val treeDoc = DocumentFile.fromTreeUri(context, perm.uri) ?: return@forEach
                collectDocumentFileNames(context, treeDoc, validExtensions, nameToUri)
            }

        for (path in rawPaths) {
            val filename = File(path).name
            val uri = nameToUri[filename]
            if (uri != null) {
                results.add(uri.toString())
            } else {
                android.util.Log.d("HomeFragment", "convertRawPathsToSaf: no SAF match for $filename")
            }
        }
        return results
    }

    /** Recursively scans a DocumentFile tree and populates filename → URI map. */
    private fun collectDocumentFileNames(
        context: Context,
        parent: DocumentFile,
        validExtensions: Set<String>,
        out: MutableMap<String, Uri>
    ) {
        try {
            val childUri = DocumentsContract.buildChildDocumentsUriUsingTree(
                parent.uri,
                DocumentsContract.getDocumentId(parent.uri)
            )
            val projection = arrayOf(
                DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                DocumentsContract.Document.COLUMN_DISPLAY_NAME,
                DocumentsContract.Document.COLUMN_MIME_TYPE
            )
            context.contentResolver.query(childUri, projection, null, null, null)?.use { cursor ->
                val idCol = cursor.getColumnIndex(DocumentsContract.Document.COLUMN_DOCUMENT_ID)
                val nameCol = cursor.getColumnIndex(DocumentsContract.Document.COLUMN_DISPLAY_NAME)
                val mimeCol = cursor.getColumnIndex(DocumentsContract.Document.COLUMN_MIME_TYPE)
                while (cursor.moveToNext()) {
                    val docId = cursor.getString(idCol) ?: continue
                    val name = cursor.getString(nameCol) ?: continue
                    val mime = cursor.getString(mimeCol) ?: continue
                    val docUri = DocumentsContract.buildDocumentUriUsingTree(parent.uri, docId)
                    if (mime == DocumentsContract.Document.MIME_TYPE_DIR) {
                        val subDoc = DocumentFile.fromTreeUri(context, docUri) ?: continue
                        collectDocumentFileNames(context, subDoc, validExtensions, out)
                    } else {
                        val ext = name.substringAfterLast('.', "").lowercase()
                        if (ext in validExtensions) {
                            out.putIfAbsent(name, docUri)
                        }
                    }
                }
            }
        } catch (_: Exception) { /* skip inaccessible trees */ }
    }

    /**
     * Convert SAF content:// URI paths to filesystem paths (for use in the Full build).
     * Only URIs whose physical path can be determined and exists on disk are kept.
     */
    private fun convertSafPathsToRaw(safPaths: List<String>): List<String> {
        val results = mutableListOf<String>()
        for (uriString in safPaths) {
            if (!uriString.startsWith("content://")) {
                results.add(uriString) // Already a raw path, keep as-is.
                continue
            }
            try {
                val uri = Uri.parse(uriString)
                // Try to extract the document ID path component and map to /storage/...
                val docId = DocumentsContract.getDocumentId(uri)
                // docId often looks like "primary:Music/song.mid" or "SDCARD:path/file.mid"
                val colonIdx = docId.indexOf(':')
                if (colonIdx >= 0) {
                    val volume = docId.substring(0, colonIdx).lowercase()
                    val relativePath = docId.substring(colonIdx + 1)
                    val candidate = when (volume) {
                        "primary" -> File("/storage/emulated/0/$relativePath")
                        else -> File("/storage/$volume/$relativePath")
                    }
                    if (candidate.exists() && candidate.isFile) {
                        results.add(candidate.absolutePath)
                        continue
                    }
                }
                android.util.Log.d("HomeFragment", "convertSafPathsToRaw: could not resolve $uriString")
            } catch (_: Exception) { }
        }
        return results
    }

    private fun collectSafMusicFiles(root: DocumentFile, recursive: Boolean, validExtensions: Set<String>): List<PlaylistItem> {
        val results = mutableListOf<PlaylistItem>()
        root.listFiles().forEach { item ->
            if (item.isDirectory) {
                if (recursive) {
                    results += collectSafMusicFiles(item, true, validExtensions)
                }
            } else {
                val name = item.name ?: return@forEach
                val extension = name.substringAfterLast('.', "").lowercase()
                if (extension in validExtensions) {
                    val cacheFile = safCacheFileForUri(item.uri, name)
                    safUriMap[cacheFile.absolutePath] = item.uri
                    if (!cacheFile.exists()) {
                        copyUriToCache(item.uri, cacheFile)
                    }
                    results.add(
                        PlaylistItem(
                            file = cacheFile,
                            uri = item.uri,
                            title = name,
                            path = item.uri.toString(),
                            isFolder = false
                        )
                    )
                }
            }
        }
        return results
    }

    private fun addAllMidiInDirectorySaf() {
        val currentPath = viewModel.currentFolderPath ?: return
        val folderDoc = getSafDocumentFile(currentPath)
        if (folderDoc == null || !folderDoc.isDirectory) {
            Toast.makeText(requireContext(), "No SAF folder selected", Toast.LENGTH_SHORT).show()
            return
        }

        val validExtensions = getMusicExtensions(requireContext())
        val midiFiles = collectSafMusicFiles(folderDoc, false, validExtensions)
        if (midiFiles.isEmpty()) {
            Toast.makeText(requireContext(), "No MIDI files found in this SAF folder", Toast.LENGTH_SHORT).show()
            return
        }

        midiFiles.forEach { item ->
            if (!viewModel.playlist.any { it.file.absolutePath == item.file.absolutePath }) {
                viewModel.addToPlaylist(item)
            }
        }
        Toast.makeText(requireContext(), "Added ${midiFiles.size} files to playlist", Toast.LENGTH_SHORT).show()
    }

    private fun addAllMidiRecursivelySaf() {
        val currentPath = viewModel.currentFolderPath ?: return
        val folderDoc = getSafDocumentFile(currentPath)
        if (folderDoc == null || !folderDoc.isDirectory) {
            Toast.makeText(requireContext(), "No SAF folder selected", Toast.LENGTH_SHORT).show()
            return
        }

        val validExtensions = getMusicExtensions(requireContext())
        val midiFiles = collectSafMusicFiles(folderDoc, true, validExtensions)
        if (midiFiles.isEmpty()) {
            Toast.makeText(requireContext(), "No MIDI files found in this SAF folder", Toast.LENGTH_SHORT).show()
            return
        }

        midiFiles.forEach { item ->
            if (!viewModel.playlist.any { it.file.absolutePath == item.file.absolutePath }) {
                viewModel.addToPlaylist(item)
            }
        }
        Toast.makeText(requireContext(), "Added ${midiFiles.size} files to playlist (recursive scan)", Toast.LENGTH_SHORT).show()
    }

    private fun loadBankFolderContentsSaf(path: String) {
        val folderUriString = when {
            path == "/" -> pickedBankFolderUri?.toString()
            path.startsWith("content://") -> path
            else -> pickedBankFolderUri?.toString()
        }

        if (folderUriString == null) {
            (activity as? MainActivity)?.requestBankFolderPicker()
            return
        }

        val folderDoc = getSafDocumentFile(folderUriString)
        if (folderDoc == null || !folderDoc.isDirectory) {
            activity?.runOnUiThread {
                bankBrowserLoading.value = false
                Toast.makeText(requireContext(), "Unable to open SAF bank folder", Toast.LENGTH_SHORT).show()
            }
            return
        }

        bankBrowserLoading.value = true
        Thread {
            try {
                val items = listSafChildrenFast(requireContext(), folderUriString, BANK_EXTENSIONS)
                    .sortedWith(compareBy({ !it.isFolder }, { it.title.lowercase() }))

                activity?.runOnUiThread {
                    bankBrowserFiles.clear()
                    bankBrowserFiles.addAll(items)
                    bankBrowserPath.value = folderUriString
                    bankBrowserLoading.value = false
                }
            } catch (ex: Exception) {
                activity?.runOnUiThread {
                    bankBrowserLoading.value = false
                    Toast.makeText(requireContext(), "Error loading SAF bank folder: ${ex.message}", Toast.LENGTH_SHORT).show()
                }
            }
        }.start()
    }

    private fun navigateToBankFolder(path: String) {
        if (!BuildConfig.USE_MANAGE_EXTERNAL_STORAGE) {
            loadBankFolderContentsSaf(path)
            return
        }

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

        val resolvedFile = resolveSafFileIfNeeded(file)
        if (!resolvedFile.exists() || !resolvedFile.isFile) {
            showBankBrowser.value = false
            postToMain {
                isLoadingBank.value = false
                context?.let {
                    Toast.makeText(it, "Unable to access selected bank file", Toast.LENGTH_SHORT).show()
                }
            }
            bankSwapInProgress.set(false)
            return
        }

        val bankBytes = runCatching { resolvedFile.length() }.getOrDefault(0L)
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
            var loadStatus = -1
            try {
                val originalName = resolvedFile.name
                val prefs = requireContext().getSharedPreferences("NeoBAE_prefs", Context.MODE_PRIVATE)
                
                // Save the parent directory for next time the bank browser is opened
                val parentPath = if (!BuildConfig.USE_MANAGE_EXTERNAL_STORAGE) {
                    bankBrowserPath.value
                } else {
                    resolvedFile.parent ?: "/sdcard"
                }
                prefs.edit().putString("last_bank_browser_path", parentPath).apply()
                
                // If mixer doesn't exist, just save the path for lazy loading
                if (Mixer.getMixer() == null) {
                    prefs.edit().putString("last_bank_path", resolvedFile.absolutePath).apply()
                    postToMain {
                        currentBankName.value = originalName
                        refreshBankBadges(resolvedFile.absolutePath)
                        isLoadingBank.value = false
                    }
                    bankSwapInProgress.set(false)
                    return@Thread
                }
                
                // Mixer exists, load bank now
                val isHsbTarget = resolvedFile.extension.equals("hsb", ignoreCase = true) || resolvedFile.extension.equals("zsb", ignoreCase = true)

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
                                    Mixer.setSpanDCFix(fixPanLfoBias.value)
                                    Mixer.setClassicChorus(classicChorus.value)
                                    Mixer.setWebTVMode(webtvMode.value)
                                    Mixer.setDLSCompatibilityMode(dlsCompatibilityMode.value)
                                    val prefs = requireContext().getSharedPreferences("NeoBAE_prefs", Context.MODE_PRIVATE)
                                    val currentReverb = prefs.getInt("default_reverb", reverbType.value)
                                    if (currentReverb == CUSTOM_REVERB_TYPE) {
                                        val active = getActiveCustomReverbPresetName(requireContext())
                                        if (!active.isNullOrEmpty()) {
                                            loadCustomReverbPreset(requireContext(), active)?.let { preset ->
                                                applyCustomReverbPresetToEngine(requireContext(), preset)
                                            }
                                        } else {
                                            val lp = prefs.getInt("custom_reverb_lowpass", 64).coerceIn(0, 127)
                                            Mixer.setNeoCustomReverbLowpass(lp)
                                        }
                                    }
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
                val r = Mixer.addBankFromFile(resolvedFile.absolutePath)
                loadStatus = r
                
                if (r == 0) {
                    prefs.edit().putString("last_bank_path", resolvedFile.absolutePath).apply()
                    invalidateNormalizeCacheForBankChange()
                    
                    postToMain {
                        currentBankName.value = originalName
                        refreshBankBadges(resolvedFile.absolutePath)
                        val toastMsg = if (hasEggsBank.value) {
                            "Loaded: $originalName  (scrambled eggs)"
                        } else {
                            "Loaded: $originalName"
                        }
                        context?.let { Toast.makeText(it, toastMsg, Toast.LENGTH_SHORT).show() }
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
                        hasEggsBank.value = false
                        hasMobileBAEBank.value = false
                        dlsBankLevel.value = 0
                        hasXmfOverlay.value = false
                        hasRmiEmbedded.value = false
                        rmiUsesSf2.value = false
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
                    val bankLoadFailed = loadStatus != 0
                    val shouldResumeHere = wasPlaying && (currentSound != null || bankLoadFailed)
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
            val prefs = requireContext().getSharedPreferences("NeoBAE_prefs", Context.MODE_PRIVATE)
            var loadStatus = -1
            
            // If mixer doesn't exist, just save the preference for lazy loading
            if (Mixer.getMixer() == null) {
                prefs.edit().putString("last_bank_path", "__builtin__").apply()
                postToMain {
                    currentBankName.value = "Built-in patches"
                    refreshBankBadges("__builtin__")
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
                                Mixer.setSpanDCFix(fixPanLfoBias.value)
                                Mixer.setClassicChorus(classicChorus.value)
                                Mixer.setWebTVMode(webtvMode.value)
                                Mixer.setDLSCompatibilityMode(dlsCompatibilityMode.value)
                                val prefs = requireContext().getSharedPreferences("NeoBAE_prefs", Context.MODE_PRIVATE)
                                val currentReverb = prefs.getInt("default_reverb", reverbType.value)
                                if (currentReverb == CUSTOM_REVERB_TYPE) {
                                    val active = getActiveCustomReverbPresetName(requireContext())
                                    if (!active.isNullOrEmpty()) {
                                        loadCustomReverbPreset(requireContext(), active)?.let { preset ->
                                            applyCustomReverbPresetToEngine(requireContext(), preset)
                                        }
                                    } else {
                                        val lp = prefs.getInt("custom_reverb_lowpass", 64).coerceIn(0, 127)
                                        Mixer.setNeoCustomReverbLowpass(lp)
                                    }
                                }
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

            val r = loadEmbeddedBuiltinBank()
            loadStatus = r
            if (r == 0) {
                invalidateNormalizeCacheForBankChange()
            }
            postToMain {
                if (r == 0) {
                    val friendly = Mixer.getBankFriendlyName()
                    currentBankName.value = friendly ?: "Built-in patches"
                    // Persist before badge refresh so we don't keep the previous .dls path
                    // (which incorrectly shows "NeoBAE DLS" once eggs/mobile flags clear).
                    prefs.edit().putString("last_bank_path", "__builtin__").apply()
                    refreshBankBadges("__builtin__")
                    
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
                    hasEggsBank.value = false
                    hasMobileBAEBank.value = false
                    dlsBankLevel.value = 0
                    hasXmfOverlay.value = false
                    hasRmiEmbedded.value = false
                    rmiUsesSf2.value = false
                    context?.let { Toast.makeText(it, "Failed to load built-in patches (err=$r)", Toast.LENGTH_SHORT).show() }
                }

                val bankLoadFailed = loadStatus != 0
                val shouldResumeHere = wasPlaying && (currentSound != null || bankLoadFailed)
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
                /* MusicGlobals is TLS after multi-mixer; bind before any song/export API
                 * on this worker thread (same as gui_export.c export_thread_proc). */
                if (Mixer.makeCurrentMixer() != 0) {
                    android.util.Log.e("HomeFragment", "Failed to makeCurrent mixer for export")
                    activity?.runOnUiThread {
                        Toast.makeText(requireContext(), "Export failed (mixer not ready)", Toast.LENGTH_SHORT).show()
                    }
                    return@Thread
                }

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
                val restoreLoopCount = if (viewModel.repeatMode == RepeatMode.SONG) 32767 else 0
                var exportTargetLoops = 0
                var exportLoopsDone = 0
                var exportLastPosMs = 0
                var exportCumulativeMs = 0L
                val exportWrapThresholdMs = if (lengthMs > 0) {
                    (lengthMs / 4).coerceIn(100, 1000)
                } else {
                    1000
                }
                
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

                        // Match playback: reverb + optional normalize before first export slice.
                        Mixer.setDefaultReverb(reverbType.value)
                        if (reverbType.value == 18) {
                            val activeReverb = getActiveCustomReverbPresetName(requireContext())
                            if (!activeReverb.isNullOrEmpty()) {
                                loadCustomReverbPreset(requireContext(), activeReverb)?.let { preset ->
                                    applyCustomReverbPresetToEngine(requireContext(), preset)
                                }
                            } else {
                                val prefs = requireContext().getSharedPreferences("NeoBAE_prefs", Context.MODE_PRIVATE)
                                val lp = prefs.getInt("custom_reverb_lowpass", 64).coerceIn(0, 127)
                                Mixer.setNeoCustomReverbLowpass(lp)
                            }
                        }
                        val exportSongForNorm = currentSong
                        if (exportSongForNorm != null) {
                            applyNormalizeGainForSong(exportSongForNorm, currentItem.file.absolutePath, rePreroll = true)
                        }
                        
                        val startResult = currentSong?.start() ?: -1
                        if (startResult != 0) {
                            throw Exception("Failed to start song for export (err=$startResult)")
                        }

                        // Export should be one-shot; avoid engine loop-mode keeping isDone=false.
                        currentSong?.setLoops(0)

                        val exportSong = currentSong
                        if (ensureSongScriptLoaded(exportSong)) {
                            exportSong?.resetScriptExporterOptions()
                            tickSongScript(exportSong, 0, lengthMs, true)
                            val scriptLoopCount = exportSong?.getScriptExporterLoopCount() ?: -1
                            if (scriptLoopCount > 0) {
                                exportTargetLoops = scriptLoopCount
                                exportSong?.setLoops(30000)
                            }
                        }

                        // Reapply MIDI channel mutes after restarting song for export
                        currentSong?.let { applyMidiChannelMuteState(it) }
                        
                        // Song start can overwrite embedded reverb default — reapply.
                        Mixer.setDefaultReverb(reverbType.value)

                        android.util.Log.d("HomeFragment", "Priming export pipeline...")
                        // Prime offline (no OpenSL): service slices until the song leaves IsDone
                        // or position advances. Never recursively re-enter exportToFile.
                        val positionMs1 = currentSong?.getPositionMs() ?: 0
                        var primed = false
                        val maxPrimes = 40
                        for (prime in 0 until maxPrimes) {
                            val primeResult = Mixer.getMixer()?.serviceOutputToFile() ?: -1
                            if (primeResult != 0) {
                                throw Exception("Export priming failed (err=$primeResult)")
                            }
                            val positionMs = currentSong?.getPositionMs() ?: 0
                            val stillDone = currentSong?.isDone() ?: true
                            if (positionMs != positionMs1 || !stillDone) {
                                primed = true
                                break
                            }
                            Thread.sleep(2)
                        }
                        if (!primed) {
                            throw Exception("Export failed (song did not start advancing)")
                        }
                        
                        android.util.Log.d("HomeFragment", "Export pipeline primed")
                        
                        activity?.runOnUiThread {
                            exportStatus.value = if (exportTargetLoops > 0) {
                                "Exporting 1/$exportTargetLoops... 0%"
                            } else {
                                "Exporting audio... 0%"
                            }
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

                                val positionMs = currentSong?.getPositionMs() ?: 0
                                val didWrap = exportLastPosMs > 0 && positionMs < exportLastPosMs && (exportLastPosMs - positionMs) > exportWrapThresholdMs

                                if (didWrap) {
                                    exportCumulativeMs += exportLastPosMs.toLong()
                                }

                                val totalPositionMsLong = exportCumulativeMs + positionMs.toLong()
                                val totalPositionMs = totalPositionMsLong.coerceIn(0L, Int.MAX_VALUE.toLong()).toInt()
                                tickSongScript(currentSong, totalPositionMs, lengthMs, true)

                                if (exportTargetLoops > 0 && didWrap) {
                                    exportLoopsDone++
                                    android.util.Log.d("HomeFragment", "Export loop wrap detected ($exportLoopsDone/$exportTargetLoops)")
                                    lastProgressPercent = 0

                                    val nextLoop = (exportLoopsDone + 1).coerceAtMost(exportTargetLoops)
                                    activity?.runOnUiThread {
                                        exportStatus.value = "Exporting $nextLoop/$exportTargetLoops... 0%"
                                    }

                                    if (exportLoopsDone >= exportTargetLoops) {
                                        currentSong?.stop(false)
                                    }
                                }

                                if (exportTargetLoops > 0) {
                                    val expectedTotalMs = lengthMs.toLong() * (exportTargetLoops.toLong() + 1L)
                                    val runawaySlackMs = maxOf(2000L, (lengthMs / 2).toLong())
                                    if (totalPositionMsLong > expectedTotalMs + runawaySlackMs) {
                                        android.util.Log.w("HomeFragment", "Export loop runaway guard triggered at ${totalPositionMsLong}ms (target loops=$exportTargetLoops)")
                                        currentSong?.stop(false)
                                    }
                                }

                                exportLastPosMs = positionMs
                                
                                // Update progress every 5% to avoid excessive UI updates
                                if (lengthMs > 0) {
                                    val progressPercent = ((positionMs * 100) / lengthMs).coerceIn(0, 100)
                                    
                                    if (progressPercent >= lastProgressPercent + 5) {
                                        lastProgressPercent = progressPercent
                                        activity?.runOnUiThread {
                                            if (exportTargetLoops > 0) {
                                                val currentLoop = (exportLoopsDone + 1).coerceAtMost(exportTargetLoops)
                                                exportStatus.value = "Exporting $currentLoop/$exportTargetLoops... $progressPercent%"
                                            } else {
                                                exportStatus.value = "Exporting audio... $progressPercent%"
                                            }
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
                        
                        // Drain period: capture trailing reverb/audio tail, but never indefinitely.
                        android.util.Log.d("HomeFragment", "Draining export buffer...")
                        exportStatus.value = "Exporting audio... waiting for reverb tail"
                        var silentConsecutive = 0
                        val requiredSilentChecks = 4
                        val maxDrainLoops = 300
                        var drainLoops = 0
                        while (drainLoops < maxDrainLoops) {
                            val serviceResult = Mixer.getMixer()?.serviceOutputToFile() ?: -1
                            if (serviceResult != 0) {
                                android.util.Log.w("HomeFragment", "Tail drain serviceOutputToFile error: $serviceResult")
                                break
                            }

                            if (Mixer.isAudioTailActive()) {
                                silentConsecutive = 0
                            } else {
                                silentConsecutive++
                                if (silentConsecutive >= requiredSilentChecks) {
                                    android.util.Log.d("HomeFragment", "Tail drain completed after ${drainLoops + 1} iterations")
                                    break
                                }
                            }

                            Thread.sleep(2)
                            drainLoops++
                        }
                        if (drainLoops >= maxDrainLoops) {
                            android.util.Log.w("HomeFragment", "Tail drain reached safety limit ($maxDrainLoops iterations)")
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
                                val song = currentSong
                                if (song != null) {
                                    try {
                                        song.preroll()
                                        Mixer.setDefaultReverb(reverbType.value)
                                        applyNormalizeGainForSong(song, currentItem.file.absolutePath, rePreroll = true)
                                    } catch (_: Exception) {
                                    }
                                }
                                // Seek back to saved position
                                currentSong?.seekToMs(savedPosition)
                                viewModel.currentPositionMs = savedPosition
                                currentSong?.start()
                                currentSong?.setLoops(restoreLoopCount)
                                viewModel.isPlaying = true
                            }
                        } catch (e: Exception) {
                            android.util.Log.e("HomeFragment", "Error restoring playback: ${e.message}")
                        }
                    } else {
                        // Preserve repeat-mode behavior for the next manual play.
                        currentSong?.setLoops(restoreLoopCount)
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
