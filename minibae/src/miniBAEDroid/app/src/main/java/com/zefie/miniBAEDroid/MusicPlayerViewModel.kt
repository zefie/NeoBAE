package com.zefie.miniBAEDroid

import androidx.lifecycle.ViewModel
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.setValue

class MusicPlayerViewModel : ViewModel() {
    // Playlist
    val playlist = mutableStateListOf<PlaylistItem>()
    val folderFiles = mutableStateListOf<PlaylistItem>()
    var currentIndex by mutableStateOf(-1)
    
    // Player state
    var isPlaying by mutableStateOf(false)
    var currentPositionMs by mutableStateOf(0)
    var totalDurationMs by mutableStateOf(0)
    var volumePercent by mutableStateOf(75)
    var currentTitle by mutableStateOf("No song loaded")
    
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
}

enum class NavigationScreen {
    HOME, SEARCH, PLAYLIST, FAVORITES, SETTINGS
}

enum class RepeatMode {
    NONE,      // No repeat
    SONG,      // Repeat current song
    PLAYLIST   // Repeat playlist
}
