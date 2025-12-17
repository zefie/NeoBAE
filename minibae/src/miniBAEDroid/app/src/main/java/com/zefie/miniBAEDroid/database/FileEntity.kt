package com.zefie.miniBAEDroid.database

data class FileEntity(
    val path: String,
    val filename: String,
    val extension: String,
    val parent_path: String,
    val size: Long,
    val last_modified: Long
)
