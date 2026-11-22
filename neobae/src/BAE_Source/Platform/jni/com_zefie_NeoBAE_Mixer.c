#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

// printf
#include <android/log.h>

#include "com_zefie_NeoBAE_Mixer.h"
#include "NeoBAE.h"
#include "GenPriv.h"
#include "GenSnd.h"

#if USE_NATIVE_DLS == TRUE
#include "GenDLS_MobileBAE.h"
#endif

#if USE_XMF_SUPPORT == TRUE && (_USING_FLUIDLITE == TRUE || USE_NATIVE_DLS == TRUE)
#include "GenXMF.h"
#endif

//http://developer.android.com/training/articles/perf-jni.html

// Cache the most-recently loaded bank's friendly name so Java callers that
// don't track native bank tokens can still query a human-friendly string.
static char g_lastBankFriendly[256] = "";

static const char *pv_path_basename(const char *path)
{
	const char *base = path ? path : "";
	const char *p;
	for (p = base; *p; ++p) {
		if (*p == '/' || *p == '\\') base = p + 1;
	}
	return base;
}

static void cache_bank_friendly(BAEMixer mixer, const BAEBankLoadInfo *info, const char *fallbackName)
{
	char friendlyBuf[256] = "";
	g_lastBankFriendly[0] = '\0';

	if (!info) {
		if (fallbackName && fallbackName[0]) {
			strncpy(g_lastBankFriendly, fallbackName, sizeof(g_lastBankFriendly) - 1);
			g_lastBankFriendly[sizeof(g_lastBankFriendly) - 1] = '\0';
		}
		return;
	}

	if (info->kind == BAE_BANK_KIND_HSB || info->kind == BAE_BANK_KIND_BUILTIN) {
		if (mixer && info->token &&
			BAE_GetBankFriendlyName(mixer, info->token, friendlyBuf, (uint32_t)sizeof(friendlyBuf)) == BAE_NO_ERROR &&
			friendlyBuf[0] != '\0')
		{
			strncpy(g_lastBankFriendly, friendlyBuf, sizeof(g_lastBankFriendly) - 1);
			g_lastBankFriendly[sizeof(g_lastBankFriendly) - 1] = '\0';
			return;
		}
	}
#if USE_NATIVE_DLS == TRUE
	else if (info->kind == BAE_BANK_KIND_DLS) {
		if (mixer &&
			BAEMixer_GetDLSBankFriendlyName(mixer, friendlyBuf, (uint32_t)sizeof(friendlyBuf)) == BAE_NO_ERROR &&
			friendlyBuf[0] != '\0')
		{
			strncpy(g_lastBankFriendly, friendlyBuf, sizeof(g_lastBankFriendly) - 1);
			g_lastBankFriendly[sizeof(g_lastBankFriendly) - 1] = '\0';
			return;
		}
		if (fallbackName && fallbackName[0]) {
			strncpy(g_lastBankFriendly, fallbackName, sizeof(g_lastBankFriendly) - 1);
			g_lastBankFriendly[sizeof(g_lastBankFriendly) - 1] = '\0';
		} else {
			strncpy(g_lastBankFriendly, "DLS Bank", sizeof(g_lastBankFriendly) - 1);
			g_lastBankFriendly[sizeof(g_lastBankFriendly) - 1] = '\0';
		}
		return;
	}
#endif
	else if (info->kind == BAE_BANK_KIND_SF2) {
		if (fallbackName && fallbackName[0]) {
			strncpy(g_lastBankFriendly, fallbackName, sizeof(g_lastBankFriendly) - 1);
			g_lastBankFriendly[sizeof(g_lastBankFriendly) - 1] = '\0';
		} else {
			strncpy(g_lastBankFriendly, "SF2 Bank", sizeof(g_lastBankFriendly) - 1);
			g_lastBankFriendly[sizeof(g_lastBankFriendly) - 1] = '\0';
		}
		return;
	}

	if (fallbackName && fallbackName[0]) {
		strncpy(g_lastBankFriendly, fallbackName, sizeof(g_lastBankFriendly) - 1);
		g_lastBankFriendly[sizeof(g_lastBankFriendly) - 1] = '\0';
	}
}

JavaVM* gJavaVM = NULL;

