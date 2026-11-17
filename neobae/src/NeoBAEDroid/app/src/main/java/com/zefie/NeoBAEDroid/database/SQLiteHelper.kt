package com.zefie.NeoBAEDroid.database

import android.content.Context
import android.util.Log
import java.io.File
import java.security.MessageDigest

class SQLiteHelper private constructor(private val context: Context) {
    private val dbDir: File
    private val openDatabases = mutableMapOf<String, Long>() // normalized path -> dbPtr
    private val parentIndexCache = object : LinkedHashMap<String, String?>(256, 0.75f, true) {
        override fun removeEldestEntry(eldest: MutableMap.MutableEntry<String, String?>): Boolean {
            return size > 256
        }
    }
    private var indexedRootsCache: List<String> = emptyList()
    private var indexedRootsCacheStampMs: Long = 0
    private var indexedRootsCacheDbCount: Int = -1

    init {
        // Ensure database directory exists
        dbDir = File(context.filesDir, "indexes")
        dbDir.mkdirs()
    }

    private fun normalizePath(path: String?): String? {
        if (path == null) return null

        val raw = path.trim().trimEnd('/')
        if (raw.startsWith("content://")) return raw
        // Backward compatibility for earlier malformed normalization like "/content:/..."
        if (raw.startsWith("/content:/")) return "content://" + raw.removePrefix("/content:/")

        return try {
            File(raw).canonicalPath
        } catch (_: Exception) {
            File(raw).absolutePath
        }
    }

    private fun invalidateIndexCaches() {
        parentIndexCache.clear()
        indexedRootsCache = emptyList()
        indexedRootsCacheStampMs = 0
        indexedRootsCacheDbCount = -1
    }

