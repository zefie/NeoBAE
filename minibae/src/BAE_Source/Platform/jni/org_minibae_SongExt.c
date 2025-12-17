// Added for playback position preservation across bank changes
#include "MiniBAE.h"
#include <jni.h>
#include <android/log.h>

#define LOG_TAG "miniBAE"

JNIEXPORT jint JNICALL Java_org_minibae_Song__1getSongPositionUS(JNIEnv* env, jclass clazz, jlong songRef){
    (void)env; (void)clazz;
    if(songRef == 0){ return 0; }
    BAESong song = (BAESong)(intptr_t)songRef;
    uint32_t us = 0;
    BAEResult r = BAESong_GetMicrosecondPosition(song, &us);
    if(r != BAE_NO_ERROR){ __android_log_print(ANDROID_LOG_WARN, LOG_TAG, "BAESong_GetMicrosecondPosition err=%d", r); return 0; }
    return (jint)us;
}

JNIEXPORT jint JNICALL Java_org_minibae_Song__1setSongPositionUS(JNIEnv* env, jclass clazz, jlong songRef, jint us){
    (void)env; (void)clazz;
    if(songRef == 0){ return (jint)BAE_NULL_OBJECT; }
    BAESong song = (BAESong)(intptr_t)songRef;
    BAEResult r = BAESong_SetMicrosecondPosition(song, (uint32_t)us);
    if(r != BAE_NO_ERROR){ __android_log_print(ANDROID_LOG_WARN, LOG_TAG, "BAESong_SetMicrosecondPosition err=%d", r); }
    return (jint)r;
}

// Retrieve total song length (microseconds). Returns 0 if unavailable or error.
JNIEXPORT jint JNICALL Java_org_minibae_Song__1getSongLengthUS(JNIEnv* env, jclass clazz, jlong songRef){
    (void)env; (void)clazz;
    if(songRef == 0){ return 0; }
    BAESong song = (BAESong)(intptr_t)songRef;
    uint32_t us = 0;
    BAEResult r = BAESong_GetMicrosecondLength(song, &us);
    if(r != BAE_NO_ERROR){ __android_log_print(ANDROID_LOG_WARN, LOG_TAG, "BAESong_GetMicrosecondLength err=%d", r); return 0; }
    return (jint)us;
}

// Pause song playback
JNIEXPORT jint JNICALL Java_org_minibae_Song__1pauseSong(JNIEnv* env, jclass clazz, jlong songRef){
    (void)env; (void)clazz;
    if(songRef == 0){ return (jint)BAE_NULL_OBJECT; }
    BAESong song = (BAESong)(intptr_t)songRef;
    BAEResult r = BAESong_Pause(song);
    if(r != BAE_NO_ERROR){ __android_log_print(ANDROID_LOG_WARN, LOG_TAG, "BAESong_Pause err=%d", r); }
    return (jint)r;
}

// Resume song playback
JNIEXPORT jint JNICALL Java_org_minibae_Song__1resumeSong(JNIEnv* env, jclass clazz, jlong songRef){
    (void)env; (void)clazz;
    if(songRef == 0){ return (jint)BAE_NULL_OBJECT; }
    BAESong song = (BAESong)(intptr_t)songRef;
    BAEResult r = BAESong_Resume(song);
    if(r != BAE_NO_ERROR){ __android_log_print(ANDROID_LOG_WARN, LOG_TAG, "BAESong_Resume err=%d", r); }
    return (jint)r;
}

// Check if song is paused
JNIEXPORT jboolean JNICALL Java_org_minibae_Song__1isSongPaused(JNIEnv* env, jclass clazz, jlong songRef){
    (void)env; (void)clazz;
    if(songRef == 0){ return JNI_FALSE; }
    BAESong song = (BAESong)(intptr_t)songRef;
    BAE_BOOL isPaused = FALSE;
    BAEResult r = BAESong_IsPaused(song, &isPaused);
    if(r != BAE_NO_ERROR){ __android_log_print(ANDROID_LOG_WARN, LOG_TAG, "BAESong_IsPaused err=%d", r); return JNI_FALSE; }
    return (isPaused == TRUE) ? JNI_TRUE : JNI_FALSE;
}

// Check if song is done
JNIEXPORT jboolean JNICALL Java_org_minibae_Song__1isSongDone(JNIEnv* env, jclass clazz, jlong songRef){
    (void)env; (void)clazz;
    if(songRef == 0){ return JNI_TRUE; }
    BAESong song = (BAESong)(intptr_t)songRef;
    BAE_BOOL isDone = FALSE;
    BAEResult r = BAESong_IsDone(song, &isDone);
    if(r != BAE_NO_ERROR){ __android_log_print(ANDROID_LOG_WARN, LOG_TAG, "BAESong_IsDone err=%d", r); return JNI_TRUE; }
    return (isDone == TRUE) ? JNI_TRUE : JNI_FALSE;
}

// Set song loop count
JNIEXPORT jint JNICALL Java_org_minibae_Song__1setSongLoops(JNIEnv* env, jclass clazz, jlong songRef, jint numLoops){
    (void)env; (void)clazz;
    if(songRef == 0){ return (jint)BAE_NULL_OBJECT; }
    BAESong song = (BAESong)(intptr_t)songRef;
    BAEResult r = BAESong_SetLoops(song, (int16_t)numLoops);
    if(r != BAE_NO_ERROR){ __android_log_print(ANDROID_LOG_WARN, LOG_TAG, "BAESong_SetLoops err=%d", r); }
    return (jint)r;
}