jint JNI_OnLoad(JavaVM* vm, void* reserved)
{
    JNIEnv* env;

	// cache java VM
	gJavaVM = vm;

    __android_log_print(ANDROID_LOG_DEBUG, "NeoBAE", "JNI_OnLoad called");

    if((*vm)->GetEnv(vm, (void**)&env, JNI_VERSION_1_6) != JNI_OK)
    {
        __android_log_print(ANDROID_LOG_ERROR, "NeoBAE", "Failed to get the environment using GetEnv()");
        return -1;
    }

    // Get jclass with env->FindClass.
    // Register methods with env->RegisterNatives.

    return JNI_VERSION_1_6;
}

/*
 * Class:     com_zefie_NeoBAE_Mixer
 * Method:    _newMixer
 * Signature: ()I
 */
JNIEXPORT jlong JNICALL Java_com_zefie_NeoBAE_Mixer__1newMixer
  (JNIEnv* env, jclass clazz)
{    
	BAEMixer mixer = BAEMixer_New();
	if (mixer)
	{
		__android_log_print(ANDROID_LOG_DEBUG, "NeoBAE", "hello mixer %p", mixer);
	}
	return (jlong)(intptr_t)mixer;
}

/*
 * Class:     com_zefie_NeoBAE_Mixer
 * Method:    _deleteMixer
 * Signature: (I)V
 */
JNIEXPORT void JNICALL Java_com_zefie_NeoBAE_Mixer__1deleteMixer
	(JNIEnv* env, jclass clazz, jlong reference)
{
		BAEMixer mixer = (BAEMixer)(intptr_t)reference;
	if (mixer)
	{
		BAEMixer_Delete(mixer);
	    __android_log_print(ANDROID_LOG_DEBUG, "NeoBAE", "goodbye mixer %p", mixer);
	}
}

/*
 * Class:     com_zefie_NeoBAE_Mixer
 * Method:    _openMixer
 * Signature: (IIIIII)I
 */
JNIEXPORT jint JNICALL Java_com_zefie_NeoBAE_Mixer__1openMixer
	(JNIEnv* env, jclass clazz, jlong reference, jint sampleRate, jint terpMode, jint maxSongVoices, jint maxSoundVoices, jint mixLevel)
{
    BAEResult    err = BAE_NOT_SETUP;
    BAEMixer mixer = (BAEMixer)(intptr_t)reference;
	if (mixer)
	{
        __android_log_print(ANDROID_LOG_DEBUG, "NeoBAE", "_openMixer request: sr=%d terp=%d songVoices=%d soundVoices=%d mixLevel=%d engageAudio=TRUE", (int)sampleRate, (int)terpMode, (int)maxSongVoices, (int)maxSoundVoices, (int)mixLevel);
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
	    	__android_log_print(ANDROID_LOG_DEBUG, "NeoBAE", "hello openMixer (hardware engaged)");
	    }
	    else
	    {
	    	__android_log_print(ANDROID_LOG_ERROR, "NeoBAE", "failed to open mixer (%d) engageAudio=TRUE", err);
	    }
	}
	return (jint)err;
}

JNIEXPORT jint JNICALL Java_com_zefie_NeoBAE_Mixer__1disengageAudio
	(JNIEnv* env, jclass clazz, jlong reference)
{
	(void)env;
	(void)clazz;
	BAEMixer mixer = (BAEMixer)(intptr_t)reference;
	if (!mixer) return (jint)BAE_PARAM_ERR;
	return (jint)BAEMixer_DisengageAudio(mixer);
}

JNIEXPORT jint JNICALL Java_com_zefie_NeoBAE_Mixer__1reengageAudio
	(JNIEnv* env, jclass clazz, jlong reference)
{
	(void)env;
	(void)clazz;
	BAEMixer mixer = (BAEMixer)(intptr_t)reference;
	if (!mixer) return (jint)BAE_PARAM_ERR;
	return (jint)BAEMixer_ReengageAudio(mixer);
}

JNIEXPORT jint JNICALL Java_com_zefie_NeoBAE_Mixer__1isAudioEngaged
	(JNIEnv* env, jclass clazz, jlong reference)
{
	(void)env;
	(void)clazz;
	BAEMixer mixer = (BAEMixer)(intptr_t)reference;
	if (!mixer) return 0;
	BAE_BOOL engaged = FALSE;
	if (BAEMixer_IsAudioEngaged(mixer, &engaged) != BAE_NO_ERROR)
	{
		return 0;
	}
	return engaged ? 1 : 0;
}

