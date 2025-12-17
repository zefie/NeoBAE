#include <jni.h>
#include <android/log.h>
#include <sqlite3.h>
#include <string.h>
#include <stdlib.h>

#define LOG_TAG "miniBAE_SQLite"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// Helper to convert Java string to C string
static char* jstring_to_cstring(JNIEnv* env, jstring jstr) {
    if (jstr == NULL) return NULL;
    const char* str = (*env)->GetStringUTFChars(env, jstr, NULL);
    char* result = strdup(str);
    (*env)->ReleaseStringUTFChars(env, jstr, str);
    return result;
}

// Open database
JNIEXPORT jlong JNICALL
Java_com_zefie_miniBAEDroid_database_SQLiteHelper_nativeOpen(JNIEnv* env, jobject obj, jstring path) {
    char* db_path = jstring_to_cstring(env, path);
    sqlite3* db = NULL;
    
    int rc = sqlite3_open(db_path, &db);
    free(db_path);
    
    if (rc != SQLITE_OK) {
        LOGE("Failed to open database: %s", sqlite3_errmsg(db));
        if (db) sqlite3_close(db);
        return 0;
    }
    
    // Enable foreign keys
    sqlite3_exec(db, "PRAGMA foreign_keys = ON", NULL, NULL, NULL);
    
    return (jlong)(uintptr_t)db;
}

// Close database
JNIEXPORT void JNICALL
Java_com_zefie_miniBAEDroid_database_SQLiteHelper_nativeClose(JNIEnv* env, jobject obj, jlong db_ptr) {
    if (db_ptr == 0) return;
    sqlite3* db = (sqlite3*)(uintptr_t)db_ptr;
    sqlite3_close(db);
}

// Execute SQL (CREATE, INSERT, UPDATE, DELETE)
JNIEXPORT jboolean JNICALL
Java_com_zefie_miniBAEDroid_database_SQLiteHelper_nativeExecute(JNIEnv* env, jobject obj, jlong db_ptr, jstring sql) {
    if (db_ptr == 0) return JNI_FALSE;
    sqlite3* db = (sqlite3*)(uintptr_t)db_ptr;
    
    char* sql_str = jstring_to_cstring(env, sql);
    char* err_msg = NULL;
    
    int rc = sqlite3_exec(db, sql_str, NULL, NULL, &err_msg);
    free(sql_str);
    
    if (rc != SQLITE_OK) {
        LOGE("SQL execution error: %s", err_msg);
        sqlite3_free(err_msg);
        return JNI_FALSE;
    }
    
    return JNI_TRUE;
}

// Execute batch insert (transaction for performance)
JNIEXPORT jboolean JNICALL
Java_com_zefie_miniBAEDroid_database_SQLiteHelper_nativeBatchInsert(
    JNIEnv* env, jobject obj, jlong db_ptr, jobjectArray paths, jobjectArray filenames,
    jobjectArray extensions, jobjectArray parent_paths, jlongArray sizes, jlongArray modified_times) {
    
    if (db_ptr == 0) return JNI_FALSE;
    sqlite3* db = (sqlite3*)(uintptr_t)db_ptr;
    
    jsize count = (*env)->GetArrayLength(env, paths);
    if (count == 0) return JNI_TRUE;
    
    // Begin transaction
    sqlite3_exec(db, "BEGIN TRANSACTION", NULL, NULL, NULL);
    
    // Prepare statement
    sqlite3_stmt* stmt = NULL;
    const char* insert_sql = "INSERT OR REPLACE INTO indexed_files (path, filename, extension, parent_path, size, last_modified) VALUES (?, ?, ?, ?, ?, ?)";
    int rc = sqlite3_prepare_v2(db, insert_sql, -1, &stmt, NULL);
    
    if (rc != SQLITE_OK) {
        LOGE("Failed to prepare statement: %s", sqlite3_errmsg(db));
        sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
        return JNI_FALSE;
    }
    
    jlong* size_arr = (*env)->GetLongArrayElements(env, sizes, NULL);
    jlong* modified_arr = (*env)->GetLongArrayElements(env, modified_times, NULL);
    
    // Insert each row
    for (int i = 0; i < count; i++) {
        jstring jpath = (jstring)(*env)->GetObjectArrayElement(env, paths, i);
        jstring jfilename = (jstring)(*env)->GetObjectArrayElement(env, filenames, i);
        jstring jext = (jstring)(*env)->GetObjectArrayElement(env, extensions, i);
        jstring jparent = (jstring)(*env)->GetObjectArrayElement(env, parent_paths, i);
        
        char* path = jstring_to_cstring(env, jpath);
        char* filename = jstring_to_cstring(env, jfilename);
        char* ext = jstring_to_cstring(env, jext);
        char* parent = jstring_to_cstring(env, jparent);
        
        sqlite3_bind_text(stmt, 1, path, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, filename, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, ext, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 4, parent, -1, SQLITE_STATIC);
        sqlite3_bind_int64(stmt, 5, size_arr[i]);
        sqlite3_bind_int64(stmt, 6, modified_arr[i]);
        
        rc = sqlite3_step(stmt);
        if (rc != SQLITE_DONE) {
            LOGE("Insert failed at row %d: %s", i, sqlite3_errmsg(db));
        }
        
        sqlite3_reset(stmt);
        
        free(path);
        free(filename);
        free(ext);
        free(parent);
        
        (*env)->DeleteLocalRef(env, jpath);
        (*env)->DeleteLocalRef(env, jfilename);
        (*env)->DeleteLocalRef(env, jext);
        (*env)->DeleteLocalRef(env, jparent);
    }
    
    (*env)->ReleaseLongArrayElements(env, sizes, size_arr, 0);
    (*env)->ReleaseLongArrayElements(env, modified_times, modified_arr, 0);
    
    sqlite3_finalize(stmt);
    
    // Commit transaction
    rc = sqlite3_exec(db, "COMMIT", NULL, NULL, NULL);
    
    return (rc == SQLITE_OK) ? JNI_TRUE : JNI_FALSE;
}

