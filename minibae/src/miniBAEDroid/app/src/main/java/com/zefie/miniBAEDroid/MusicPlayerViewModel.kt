package com.zefie.miniBAEDroid

import android.content.Context
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import androidx.compose.runtime.Stable
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.setValue
import com.zefie.miniBAEDroid.database.SQLiteHelper
import com.zefie.miniBAEDroid.database.FileEntity
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.File

enum class SortMode {
    NAME_ASC,
    NAME_DESC,
    SIZE_ASC,
    SIZE_DESC
}

private fun nextSortMode(current: SortMode): SortMode {
    return when (current) {
        SortMode.NAME_ASC -> SortMode.NAME_DESC
        SortMode.NAME_DESC -> SortMode.SIZE_ASC
        SortMode.SIZE_ASC -> SortMode.SIZE_DESC
        SortMode.SIZE_DESC -> SortMode.NAME_ASC
    }
}

@Stable
class MusicPlayerViewModel : ViewModel() {
    // Playlist
    val playlist = mutableStateListOf<PlaylistItem>()
    val folderFiles = mutableStateListOf<PlaylistItem>()
    var currentIndex by mutableStateOf(-1)
    
    // Database and indexer (initialized lazily)
    private var database: SQLiteHelper? = null
    private var fileIndexer: FileIndexer? = null
    private var appContext: Context? = null
    var isDatabaseReady by mutableStateOf(false)
    
    // Search state
    private val _searchResults = MutableStateFlow<List<PlaylistItem>>(emptyList())
    val searchResults: StateFlow<List<PlaylistItem>> = _searchResults
    
    var isSearching by mutableStateOf(false)
    var indexedFileCount by mutableStateOf(0)
    
    // Legacy cache (kept for compatibility but not used for database search)
    private var cachedSearchFiles: List<PlaylistItem>? = null
    private var cachedSearchPath: String? = null
    
    // Player state
    var isPlaying by mutableStateOf(false)
    var currentPositionMs by mutableStateOf(0)
    var totalDurationMs by mutableStateOf(0)
    var volumePercent by mutableStateOf(75)
    var currentTitle by mutableStateOf("No song loaded")
    var currentLyric by mutableStateOf("")
    
    // UI state
    var isDraggingSeekBar by mutableStateOf(false)
    
    // Navigation state
    var currentScreen by mutableStateOf(NavigationScreen.HOME)
    var searchQuery by mutableStateOf("")

    // Sorting
    var homeSortMode by mutableStateOf(SortMode.NAME_ASC)
    var searchSortMode by mutableStateOf(SortMode.NAME_ASC)
    
    private val _currentFolderPath = mutableStateOf<String?>(null)
    var currentFolderPath: String?
        get() = _currentFolderPath.value
        set(value) {
            _currentFolderPath.value = value
            checkIfCurrentPathIndexed()
        }
        
    var isCurrentPathIndexed by mutableStateOf(false)
    var hasExactDatabase by mutableStateOf(false)
    
    val favorites = mutableStateListOf<String>() // Store file paths of favorited songs
    var showFullPlayer by mutableStateOf(false)
    var repeatMode by mutableStateOf(RepeatMode.NONE)
    var isShuffled by mutableStateOf(false)
    private var shuffledIndices = mutableListOf<Int>()

    // MIDI channel mutes (checked = enabled/unmuted in UI)
    val midiChannelEnabled = mutableStateListOf<Boolean>().apply { repeat(16) { add(true) } }

    fun setMidiChannelMuted(channel: Int, muted: Boolean) {
        if (channel !in 0 until 16) return
        midiChannelEnabled[channel] = !muted
    }

    fun getMidiChannelMuteStatus(): BooleanArray {
        val muted = BooleanArray(16)
        for (i in 0 until 16) {
            muted[i] = !midiChannelEnabled[i]
        }
        return muted
    }
    
