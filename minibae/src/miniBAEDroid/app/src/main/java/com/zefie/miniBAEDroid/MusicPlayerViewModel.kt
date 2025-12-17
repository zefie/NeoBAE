package com.zefie.miniBAEDroid

import android.content.Context
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
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

class MusicPlayerViewModel : ViewModel() {
    // Playlist
    val playlist = mutableStateListOf<PlaylistItem>()
    val folderFiles = mutableStateListOf<PlaylistItem>()
    var currentIndex by mutableStateOf(-1)
    
    // Database and indexer (initialized lazily)
    private var database: SQLiteHelper? = null
    private var fileIndexer: FileIndexer? = null
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
    var currentFolderPath by mutableStateOf<String?>(null)
    val favorites = mutableStateListOf<String>() // Store file paths of favorited songs
    var showFullPlayer by mutableStateOf(false)
    var repeatMode by mutableStateOf(RepeatMode.NONE)
    var isShuffled by mutableStateOf(false)
    private var shuffledIndices = mutableListOf<Int>()
    
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
            database = SQLiteHelper.getInstance(context)
            fileIndexer = FileIndexer(context)
            
            // Load indexed file count
            viewModelScope.launch {
                indexedFileCount = fileIndexer?.getIndexedFileCount() ?: 0
                isDatabaseReady = true
                android.util.Log.d("MusicPlayerViewModel", "Database initialized, indexed files: $indexedFileCount")
            }
        } else {
            android.util.Log.d("MusicPlayerViewModel", "Database already initialized")
            // Make sure state is set if already initialized
            if (!isDatabaseReady) {
                viewModelScope.launch {
                    indexedFileCount = fileIndexer?.getIndexedFileCount() ?: 0
                    isDatabaseReady = true
                }
            }
        }
    }
    
    // Check if current path has an index
    fun isPathIndexed(path: String?): Boolean {
        if (database == null || path == null || path == "/") return false
        return database!!.hasIndexForPath(path)
    }
    
    // Database-backed search (instant results)
    fun searchFilesInDatabase(query: String, currentPath: String?) {
        android.util.Log.d("MusicPlayerViewModel", "searchFilesInDatabase: query='$query', path='$currentPath', db=${database != null}")
        
        if (query.length < 3) {
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
                            db.searchFilesInPath(currentPath, query)
                        } else {
                            // Use current path or default to /sdcard for finding parent index
                            val searchPath = currentPath ?: "/sdcard"
                            android.util.Log.d("MusicPlayerViewModel", "Searching in root, searchPath: $searchPath")
                            db.searchFiles(searchPath, query)
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
                    var hasCompleted = false
                    
                    // Collect progress
                    val progressJob = launch {
                        indexer.progress.collect { progress ->
                            if (!progress.isIndexing && !hasCompleted) {
                                hasCompleted = true
                                // Update count when done
                                indexedFileCount = indexer.getIndexedFileCount()
                                onComplete(progress.filesIndexed, progress.foldersScanned, progress.totalSize)
                            }
                        }
                    }
                    
                    // Start indexing from specified directory
                    indexer.rebuildIndex(rootPath)
                    
                    // Cancel progress collection after indexing completes
                    progressJob.cancel()
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
        
        val validExtensions = setOf("mid", "midi", "kar", "rmf", "rmi")
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
    HOME, SEARCH, PLAYLIST, FAVORITES, SETTINGS
}

enum class RepeatMode {
    NONE,      // No repeat
    SONG,      // Repeat current song
    PLAYLIST   // Repeat playlist
}
