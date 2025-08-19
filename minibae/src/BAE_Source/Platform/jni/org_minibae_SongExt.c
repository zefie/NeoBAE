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
    if(songRef == 0){ return (jint)BAE_BAD_REFERENCE; }
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