    fun toggleShuffle() {
        isShuffled = !isShuffled
        if (isShuffled) {
            // Generate shuffled order
            shuffledIndices = (0 until playlist.size).toMutableList().apply { shuffle() }
            // Find current song in shuffled list
            if (currentIndex >= 0) {
                val currentItemIndex = shuffledIndices.indexOf(currentIndex)
                if (currentItemIndex >= 0) {
                    // Move current song to the front of shuffled list
                    shuffledIndices.removeAt(currentItemIndex)
                    shuffledIndices.add(0, currentIndex)
                }
            }
        } else {
            // Clear shuffle order
            shuffledIndices.clear()
        }
    }

    fun cycleHomeSortMode() {
        homeSortMode = nextSortMode(homeSortMode)
    }

    fun cycleSearchSortMode() {
        searchSortMode = nextSortMode(searchSortMode)
    }
    
    private fun getNextIndex(): Int? {
        if (playlist.isEmpty()) return null
        
        if (isShuffled) {
            if (shuffledIndices.isEmpty()) return null
            val currentPos = shuffledIndices.indexOf(currentIndex)
            if (currentPos >= 0 && currentPos < shuffledIndices.size - 1) {
                return shuffledIndices[currentPos + 1]
            }
            return null
        } else {
            return if (currentIndex < playlist.size - 1) currentIndex + 1 else null
        }
    }
    
    private fun getPreviousIndex(): Int? {
        if (playlist.isEmpty()) return null
        
        if (isShuffled) {
            if (shuffledIndices.isEmpty()) return null
            val currentPos = shuffledIndices.indexOf(currentIndex)
            if (currentPos > 0) {
                return shuffledIndices[currentPos - 1]
            }
            return null
        } else {
            return if (currentIndex > 0) currentIndex - 1 else null
        }
    }
    
    fun addToPlaylist(item: PlaylistItem) {
        if (playlist.none { it.id == item.id }) {
            playlist.add(item)
            // Update shuffle list if shuffle is enabled
            if (isShuffled) {
                shuffledIndices.add(playlist.size - 1)
            }
        }
    }
    
    fun addAllToPlaylist(items: List<PlaylistItem>) {
        val sizeBefore = playlist.size
        items.forEach { addToPlaylist(it) }
        // Regenerate shuffle if items were added and shuffle is on
        if (isShuffled && playlist.size > sizeBefore) {
            // Re-shuffle the newly added items
            val newIndices = (sizeBefore until playlist.size).toMutableList().apply { shuffle() }
            shuffledIndices.addAll(newIndices)
        }
    }
    
    fun removeFromPlaylist(index: Int) {
        if (index in playlist.indices) {
            if (index == currentIndex) {
                // If removing current song, stop playback
                currentIndex = -1
                isPlaying = false
            } else if (index < currentIndex) {
                // Adjust current index if removing earlier item
                currentIndex--
            }
            playlist.removeAt(index)
            
            // Update shuffle list if shuffle is enabled
            if (isShuffled) {
                shuffledIndices.remove(index)
                // Adjust indices greater than removed index
                shuffledIndices.replaceAll { if (it > index) it - 1 else it }
            }
        }
    }
    
    fun clearPlaylist() {
        playlist.clear()
        currentIndex = -1
        isPlaying = false
        currentTitle = "No song loaded"
        shuffledIndices.clear()
    }
    
    fun moveItem(from: Int, to: Int) {
        if (from in playlist.indices && to in playlist.indices) {
            val item = playlist.removeAt(from)
            playlist.add(to, item)
            
            // Adjust current index
            when {
                currentIndex == from -> currentIndex = to
                currentIndex in (from + 1)..to -> currentIndex--
                currentIndex in to until from -> currentIndex++
            }

            // Keep shuffle indices coherent (they store playlist indices).
            if (isShuffled && shuffledIndices.isNotEmpty()) {
                shuffledIndices.replaceAll { idx ->
                    when {
                        idx == from -> to
                        from < to && idx in (from + 1)..to -> idx - 1
                        to < from && idx in to until from -> idx + 1
                        else -> idx
                    }
                }
            }
        }
    }