JNIEXPORT jint JNICALL Java_com_zefie_NeoBAE_Mixer__1isAudioTailActive
    (JNIEnv* env, jclass clazz, jlong reference)
{
    (void)env;
    (void)clazz;
    BAEMixer mixer = (BAEMixer)(intptr_t)reference;
    if (!mixer) return 0;
    /* BAEMixer_IsAudioTailActive returns bool; convert to jint 0/1 */
    return BAEMixer_IsAudioTailActive(mixer) ? 1 : 0;
}

/* Mixer helper JNI wrappers */
JNIEXPORT jint JNICALL Java_com_zefie_NeoBAE_Mixer__1setDefaultReverb
    (JNIEnv* env, jclass clazz, jlong reference, jint reverbType)
{
		BAEMixer mixer = (BAEMixer)(intptr_t)reference;
		if(!mixer) return -1;
		BAEResult r = BAEMixer_SetDefaultReverb(mixer, (BAEReverbType)reverbType);
		return (jint)r;
}

JNIEXPORT jint JNICALL Java_com_zefie_NeoBAE_Mixer__1getActiveVoiceCount
	(JNIEnv* env, jclass clazz, jlong reference)
{
	(void)env;
	(void)clazz;
	(void)reference;
	GM_AudioInfo status;
	XSetMemory(&status, (int32_t)sizeof(status), 0);
	GM_GetRealtimeAudioInformation(&status);
	return (jint)status.voicesActive;
}

// Custom Neo reverb parameter JNI wrappers (global reverb params)
JNIEXPORT void JNICALL Java_com_zefie_NeoBAE_Mixer__1setNeoCustomReverbCombCount
	(JNIEnv* env, jclass clazz, jlong reference, jint combCount)
{
	(void)env;
	(void)clazz;
	(void)reference;
	SetNeoCustomReverbCombCount((int)combCount);
}

JNIEXPORT jint JNICALL Java_com_zefie_NeoBAE_Mixer__1getNeoCustomReverbCombCount
	(JNIEnv* env, jclass clazz, jlong reference)
{
	(void)env;
	(void)clazz;
	(void)reference;
	return (jint)GetNeoCustomReverbCombCount();
}

JNIEXPORT void JNICALL Java_com_zefie_NeoBAE_Mixer__1setNeoCustomReverbCombDelay
	(JNIEnv* env, jclass clazz, jlong reference, jint combIndex, jint delayMs)
{
	(void)env;
	(void)clazz;
	(void)reference;
	SetNeoCustomReverbCombDelay((int)combIndex, (int)delayMs);
}

JNIEXPORT jint JNICALL Java_com_zefie_NeoBAE_Mixer__1getNeoCustomReverbCombDelay
	(JNIEnv* env, jclass clazz, jlong reference, jint combIndex)
{
	(void)env;
	(void)clazz;
	(void)reference;
	return (jint)GetNeoCustomReverbCombDelay((int)combIndex);
}

JNIEXPORT void JNICALL Java_com_zefie_NeoBAE_Mixer__1setNeoCustomReverbCombFeedback
	(JNIEnv* env, jclass clazz, jlong reference, jint combIndex, jint feedback)
{
	(void)env;
	(void)clazz;
	(void)reference;
	SetNeoCustomReverbCombFeedback((int)combIndex, (int)feedback);
}

JNIEXPORT jint JNICALL Java_com_zefie_NeoBAE_Mixer__1getNeoCustomReverbCombFeedback
	(JNIEnv* env, jclass clazz, jlong reference, jint combIndex)
{
	(void)env;
	(void)clazz;
	(void)reference;
	return (jint)GetNeoCustomReverbCombFeedback((int)combIndex);
}

JNIEXPORT void JNICALL Java_com_zefie_NeoBAE_Mixer__1setNeoCustomReverbCombGain
	(JNIEnv* env, jclass clazz, jlong reference, jint combIndex, jint gain)
{
	(void)env;
	(void)clazz;
	(void)reference;
	SetNeoCustomReverbCombGain((int)combIndex, (int)gain);
}

JNIEXPORT jint JNICALL Java_com_zefie_NeoBAE_Mixer__1getNeoCustomReverbCombGain
	(JNIEnv* env, jclass clazz, jlong reference, jint combIndex)
{
	(void)env;
	(void)clazz;
	(void)reference;
	return (jint)GetNeoCustomReverbCombGain((int)combIndex);
}

