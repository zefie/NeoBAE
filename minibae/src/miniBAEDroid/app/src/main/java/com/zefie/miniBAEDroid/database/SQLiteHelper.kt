package com.zefie.miniBAEDroid.database

import android.content.Context
import android.util.Log
import java.io.File
import java.security.MessageDigest

class SQLiteHelper private constructor(private val context: Context) {
    private val dbDir: File
    private val openDatabases = mutableMapOf<String, Long>() // path -> dbPtr
    
    init {
        // Ensure database directory exists
        dbDir = File(context.filesDir, "indexes")
        dbDir.mkdirs()
    }
    
    companion object {
        @Volatile
        private var instance: SQLiteHelper? = null
        
        fun getInstance(context: Context): SQLiteHelper {
            return instance ?: synchronized(this) {
                instance ?: SQLiteHelper(context.applicationContext).also { instance = it }
            }
        }
    }
    
    /**
     * Get or create a database for the specified directory path
     */
    private fun getOrCreateDatabase(indexPath: String): Long {
        // Check if already open
        openDatabases[indexPath]?.let { return it }
        
        // Create database file based on path hash
        val dbName = "index_${pathToHash(indexPath)}.db"
        val dbFile = File(dbDir, dbName)
        
        // Libraries already loaded in MainActivity
        val dbPtr = nativeOpen(dbFile.absolutePath)
        if (dbPtr != 0L) {
            createTables(dbPtr)
            openDatabases[indexPath] = dbPtr
            
            // Store the indexed path in metadata
            saveIndexMetadata(dbPtr, indexPath)
        } else {
            Log.e("SQLiteHelper", "Failed to open database at: ${dbFile.absolutePath}")
        }
        
        return dbPtr
    }
    
    /**
     * Convert path to hash for database filename
     */
    private fun pathToHash(path: String): String {
        val digest = MessageDigest.getInstance("MD5")
        val hash = digest.digest(path.toByteArray())
        return hash.joinToString("") { "%02x".format(it) }.take(16)
    }
    
    /**
     * Find the closest parent directory that has an index
     */
    private fun findParentIndex(searchPath: String): String? {
        // List all database files
        val dbFiles = dbDir.listFiles { _, name -> name.startsWith("index_") && name.endsWith(".db") }
        
        Log.d("SQLiteHelper", "findParentIndex: searching for '$searchPath', found ${dbFiles?.size ?: 0} db files in ${dbDir.absolutePath}")
        
        if (dbFiles == null || dbFiles.isEmpty()) return null
        
        var closestParent: String? = null
        var closestParentLength = 0
        
        dbFiles.forEach { dbFile ->
            val dbPtr = nativeOpen(dbFile.absolutePath)
            if (dbPtr != 0L) {
                val indexPath = getIndexMetadata(dbPtr)
                Log.d("SQLiteHelper", "  Checking db: ${dbFile.name}, indexPath=$indexPath")
                nativeClose(dbPtr)
                
                if (indexPath != null && searchPath.startsWith(indexPath) && indexPath.length > closestParentLength) {
                    closestParent = indexPath
                    closestParentLength = indexPath.length
                    Log.d("SQLiteHelper", "  -> New closest parent: $indexPath")
                }
            }
        }
        
        Log.d("SQLiteHelper", "findParentIndex: result=$closestParent")
        return closestParent
    }
    
    /**
     * Save metadata about which path this database indexes
     */
    private fun saveIndexMetadata(dbPtr: Long, indexPath: String) {
        nativeExecute(dbPtr, "CREATE TABLE IF NOT EXISTS index_metadata (key TEXT PRIMARY KEY, value TEXT)")
        nativeExecute(dbPtr, "INSERT OR REPLACE INTO index_metadata (key, value) VALUES ('indexed_path', '$indexPath')")
    }
    
    /**
     * Get the indexed path from database metadata
     */
    private fun getIndexMetadata(dbPtr: Long): String? {
        val result = nativeQuery(dbPtr, "SELECT value FROM index_metadata WHERE key = 'indexed_path'")
        return result?.firstOrNull()?.substringBefore("|")
    }
    
    private fun createTables(dbPtr: Long) {
        val createTableSQL = """
            CREATE TABLE IF NOT EXISTS indexed_files (
                path TEXT PRIMARY KEY,
                filename TEXT NOT NULL,
                extension TEXT NOT NULL,
                parent_path TEXT NOT NULL,
                size INTEGER NOT NULL,
                last_modified INTEGER NOT NULL
            )
        """.trimIndent()
        
        val success = nativeExecute(dbPtr, createTableSQL)
        if (!success) {
            Log.e("SQLiteHelper", "Failed to create table")
            return
        }
        
        // Create indexes
        nativeExecute(dbPtr, "CREATE INDEX IF NOT EXISTS idx_filename ON indexed_files(filename)")
        nativeExecute(dbPtr, "CREATE INDEX IF NOT EXISTS idx_extension ON indexed_files(extension)")
        nativeExecute(dbPtr, "CREATE INDEX IF NOT EXISTS idx_parent_path ON indexed_files(parent_path)")
    }
    
