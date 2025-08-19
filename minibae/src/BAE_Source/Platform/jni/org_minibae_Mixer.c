#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

// printf
#include <android/log.h>

// for native asset manager
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>

#include "org_minibae_Mixer.h"
#include "MiniBAE.h"
#include "BAEPatches.h"

//http://developer.android.com/training/articles/perf-jni.html

static JavaVM* gJavaVM = NULL;

// Cache the most-recently loaded bank's friendly name so Java callers that
// don't track native bank tokens can still query a human-friendly string.
static char g_lastBankFriendly[256] = "";


jint JNI_OnLoad(JavaVM* vm, void* reserved)
{
    JNIEnv* env;

	// cache java VM
	gJavaVM = vm;

    __android_log_print(ANDROID_LOG_DEBUG, "miniBAE", "JNI_OnLoad called");

    if((*vm)->GetEnv(vm, (void**)&env, JNI_VERSION_1_6) != JNI_OK)
    {
        __android_log_print(ANDROID_LOG_ERROR, "miniBAE", "Failed to get the environment using GetEnv()");
        return -1;
    }

    // Get jclass with env->FindClass.
    // Register methods with env->RegisterNatives.

    return JNI_VERSION_1_6;
}

/*
 * Class:     org_minibae_Mixer
 * Method:    _newMixer
 * Signature: ()I
 */
JNIEXPORT jlong JNICALL Java_org_minibae_Mixer__1newMixer
  (JNIEnv* env, jclass clazz)
{    
	BAEMixer mixer = BAEMixer_New();
	if (mixer)
	{
		__android_log_print(ANDROID_LOG_DEBUG, "miniBAE", "hello mixer %p", mixer);
	}
	return (jlong)(intptr_t)mixer;
}

/*
 * Class:     org_minibae_Mixer
 * Method:    _deleteMixer
 * Signature: (I)V
 */
JNIEXPORT void JNICALL Java_org_minibae_Mixer__1deleteMixer
	(JNIEnv* env, jclass clazz, jlong reference)
{
		BAEMixer mixer = (BAEMixer)(intptr_t)reference;
	if (mixer)
	{
		BAEMixer_Delete(mixer);
	    __android_log_print(ANDROID_LOG_DEBUG, "miniBAE", "goodbye mixer %p", mixer);
	}
}

/*
 * Class:     org_minibae_Mixer
 * Method:    _openMixer
 * Signature: (IIIIII)I
 */
JNIEXPORT jint JNICALL Java_org_minibae_Mixer__1openMixer
	(JNIEnv* env, jclass clazz, jlong reference, jint sampleRate, jint terpMode, jint maxSongVoices, jint maxSoundVoices, jint mixLevel)
{
	jint status = -1;
    BAEResult    err;
		BAEMixer mixer = (BAEMixer)(intptr_t)reference;
	if (mixer)
	{
        __android_log_print(ANDROID_LOG_DEBUG, "miniBAE", "_openMixer request: sr=%d terp=%d songVoices=%d soundVoices=%d mixLevel=%d engageAudio=TRUE", (int)sampleRate, (int)terpMode, (int)maxSongVoices, (int)maxSoundVoices, (int)mixLevel);
        err = BAEMixer_Open(mixer,
                            sampleRate,
                            terpMode,
                            BAE_USE_STEREO | BAE_USE_16,
                            maxSongVoices,
                            maxSoundVoices, // pcm voices
                            mixLevel,
                            TRUE); // engageAudio immediately for Android debug
        if (err == BAE_NO_ERROR)
        {
	    	__android_log_print(ANDROID_LOG_DEBUG, "miniBAE", "hello openMixer (hardware engaged)");
						status = 0;
	    }
	    else
	    {
	    	__android_log_print(ANDROID_LOG_ERROR, "miniBAE", "failed to open mixer (%d) engageAudio=TRUE", err);
	    }
	}
	return status;
}

/* Mixer helper JNI wrappers */
JNIEXPORT jint JNICALL Java_org_minibae_Mixer__1setDefaultReverb
    (JNIEnv* env, jclass clazz, jlong reference, jint reverbType)
{
		BAEMixer mixer = (BAEMixer)(intptr_t)reference;
		if(!mixer) return -1;
		BAEResult r = BAEMixer_SetDefaultReverb(mixer, (BAEReverbType)reverbType);
		return (jint)r;
}