// Query for search results - returns Java String array
JNIEXPORT jobjectArray JNICALL
Java_com_zefie_miniBAEDroid_database_SQLiteHelper_nativeQuery(
    JNIEnv* env, jobject obj, jlong db_ptr, jstring sql) {
    
    if (db_ptr == 0) return NULL;
    sqlite3* db = (sqlite3*)(uintptr_t)db_ptr;
    
    char* sql_str = jstring_to_cstring(env, sql);
    sqlite3_stmt* stmt = NULL;
    
    int rc = sqlite3_prepare_v2(db, sql_str, -1, &stmt, NULL);
    free(sql_str);
    
    if (rc != SQLITE_OK) {
        LOGE("Query preparation failed: %s", sqlite3_errmsg(db));
        return NULL;
    }
    
    // Count results first
    int count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        count++;
    }
    
    sqlite3_reset(stmt);
    
    // Create result array (path|filename|extension|parent_path|size|last_modified format)
    jclass stringClass = (*env)->FindClass(env, "java/lang/String");
    jobjectArray result = (*env)->NewObjectArray(env, count, stringClass, NULL);
    
    int index = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* path = (const char*)sqlite3_column_text(stmt, 0);
        const char* filename = (const char*)sqlite3_column_text(stmt, 1);
        const char* ext = (const char*)sqlite3_column_text(stmt, 2);
        const char* parent = (const char*)sqlite3_column_text(stmt, 3);
        long long size = sqlite3_column_int64(stmt, 4);
        long long modified = sqlite3_column_int64(stmt, 5);
        
        // Format: "path|filename|ext|parent|size|modified"
        char buffer[4096];
        snprintf(buffer, sizeof(buffer), "%s|%s|%s|%s|%lld|%lld",
                 path ? path : "",
                 filename ? filename : "",
                 ext ? ext : "",
                 parent ? parent : "",
                 size, modified);
        
        jstring str = (*env)->NewStringUTF(env, buffer);
        (*env)->SetObjectArrayElement(env, result, index++, str);
        (*env)->DeleteLocalRef(env, str);
    }
    
    sqlite3_finalize(stmt);
    
    return result;
}

// Get count
JNIEXPORT jint JNICALL
Java_com_zefie_miniBAEDroid_database_SQLiteHelper_nativeGetCount(
    JNIEnv* env, jobject obj, jlong db_ptr, jstring sql) {
    
    if (db_ptr == 0) return 0;
    sqlite3* db = (sqlite3*)(uintptr_t)db_ptr;
    
    char* sql_str = jstring_to_cstring(env, sql);
    sqlite3_stmt* stmt = NULL;
    
    int rc = sqlite3_prepare_v2(db, sql_str, -1, &stmt, NULL);
    free(sql_str);
    
    if (rc != SQLITE_OK) {
        return 0;
    }
    
    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }
    
    sqlite3_finalize(stmt);
    
    return count;
}