    fun replacePlaylistPreservingCurrent(newItems: List<PlaylistItem>) {
        val currentPath = getCurrentItem()?.path
        playlist.clear()
        playlist.addAll(newItems)

        currentIndex = if (currentPath != null) {
            playlist.indexOfFirst { it.path == currentPath }
        } else {
            if (playlist.isNotEmpty()) 0 else -1
        }

        if (isShuffled) {
            // Regenerate shuffled order for the new playlist.
            shuffledIndices = (0 until playlist.size).toMutableList().apply { shuffle() }
            if (currentIndex >= 0) {
                val currentPos = shuffledIndices.indexOf(currentIndex)
                if (currentPos >= 0) {
                    shuffledIndices.removeAt(currentPos)
                    shuffledIndices.add(0, currentIndex)
                }
            }
        }
    }
    
    fun playAtIndex(index: Int) {
        if (index in playlist.indices) {
            currentIndex = index
            currentTitle = playlist[index].title
        }
    }
    
    fun hasNext(): Boolean = getNextIndex() != null
    fun hasPrevious(): Boolean = getPreviousIndex() != null
    
    fun playNext() {
        getNextIndex()?.let { playAtIndex(it) }
    }
    
    fun playPrevious() {
        getPreviousIndex()?.let { playAtIndex(it) }
    }
    
    fun getCurrentItem(): PlaylistItem? {
        return if (currentIndex in playlist.indices) {
            playlist[currentIndex]
        } else null
    }
    
    fun toggleFavorite(filePath: String) {
        if (favorites.contains(filePath)) {
            favorites.remove(filePath)
        } else {
            favorites.add(filePath)
        }
    }
    
    fun isFavorite(filePath: String): Boolean {
        return favorites.contains(filePath)
    }
    
    // Initialize database (call from Fragment with context)
    fun initializeDatabase(context: Context) {
        if (database == null) {
            android.util.Log.d("MusicPlayerViewModel", "Initializing database...")
            appContext = context.applicationContext
            database = SQLiteHelper.getInstance(context)
            fileIndexer = FileIndexer(context)
            
            // Load indexed file count
            viewModelScope.launch {
                indexedFileCount = fileIndexer?.getIndexedFileCount() ?: 0
                isDatabaseReady = true
                checkIfCurrentPathIndexed()
                android.util.Log.d("MusicPlayerViewModel", "Database initialized, indexed files: $indexedFileCount")
            }
        } else {
            android.util.Log.d("MusicPlayerViewModel", "Database already initialized")
            // Make sure state is set if already initialized
            if (!isDatabaseReady) {
                viewModelScope.launch {
                    indexedFileCount = fileIndexer?.getIndexedFileCount() ?: 0
                    isDatabaseReady = true
                    checkIfCurrentPathIndexed()
                }
            } else {
                // Even if ready, re-check current path status
                checkIfCurrentPathIndexed()
            }
        }
    }
    
    // Check if current path has an index
    fun isPathIndexed(path: String?): Boolean {
        if (database == null || path == null || path == "/") return false
        return database!!.hasIndexForPath(path)
    }
    
    fun checkIfCurrentPathIndexed() {
        viewModelScope.launch(Dispatchers.IO) {
            val indexed = isPathIndexed(currentFolderPath)
            val hasExact = hasExactDatabaseForCurrentPath()
            // Update the indexed file count for the current path's database
            val count = fileIndexer?.getIndexedFileCountForPath(currentFolderPath) ?: 0
            withContext(Dispatchers.Main) {
                isCurrentPathIndexed = indexed
                hasExactDatabase = hasExact
                indexedFileCount = count
            }
        }
    }
    