JNIEXPORT void JNICALL Java_com_zefie_NeoBAE_Mixer__1setNeoCustomReverbLowpass
	(JNIEnv* env, jclass clazz, jlong reference, jint lowpass)
{
	(void)env;
	(void)clazz;
	(void)reference;
	SetNeoCustomReverbLowpass((int)lowpass);
}

JNIEXPORT void JNICALL Java_com_zefie_NeoBAE_Mixer__1setNeoReverbMix
	(JNIEnv* env, jclass clazz, jlong reference, jint wetLevel)
{
	(void)env;
	(void)clazz;
	(void)reference;
	SetNeoReverbMix((int)wetLevel);
}

JNIEXPORT jint JNICALL Java_com_zefie_NeoBAE_Mixer__1getNeoReverbMix
	(JNIEnv* env, jclass clazz, jlong reference)
{
	(void)env;
	(void)clazz;
	(void)reference;
	return (jint)GetNeoReverbMix();
}

/* EQ JNI wrappers */
JNIEXPORT void JNICALL Java_com_zefie_NeoBAE_Mixer__1setEQEnabled
	(JNIEnv* env, jclass clazz, jlong reference, jboolean enabled)
{
	(void)env;
	(void)clazz;
	BAEMixer mixer = (BAEMixer)(intptr_t)reference;
	if (mixer) {
		BAEMixer_SetEQEnabled(mixer, enabled ? TRUE : FALSE);
	}
}

JNIEXPORT jboolean JNICALL Java_com_zefie_NeoBAE_Mixer__1getEQEnabled
	(JNIEnv* env, jclass clazz, jlong reference)
{
	(void)env;
	(void)clazz;
	BAEMixer mixer = (BAEMixer)(intptr_t)reference;
	if (mixer) {
		BAE_BOOL enabled = FALSE;
		if (BAEMixer_GetEQEnabled(mixer, &enabled) == BAE_NO_ERROR) {
			return enabled ? JNI_TRUE : JNI_FALSE;
		}
	}
	return JNI_FALSE;
}

JNIEXPORT void JNICALL Java_com_zefie_NeoBAE_Mixer__1setEQGain
	(JNIEnv* env, jclass clazz, jlong reference, jint bandIndex, jfloat gain)
{
	(void)env;
	(void)clazz;
	BAEMixer mixer = (BAEMixer)(intptr_t)reference;
	if (mixer) {
		BAEMixer_SetEQGain(mixer, (uint32_t)bandIndex, (float)gain);
	}
}

JNIEXPORT jfloat JNICALL Java_com_zefie_NeoBAE_Mixer__1getEQGain
	(JNIEnv* env, jclass clazz, jlong reference, jint bandIndex)
{
	(void)env;
	(void)clazz;
	BAEMixer mixer = (BAEMixer)(intptr_t)reference;
	if (mixer) {
		float gain = 0.0f;
		if (BAEMixer_GetEQGain(mixer, (uint32_t)bandIndex, &gain) == BAE_NO_ERROR) {
			return (jfloat)gain;
		}
	}
	return 0.0f;
}

/*
 * Class:     com_zefie_NeoBAE_Mixer
 * Method:    _getNeoReverbPresetParams
 * Signature: (II[I[I[I[I[I)V
 */
JNIEXPORT void JNICALL Java_com_zefie_NeoBAE_Mixer__1getNeoReverbPresetParams
  (JNIEnv* env, jclass clazz, jlong reference, jint reverbType, jintArray combCount, jintArray delaysMs, jintArray feedback, jintArray gain, jintArray lowpass, jintArray mix)
{
	(void)env;
	(void)clazz;
	(void)reference;

	int cCombCount;
	int cDelaysMs[NEO_CUSTOM_MAX_COMBS];
	int cFeedback[NEO_CUSTOM_MAX_COMBS];
	int cGain[NEO_CUSTOM_MAX_COMBS];
	int cLowpass;
    int cMix;

	GetNeoReverbPresetParams((int)reverbType, &cCombCount, cDelaysMs, cFeedback, cGain, &cLowpass, &cMix);

	// Copy results back to Java arrays
	(*env)->SetIntArrayRegion(env, combCount, 0, 1, &cCombCount);
	(*env)->SetIntArrayRegion(env, delaysMs, 0, NEO_CUSTOM_MAX_COMBS, cDelaysMs);
	(*env)->SetIntArrayRegion(env, feedback, 0, NEO_CUSTOM_MAX_COMBS, cFeedback);
	(*env)->SetIntArrayRegion(env, gain, 0, NEO_CUSTOM_MAX_COMBS, cGain);
	(*env)->SetIntArrayRegion(env, lowpass, 0, 1, &cLowpass);
    (*env)->SetIntArrayRegion(env, mix, 0, 1, &cMix);
}

