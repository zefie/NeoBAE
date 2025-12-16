package com.zefie.miniBAEDroid

import java.io.File

data class PlaylistItem(
    val file: File,
    val title: String = file.nameWithoutExtension,
    val path: String = file.absolutePath,
    val id: Long = path.hashCode().toLong(),
    val durationMs: Int = 0 // Will be populated when available
)
