#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

// printf
#include <android/log.h>

// for native asset manager
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>

#include "com_zefie_NeoBAE_Mixer.h"
#include "NeoBAE.h"

#if USE_SF2_SUPPORT == TRUE && _USING_FLUIDSYNTH == TRUE
#include "GenSF2_FluidSynth.h"
#endif

//http://developer.android.com/training/articles/perf-jni.html

// Cache the most-recently loaded bank's friendly name so Java callers that
// don't track native bank tokens can still query a human-friendly string.
static char g_lastBankFriendly[256] = "";

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

/* Mixer helper JNI wrappers */
JNIEXPORT jint JNICALL Java_com_zefie_NeoBAE_Mixer__1setDefaultReverb
    (JNIEnv* env, jclass clazz, jlong reference, jint reverbType)
{
		BAEMixer mixer = (BAEMixer)(intptr_t)reference;
		if(!mixer) return -1;
		BAEResult r = BAEMixer_SetDefaultReverb(mixer, (BAEReverbType)reverbType);
		return (jint)r;
}

JNIEXPORT jint JNICALL Java_com_zefie_NeoBAE_Mixer__1addBankFromFile
    (JNIEnv* env, jclass clazz, jlong reference, jstring path)
{
		BAEMixer mixer = (BAEMixer)(intptr_t)reference;
		if(!mixer) return -1;
		const char* cpath = (*env)->GetStringUTFChars(env, path, NULL);
	
	// Check if this is an SF2/DLS file (requires FluidSynth)
	const char *ext = strrchr(cpath, '.');
	
	BAEMixer_UnloadBanks(mixer);
#if USE_SF2_SUPPORT == TRUE && _USING_FLUIDSYNTH == TRUE
	GM_UnloadSF2Soundfont();
	GM_SetMixerSF2Mode(FALSE);
	
	if (ext && (strcasecmp(ext, ".sf2") == 0 || strcasecmp(ext, ".dls") == 0 || 
	            strcasecmp(ext, ".sf3") == 0 || strcasecmp(ext, ".sfo") == 0))
	{
		// Load SF2/DLS bank
		OPErr err = GM_LoadSF2Soundfont(cpath);		
		if (err != NO_ERR)
		{
			(*env)->ReleaseStringUTFChars(env, path, cpath);
			__android_log_print(ANDROID_LOG_ERROR, "NeoBAE", "SF2 bank load failed: %d", err);
			return (jint)err;
		}
		GM_SetMixerSF2Mode(TRUE);
		// Set friendly name to filename (must happen before ReleaseStringUTFChars)
		const char *base = cpath;
		for (const char *p = cpath; *p; ++p) {
			if (*p == '/' || *p == '\\') base = p + 1;
		}
		strncpy(g_lastBankFriendly, base, sizeof(g_lastBankFriendly)-1);
		g_lastBankFriendly[sizeof(g_lastBankFriendly)-1] = '\0';
		__android_log_print(ANDROID_LOG_DEBUG, "NeoBAE", "SF2 bank loaded: %s", cpath);
		(*env)->ReleaseStringUTFChars(env, path, cpath);
		return 0;
	}
#endif
	
	// Standard HSB bank loading
	BAEBankToken token = 0;
	BAEResult r = BAEMixer_AddBankFromFile(mixer, (BAEPathName)cpath, &token);
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

JNIEXPORT jint JNICALL Java_com_zefie_NeoBAE_Mixer__1setMasterVolume
    (JNIEnv* env, jclass clazz, jlong reference, jint fixedVolume)
{
		BAEMixer mixer = (BAEMixer)(intptr_t)reference;
		if(!mixer) return -1;
		BAEMixer_SetMasterVolume(mixer, (BAE_UNSIGNED_FIXED)fixedVolume);
		return 0;
}

#if defined(__ANDROID__)
// Android-only: post-mix output gain boost control (0..512, where 256 == 1.0x).
extern void BAE_Android_SetOutputGainBoost(int16_t boost256);

JNIEXPORT jint JNICALL Java_com_zefie_NeoBAE_Mixer__1setAndroidOutputGainBoost
	(JNIEnv* env, jclass clazz, jint boost256)
{
	(void)env;
	(void)clazz;
	BAE_Android_SetOutputGainBoost((int16_t)boost256);
	return 0;
}
#endif

JNIEXPORT jstring JNICALL Java_com_zefie_NeoBAE_Mixer__1getBankFriendlyName
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
	JNIEXPORT jint JNICALL Java_com_zefie_NeoBAE_Mixer__1addBankFromAsset
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
	BAEMixer_UnloadBanks(mixer);
#if USE_SF2_SUPPORT == TRUE && _USING_FLUIDSYNTH == TRUE
	GM_UnloadSF2Soundfont();
	GM_SetMixerSF2Mode(FALSE);
	
	// Check for SF2/DLS format by magic bytes
	XBOOL isSF2 = FALSE;
	if (read_total >= 12) {
		if (mem[0] == 'R' && mem[1] == 'I' && mem[2] == 'F' && mem[3] == 'F') {
			if ((mem[8] == 's' && mem[9] == 'f' && mem[10] == 'b' && mem[11] == 'k') ||
			    (mem[8] == 'D' && mem[9] == 'L' && mem[10] == 'S' && mem[11] == ' ')) {
				isSF2 = TRUE;
			}
		}
	}
	
	if (isSF2) {
		// Load as SF2/DLS soundfont
		OPErr err = GM_LoadSF2SoundfontFromMemory((const unsigned char*)mem, (size_t)read_total);
		free(mem);
		(*env)->ReleaseStringUTFChars(env, assetName, aname);
		if (err != NO_ERR) {
			__android_log_print(ANDROID_LOG_ERROR, "NeoBAE", "SF2 asset load failed: %d", err);
			return (jint)err;
		}
		GM_SetMixerSF2Mode(TRUE);
		strncpy(g_lastBankFriendly, aname, sizeof(g_lastBankFriendly)-1);
		g_lastBankFriendly[sizeof(g_lastBankFriendly)-1] = '\0';
		__android_log_print(ANDROID_LOG_DEBUG, "NeoBAE", "SF2 asset loaded: %s", aname);
		return 0;
	}
#endif
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
 * Class:     com_zefie_NeoBAE_Mixer
 * Method:    _addBankFromMemory
 * Signature: ([B)I
 */
JNIEXPORT jint JNICALL Java_com_zefie_NeoBAE_Mixer__1addBankFromMemory
	(JNIEnv* env, jclass clazz, jlong reference, jbyteArray data)
{
	BAEMixer mixer = (BAEMixer)(intptr_t)reference;
	if(!mixer) return -1;
	if(!data) return (jint)BAE_PARAM_ERR;

	jsize len = (*env)->GetArrayLength(env, data);
	jbyte *bytes = (*env)->GetByteArrayElements(env, data, NULL);
	if(!bytes) return (jint)BAE_MEMORY_ERR;

	__android_log_print(ANDROID_LOG_DEBUG, "NeoBAE", "addBankFromMemory: len=%d bytes", (int)len);

	BAEMixer_UnloadBanks(mixer);
#if USE_SF2_SUPPORT == TRUE && _USING_FLUIDSYNTH == TRUE
	__android_log_print(ANDROID_LOG_DEBUG, "NeoBAE", "SF2 support is enabled");
	GM_UnloadSF2Soundfont();
	GM_SetMixerSF2Mode(FALSE);
	
	// Try to detect if this is SF2/DLS format by checking magic bytes
	// SF2 starts with "RIFF....sfbk" (offset 0 and 8)
	// DLS starts with "RIFF....DLS " (offset 0 and 8)
	XBOOL isSF2 = FALSE;
	if (len >= 12) {
		unsigned char *ubytes = (unsigned char*)bytes;
		__android_log_print(ANDROID_LOG_DEBUG, "NeoBAE", "Magic bytes: %02X %02X %02X %02X ... %02X %02X %02X %02X",
			ubytes[0], ubytes[1], ubytes[2], ubytes[3], ubytes[8], ubytes[9], ubytes[10], ubytes[11]);
		if (ubytes[0] == 'R' && ubytes[1] == 'I' && ubytes[2] == 'F' && ubytes[3] == 'F') {
			if ((ubytes[8] == 's' && ubytes[9] == 'f' && ubytes[10] == 'b' && ubytes[11] == 'k') ||
			    (ubytes[8] == 'D' && ubytes[9] == 'L' && ubytes[10] == 'S' && ubytes[11] == ' ')) {
				isSF2 = TRUE;
				__android_log_print(ANDROID_LOG_DEBUG, "NeoBAE", "Detected SF2/DLS format");
			}
		}
	}
	
	if (isSF2) {
		__android_log_print(ANDROID_LOG_DEBUG, "NeoBAE", "Loading SF2 soundfont from memory...");
		// Load as SF2/DLS soundfont
		OPErr err = GM_LoadSF2SoundfontFromMemory((const unsigned char*)bytes, (size_t)len);
		(*env)->ReleaseByteArrayElements(env, data, bytes, JNI_ABORT);
		if (err != NO_ERR) {
			__android_log_print(ANDROID_LOG_ERROR, "NeoBAE", "SF2 bank load from memory failed: %d", err);
			return (jint)err;
		}
		GM_SetMixerSF2Mode(TRUE);
		strncpy(g_lastBankFriendly, "SF2 Bank", sizeof(g_lastBankFriendly)-1);
		g_lastBankFriendly[sizeof(g_lastBankFriendly)-1] = '\0';
		__android_log_print(ANDROID_LOG_DEBUG, "NeoBAE", "SF2 bank loaded from memory");
		return 0;
	} else {
		__android_log_print(ANDROID_LOG_DEBUG, "NeoBAE", "Not SF2/DLS format, trying HSB bank load");
	}
#else
	__android_log_print(ANDROID_LOG_DEBUG, "NeoBAE", "SF2 support is NOT enabled - USE_SF2_SUPPORT=%d _USING_FLUIDSYNTH=%d", USE_SF2_SUPPORT, _USING_FLUIDSYNTH);
#endif

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
 * Class:     com_zefie_NeoBAE_Mixer
 * Method:    _addBankFromMemoryWithFilename
 * Signature: (J[BLjava/lang/String;)I
 */
JNIEXPORT jint JNICALL Java_com_zefie_NeoBAE_Mixer__1addBankFromMemoryWithFilename
	(JNIEnv* env, jclass clazz, jlong reference, jbyteArray data, jstring filename)
{
	BAEMixer mixer = (BAEMixer)(intptr_t)reference;
	if(!mixer) return -1;
	if(!data) return (jint)BAE_PARAM_ERR;

	jsize len = (*env)->GetArrayLength(env, data);
	jbyte *bytes = (*env)->GetByteArrayElements(env, data, NULL);
	if(!bytes) return (jint)BAE_MEMORY_ERR;

	__android_log_print(ANDROID_LOG_DEBUG, "NeoBAE", "addBankFromMemoryWithFilename: len=%d bytes", (int)len);

	BAEMixer_UnloadBanks(mixer);
#if USE_SF2_SUPPORT == TRUE && _USING_FLUIDSYNTH == TRUE
	__android_log_print(ANDROID_LOG_DEBUG, "NeoBAE", "SF2 support is enabled");
	GM_UnloadSF2Soundfont();
	GM_SetMixerSF2Mode(FALSE);
	
	// Try to detect if this is SF2/DLS format by checking magic bytes
	// SF2 starts with "RIFF....sfbk" (offset 0 and 8)
	// DLS starts with "RIFF....DLS " (offset 0 and 8)
	XBOOL isSF2 = FALSE;
	if (len >= 12) {
		unsigned char *ubytes = (unsigned char*)bytes;
		__android_log_print(ANDROID_LOG_DEBUG, "NeoBAE", "Magic bytes: %02X %02X %02X %02X ... %02X %02X %02X %02X",
			ubytes[0], ubytes[1], ubytes[2], ubytes[3], ubytes[8], ubytes[9], ubytes[10], ubytes[11]);
		if (ubytes[0] == 'R' && ubytes[1] == 'I' && ubytes[2] == 'F' && ubytes[3] == 'F') {
			if ((ubytes[8] == 's' && ubytes[9] == 'f' && ubytes[10] == 'b' && ubytes[11] == 'k') ||
			    (ubytes[8] == 'D' && ubytes[9] == 'L' && ubytes[10] == 'S' && ubytes[11] == ' ')) {
				isSF2 = TRUE;
				__android_log_print(ANDROID_LOG_DEBUG, "NeoBAE", "Detected SF2/DLS format");
			}
		}
	}
	
	if (isSF2) {
		__android_log_print(ANDROID_LOG_DEBUG, "NeoBAE", "Loading SF2 soundfont from memory...");
		// Load as SF2/DLS soundfont
		OPErr err = GM_LoadSF2SoundfontFromMemory((const unsigned char*)bytes, (size_t)len);
		(*env)->ReleaseByteArrayElements(env, data, bytes, JNI_ABORT);
		if (err != NO_ERR) {
			__android_log_print(ANDROID_LOG_ERROR, "NeoBAE", "SF2 bank load from memory failed: %d", err);
			return (jint)err;
		}
		GM_SetMixerSF2Mode(TRUE);
		// Use provided filename if available
		if (filename) {
			const char *fname = (*env)->GetStringUTFChars(env, filename, NULL);
			if (fname) {
				strncpy(g_lastBankFriendly, fname, sizeof(g_lastBankFriendly)-1);
				g_lastBankFriendly[sizeof(g_lastBankFriendly)-1] = '\0';
				(*env)->ReleaseStringUTFChars(env, filename, fname);
			} else {
				strncpy(g_lastBankFriendly, "SF2 Bank", sizeof(g_lastBankFriendly)-1);
				g_lastBankFriendly[sizeof(g_lastBankFriendly)-1] = '\0';
			}
		} else {
			strncpy(g_lastBankFriendly, "SF2 Bank", sizeof(g_lastBankFriendly)-1);
			g_lastBankFriendly[sizeof(g_lastBankFriendly)-1] = '\0';
		}
		__android_log_print(ANDROID_LOG_DEBUG, "NeoBAE", "SF2 bank loaded from memory: %s", g_lastBankFriendly);
		return 0;
	} else {
		__android_log_print(ANDROID_LOG_DEBUG, "NeoBAE", "Not SF2/DLS format, trying HSB bank load");
	}
#else
	__android_log_print(ANDROID_LOG_DEBUG, "NeoBAE", "SF2 support is NOT enabled - USE_SF2_SUPPORT=%d _USING_FLUIDSYNTH=%d", USE_SF2_SUPPORT, _USING_FLUIDSYNTH);
#endif

	BAEBankToken token = 0;
	BAEResult br = BAEMixer_AddBankFromMemory(mixer, (void*)bytes, (uint32_t)len, &token);
	if(br == BAE_NO_ERROR){
		char friendlyBuf[256] = "";
		if(BAE_GetBankFriendlyName(mixer, token, friendlyBuf, (uint32_t)sizeof(friendlyBuf)) == BAE_NO_ERROR) {
			strncpy(g_lastBankFriendly, friendlyBuf, sizeof(g_lastBankFriendly)-1);
			g_lastBankFriendly[sizeof(g_lastBankFriendly)-1] = '\0';
		} else {
			// No friendly name in bank, use filename if provided
			if (filename) {
				const char *fname = (*env)->GetStringUTFChars(env, filename, NULL);
				if (fname) {
					strncpy(g_lastBankFriendly, fname, sizeof(g_lastBankFriendly)-1);
					g_lastBankFriendly[sizeof(g_lastBankFriendly)-1] = '\0';
					(*env)->ReleaseStringUTFChars(env, filename, fname);
				} else {
					g_lastBankFriendly[0] = '\0';
				}
			} else {
				g_lastBankFriendly[0] = '\0';
			}
		}
	}

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

