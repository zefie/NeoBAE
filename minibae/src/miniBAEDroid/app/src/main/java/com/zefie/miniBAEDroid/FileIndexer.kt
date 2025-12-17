package com.zefie.miniBAEDroid

import android.content.Context
import com.zefie.miniBAEDroid.database.SQLiteHelper
import com.zefie.miniBAEDroid.database.FileEntity
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import java.io.File

data class IndexingProgress(
    val isIndexing: Boolean = false,
    val currentPath: String = "",
    val filesIndexed: Int = 0,
    val foldersScanned: Int = 0,
    val totalSize: Long = 0
)

class FileIndexer(private val context: Context) {
    private val database = SQLiteHelper.getInstance(context)
    private val validExtensions = setOf("mid", "midi", "kar", "rmf", "rmi")
    
    private val _progress = MutableStateFlow(IndexingProgress())
    val progress: StateFlow<IndexingProgress> = _progress
    
    private var indexingJob: Job? = null
    private var currentIndexPath: String = "" // Track which path we're indexing
    
    /**
     * Build/rebuild the entire file index for a given directory
     * This indexes the directory and all its subdirectories
     */
    suspend fun rebuildIndex(rootPath: String) = withContext(Dispatchers.IO) {
        if (_progress.value.isIndexing) {
            return@withContext // Already indexing
        }
        
        indexingJob = coroutineContext[Job]
        currentIndexPath = rootPath // Store the root we're indexing
        
        try {
            _progress.value = IndexingProgress(isIndexing = true)
            
            // Clear existing index for this path
            database.clearAll(rootPath)
            
            // Start indexing from specified root
            val root = File(rootPath)
            if (root.exists() && root.isDirectory) {
                android.util.Log.i("FileIndexer", "Indexing directory: ${root.absolutePath}")
                indexDirectory(root)
            }
            
            _progress.value = _progress.value.copy(isIndexing = false)
        } catch (e: CancellationException) {
            _progress.value = IndexingProgress(isIndexing = false)
            throw e
        } catch (e: Exception) {
            _progress.value = IndexingProgress(isIndexing = false)
            throw e
        } finally {
            indexingJob = null
        }
    }
    
    /**
     * Incremental update - only index changed files
     */
    suspend fun incrementalUpdate(rootPath: String) = withContext(Dispatchers.IO) {
        if (_progress.value.isIndexing) {
            return@withContext
        }
        
        try {
            _progress.value = IndexingProgress(isIndexing = true)
            
            val root = File(rootPath)
            if (root.exists() && root.isDirectory) {
                indexDirectoryIncremental(root)
            }
            
            _progress.value = _progress.value.copy(isIndexing = false)
        } catch (e: Exception) {
            _progress.value = IndexingProgress(isIndexing = false)
            throw e
        }
    }
    
    /**
     * Iterative directory traversal using queue (avoids recursion stack issues)
     */
    private suspend fun indexDirectory(root: File) {
        val queue = ArrayDeque<File>()
        queue.add(root)
        
        val batch = mutableListOf<FileEntity>()
        val batchSize = 100 // Insert in batches for performance
        
        var filesIndexed = 0
        var foldersScanned = 0
        var totalSize = 0L
        
        while (queue.isNotEmpty()) {
            val current = queue.removeFirst()
            
            try {
                _progress.value = _progress.value.copy(
                    currentPath = current.absolutePath,
                    filesIndexed = filesIndexed,
                    foldersScanned = foldersScanned,
                    totalSize = totalSize
                )
                
                val files = current.listFiles() ?: continue
                foldersScanned++
                
                for (file in files) {
                    when {
                        file.isDirectory && file.canRead() -> {
                            // Skip hidden and system directories for performance
                            if (!file.name.startsWith(".") && 
                                file.name != "Android" && 
                                file.name != "DCIM" && 
                                file.name != "Pictures") {
                                queue.add(file)
                            }
                        }
                        file.isFile && file.extension.lowercase() in validExtensions -> {
                            val entity = FileEntity(
                                path = file.absolutePath,
                                filename = file.nameWithoutExtension,
                                extension = file.extension.lowercase(),
                                parent_path = file.parent ?: "",
                                size = file.length(),
                                last_modified = file.lastModified()
                            )
                            batch.add(entity)
                            filesIndexed++
                            totalSize += file.length()
                            
                            // Insert batch when full
                            if (batch.size >= batchSize) {
                                database.insertFiles(currentIndexPath, batch.toList())
                                batch.clear()
                            }
                        }
                    }
                }
                
                // Yield to prevent blocking
                if (filesIndexed % 500 == 0) {
                    yield()
                }
            } catch (e: SecurityException) {
                // Skip inaccessible directories
                continue
            }
        }
        
        // Insert remaining batch
        if (batch.isNotEmpty()) {
            database.insertFiles(currentIndexPath, batch)
        }
        
        _progress.value = _progress.value.copy(
            filesIndexed = filesIndexed,
            foldersScanned = foldersScanned,
            totalSize = totalSize
        )
    }
    
    /**
     * Incremental indexing - only update changed files
     */
    private suspend fun indexDirectoryIncremental(root: File) {
        val queue = ArrayDeque<File>()
        queue.add(root)
        
        val batch = mutableListOf<FileEntity>()
        val batchSize = 100
        
        var filesIndexed = 0
        var foldersScanned = 0
        
        while (queue.isNotEmpty()) {
            val current = queue.removeFirst()
            
            try {
                _progress.value = _progress.value.copy(
                    currentPath = current.absolutePath,
                    filesIndexed = filesIndexed,
                    foldersScanned = foldersScanned
                )
                
                val files = current.listFiles() ?: continue
                foldersScanned++
                
                for (file in files) {
                    when {
                        file.isDirectory && file.canRead() -> {
                            if (!file.name.startsWith(".") && 
                                file.name != "Android" && 
                                file.name != "DCIM" && 
                                file.name != "Pictures") {
                                queue.add(file)
                            }
                        }
                        file.isFile && file.extension.lowercase() in validExtensions -> {
                            // Check if file needs updating
                            val lastModified = database.getLastModified(file.absolutePath)
                            if (lastModified == null || file.lastModified() > lastModified) {
                                val entity = FileEntity(
                                    path = file.absolutePath,
                                    filename = file.nameWithoutExtension,
                                    extension = file.extension.lowercase(),
                                    parent_path = file.parent ?: "",
                                    size = file.length(),
                                    last_modified = file.lastModified()
                                )
                                batch.add(entity)
                                filesIndexed++
                                
                                if (batch.size >= batchSize) {
                                    database.insertFiles(currentIndexPath, batch.toList())
                                    batch.clear()
                                }
                            }
                        }
                    }
                }
                
                if (filesIndexed % 500 == 0) {
                    yield()
                }
            } catch (e: SecurityException) {
                continue
            }
        }
        
        if (batch.isNotEmpty()) {
            database.insertFiles(currentIndexPath, batch)
        }
        
        _progress.value = _progress.value.copy(
            filesIndexed = filesIndexed,
            foldersScanned = foldersScanned
        )
    }
    
    fun cancelIndexing() {
        indexingJob?.cancel()
        _progress.value = IndexingProgress(isIndexing = false)
    }
    
    suspend fun getIndexedFileCount(): Int {
        return withContext(Dispatchers.IO) {
            // Always return total count across all indexes to be consistent
            database.getTotalFileCount()
        }
    }
}