    fun insertFiles(indexPath: String, entities: List<FileEntity>) {
        if (entities.isEmpty()) return
        val dbPtr = getOrCreateDatabase(indexPath)
        if (dbPtr == 0L) return
        
        val paths = entities.map { it.path }.toTypedArray()
        val filenames = entities.map { it.filename }.toTypedArray()
        val extensions = entities.map { it.extension }.toTypedArray()
        val parentPaths = entities.map { it.parent_path }.toTypedArray()
        val sizes = entities.map { it.size }.toLongArray()
        val modifiedTimes = entities.map { it.last_modified }.toLongArray()
        
        nativeBatchInsert(dbPtr, paths, filenames, extensions, parentPaths, sizes, modifiedTimes)
    }
    
    fun searchFiles(searchPath: String, query: String, limit: Int = 1000): List<FileEntity> {
        // Find the closest parent index
        val indexPath = findParentIndex(searchPath)
        Log.d("SQLiteHelper", "searchFiles: searchPath=$searchPath, indexPath=$indexPath, limit=$limit")
        if (indexPath == null) return emptyList()
        
        val dbPtr = getOrCreateDatabase(indexPath)
        if (dbPtr == 0L) return emptyList()
        
        val limitClause = if (limit > 0) " LIMIT $limit" else ""
        val sql = "SELECT path, filename, extension, parent_path, size, last_modified FROM indexed_files WHERE filename LIKE '%$query%' ORDER BY filename COLLATE NOCASE$limitClause"
        val results = executeQuery(dbPtr, sql)
        Log.d("SQLiteHelper", "searchFiles: found ${results.size} results")
        return results
    }
    
    fun searchFilesInPath(parentPath: String, query: String, limit: Int = 1000): List<FileEntity> {
        // Find the closest parent index
        val indexPath = findParentIndex(parentPath)
        Log.d("SQLiteHelper", "searchFilesInPath: parentPath=$parentPath, indexPath=$indexPath, limit=$limit")
        if (indexPath == null) return emptyList()
        
        val dbPtr = getOrCreateDatabase(indexPath)
        if (dbPtr == 0L) return emptyList()
        
        val limitClause = if (limit > 0) " LIMIT $limit" else ""
        val sql = "SELECT path, filename, extension, parent_path, size, last_modified FROM indexed_files WHERE parent_path LIKE '$parentPath%' AND filename LIKE '%$query%' ORDER BY filename COLLATE NOCASE$limitClause"
        val results = executeQuery(dbPtr, sql)
        Log.d("SQLiteHelper", "searchFilesInPath: query=$sql, found ${results.size} results")
        return results
    }
    
    fun getAllFiles(searchPath: String, limit: Int = 1000): List<FileEntity> {
        // Find the closest parent index
        val indexPath = findParentIndex(searchPath)
        Log.d("SQLiteHelper", "getAllFiles: searchPath=$searchPath, indexPath=$indexPath, limit=$limit")
        if (indexPath == null) return emptyList()
        
        val dbPtr = getOrCreateDatabase(indexPath)
        if (dbPtr == 0L) return emptyList()
        
        val limitClause = if (limit > 0) " LIMIT $limit" else ""
        val sql = "SELECT path, filename, extension, parent_path, size, last_modified FROM indexed_files ORDER BY filename COLLATE NOCASE$limitClause"
        val results = executeQuery(dbPtr, sql)
        Log.d("SQLiteHelper", "getAllFiles: found ${results.size} results")
        return results
    }
    
    fun getAllFilesInPath(parentPath: String, limit: Int = 1000): List<FileEntity> {
        // Find the closest parent index
        val indexPath = findParentIndex(parentPath)
        Log.d("SQLiteHelper", "getAllFilesInPath: parentPath=$parentPath, indexPath=$indexPath, limit=$limit")
        if (indexPath == null) return emptyList()
        
        val dbPtr = getOrCreateDatabase(indexPath)
        if (dbPtr == 0L) return emptyList()
        
        val limitClause = if (limit > 0) " LIMIT $limit" else ""
        val sql = "SELECT path, filename, extension, parent_path, size, last_modified FROM indexed_files WHERE parent_path LIKE '$parentPath%' ORDER BY filename COLLATE NOCASE$limitClause"
        val results = executeQuery(dbPtr, sql)
        Log.d("SQLiteHelper", "getAllFilesInPath: found ${results.size} results")
        return results
    }
    
    fun getLastModified(path: String): Long? {
        val indexPath = findParentIndex(path) ?: return null
        val dbPtr = getOrCreateDatabase(indexPath)
        if (dbPtr == 0L) return null
        
        val sql = "SELECT last_modified FROM indexed_files WHERE path = '$path'"
        val results = nativeQuery(dbPtr, sql)
        if (results.isNullOrEmpty()) return null
        
        val parts = results[0].split("|")
        return if (parts.size >= 6) parts[5].toLongOrNull() else null
    }
    