JNIEXPORT jint JNICALL Java_com_zefie_NeoBAE_Mixer__1addBankFromFile
    (JNIEnv* env, jclass clazz, jlong reference, jstring path)
{
	BAEMixer mixer = (BAEMixer)(intptr_t)reference;
	BAEBankLoadInfo info;
	BAEResult r;
	const char* cpath;

	if (!mixer) return -1;
	cpath = (*env)->GetStringUTFChars(env, path, NULL);
	if (!cpath) return (jint)BAE_BAD_FILE;

	memset(&info, 0, sizeof(info));
	r = BAEMixer_LoadBankFromPath(mixer, (BAEPathName)cpath, &info);
	if (r == BAE_NO_ERROR) {
		cache_bank_friendly(mixer, &info, pv_path_basename(cpath));
		__android_log_print(ANDROID_LOG_DEBUG, "NeoBAE", "Bank loaded (kind=%d): %s (display=%s)",
			(int)info.kind, cpath, g_lastBankFriendly);
	} else {
		__android_log_print(ANDROID_LOG_ERROR, "NeoBAE", "Bank load failed: %d (%s)", (int)r, cpath);
	}
	(*env)->ReleaseStringUTFChars(env, path, cpath);
	return (jint)r;
}

JNIEXPORT jint JNICALL Java_com_zefie_NeoBAE_Mixer__1setMasterVolume
    (JNIEnv* env, jclass clazz, jlong reference, jint fixedVolume)
{
		BAEMixer mixer = (BAEMixer)(intptr_t)reference;
		if(!mixer) return -1;
		BAEMixer_SetMasterVolume(mixer, (BAE_UNSIGNED_FIXED)fixedVolume);
		return 0;
}

JNIEXPORT jint JNICALL Java_com_zefie_NeoBAE_Mixer__1setGlobalVolume
    (JNIEnv* env, jclass clazz, jlong reference, jint fixedVolume)
{
		BAEMixer mixer = (BAEMixer)(intptr_t)reference;
		if(!mixer) return -1;
		BAEMixer_SetGlobalVolume(mixer, (BAE_UNSIGNED_FIXED)fixedVolume);
		return 0;
}

JNIEXPORT jint JNICALL Java_com_zefie_NeoBAE_Mixer__1getGlobalVolume
    (JNIEnv* env, jclass clazz, jlong reference, jintArray outVolume)
{
		BAEMixer mixer = (BAEMixer)(intptr_t)reference;
		if(!mixer) return -1;
		BAE_UNSIGNED_FIXED volume;
		if(BAEMixer_GetGlobalVolume(mixer, &volume) != BAE_NO_ERROR) return -1;
		(*env)->SetIntArrayRegion(env, outVolume, 0, 1, (jint*)&volume);
		return 0;
}

JNIEXPORT jstring JNICALL Java_com_zefie_NeoBAE_Mixer__1getBankFriendlyName
    (JNIEnv* env, jclass clazz, jlong reference)
{
		BAEMixer mixer = (BAEMixer)(intptr_t)reference;
		if(!mixer) return NULL;
		char buf[256];
		// First try the official API with no token (legacy callers expect this),
		// then live DLS/RMI main-bank friendly name, then the cached name
		// filled when a bank was successfully added via other JNI entrypoints.
		if(BAE_GetBankFriendlyName(mixer, NULL, buf, (uint32_t)sizeof(buf)) == BAE_NO_ERROR) {
			return (*env)->NewStringUTF(env, buf);
		}
#if USE_NATIVE_DLS == TRUE
		if(BAEMixer_GetDLSBankFriendlyName(mixer, buf, (uint32_t)sizeof(buf)) == BAE_NO_ERROR &&
		   buf[0] != '\0') {
			return (*env)->NewStringUTF(env, buf);
		}
#endif
		if(g_lastBankFriendly[0] != '\0') {
			return (*env)->NewStringUTF(env, g_lastBankFriendly);
		}
		return NULL;
}

/*
 * Class:     com_zefie_NeoBAE_Mixer
 * Method:    _loadBuiltinBank
 * Signature: (J)I
 */