JNIEXPORT jint JNICALL Java_org_minibae_Mixer__1addBankFromFile
    (JNIEnv* env, jclass clazz, jlong reference, jstring path)
{
		BAEMixer mixer = (BAEMixer)(intptr_t)reference;
		if(!mixer) return -1;
		const char* cpath = (*env)->GetStringUTFChars(env, path, NULL);
		BAEBankToken token = 0;
		BAEResult r = BAEMixer_AddBankFromFile(mixer, cpath, &token);
		if(r == BAE_NO_ERROR) {
			char friendlyBuf[256] = "";
			if(BAE_GetBankFriendlyName(mixer, token, friendlyBuf, (uint32_t)sizeof(friendlyBuf)) == BAE_NO_ERROR) {
				strncpy(g_lastBankFriendly, friendlyBuf, sizeof(g_lastBankFriendly)-1);
				g_lastBankFriendly[sizeof(g_lastBankFriendly)-1] = '\0';
			} else {
				g_lastBankFriendly[0] = '\0';
			}
		}
		(*env)->ReleaseStringUTFChars(env, path, cpath);
		return (jint)r;
}

JNIEXPORT jint JNICALL Java_org_minibae_Mixer__1setMasterVolume
    (JNIEnv* env, jclass clazz, jlong reference, jint fixedVolume)
{
		BAEMixer mixer = (BAEMixer)(intptr_t)reference;
		if(!mixer) return -1;
		BAEMixer_SetMasterVolume(mixer, (BAE_UNSIGNED_FIXED)fixedVolume);
		return 0;
}

JNIEXPORT jint JNICALL Java_org_minibae_Mixer__1setDefaultVelocityCurve
	(JNIEnv* env, jclass clazz, jint curveType)
{
		BAE_SetDefaultVelocityCurve((int)curveType);
		return 0;
}

JNIEXPORT jstring JNICALL Java_org_minibae_Mixer__1getBankFriendlyName
    (JNIEnv* env, jclass clazz, jlong reference)
{
		BAEMixer mixer = (BAEMixer)(intptr_t)reference;
		if(!mixer) return NULL;
		char buf[256];
		// First try the official API with no token (legacy callers expect this),
		// then fall back to the cached friendly name filled when a bank was
		// successfully added via the other JNI entrypoints.
		if(BAE_GetBankFriendlyName(mixer, NULL, buf, (uint32_t)sizeof(buf)) == BAE_NO_ERROR) {
			return (*env)->NewStringUTF(env, buf);
		}
		if(g_lastBankFriendly[0] != '\0') {
			return (*env)->NewStringUTF(env, g_lastBankFriendly);
		}
		return NULL;
}

// Load a bank asset into memory and add it via BAEMixer_AddBankFromMemory
	JNIEXPORT jint JNICALL Java_org_minibae_Mixer__1addBankFromAsset
		(JNIEnv* env, jclass clazz, jlong reference, jobject assetManager, jstring assetName)
	{
		BAEMixer mixer = (BAEMixer)(intptr_t)reference;
		if(!mixer) return -1;
		if(!assetManager || !assetName) return (jint)BAE_PARAM_ERR;

		const char* aname = (*env)->GetStringUTFChars(env, assetName, NULL);
		if(!aname) return (jint)BAE_PARAM_ERR;

		AAssetManager* mgr = AAssetManager_fromJava(env, assetManager);
		if(!mgr){ (*env)->ReleaseStringUTFChars(env, assetName, aname); return (jint)BAE_GENERAL_ERR; }

		AAsset* asset = AAssetManager_open(mgr, aname, AASSET_MODE_STREAMING);
		if(!asset){ (*env)->ReleaseStringUTFChars(env, assetName, aname); return (jint)BAE_FILE_NOT_FOUND; }

		off_t asset_len = AAsset_getLength(asset);
		if(asset_len <= 0){ AAsset_close(asset); (*env)->ReleaseStringUTFChars(env, assetName, aname); return (jint)BAE_BAD_FILE; }
		unsigned char *mem = (unsigned char*)malloc((size_t)asset_len);
		if(!mem){ AAsset_close(asset); (*env)->ReleaseStringUTFChars(env, assetName, aname); return (jint)BAE_MEMORY_ERR; }
		int32_t read_total = 0; int32_t r = 0;
		while(read_total < asset_len && (r = AAsset_read(asset, mem + read_total, (size_t)(asset_len - read_total))) > 0){ read_total += r; }
		AAsset_close(asset);

		BAEBankToken token = 0;
		BAEResult br = BAEMixer_AddBankFromMemory(mixer, (void*)mem, (uint32_t)read_total, &token);
		if(br == BAE_NO_ERROR){
			// After a successful add, try to resolve a friendly name for this token
			// and cache it for Java-side reads.
			char friendlyBuf[256] = "";
			if(BAE_GetBankFriendlyName(mixer, token, friendlyBuf, (uint32_t)sizeof(friendlyBuf)) == BAE_NO_ERROR) {
				strncpy(g_lastBankFriendly, friendlyBuf, sizeof(g_lastBankFriendly)-1);
				g_lastBankFriendly[sizeof(g_lastBankFriendly)-1] = '\0';
			} else {
				// clear cache if none
				g_lastBankFriendly[0] = '\0';
			}
		}

		free(mem);
		(*env)->ReleaseStringUTFChars(env, assetName, aname);
		return (jint)br;
	}


