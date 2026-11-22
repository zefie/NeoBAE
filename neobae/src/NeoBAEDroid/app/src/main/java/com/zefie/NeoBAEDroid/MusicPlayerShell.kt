package com.zefie.NeoBAEDroid

import android.R
import android.content.Context
import android.content.Intent
import android.content.res.Configuration
import android.net.Uri
import android.os.Build
import android.provider.DocumentsContract
import android.widget.Toast
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.gestures.detectHorizontalDragGestures
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.*
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.*
import androidx.compose.material.icons.filled.*
import androidx.compose.material.pullrefresh.PullRefreshIndicator
import androidx.compose.material.pullrefresh.pullRefresh
import androidx.compose.material.pullrefresh.rememberPullRefreshState
import androidx.compose.runtime.*
import androidx.compose.runtime.collectAsState
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.rotate
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.onGloballyPositioned
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextDecoration
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.zIndex
import androidx.core.view.WindowCompat
import androidx.documentfile.provider.DocumentFile
import androidx.lifecycle.viewmodel.compose.viewModel
import com.zefie.NeoBAE.Mixer
import com.zefie.NeoBAE.Song
import java.io.File
import java.io.IOException
import kotlin.math.roundToInt
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

internal fun saveFavorites(context: Context, favorites: List<String>) {
    try {
        val prefs = context.getSharedPreferences("NeoBAE_prefs", Context.MODE_PRIVATE)
        val json = favorites.joinToString("|||")
        prefs.edit().putString("savedFavorites", json).apply()
    } catch (ex: Exception) {
        android.util.Log.e("HomeFragment", "Failed to save favorites: ${ex.message}")
    }
}

