package com.zefie.miniBAEDroid

import android.os.Bundle
import android.content.Context
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.Toast
import android.net.Uri
import androidx.documentfile.provider.DocumentFile
import androidx.fragment.app.Fragment
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.*
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.VolumeOff
import androidx.compose.material.icons.filled.VolumeDown
import androidx.compose.material.icons.filled.VolumeUp
import androidx.compose.animation.Crossfade
import androidx.compose.runtime.*
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.expandHorizontally
import androidx.compose.animation.shrinkHorizontally
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.ComposeView
import androidx.compose.ui.unit.dp
import java.io.File
import org.minibae.Mixer
import org.minibae.Song

class HomeFragment : Fragment() {

    private var pickedFolderUri: Uri? = null
    private var currentSong: Song? = null
    private var currentVolumePercent: Int = 100

    // Called by the activity when a folder is picked via SAF
    fun onFolderPicked(uri: Uri) {
        pickedFolderUri = uri
        Toast.makeText(this.requireContext(), "Folder selected: $uri", Toast.LENGTH_SHORT).show()
        try {
            val takeFlags = (android.content.Intent.FLAG_GRANT_READ_URI_PERMISSION or android.content.Intent.FLAG_GRANT_WRITE_URI_PERMISSION)
            requireContext().contentResolver.takePersistableUriPermission(uri, takeFlags)
        } catch (_: Exception) { }
        // Persist chosen folder for next launch
        try {
            val prefs = requireContext().getSharedPreferences("miniBAE_prefs", Context.MODE_PRIVATE)
            prefs.edit().putString("lastFolderUri", uri.toString()).apply()
        } catch (_: Exception) { }
    // Immediately refresh file listing to reflect new folder
    refreshFiles()
    }

    private var filesState: MutableState<List<File>>? = null
    private var loadingState: MutableState<Boolean>? = null

    override fun onCreateView(inflater: LayoutInflater, container: ViewGroup?, savedInstanceState: Bundle?): View {
        // Attempt to restore last picked folder (only once)
        if (pickedFolderUri == null) {
            try {
                val prefs = requireContext().getSharedPreferences("miniBAE_prefs", Context.MODE_PRIVATE)
                val last = prefs.getString("lastFolderUri", null)
                if (last != null) {
                    val uri = Uri.parse(last)
                    // Ensure we still hold persistable permission
                    val hasPerm = requireContext().contentResolver.persistedUriPermissions.any { it.uri == uri }
                    if (hasPerm) {
                        pickedFolderUri = uri
                    }
                }
                Mixer.setDefaultVelocityCurve(prefs.getInt("velocity_curve", 0)  )
                Mixer.setDefaultReverb(prefs.getInt("default_reverb", 1))
            } catch (_: Exception) { }
        }
        return ComposeView(requireContext()).apply {
            setContent {
                var volume by remember { mutableStateOf(currentVolumePercent.toFloat()) }
                var showSlider by remember { mutableStateOf(false) }
                val files = remember { mutableStateOf(getMediaFiles()) }
                val loading = remember { mutableStateOf(false) }
                filesState = files
                loadingState = loading

                // If we restored a folder, ensure a fresh async scan (will clear then repopulate)
                LaunchedEffect(pickedFolderUri) {
                    if (pickedFolderUri != null) {
                        refreshFiles()
                    }
                }

                LaunchedEffect(volume) {
                    val pct = volume.toInt().coerceIn(0, 100)
                    currentVolumePercent = pct
                    applyVolume()
                }

                var positionMs by remember { mutableStateOf(0) }
                var lengthMs by remember { mutableStateOf(0) }
                var dragging by remember { mutableStateOf(false) }

                // Poll current position every 250ms when not user-dragging
                LaunchedEffect(currentSong, dragging) {
                    while (currentSong != null && !dragging) {
                        try {
                            val pos = currentSong?.getPositionMs() ?: 0
                            val len = currentSong?.getLengthMs() ?: 0
                            positionMs = pos
                            if (len > 0) lengthMs = len
                        } catch (_: Exception) {}
                        kotlinx.coroutines.delay(250)
                    }
                }

                HomeScreen(
                    files = files.value,
                    loading = loading.value,
                    onClick = { file -> startPlayback(file) },
                    volume = volume,
                    showSlider = showSlider,
                    onToggleSlider = { showSlider = !showSlider },
                    onVolumeChange = { volume = it },
                    positionMs = positionMs,
                    lengthMs = lengthMs,
                    onSeek = { newMs ->
                        dragging = false
                        currentSong?.seekToMs(newMs)
                        positionMs = newMs
                    },
                    onStartDrag = { dragging = true },
                    onDrag = { newMs -> positionMs = newMs }
                )
            }
        }
    }

    fun refreshFiles() {
        // Clear list immediately so UI reflects refresh action
        filesState?.value = emptyList()
        loadingState?.value = true
        Thread {
            val newList = try { getMediaFiles() } catch (_: Exception) { emptyList() }
            activity?.runOnUiThread {
                filesState?.value = newList
                loadingState?.value = false
                Toast.makeText(requireContext(), "Loaded ${newList.size} file(s)", Toast.LENGTH_SHORT).show()
            }
        }.start()
    }

