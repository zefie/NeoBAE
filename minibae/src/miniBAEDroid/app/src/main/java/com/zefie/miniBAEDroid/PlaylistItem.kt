package com.zefie.miniBAEDroid

import java.io.File

data class PlaylistItem(
    val file: File,
    var title: String = if (file.isDirectory) file.name else file.nameWithoutExtension,
    val path: String = file.absolutePath,
    val id: Long = path.hashCode().toLong(),
    val durationMs: Int = 0, // Will be populated when available
    var isFolder: Boolean = file.isDirectory
)