internal fun syncVirtualPlaylistToFavoritesOrder(viewModel: MusicPlayerViewModel) {
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
    onImportFavorites: () -> Unit,
    onExportFavorites: () -> Unit,
    onNavigate: (NavigationScreen) -> Unit,
    onShufflePlay: () -> Unit,
    onNavigateToFolder: (String) -> Unit,
    onAddToPlaylist: (File) -> Unit,
    bankName: String,
    bankPath: String,
    hasEggsBank: Boolean,
    hasMobileBAEBank: Boolean,
    dlsBankLevel: Int,
    hasXmfOverlay: Boolean,
    hasRmiEmbedded: Boolean,
    rmiUsesSf2: Boolean,
    isLoadingBank: Boolean,
    isExporting: Boolean,
    exportStatus: String,
    reverbType: Int,
    velocityCurve: Int,
    fixPanLfoBias: Boolean,
    classicChorus: Boolean,
    webtvMode: Boolean,
    dlsCompatibilityMode: Boolean,
    normalizePlayback: Boolean,
    exportCodec: Int,
    baeScriptEnabled: Boolean,
    baeScriptSource: String,
    searchResultLimit: Int,
    enabledExtensions: Set<String>,
    onLoadBuiltin: () -> Unit,
    onReverbChange: (Int) -> Unit,
    onCurveChange: (Int) -> Unit,
    onFixPanLfoChange: (Boolean) -> Unit,
    onClassicChorusChange: (Boolean) -> Unit,
    onWebTVModeChange: (Boolean) -> Unit,
    onDLSCompatibilityModeChange: (Boolean) -> Unit,
    onNormalizePlaybackChange: (Boolean) -> Unit,
    onExportCodecChange: (Int) -> Unit,
    onBaeScriptEnabledChange: (Boolean) -> Unit,
    onBaeScriptSourceChange: (String) -> Unit,
    onSearchLimitChange: (Int) -> Unit,
    onExtensionEnabledChange: (String, Boolean) -> Unit,
    onExportRequest: (String, Int) -> Unit,
    getMidiChannelMuteStatus: () -> BooleanArray?,
    onSetMidiChannelMuted: (Int, Boolean) -> Unit,
    onRefreshStorage: () -> Unit,
    onRepeatModeChange: () -> Unit,
    onAddAllMidi: () -> Unit,
    onAddAllMidiRecursive: () -> Unit,
    showBankBrowser: Boolean,
    bankBrowserPath: String,
    bankBrowserFiles: List<PlaylistItem>,
    bankBrowserLoading: Boolean,
    onBrowseBanks: () -> Unit,
    onPickBankFolder: () -> Unit,
    onBankBrowserNavigate: (String) -> Unit,
    onBankBrowserSelect: (File) -> Unit,
    onBankBrowserClose: () -> Unit
) {
    val searchEnabled = true
    val favoritesEnabled = true

    LaunchedEffect(searchEnabled, viewModel.currentScreen) {
        if (!searchEnabled &&
            viewModel.currentScreen == NavigationScreen.SEARCH
        ) {
            onNavigate(NavigationScreen.HOME)
        }
    }

    val activeScreen = if (!searchEnabled &&
        viewModel.currentScreen == NavigationScreen.SEARCH
    ) {
        NavigationScreen.HOME
    } else {
        viewModel.currentScreen
    }

    // State for delete confirmation dialog
    var showDeleteDialog by remember { mutableStateOf(false) }
    var deleteTargetPath by remember { mutableStateOf<String?>(null) }
    var showOverwriteIndexDialog by remember { mutableStateOf(false) }
    var overwriteIndexTargetPath by remember { mutableStateOf<String?>(null) }
    val context = LocalContext.current

    Box(modifier = Modifier.fillMaxSize()) {
        Scaffold(
            modifier = Modifier.systemBarsPadding(),
            topBar = {
            // Header with folder navigation
            TopAppBar(
                title = {
                    Column(modifier = Modifier.padding(start = 0.dp)) {
                        // Dynamic title based on current screen or bank browser
                        val titleText = if (showBankBrowser) {
                            "Bank Select"
                        } else if (viewModel.showFullPlayer) {
                            "Now Playing"
                        } else {
                            when (activeScreen) {
                                NavigationScreen.HOME -> "Home"
                                NavigationScreen.SEARCH -> "Search"
                                NavigationScreen.FAVORITES -> "Favorites"
                                NavigationScreen.SETTINGS -> "Settings"
                                NavigationScreen.FILE_TYPES -> "Settings"
                                NavigationScreen.CUSTOM_REVERB -> "Custom Reverb"
                            }
                        }
                        Text(
                            text = titleText,
                            fontSize = 20.sp,
                            fontWeight = FontWeight.Bold
                        )
                        
                        // Dynamic subtitle based on current screen or bank browser
                        val subtitleText = if (showBankBrowser) {
                            getDisplayPath(bankBrowserPath)
                        } else if (viewModel.showFullPlayer) {
                            val label = viewModel.playlistModeLabel
                            "Playlist: $label"
                        } else {
                            when (activeScreen) {
                                NavigationScreen.HOME -> {
                                    getDisplayPath(viewModel.currentFolderPath)
                                }
                                NavigationScreen.SEARCH -> {
                                    val searchResults by viewModel.searchResults.collectAsState()
                                    val resultCount = searchResults.size
                                    val totalResults = viewModel.indexedFileCount
                                    if (resultCount == 0) {
                                        "No results"
                                    } else {
                                        "Showing $resultCount of $totalResults result${if (totalResults != 1) "s" else ""}"
                                    }
                                }
                                NavigationScreen.FAVORITES -> {
                                    val count = viewModel.favorites.size
                                    "$count favorite${if (count != 1) "s" else ""}"
                                }
                                NavigationScreen.SETTINGS -> "Configure NeoBAE"
                                NavigationScreen.FILE_TYPES -> "Choose file types to enable"
                                NavigationScreen.CUSTOM_REVERB -> "Adjust parameters in real-time"
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
                        if (!BuildConfig.USE_MANAGE_EXTERNAL_STORAGE) {
                            IconButton(onClick = onPickBankFolder, enabled = !isLoadingBank) {
                                Icon(
                                    Icons.Filled.FolderOpen,
                                    contentDescription = "Choose Folder",
                                    modifier = Modifier.size(24.dp)
                                )
                            }
                        }
                        IconButton(onClick = onBankBrowserClose, enabled = !isLoadingBank) {
                            Icon(
                                Icons.Filled.Close,
                                contentDescription = "Close",
                                modifier = Modifier.size(24.dp)
                            )
                        }
                    }
                    // Close button for File Types page
                    else if (!viewModel.showFullPlayer && activeScreen == NavigationScreen.FILE_TYPES) {
                        IconButton(onClick = { onNavigate(NavigationScreen.SETTINGS) }, enabled = !isLoadingBank) {
                            Icon(
                                Icons.Filled.Close,
                                contentDescription = "Close",
                                modifier = Modifier.size(24.dp)
                            )
                        }
                    }
                    // Close button for Custom Reverb page
                    else if (!viewModel.showFullPlayer && activeScreen == NavigationScreen.CUSTOM_REVERB) {
                        IconButton(onClick = { onNavigate(NavigationScreen.SETTINGS) }, enabled = !isLoadingBank) {
                            Icon(
                                Icons.Filled.Close,
                                contentDescription = "Close",
                                modifier = Modifier.size(24.dp)
                            )
                        }
                    }
                    // Build Index button for Search screen
                    else if (searchEnabled && !viewModel.showFullPlayer && activeScreen == NavigationScreen.SEARCH) {
                        val indexingProgress by viewModel.getIndexingProgress()?.collectAsState() ?: remember { mutableStateOf(IndexingProgress()) }

                        // Sort icon (left of trash can)
                        IconButton(
                            onClick = {
                                viewModel.cycleSearchSortMode()
                                try {
                                    context.getSharedPreferences("NeoBAE_prefs", Context.MODE_PRIVATE)
                                        .edit()
                                        .putString("search_sort_mode", viewModel.searchSortMode.name)
                                        .apply()
                                } catch (_: Exception) {
                                }
                            },
                            enabled = !isLoadingBank
                        ) {
                            SortModeIcon(viewModel.searchSortMode)
                        }
                        
                        // Delete database icon (trash can) - enabled for any directory covered by an index
                        val indexRootPath = viewModel.currentIndexRootPath
                        val canDeleteDatabase = indexRootPath != null
                        IconButton(
                            onClick = {
                                deleteTargetPath = indexRootPath
                                showDeleteDialog = true
                            },
                            enabled = canDeleteDatabase && !isLoadingBank
                        ) {
                            Icon(
                                Icons.Filled.Delete,
                                contentDescription = "Delete Database",
                                tint = if (canDeleteDatabase) MaterialTheme.colors.onSurface else MaterialTheme.colors.onSurface.copy(alpha = 0.38f)
                            )
                        }
                        
                        // Build/Stop Index icon (refresh)
                        IconButton(
                            onClick = {
                                if (indexingProgress.isIndexing) {
                                    viewModel.stopIndexing()
                                } else {
                                    val requestedPath = viewModel.currentFolderPath ?: "/sdcard"
                                    val existingIndexRoot = viewModel.currentIndexRootPath

                                    if (existingIndexRoot != null) {
                                        overwriteIndexTargetPath = existingIndexRoot
                                        showOverwriteIndexDialog = true
                                    } else {
                                        viewModel.rebuildIndex(requestedPath) { files, folders, size ->
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

                    // Import/Export buttons for Favorites screen
                    else if (favoritesEnabled && !viewModel.showFullPlayer && activeScreen == NavigationScreen.FAVORITES) {
                        IconButton(
                            onClick = onImportFavorites,
                            enabled = !isLoadingBank
                        ) {
                            Icon(
                                Icons.Filled.FileUpload,
                                contentDescription = "Import Favorites"
                            )
                        }

                        IconButton(
                            onClick = onExportFavorites,
                            enabled = !isLoadingBank
                        ) {
                            Icon(
                                Icons.Filled.FileDownload,
                                contentDescription = "Export Favorites"
                            )
                        }
                    }

                    // Sort button for Home screen (placed where Search has the reindex control)
                    else if (!viewModel.showFullPlayer && activeScreen == NavigationScreen.HOME) {
                        IconButton(
                            onClick = {
                                viewModel.cycleHomeSortMode()
                                try {
                                    context.getSharedPreferences("NeoBAE_prefs", Context.MODE_PRIVATE)
                                        .edit()
                                        .putString("home_sort_mode", viewModel.homeSortMode.name)
                                        .apply()
                                } catch (_: Exception) {
                                }
                            },
                            enabled = !isLoadingBank
                        ) {
                            SortModeIcon(viewModel.homeSortMode)
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
                                    val folderName = getDisplayFolderName(viewModel.currentFolderPath)
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
                        selected = !viewModel.showFullPlayer && activeScreen == NavigationScreen.HOME,
                        onClick = { 
                            viewModel.showFullPlayer = false
                            onNavigate(NavigationScreen.HOME)
                        },
                        enabled = !isLoadingBank,
                        selectedContentColor = MaterialTheme.colors.primary,
                        unselectedContentColor = Color.Gray
                    )
                    if (searchEnabled) {
                        BottomNavigationItem(
                            icon = { Icon(Icons.Filled.Search, contentDescription = "Search") },
                            selected = !viewModel.showFullPlayer && activeScreen == NavigationScreen.SEARCH,
                            onClick = {
                                viewModel.showFullPlayer = false
                                onNavigate(NavigationScreen.SEARCH)
                            },
                            enabled = !isLoadingBank,
                            selectedContentColor = MaterialTheme.colors.primary,
                            unselectedContentColor = Color.Gray
                        )
                    }
                    if (favoritesEnabled) {
                        BottomNavigationItem(
                            icon = { Icon(Icons.Filled.Favorite, contentDescription = "Favorites") },
                            selected = !viewModel.showFullPlayer && activeScreen == NavigationScreen.FAVORITES,
                            onClick = {
                                viewModel.showFullPlayer = false
                                onNavigate(NavigationScreen.FAVORITES)
                            },
                            enabled = !isLoadingBank,
                            selectedContentColor = MaterialTheme.colors.primary,
                            unselectedContentColor = Color.Gray
                        )
                    }
                    BottomNavigationItem(
                        icon = { Icon(Icons.Filled.Settings, contentDescription = "Settings") },
                        selected = !viewModel.showFullPlayer && (activeScreen == NavigationScreen.SETTINGS || activeScreen == NavigationScreen.FILE_TYPES),
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
                when (activeScreen) {
                NavigationScreen.HOME -> HomeScreenContent(
                    viewModel = viewModel,
                    loading = loading,
                    onPlaylistItemClick = onPlaylistItemClick,
                    onToggleFavorite = onToggleFavorite,
                    showFavorites = favoritesEnabled,
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
                    showFavorites = favoritesEnabled,
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
                    bankPath = bankPath,
                    hasEggsBank = hasEggsBank,
                    hasMobileBAEBank = hasMobileBAEBank,
                    dlsBankLevel = dlsBankLevel,
                    hasXmfOverlay = hasXmfOverlay,
                    hasRmiEmbedded = hasRmiEmbedded,
                    rmiUsesSf2 = rmiUsesSf2,
                    isLoadingBank = isLoadingBank,
                    reverbType = reverbType,
                    velocityCurve = velocityCurve,
                    fixPanLfoBias = fixPanLfoBias,
                    classicChorus = classicChorus,
                    webtvMode = webtvMode,
                    dlsCompatibilityMode = dlsCompatibilityMode,
                    normalizePlayback = normalizePlayback,
                    exportCodec = exportCodec,
                    baeScriptEnabled = baeScriptEnabled,
                    baeScriptSource = baeScriptSource,
                    searchResultLimit = searchResultLimit,
                    showSearchFeatures = searchEnabled,
                    onLoadBuiltin = onLoadBuiltin,
                    onReverbChange = onReverbChange,
                    onCurveChange = onCurveChange,
                    onFixPanLfoChange = onFixPanLfoChange,
                    onClassicChorusChange = onClassicChorusChange,
                    onWebTVModeChange = onWebTVModeChange,
                    onDLSCompatibilityModeChange = { enabled ->
                        HomeFragment.dlsCompatibilityMode.value = enabled
                        Mixer.setDLSCompatibilityMode(enabled)
                        context.getSharedPreferences("NeoBAE_prefs", Context.MODE_PRIVATE)
                            .edit().putBoolean("dls_compatibility_mode", enabled).apply()
                    },
                    onNormalizePlaybackChange = onNormalizePlaybackChange,
                    onVolumeChange = onVolumeChange,
                    onExportCodecChange = onExportCodecChange,
                    onBaeScriptEnabledChange = onBaeScriptEnabledChange,
                    onBaeScriptSourceChange = onBaeScriptSourceChange,
                    onSearchLimitChange = onSearchLimitChange,
                    onOpenFileTypes = { onNavigate(NavigationScreen.FILE_TYPES) },
                    onBrowseBanks = onBrowseBanks,
                    onOpenCustomReverb = {
                        if (Mixer.exists()) {
                            onNavigate(NavigationScreen.CUSTOM_REVERB)
                        } else {
                            Toast.makeText(context, "Please start playback first to access custom reverb settings", Toast.LENGTH_SHORT).show()
                        }
                    },
                    onCustomReverbSync = { viewModel.bumpCustomReverbSync() },
                    customEQSyncSerial = viewModel.customEQSyncSerial,
                    onCustomEQSync = { viewModel.bumpCustomEQSync() },
                    onAddFolder = onAddFolder,
                    onRemovePersistedFolder = { folderUri ->
                        val uri = Uri.parse(folderUri)
                        runCatching {
                            context.contentResolver.releasePersistableUriPermission(
                                uri,
                                Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_WRITE_URI_PERMISSION
                            )
                        }
                        pruneUnavailableFavorites(context, viewModel.favorites)
                        saveFavorites(context, viewModel.favorites)
                    }
                )
                NavigationScreen.FILE_TYPES -> FileTypesScreenContent(
                    enabledExtensions = enabledExtensions,
                    onExtensionEnabledChange = onExtensionEnabledChange
                )
                NavigationScreen.CUSTOM_REVERB -> CustomReverbScreenContent(
                    ctx = context,
                    syncSerial = viewModel.customReverbSyncSerial,
                    onLowpassChanged = { /* no-op */ }
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
                showFavorites = favoritesEnabled,
                exportCodec = exportCodec,
                onExportRequest = onExportRequest,
                getMidiChannelMuteStatus = getMidiChannelMuteStatus,
                onSetMidiChannelMuted = onSetMidiChannelMuted,
                onRepeatModeChange = onRepeatModeChange,
                onNavigateToSettings = { 
                    viewModel.showFullPlayer = false
                    onNavigate(NavigationScreen.SETTINGS)
                }
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
                    showPickFolderAction = !BuildConfig.USE_MANAGE_EXTERNAL_STORAGE,
                    onPickFolder = onPickBankFolder,
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
        val target = deleteTargetPath ?: viewModel.currentIndexRootPath ?: viewModel.currentFolderPath
        androidx.compose.material.AlertDialog(
            onDismissRequest = { showDeleteDialog = false },
            title = { Text("Delete Database") },
            text = {
                Text("This will delete the database for:\n\"$target\"\n\nAre you sure you wish to continue? This action cannot be undone.")
            },
            confirmButton = {
                androidx.compose.material.TextButton(
                    onClick = {
                        showDeleteDialog = false
                        val indexPathToDelete = target
                        if (indexPathToDelete.isNullOrBlank()) {
                            Toast.makeText(
                                context,
                                "No database found to delete",
                                Toast.LENGTH_SHORT
                            ).show()
                            return@TextButton
                        }
                        viewModel.deleteDatabaseForIndexPath(indexPathToDelete) { success ->
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

    // Overwrite index confirmation dialog (shown when an index already exists for this path)
    if (showOverwriteIndexDialog) {
        val target = overwriteIndexTargetPath
        androidx.compose.material.AlertDialog(
            onDismissRequest = {
                showOverwriteIndexDialog = false
                overwriteIndexTargetPath = null
            },
            title = { Text("Rebuild Index") },
            text = {
                Text("An index already exists for:\n\"$target\"\n\nRebuilding will overwrite the current index. Continue?")
            },
            confirmButton = {
                androidx.compose.material.TextButton(
                    onClick = {
                        showOverwriteIndexDialog = false
                        val indexRoot = target
                        overwriteIndexTargetPath = null
                        if (indexRoot.isNullOrBlank()) return@TextButton
                        viewModel.rebuildIndex(indexRoot) { files, folders, size ->
                            Toast.makeText(
                                context,
                                "Indexed $files files in $folders folders (${size / 1024 / 1024} MB)",
                                Toast.LENGTH_LONG
                            ).show()
                            if (viewModel.searchQuery.isNotEmpty()) {
                                viewModel.searchFilesInDatabase(viewModel.searchQuery, viewModel.currentFolderPath, searchResultLimit)
                            } else {
                                viewModel.getAllFilesInDatabase(viewModel.currentFolderPath, searchResultLimit)
                            }
                        }
                    }
                ) {
                    Text("Overwrite")
                }
            },
            dismissButton = {
                androidx.compose.material.TextButton(
                    onClick = {
                        showOverwriteIndexDialog = false
                        overwriteIndexTargetPath = null
                    }
                ) {
                    Text("Cancel")
                }
            }
        )
    }
}