    private fun getIndexedRoots(forceRefresh: Boolean = false): List<String> {
        val now = System.currentTimeMillis()
        val dbFiles = dbDir.listFiles { _, name -> name.startsWith("index_") && name.endsWith(".db") }
        val dbCount = dbFiles?.size ?: 0

        val cacheFresh = !forceRefresh &&
            indexedRootsCache.isNotEmpty() &&
            indexedRootsCacheDbCount == dbCount &&
            (now - indexedRootsCacheStampMs) < 5000

        if (cacheFresh) {
            return indexedRootsCache
        }

        if (dbFiles == null || dbFiles.isEmpty()) {
            indexedRootsCache = emptyList()
            indexedRootsCacheDbCount = 0
            indexedRootsCacheStampMs = now
            return indexedRootsCache
        }

        val roots = mutableListOf<String>()
        dbFiles.forEach { dbFile ->
            val dbPtr = nativeOpen(dbFile.absolutePath)
            if (dbPtr != 0L) {
                val indexPath = getIndexMetadata(dbPtr)
                nativeClose(dbPtr)
                if (!indexPath.isNullOrBlank()) {
                    val normalized = normalizePath(indexPath) ?: indexPath
                    roots.add(normalized)
                }
            }
        }

        indexedRootsCache = roots.distinct()
        indexedRootsCacheDbCount = dbCount
        indexedRootsCacheStampMs = now
        return indexedRootsCache
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
        val normalizedIndexPath = normalizePath(indexPath) ?: indexPath
        // Check if already open
        openDatabases[normalizedIndexPath]?.let { return it }
        
        // Create database file based on path hash
        val dbName = "index_${pathToHash(normalizedIndexPath)}.db"
        val dbFile = File(dbDir, dbName)
        
        // Libraries already loaded in MainActivity
        val dbPtr = nativeOpen(dbFile.absolutePath)
        if (dbPtr != 0L) {
            createTables(dbPtr)
            openDatabases[normalizedIndexPath] = dbPtr
            
            // Store the indexed path in metadata
            saveIndexMetadata(dbPtr, normalizedIndexPath)
            invalidateIndexCaches()
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
        val normalizedSearchPath = normalizePath(searchPath) ?: searchPath
        parentIndexCache[normalizedSearchPath]?.let { return it }

        val roots = getIndexedRoots()
        if (roots.isEmpty()) {
            parentIndexCache[normalizedSearchPath] = null
            return null
        }
        
        var closestParent: String? = null
        var closestParentLength = 0

        roots.forEach { normalizedIndexPath ->
            if (normalizedSearchPath.startsWith(normalizedIndexPath) && normalizedIndexPath.length > closestParentLength) {
                closestParent = normalizedIndexPath
                closestParentLength = normalizedIndexPath.length
            }
        }

        parentIndexCache[normalizedSearchPath] = closestParent
        return closestParent
    }
    
    /**
     * Save metadata about which path this database indexes
     */
    private fun saveIndexMetadata(dbPtr: Long, indexPath: String) {
        val normalizedIndexPath = normalizePath(indexPath) ?: indexPath
        nativeExecute(dbPtr, "CREATE TABLE IF NOT EXISTS index_metadata (key TEXT PRIMARY KEY, value TEXT)")
        nativeExecute(dbPtr, "INSERT OR REPLACE INTO index_metadata (key, value) VALUES ('indexed_path', '${sqlEscape(normalizedIndexPath)}')")
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
    
    private fun sqlEscape(value: String): String = value.replace("'", "''")

    /**
     * Build a WHERE fragment matching rows whose parent_path is under [parentPath].
     * Filesystem paths have several aliases on Android (/sdcard, /storage/emulated/0,
     * the canonical FUSE path). Index roots are canonicalized but rows keep whatever form
     * was used when indexing, so match every known alias of the folder.
     */
    private fun parentPathClause(parentPath: String): String {
        val raw = parentPath.trim().trimEnd('/')
        if (raw.startsWith("content://")) {
            // SAF document IDs encode subfolders as %2F, so keep a plain prefix match.
            return "(parent_path LIKE '${sqlEscape(raw)}%')"
        }
        val candidates = linkedSetOf(raw)
        run {
            val canonical = normalizePath(raw)
            if (canonical != null) candidates.add(canonical)
            val aliasRoots = listOf("/sdcard", "/storage/emulated/0", "/storage/self/primary")
            val canonicalAliasRoots = aliasRoots.mapNotNull { normalizePath(it) }.toSet()
            for (form in candidates.toList()) {
                val matchedRoot = (aliasRoots + canonicalAliasRoots).firstOrNull { root ->
                    form == root || form.startsWith("$root/")
                } ?: continue
                val rest = form.removePrefix(matchedRoot)
                aliasRoots.forEach { candidates.add(it + rest) }
                canonicalAliasRoots.forEach { candidates.add(it + rest) }
            }
        }
        return candidates.joinToString(" OR ", prefix = "(", postfix = ")") { candidate ->
            val esc = sqlEscape(candidate)
            "parent_path = '$esc' OR parent_path LIKE '$esc/%'"
        }
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
        val sql = "SELECT path, filename, extension, parent_path, size, last_modified FROM indexed_files WHERE filename LIKE '%${sqlEscape(query)}%' ORDER BY filename COLLATE NOCASE$limitClause"
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
        val sql = "SELECT path, filename, extension, parent_path, size, last_modified FROM indexed_files WHERE ${parentPathClause(parentPath)} AND filename LIKE '%${sqlEscape(query)}%' ORDER BY filename COLLATE NOCASE$limitClause"
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
        val sql = "SELECT path, filename, extension, parent_path, size, last_modified FROM indexed_files WHERE ${parentPathClause(parentPath)} ORDER BY filename COLLATE NOCASE$limitClause"
        val results = executeQuery(dbPtr, sql)
        Log.d("SQLiteHelper", "getAllFilesInPath: found ${results.size} results")
        return results
    }
    
    fun getLastModified(path: String): Long? {
        val indexPath = findParentIndex(path) ?: return null
        val dbPtr = getOrCreateDatabase(indexPath)
        if (dbPtr == 0L) return null
        
        val sql = "SELECT last_modified FROM indexed_files WHERE path = '${sqlEscape(path)}'"
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
     * Get the closest indexed root that covers the given path.
     * Returns null if no index covers the path.
     */
    fun getIndexRootForPath(searchPath: String): String? {
        return findParentIndex(searchPath)
    }
    
    /**
     * Check if a path exactly matches an existing database (not just a parent)
     */
    fun hasExactIndexForPath(searchPath: String): Boolean {
        val normalized = normalizePath(searchPath) ?: searchPath
        return getIndexedRoots().any { it == normalized }
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
        val deleted = if (dbFile.exists()) {
            val deleted = dbFile.delete()
            Log.d("SQLiteHelper", "deleteDatabase: deleted=$deleted")
            deleted
        } else {
            Log.d("SQLiteHelper", "deleteDatabase: File does not exist")
            false
        }

        if (deleted) {
            invalidateIndexCaches()
        }
        return deleted
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