    // Database-backed search (instant results)
    fun searchFilesInDatabase(query: String, currentPath: String?, limit: Int = 1000) {
        android.util.Log.d("MusicPlayerViewModel", "searchFilesInDatabase: query='$query', path='$currentPath', limit=$limit, db=${database != null}")
        
        if (query.isEmpty()) {
            _searchResults.value = emptyList()
            return
        }
        
        if (database == null) {
            android.util.Log.w("MusicPlayerViewModel", "Database not initialized!")
            _searchResults.value = emptyList()
            return
        }
        
        // Check if current path is covered by an index
        if (currentPath != null && currentPath != "/" && !database!!.hasIndexForPath(currentPath)) {
            android.util.Log.w("MusicPlayerViewModel", "Current path not indexed: $currentPath")
            _searchResults.value = emptyList()
            return
        }
        
        isSearching = true
        viewModelScope.launch {
            try {
                val results = withContext(Dispatchers.IO) {
                    val db = database
                    if (db != null) {
                        val entities = if (currentPath != null && currentPath != "/") {
                            android.util.Log.d("MusicPlayerViewModel", "Searching in path: $currentPath")
                            db.searchFilesInPath(currentPath, query, limit)
                        } else {
                            // Use current path or default to /sdcard for finding parent index
                            val searchPath = currentPath ?: "/sdcard"
                            android.util.Log.d("MusicPlayerViewModel", "Searching in root, searchPath: $searchPath")
                            db.searchFiles(searchPath, query, limit)
                        }
                        
                        // Convert entities to PlaylistItems
                        entities.map { entity ->
                            PlaylistItem(
                                file = File(entity.path),
                                title = entity.filename,
                                path = entity.path,
                                durationMs = 0,
                                isFolder = false
                            )
                        }
                    } else {
                        emptyList()
                    }
                }
                _searchResults.value = results
            } catch (e: Exception) {
                _searchResults.value = emptyList()
            } finally {
                isSearching = false
            }
        }
    }
    
    // Get all files in database (for showing all results when search is empty)
    fun getAllFilesInDatabase(currentPath: String?, limit: Int = 1000) {
        android.util.Log.d("MusicPlayerViewModel", "getAllFilesInDatabase: path='$currentPath', limit=$limit, db=${database != null}")
        
        if (database == null) {
            android.util.Log.w("MusicPlayerViewModel", "Database not initialized!")
            _searchResults.value = emptyList()
            return
        }
        
        // Check if current path is covered by an index
        if (currentPath != null && currentPath != "/" && !database!!.hasIndexForPath(currentPath)) {
            android.util.Log.w("MusicPlayerViewModel", "Current path not indexed: $currentPath")
            _searchResults.value = emptyList()
            return
        }
        
        isSearching = true
        viewModelScope.launch {
            try {
                val results = withContext(Dispatchers.IO) {
                    val db = database
                    if (db != null) {
                        val entities = if (currentPath != null && currentPath != "/") {
                            android.util.Log.d("MusicPlayerViewModel", "Getting all files in path: $currentPath")
                            db.getAllFilesInPath(currentPath, limit)
                        } else {
                            // Use current path or default to /sdcard for finding parent index
                            val searchPath = currentPath ?: "/sdcard"
                            android.util.Log.d("MusicPlayerViewModel", "Getting all files in root, searchPath: $searchPath")
                            db.getAllFiles(searchPath, limit)
                        }
                        
                        // Convert entities to PlaylistItems
                        entities.map { entity ->
                            PlaylistItem(
                                file = File(entity.path),
                                title = entity.filename,
                                path = entity.path,
                                durationMs = 0,
                                isFolder = false
                            )
                        }
                    } else {
                        emptyList()
                    }
                }
                _searchResults.value = results
            } catch (e: Exception) {
                _searchResults.value = emptyList()
            } finally {
                isSearching = false
            }
        }
    }
    