JNIEXPORT jint JNICALL Java_com_zefie_NeoBAE_Mixer__1loadBuiltinBank
	(JNIEnv* env, jclass clazz, jlong reference)
{
	BAEMixer mixer = (BAEMixer)(intptr_t)reference;

	(void)env;
	(void)clazz;

	if (!mixer) return -1;

#if _BUILT_IN_PATCHES == TRUE
	{
		BAEBankLoadInfo info;
		BAEResult br;

		memset(&info, 0, sizeof(info));
		br = BAEMixer_LoadBankBuiltinOnly(mixer, &info);
		if (br == BAE_NO_ERROR) {
			cache_bank_friendly(mixer, &info, "Built-in patches");
			__android_log_print(ANDROID_LOG_DEBUG, "NeoBAE",
				"Builtin bank loaded (kind=%d, display=%s)",
				(int)info.kind, g_lastBankFriendly);
		} else {
			__android_log_print(ANDROID_LOG_ERROR, "NeoBAE",
				"Builtin bank load failed: %d", (int)br);
		}
		return (jint)br;
	}
#else
	__android_log_print(ANDROID_LOG_ERROR, "NeoBAE",
		"Builtin bank unavailable (_BUILT_IN_PATCHES disabled)");
	return (jint)BAE_NOT_SETUP;
#endif
}


/*
 * Class:     com_zefie_NeoBAE_Mixer
 * Method:    _addBankFromMemory
 * Signature: ([B)I
 */
JNIEXPORT jint JNICALL Java_com_zefie_NeoBAE_Mixer__1addBankFromMemory
	(JNIEnv* env, jclass clazz, jlong reference, jbyteArray data)
{
	BAEMixer mixer = (BAEMixer)(intptr_t)reference;
	BAEBankLoadInfo info;
	BAEResult br;
	jsize len;
	jbyte *bytes;

	if (!mixer) return -1;
	if (!data) return (jint)BAE_PARAM_ERR;

	len = (*env)->GetArrayLength(env, data);
	bytes = (*env)->GetByteArrayElements(env, data, NULL);
	if (!bytes) return (jint)BAE_MEMORY_ERR;

	__android_log_print(ANDROID_LOG_DEBUG, "NeoBAE", "addBankFromMemory: len=%d bytes", (int)len);

	memset(&info, 0, sizeof(info));
	br = BAEMixer_LoadBankFromMemory(mixer, (void*)bytes, (uint32_t)len, NULL, &info);
	if (br == BAE_NO_ERROR) {
		cache_bank_friendly(mixer, &info, NULL);
		__android_log_print(ANDROID_LOG_DEBUG, "NeoBAE", "Bank loaded from memory (kind=%d, display=%s)",
			(int)info.kind, g_lastBankFriendly);
	} else {
		__android_log_print(ANDROID_LOG_ERROR, "NeoBAE", "Bank load from memory failed: %d", (int)br);
	}

	(*env)->ReleaseByteArrayElements(env, data, bytes, JNI_ABORT);
	return (jint)br;
}

/*
 * Class:     com_zefie_NeoBAE_Mixer
 * Method:    _addBankFromMemoryWithFilename
 * Signature: (J[BLjava/lang/String;)I
 */
JNIEXPORT jint JNICALL Java_com_zefie_NeoBAE_Mixer__1addBankFromMemoryWithFilename
	(JNIEnv* env, jclass clazz, jlong reference, jbyteArray data, jstring filename)
{
	BAEMixer mixer = (BAEMixer)(intptr_t)reference;
	BAEBankLoadInfo info;
	BAEResult br;
	jsize len;
	jbyte *bytes;
	const char *fname = NULL;
	const char *hint = NULL;

	if (!mixer) return -1;
	if (!data) return (jint)BAE_PARAM_ERR;

	len = (*env)->GetArrayLength(env, data);
	bytes = (*env)->GetByteArrayElements(env, data, NULL);
	if (!bytes) return (jint)BAE_MEMORY_ERR;

	if (filename) {
		fname = (*env)->GetStringUTFChars(env, filename, NULL);
		hint = fname;
	}

	__android_log_print(ANDROID_LOG_DEBUG, "NeoBAE", "addBankFromMemoryWithFilename: len=%d bytes", (int)len);

	memset(&info, 0, sizeof(info));
	br = BAEMixer_LoadBankFromMemory(mixer, (void*)bytes, (uint32_t)len, hint, &info);
	if (br == BAE_NO_ERROR) {
		cache_bank_friendly(mixer, &info, hint ? pv_path_basename(hint) : NULL);
		__android_log_print(ANDROID_LOG_DEBUG, "NeoBAE", "Bank loaded from memory (kind=%d, display=%s)",
			(int)info.kind, g_lastBankFriendly);
	} else {
		__android_log_print(ANDROID_LOG_ERROR, "NeoBAE", "Bank load from memory failed: %d", (int)br);
	}

	if (fname)
		(*env)->ReleaseStringUTFChars(env, filename, fname);
	(*env)->ReleaseByteArrayElements(env, data, bytes, JNI_ABORT);
	return (jint)br;
}

	// Note: JNI setter for native cache dir is implemented in com_zefie_NeoBAE_Sound.c