/*
 * Class:     org_minibae_Mixer
 * Method:    _addBankFromMemory
 * Signature: ([B)I
 */
JNIEXPORT jint JNICALL Java_org_minibae_Mixer__1addBankFromMemory
	(JNIEnv* env, jclass clazz, jlong reference, jbyteArray data)
{
	BAEMixer mixer = (BAEMixer)(intptr_t)reference;
	if(!mixer) return -1;
	if(!data) return (jint)BAE_PARAM_ERR;

	jsize len = (*env)->GetArrayLength(env, data);
	jbyte *bytes = (*env)->GetByteArrayElements(env, data, NULL);
	if(!bytes) return (jint)BAE_MEMORY_ERR;

	BAEBankToken token = 0;
	BAEResult br = BAEMixer_AddBankFromMemory(mixer, (void*)bytes, (uint32_t)len, &token);
	if(br == BAE_NO_ERROR){
		char friendlyBuf[256] = "";
		if(BAE_GetBankFriendlyName(mixer, token, friendlyBuf, (uint32_t)sizeof(friendlyBuf)) == BAE_NO_ERROR) {
			strncpy(g_lastBankFriendly, friendlyBuf, sizeof(g_lastBankFriendly)-1);
			g_lastBankFriendly[sizeof(g_lastBankFriendly)-1] = '\0';
		} else {
			g_lastBankFriendly[0] = '\0';
		}
	}

	(*env)->ReleaseByteArrayElements(env, data, bytes, JNI_ABORT);
	return (jint)br;
}

/*
 * Class:     org_minibae_Mixer
 * Method:    _addBuiltInPatches
 * Signature: ()I
 */
JNIEXPORT jint JNICALL Java_org_minibae_Mixer__1addBuiltInPatches
	(JNIEnv* env, jclass clazz, jlong reference)
{
	BAEMixer mixer = (BAEMixer)(intptr_t)reference;
	if(!mixer) return -1;

	extern unsigned char BAE_PATCHES[];
	extern unsigned long BAE_PATCHES_size;

	if(BAE_PATCHES == NULL || BAE_PATCHES_size == 0) return (jint)BAE_BAD_FILE;

	BAEBankToken token = 0;
	BAEResult br = BAEMixer_AddBankFromMemory(mixer, (void*)BAE_PATCHES, (uint32_t)BAE_PATCHES_size, &token);
	if(br == BAE_NO_ERROR){
		char friendlyBuf[256] = "";
		if(BAE_GetBankFriendlyName(mixer, token, friendlyBuf, (uint32_t)sizeof(friendlyBuf)) == BAE_NO_ERROR) {
			strncpy(g_lastBankFriendly, friendlyBuf, sizeof(g_lastBankFriendly)-1);
			g_lastBankFriendly[sizeof(g_lastBankFriendly)-1] = '\0';
		} else {
			g_lastBankFriendly[0] = '\0';
		}
	}
	return (jint)br;
}

	// Note: JNI setter for native cache dir is implemented in org_minibae_Sound.c

