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

@OptIn(ExperimentalMaterialApi::class)
@Composable
fun HomeScreenContent(
    viewModel: MusicPlayerViewModel,
    loading: Boolean,
    onPlaylistItemClick: (File) -> Unit,
    onToggleFavorite: (String) -> Unit,
    showFavorites: Boolean,
    onAddFolder: () -> Unit,
    onShufflePlay: () -> Unit,
    onNavigateToFolder: (String) -> Unit,
    onAddToPlaylist: (File) -> Unit,
    onRefreshStorage: () -> Unit,
    onAddAllMidi: () -> Unit,
    onAddAllMidiRecursive: () -> Unit
) {
    val listState = rememberLazyListState()
    var refreshing by remember { mutableStateOf(false) }
    val refreshScope = rememberCoroutineScope()

    // Scroll to top whenever the displayed folder changes.
    LaunchedEffect(viewModel.currentFolderPath) {
        listState.scrollToItem(0)
    }

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
        derivedStateOf {
            val base = folderFilesSnapshot.filter { !it.isFolder && !it.title.startsWith("🔄") }
            sortPlaylistItems(base, viewModel.homeSortMode)
        }
    }
    val favoritesSet by remember {
        derivedStateOf { viewModel.favorites.toSet() }
    }

    // Used for the in-list "current item" status icon.
    val currentLoadedPath by remember {
        derivedStateOf { viewModel.getCurrentItem()?.file?.absolutePath }
    }
    val isPlaying by remember { derivedStateOf { viewModel.isPlaying } }
    val showSelectFolderRow = !BuildConfig.USE_MANAGE_EXTERNAL_STORAGE

    // If a song is playing and the user changes sort, jump to the current song in the list.
    LaunchedEffect(viewModel.homeSortMode) {
        if (!viewModel.isPlaying) return@LaunchedEffect

        val currentPath = viewModel.getCurrentItem()?.file?.absolutePath ?: return@LaunchedEffect
        if (songFiles.isEmpty()) return@LaunchedEffect

        val idxInSongs = songFiles.indexOfFirst { it.file.absolutePath == currentPath }
        if (idxInSongs < 0) return@LaunchedEffect

        val currentFolderPath = viewModel.currentFolderPath
        val hasParentItem = currentFolderPath != null && currentFolderPath != "/" && File(currentFolderPath).parent != null
        val baseOffset = (if (showSelectFolderRow) 1 else 0) + (if (hasParentItem) 1 else 0) + folderAndSpecialItems.size
        listState.animateScrollToItem((baseOffset + idxInSongs).coerceAtLeast(0))
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
                        ,
                        state = listState
                    ) {
                    if (showSelectFolderRow) {
                        item(key = "select_folder") {
                            Surface(
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .clickable { onAddFolder() },
                                color = Color.Transparent
                            ) {
                                Row(
                                    modifier = Modifier
                                        .fillMaxWidth()
                                        .padding(horizontal = 16.dp, vertical = 12.dp),
                                    verticalAlignment = Alignment.CenterVertically
                                ) {
                                    Icon(
                                        Icons.Filled.FolderOpen,
                                        contentDescription = null,
                                        tint = MaterialTheme.colors.primary,
                                        modifier = Modifier.size(40.dp)
                                    )
                                    Spacer(modifier = Modifier.width(12.dp))
                                    Text(
                                        text = if (viewModel.currentFolderPath == "/") "Add Folder" else "Select Folder",
                                        fontSize = 14.sp,
                                        fontWeight = FontWeight.Bold,
                                        color = MaterialTheme.colors.onBackground
                                    )
                                }
                            }
                            Divider(color = Color.Gray.copy(alpha = 0.2f))
                        }
                    }

                    // Show parent directory ".." option
                    viewModel.currentFolderPath?.let { currentPath ->
                        val parentPath = if (currentPath.startsWith("content://")) {
                            // At a SAF tree root, ".." returns to the saved-folders list.
                            getSafParentPath(currentPath) ?: if (showSelectFolderRow) "/" else null
                        } else {
                            File(currentPath).parent
                        }
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
                                isCurrent = (currentLoadedPath != null && currentLoadedPath == item.file.absolutePath),
                                isPlaying = isPlaying,
                                showFavoriteButton = showFavorites,
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
    showFavorites: Boolean,
    onAddToPlaylist: (File) -> Unit,
    searchResultLimit: Int
) {
    val context = LocalContext.current
    val listState = rememberLazyListState()
    val indexingProgress by viewModel.getIndexingProgress()?.collectAsState() ?: remember { mutableStateOf(IndexingProgress()) }
    val searchResults by viewModel.searchResults.collectAsState()
    val sortedSearchResults by remember {
        derivedStateOf { sortPlaylistItems(searchResults, viewModel.searchSortMode) }
    }
    val indexingDisplayPath by remember(indexingProgress.currentPath) {
        mutableStateOf(getDisplayPath(indexingProgress.currentPath))
    }
    val configuration = LocalConfiguration.current
    val isLandscape = configuration.orientation == Configuration.ORIENTATION_LANDSCAPE

    // Used for the in-list "current item" status icon.
    val currentLoadedPath by remember {
        derivedStateOf { viewModel.getCurrentItem()?.file?.absolutePath }
    }
    val isPlaying by remember { derivedStateOf { viewModel.isPlaying } }

    // If a song is playing and the user changes sort, jump to the current song in the list.
    LaunchedEffect(viewModel.searchSortMode) {
        if (!viewModel.isPlaying) return@LaunchedEffect
        val currentPath = viewModel.getCurrentItem()?.file?.absolutePath ?: return@LaunchedEffect
        val idx = sortedSearchResults.indexOfFirst { it.file.absolutePath == currentPath }
        if (idx >= 0) {
            listState.animateScrollToItem(idx)
        }
    }
    
    // Initialize database on first composition
    LaunchedEffect(Unit) {
        viewModel.initializeDatabase(context)
    }
    
    // Trigger search when query changes or when showing all results
    // IMPORTANT: Wait for database to be ready before searching
    LaunchedEffect(viewModel.searchQuery, searchResultLimit, viewModel.isDatabaseReady, viewModel.currentFolderPath) {
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
        val isSafCurrentPath = viewModel.currentFolderPath?.startsWith("content://") == true
        
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

        val showSafIndexingNotice = isSafCurrentPath &&
            (indexingProgress.isIndexing || viewModel.indexedFileCount == 0 || !isCurrentPathIndexed)

        if (showSafIndexingNotice) {
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
                        "SAF indexing can take some time, especially for large folders.",
                        fontSize = 13.sp,
                        color = Color.Gray
                    )
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
                            indexingDisplayPath,
                            fontSize = 10.sp,
                            color = Color.Gray,
                            maxLines = 2,
                            overflow = TextOverflow.Ellipsis
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
                if (sortedSearchResults.isEmpty()) {
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
                    LazyColumn(modifier = Modifier.fillMaxSize(), state = listState) {
                        itemsIndexed(sortedSearchResults) { index, item ->
                            FolderSongListItem(
                                item = item,
                                isFavorite = viewModel.isFavorite(item.path),
                                isCurrent = (currentLoadedPath != null && currentLoadedPath == item.file.absolutePath),
                                isPlaying = isPlaying,
                                showFavoriteButton = showFavorites,
                                onClick = { onPlaylistItemClick(item.file) },
                                onToggleFavorite = { onToggleFavorite(item.path) },
                                onAddToPlaylist = { onAddToPlaylist(item.file) }
                            )
                            if (index < sortedSearchResults.size - 1) {
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
            sortedSearchResults.isEmpty() -> {
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
                LazyColumn(modifier = Modifier.fillMaxSize(), state = listState) {
                    itemsIndexed(sortedSearchResults) { index, item ->
                        FolderSongListItem(
                            item = item,
                            isFavorite = viewModel.isFavorite(item.path),
                            isCurrent = (currentLoadedPath != null && currentLoadedPath == item.file.absolutePath),
                            isPlaying = isPlaying,
                            showFavoriteButton = showFavorites,
                            onClick = { onPlaylistItemClick(item.file) },
                            onToggleFavorite = { onToggleFavorite(item.path) },
                            onAddToPlaylist = { onAddToPlaylist(item.file) }
                        )
                        if (index < sortedSearchResults.size - 1) {
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
    val context = LocalContext.current

    // Keep the list clean so indices match and drag-reorder stays consistent.
    LaunchedEffect(Unit) {
        pruneUnavailableFavorites(context, viewModel.favorites)
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
                    val item = remember(path) { buildFavoritePlaylistItem(context, path) }
                    if (item == null) return@itemsIndexed
                    val isDragging = draggingIndex == index
                    val isCurrent = viewModel.getCurrentItem()?.file?.absolutePath == item.file.absolutePath
                    val isPlaying = viewModel.isPlaying

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
                            color = if (isCurrent) MaterialTheme.colors.surface.copy(alpha = 0.5f) else Color.Transparent
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
                                    Row(
                                        modifier = Modifier.fillMaxWidth(),
                                        verticalAlignment = Alignment.CenterVertically
                                    ) {
                                        if (isCurrent) {
                                            Icon(
                                                imageVector = if (isPlaying) Icons.Filled.PlayArrow else Icons.Filled.Pause,
                                                contentDescription = if (isPlaying) "Playing" else "Paused",
                                                tint = MaterialTheme.colors.primary,
                                                modifier = Modifier.size(18.dp)
                                            )
                                            Spacer(modifier = Modifier.width(4.dp))
                                        }

                                        Text(
                                            text = item.title,
                                            fontSize = 14.sp,
                                            fontWeight = FontWeight.Normal,
                                            color = MaterialTheme.colors.onBackground,
                                            maxLines = 1,
                                            overflow = TextOverflow.Ellipsis,
                                            modifier = Modifier.weight(1f)
                                        )
                                    }
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
@Composable
fun FolderSongListItem(
    item: PlaylistItem,
    isFavorite: Boolean,
    isCurrent: Boolean = false,
    isPlaying: Boolean = false,
    showFavoriteButton: Boolean = true,
    onClick: () -> Unit,
    onToggleFavorite: () -> Unit,
    onAddToPlaylist: () -> Unit
) {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        color = if (isCurrent) MaterialTheme.colors.surface.copy(alpha = 0.5f) else Color.Transparent
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
                val fileSizeBytes = remember(item.path) {
                    resolvePlaylistItemSize(item)
                }
                val fileSizeText = remember(fileSizeBytes) {
                    if (fileSizeBytes > 0L) formatFileSize(fileSizeBytes) else ""
                }
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    if (isCurrent) {
                        Icon(
                            imageVector = if (isPlaying) Icons.Filled.PlayArrow else Icons.Filled.Pause,
                            contentDescription = if (isPlaying) "Playing" else "Paused",
                            tint = MaterialTheme.colors.primary,
                            modifier = Modifier.size(18.dp)
                        )
                        Spacer(modifier = Modifier.width(4.dp))
                    }
                    Text(
                        text = item.title,
                        fontSize = 14.sp,
                        fontWeight = FontWeight.Normal,
                        color = MaterialTheme.colors.onBackground,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                        modifier = Modifier.weight(1f)
                    )
                }

                if (fileSizeText.isNotEmpty()) {
                    Text(
                        text = fileSizeText,
                        fontSize = 12.sp,
                        color = MaterialTheme.colors.onBackground.copy(alpha = 0.6f),
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis
                    )
                }
            }
            
            if (showFavoriteButton) {
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
}