/*
 * Class:     com_zefie_NeoBAE_Mixer
 * Method:    _getVersion
 * Signature: ()Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_com_zefie_NeoBAE_Mixer__1getVersion
	(JNIEnv* env, jclass clazz)
{
	const char* version = BAE_GetVersion();
	jstring result = (*env)->NewStringUTF(env, version);
	free((void*)version); // BAE_GetVersion returns malloc'd string
	return result;
}

/*
 * Class:     com_zefie_NeoBAE_Mixer
 * Method:    _getCompileInfo
 * Signature: ()Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_com_zefie_NeoBAE_Mixer__1getCompileInfo
	(JNIEnv* env, jclass clazz)
{
	const char* compileInfo = BAE_GetCompileInfo();
	jstring result = (*env)->NewStringUTF(env, compileInfo);
	free((void*)compileInfo); // BAE_GetCompileInfo returns malloc'd string
	return result;
}

/*
 * Class:     com_zefie_NeoBAE_Mixer
 * Method:    _getFeatureString
 * Signature: ()Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_com_zefie_NeoBAE_Mixer__1getFeatureString
	(JNIEnv* env, jclass clazz)
{
	const char* features = BAE_GetFeatureString();
	return (*env)->NewStringUTF(env, features); // BAE_GetFeatureString returns static string, no free needed
}

JNIEXPORT jint JNICALL Java_com_zefie_NeoBAE_Mixer__1setSpanDCFix
	(JNIEnv* env, jclass clazz, jboolean enable)
{
	return (jint)BAE_SetSpanDCFix(enable ? TRUE : FALSE);
}

JNIEXPORT jboolean JNICALL Java_com_zefie_NeoBAE_Mixer__1getSpanDCFix
	(JNIEnv* env, jclass clazz)
{
	BAE_BOOL enabled = FALSE;
	BAE_GetSpanDCFix(&enabled);
	return (jboolean)(enabled ? JNI_TRUE : JNI_FALSE);
}

JNIEXPORT jint JNICALL Java_com_zefie_NeoBAE_Mixer__1setSongNormalizeGain
	(JNIEnv* env, jclass clazz, jlong reference, jint gainPct)
{
	(void)env;
	(void)clazz;
	if (!reference) return (jint)BAE_NULL_OBJECT;
	return (jint)BAEMixer_SetSongNormalizeGain((BAEMixer)(intptr_t)reference, (int32_t)gainPct);
}

JNIEXPORT jint JNICALL Java_com_zefie_NeoBAE_Mixer__1setClassicChorus
	(JNIEnv* env, jclass clazz, jboolean enable)
{
	return (jint)BAE_SetClassicChorus(enable ? TRUE : FALSE);
}

JNIEXPORT jboolean JNICALL Java_com_zefie_NeoBAE_Mixer__1getClassicChorus
	(JNIEnv* env, jclass clazz)
{
	BAE_BOOL enabled = FALSE;
	BAE_GetClassicChorus(&enabled);
	return (jboolean)(enabled ? JNI_TRUE : JNI_FALSE);
}

JNIEXPORT jint JNICALL Java_com_zefie_NeoBAE_Mixer__1setWebTVMode
	(JNIEnv* env, jclass clazz, jboolean enable)
{
	(void)env;
	(void)clazz;
	return (jint)BAE_SetWebTVMode(enable ? TRUE : FALSE);
}

JNIEXPORT jboolean JNICALL Java_com_zefie_NeoBAE_Mixer__1getWebTVMode
	(JNIEnv* env, jclass clazz)
{
	BAE_BOOL enabled = FALSE;
	(void)env;
	(void)clazz;
	BAE_GetWebTVMode(&enabled);
	return (jboolean)(enabled ? JNI_TRUE : JNI_FALSE);
}

JNIEXPORT jint JNICALL Java_com_zefie_NeoBAE_Mixer__1setDLSCompatibilityMode
	(JNIEnv* env, jclass clazz, jboolean enable)
{
	(void)env;
	(void)clazz;
#if USE_NATIVE_DLS == TRUE
	GM_DLS_SetMobileBAEQuirks(enable ? FALSE : TRUE); // inverted: compat mode disables quirks
	return (jint)BAE_NO_ERROR;
#else
	return (jint)BAE_ERROR_FEATURE_UNAVAIL;
#endif
}

JNIEXPORT jboolean JNICALL Java_com_zefie_NeoBAE_Mixer__1getDLSCompatibilityMode
	(JNIEnv* env, jclass clazz)
{
	(void)env;
	(void)clazz;
#if USE_NATIVE_DLS == TRUE
	/* Returns bool; inverted for UI: compat mode = quirks disabled. */
	return (jboolean)(GM_DLS_GetMobileBAEQuirks() ? JNI_FALSE : JNI_TRUE);