    fun getFileCount(indexPath: String): Int {
        val dbPtr = getOrCreateDatabase(indexPath)
        if (dbPtr == 0L) return 0
        val sql = "SELECT COUNT(*) FROM indexed_files"
        return nativeGetCount(dbPtr, sql)
    }
    
    /**
     * Get total count across all indexes
     */
    fun getTotalFileCount(): Int {
        var total = 0
        val dbFiles = dbDir.listFiles { _, name -> name.startsWith("index_") && name.endsWith(".db") }
        
        Log.d("SQLiteHelper", "getTotalFileCount: dbDir=${dbDir.absolutePath}, found ${dbFiles?.size ?: 0} files")
        
        dbFiles?.forEach { dbFile ->
            Log.d("SQLiteHelper", "  Checking db file: ${dbFile.name}")
            val tempDbPtr = nativeOpen(dbFile.absolutePath)
            if (tempDbPtr != 0L) {
                val count = nativeGetCount(tempDbPtr, "SELECT COUNT(*) FROM indexed_files")
                Log.d("SQLiteHelper", "    Count: $count")
                total += count
                nativeClose(tempDbPtr)
            } else {
                Log.e("SQLiteHelper", "    Failed to open!")
            }
        }
        
        Log.d("SQLiteHelper", "getTotalFileCount: returning $total")
        return total
    }
    
    /**
     * Check if a path is covered by any existing index
     */
    fun hasIndexForPath(searchPath: String): Boolean {
        val indexPath = findParentIndex(searchPath)
        Log.d("SQLiteHelper", "hasIndexForPath: searchPath=$searchPath, found=$indexPath")
        return indexPath != null
    }
    
    /**
     * Check if a path exactly matches an existing database (not just a parent)
     */
    fun hasExactIndexForPath(searchPath: String): Boolean {
        val dbFiles = dbDir.listFiles { _, name -> name.startsWith("index_") && name.endsWith(".db") }
        if (dbFiles == null || dbFiles.isEmpty()) return false
        
        dbFiles.forEach { dbFile ->
            val dbPtr = nativeOpen(dbFile.absolutePath)
            if (dbPtr != 0L) {
                val indexPath = getIndexMetadata(dbPtr)
                nativeClose(dbPtr)
                if (indexPath == searchPath) {
                    return true
                }
            }
        }
        return false
    }
    
    /**
     * Delete the database for a specific path
     */
    fun deleteDatabase(indexPath: String): Boolean {
        // Close the database if it's open
        openDatabases[indexPath]?.let { dbPtr ->
            nativeClose(dbPtr)
            openDatabases.remove(indexPath)
        }
        
        // Delete the database file
        val dbName = "index_${pathToHash(indexPath)}.db"
        val dbFile = File(dbDir, dbName)
        
        Log.d("SQLiteHelper", "deleteDatabase: Attempting to delete ${dbFile.absolutePath}")
        return if (dbFile.exists()) {
            val deleted = dbFile.delete()
            Log.d("SQLiteHelper", "deleteDatabase: deleted=$deleted")
            deleted
        } else {
            Log.d("SQLiteHelper", "deleteDatabase: File does not exist")
            false
        }
    }
    
    /**
     * Get file count for the database that covers the given path
     */
    fun getFileCountForPath(searchPath: String): Int {
        val indexPath = findParentIndex(searchPath)
        if (indexPath != null) {
            return getFileCount(indexPath)
        }
        return 0
    }
    
    fun clearAll(indexPath: String) {
        val dbPtr = getOrCreateDatabase(indexPath)
        if (dbPtr == 0L) return
        nativeExecute(dbPtr, "DELETE FROM indexed_files")
    }
    
    private fun executeQuery(dbPtr: Long, sql: String): List<FileEntity> {
        val results = nativeQuery(dbPtr, sql) ?: return emptyList()
        
        return results.mapNotNull { row ->
            val parts = row.split("|")
            if (parts.size >= 6) {
                FileEntity(
                    path = parts[0],
                    filename = parts[1],
                    extension = parts[2],
                    parent_path = parts[3],
                    size = parts[4].toLongOrNull() ?: 0L,
                    last_modified = parts[5].toLongOrNull() ?: 0L
                )
            } else {
                null
            }
        }
    }
    
    fun close() {
        openDatabases.values.forEach { dbPtr ->
            if (dbPtr != 0L) {
                nativeClose(dbPtr)
            }
        }
        openDatabases.clear()
    }
    
    // Native methods
    private external fun nativeOpen(path: String): Long
    private external fun nativeClose(dbPtr: Long)
    private external fun nativeExecute(dbPtr: Long, sql: String): Boolean
    private external fun nativeBatchInsert(
        dbPtr: Long,
        paths: Array<String>,
        filenames: Array<String>,
        extensions: Array<String>,
        parentPaths: Array<String>,
        sizes: LongArray,
        modifiedTimes: LongArray
    ): Boolean
    private external fun nativeQuery(dbPtr: Long, sql: String): Array<String>?
    private external fun nativeGetCount(dbPtr: Long, sql: String): Int
}
