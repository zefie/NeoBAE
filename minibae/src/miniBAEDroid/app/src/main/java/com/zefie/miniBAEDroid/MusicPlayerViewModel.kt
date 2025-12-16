package com.zefie.miniBAEDroid

import androidx.lifecycle.ViewModel
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.setValue

class MusicPlayerViewModel : ViewModel() {
    // Playlist
    val playlist = mutableStateListOf<PlaylistItem>()
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
    
    fun addToPlaylist(item: PlaylistItem) {
        if (playlist.none { it.id == item.id }) {
            playlist.add(item)
        }
    }
    
    fun addAllToPlaylist(items: List<PlaylistItem>) {
        items.forEach { addToPlaylist(it) }
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
        }
    }
    
    fun clearPlaylist() {
        playlist.clear()
        currentIndex = -1
        isPlaying = false
        currentTitle = "No song loaded"
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
    
    fun hasNext(): Boolean = currentIndex < playlist.size - 1
    fun hasPrevious(): Boolean = currentIndex > 0
    
    fun playNext() {
        if (hasNext()) {
            playAtIndex(currentIndex + 1)
        }
    }
    
    fun playPrevious() {
        if (hasPrevious()) {
            playAtIndex(currentIndex - 1)
        }
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
    HOME, SEARCH, FAVORITES, SETTINGS
}