#else
	return (jboolean)JNI_FALSE;
#endif
}

JNIEXPORT jboolean JNICALL Java_com_zefie_NeoBAE_Mixer__1hasEggsDLSBank
	(JNIEnv* env, jclass clazz, jlong reference)
{
	(void)env;
	(void)clazz;
#if USE_NATIVE_DLS == TRUE
	BAEMixer mixer = (BAEMixer)(intptr_t)reference;
	if (!mixer) {
		return (jboolean)JNI_FALSE;
	}
	return (jboolean)(BAEMixer_HasEggsDLSBank(mixer) ? JNI_TRUE : JNI_FALSE);
#else
	(void)reference;
	return (jboolean)JNI_FALSE;
#endif
}

JNIEXPORT jboolean JNICALL Java_com_zefie_NeoBAE_Mixer__1hasMobileBAEDLSBank
	(JNIEnv* env, jclass clazz, jlong reference)
{
	(void)env;
	(void)clazz;
#if USE_NATIVE_DLS == TRUE
	BAEMixer mixer = (BAEMixer)(intptr_t)reference;
	if (!mixer) {
		return (jboolean)JNI_FALSE;
	}
	return (jboolean)(BAEMixer_HasMobileBAEDLSBank(mixer) ? JNI_TRUE : JNI_FALSE);
#else
	(void)reference;
	return (jboolean)JNI_FALSE;
#endif
}

JNIEXPORT jboolean JNICALL Java_com_zefie_NeoBAE_Mixer__1hasMobileBAEMainBank
	(JNIEnv* env, jclass clazz, jlong reference)
{
	(void)env;
	(void)clazz;
#if USE_NATIVE_DLS == TRUE
	BAEMixer mixer = (BAEMixer)(intptr_t)reference;
	if (!mixer) {
		return (jboolean)JNI_FALSE;
	}
	return (jboolean)(BAEMixer_HasMobileBAEMainBank(mixer) ? JNI_TRUE : JNI_FALSE);
#else
	(void)reference;
	return (jboolean)JNI_FALSE;
#endif
}

JNIEXPORT jboolean JNICALL Java_com_zefie_NeoBAE_Mixer__1hasXMFDLSOverlayBank
	(JNIEnv* env, jclass clazz, jlong reference)
{
	(void)env;
	(void)clazz;
#if USE_NATIVE_DLS == TRUE
	BAEMixer mixer = (BAEMixer)(intptr_t)reference;
	if (!mixer) {
		return (jboolean)JNI_FALSE;
	}
	return (jboolean)(BAEMixer_HasXMFDLSOverlayBank(mixer) ? JNI_TRUE : JNI_FALSE);
#else
	(void)reference;
	return (jboolean)JNI_FALSE;
#endif
}

JNIEXPORT jint JNICALL Java_com_zefie_NeoBAE_Mixer__1getDLSBankLevel
	(JNIEnv* env, jclass clazz, jlong reference)
{
	(void)env;
	(void)clazz;
#if USE_NATIVE_DLS == TRUE
	BAEMixer mixer = (BAEMixer)(intptr_t)reference;
	if (!mixer) {
		return 0;
	}
	return (jint)BAEMixer_GetDLSBankLevel(mixer);
#else
	(void)reference;
	return 0;
#endif
}