    private fun startPlayback(file: File) {
        val activity = activity as? MainActivity
        val path = file.absolutePath
        if (activity?.playFile != null) {
            activity.playFile?.invoke(path)
            return
        }
        try {
            val song = Mixer.createSong()
            if (song != null) {
                currentSong = song
                val bytes = file.readBytes()
                val status = song.loadFromMemory(bytes)
                if (status == 0) {
                    applyVolume()
                    val r = song.start()
                    if (r == 0) {
                        Toast.makeText(requireContext(), "Playing ${file.name}", Toast.LENGTH_SHORT).show()
                    } else {
                        Toast.makeText(requireContext(), "Failed to start song (err=$r)", Toast.LENGTH_SHORT).show()
                    }
                } else {
                    Toast.makeText(requireContext(), "Failed to load song (err=$status)", Toast.LENGTH_SHORT).show()
                }
            } else {
                Toast.makeText(requireContext(), "Audio mixer not initialized", Toast.LENGTH_SHORT).show()
            }
        } catch (ex: Exception) {
            Toast.makeText(requireContext(), "Playback error: ${ex.localizedMessage}", Toast.LENGTH_SHORT).show()
        }
    }

    private fun applyVolume() {
        Mixer.setMasterVolumePercent(currentVolumePercent)
        currentSong?.setVolumePercent(currentVolumePercent)
    }

    private fun getMediaFiles(): List<File> {
        val musicDir: File = if (pickedFolderUri != null) {
            val docTree = DocumentFile.fromTreeUri(requireContext(), pickedFolderUri!!)
            if (docTree != null) {
                val targetDir = File(requireContext().cacheDir, "pickedFolder")
                if (!targetDir.exists()) targetDir.mkdirs()
                // Clear previous cached copies so deletions are reflected
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
            } else {
                File("/sdcard/Music")
            }
        } else {
            File("/sdcard/Music")
        }
        val validExtensions = setOf("mid", "midi", "kar", "rmf")
        val map = LinkedHashMap<String, File>()
        val dir = musicDir
        if (dir.exists() && dir.isDirectory) {
            dir.listFiles { file -> file.isFile && file.extension.lowercase() in validExtensions }?.forEach { f ->
                map[f.absolutePath] = f
            }
        }
        // Return sorted by name (case-insensitive)
        return map.values.sortedBy { it.name.lowercase() }
    }
}

@Composable
fun HomeScreen(
    files: List<File>,
    loading: Boolean,
    onClick: (File) -> Unit,
    volume: Float,
    showSlider: Boolean,
    onToggleSlider: () -> Unit,
    onVolumeChange: (Float) -> Unit,
    positionMs: Int,
    lengthMs: Int,
    onSeek: (Int) -> Unit,
    onStartDrag: () -> Unit,
    onDrag: (Int) -> Unit
) {
    Column(modifier = Modifier.fillMaxSize()) {
        Box(modifier = Modifier.weight(1f)) {
            when {
                loading -> {
                    Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                        CircularProgressIndicator()
                    }
                }
                files.isEmpty() -> {
                    Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                        Text("No media files found")
                    }
                }
                else -> {
                    LazyColumn(modifier = Modifier.fillMaxSize()) {
                        items(files) { file ->
                            Text(
                                text = file.name,
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .clickable { onClick(file) }
                                    .padding(horizontal = 16.dp, vertical = 12.dp)
                            )
                            Divider()
                        }
                    }
                }
            }
        }
        Surface(elevation = 4.dp) {
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = 12.dp, vertical = 8.dp),
                horizontalArrangement = Arrangement.SpaceBetween
            ) {
                Text("Playback", style = MaterialTheme.typography.subtitle1, modifier = Modifier.alignByBaseline())
                IconButton(onClick = onToggleSlider) {
                    val vol = volume.toInt()
                    val icon = when {
                        vol == 0 -> Icons.Filled.VolumeOff
                        vol < 50 -> Icons.Filled.VolumeDown
                        else -> Icons.Filled.VolumeUp
                    }
                    Crossfade(targetState = icon, label = "volIcon") { ic ->
                        Icon(ic, contentDescription = "Volume", tint = MaterialTheme.colors.primary)
                    }
                }
                AnimatedVisibility(
                    visible = showSlider,
                    enter = fadeIn() + expandHorizontally(),
                    exit = fadeOut() + shrinkHorizontally()
                ) {
                    Slider(
                        value = volume,
                        onValueChange = { onVolumeChange(it) },
                        valueRange = 0f..100f,
                        modifier = Modifier
                            .width(180.dp)
                            .padding(start = 4.dp),
                        colors = SliderDefaults.colors(
                            thumbColor = MaterialTheme.colors.primary,
                            activeTrackColor = MaterialTheme.colors.primary,
                            inactiveTrackColor = MaterialTheme.colors.onSurface.copy(alpha = 0.24f)
                        )
                    )
                }
            }
            // Seek bar with time display if we have a length
            if (lengthMs > 0) {
                val posClamped = positionMs.coerceIn(0, lengthMs)
                val progress = posClamped / lengthMs.toFloat()
                Column(modifier = Modifier.fillMaxWidth().padding(horizontal = 12.dp)) {
                    Slider(
                        value = progress,
                        onValueChange = { frac ->
                            val newMs = (frac * lengthMs).toInt()
                            onDrag(newMs)
                        },
                        onValueChangeFinished = {
                            onSeek(positionMs)
                        },
                        modifier = Modifier.fillMaxWidth(),
                        colors = SliderDefaults.colors(
                            thumbColor = MaterialTheme.colors.secondary,
                            activeTrackColor = MaterialTheme.colors.secondary,
                            inactiveTrackColor = MaterialTheme.colors.onSurface.copy(alpha = 0.24f)
                        )
                    )
                    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                        Text(formatTime(positionMs), style = MaterialTheme.typography.caption)
                        Text(formatTime(lengthMs), style = MaterialTheme.typography.caption)
                    }
                }
            } else {
                if (positionMs > 0) { // show position even if length unknown
                    Row(Modifier.fillMaxWidth().padding(horizontal = 12.dp), horizontalArrangement = Arrangement.Start) {
                        Text(formatTime(positionMs), style = MaterialTheme.typography.caption)
                    }
                }
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