    // Build/rebuild the file index for the specified directory and its subdirectories
    fun rebuildIndex(rootPath: String, onComplete: (Int, Int, Long) -> Unit) {
        viewModelScope.launch {
            try {
                fileIndexer?.let { indexer ->
                    // Start indexing from specified directory
                    indexer.rebuildIndex(rootPath)
                    
                    // Update count when done
                    indexedFileCount = indexer.getIndexedFileCount()
                    
                    // Update indexed status for current path
                    checkIfCurrentPathIndexed()
                    
                    // Get final progress to pass to callback
                    val finalProgress = indexer.progress.value
                    onComplete(finalProgress.filesIndexed, finalProgress.foldersScanned, finalProgress.totalSize)
                }
            } catch (e: Exception) {
                // Handle error (including cancellation)
                onComplete(0, 0, 0)
            }
        }
    }
    
    // Stop indexing
    fun stopIndexing() {
        fileIndexer?.cancelIndexing()
    }
    
    // Check if current path exactly matches a database (not just a parent)
    fun hasExactDatabaseForCurrentPath(): Boolean {
        val path = currentFolderPath ?: return false
        return database?.hasExactIndexForPath(path) ?: false
    }
    
    // Delete database for the current path
    fun deleteCurrentDatabase(onComplete: (Boolean) -> Unit) {
        val path = currentFolderPath
        if (path == null) {
            onComplete(false)
            return
        }
        
        viewModelScope.launch(Dispatchers.IO) {
            val success = database?.deleteDatabase(path) ?: false
            if (success) {
                // Update indexed file count
                indexedFileCount = fileIndexer?.getIndexedFileCount() ?: 0
                // Update current path indexed status
                checkIfCurrentPathIndexed()
            }
            withContext(Dispatchers.Main) {
                onComplete(success)
            }
        }
    }
    
    // Get indexer progress flow
    fun getIndexingProgress() = fileIndexer?.progress
    
    override fun onCleared() {
        super.onCleared()
        // Cancel indexing when ViewModel is cleared
        stopIndexing()
    }
    
    // Legacy filesystem search (fallback if database not initialized)
    fun searchFiles(query: String, currentPath: String?): List<PlaylistItem> {
        // Don't search if query is less than 3 characters
        if (query.length < 3) {
            return emptyList()
        }
        
        // Rebuild cache if path changed
        if (cachedSearchPath != currentPath) {
            cachedSearchFiles = null
            cachedSearchPath = currentPath
        }
        
        // Build cache if not exists
        if (cachedSearchFiles == null) {
            cachedSearchFiles = buildFileCache(currentPath)
        }
        
        // Search in cache (case-insensitive, files only)
        val searchLower = query.lowercase()
        return cachedSearchFiles?.filter { 
            !it.isFolder && it.title.lowercase().contains(searchLower)
        } ?: emptyList()
    }
    
    private fun buildFileCache(currentPath: String?): List<PlaylistItem> {
        if (currentPath == null || currentPath == "/") {
            return emptyList()
        }
        
        val folder = File(currentPath)
        if (!folder.exists() || !folder.isDirectory) {
            return emptyList()
        }
        
        val validExtensions = appContext?.let { HomeFragment.getMusicExtensions(it) }
            ?: setOf("mid", "midi", "kar", "rmf", "rmi")
        val result = mutableListOf<PlaylistItem>()
        
        // Recursively scan directory for music files only (no folders)
        fun scanDir(dir: File) {
            try {
                val files = dir.listFiles() ?: return
                for (file in files) {
                    when {
                        file.isDirectory && file.canRead() -> scanDir(file)
                        file.isFile && file.extension.lowercase() in validExtensions -> {
                            result.add(PlaylistItem(file))
                        }
                    }
                }
            } catch (_: SecurityException) {
                // Skip inaccessible directories
            }
        }
        
        scanDir(folder)
        return result.sortedBy { it.title.lowercase() }
    }
    
    fun invalidateSearchCache() {
        cachedSearchFiles = null
        cachedSearchPath = null
    }
}

enum class NavigationScreen {
    HOME, SEARCH, FAVORITES, SETTINGS, FILE_TYPES
}

enum class RepeatMode {
    NONE,      // No repeat
    SONG,      // Repeat current song
    PLAYLIST   // Repeat playlist
}
