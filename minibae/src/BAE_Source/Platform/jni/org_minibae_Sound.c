#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

// printf
#include <android/log.h>

// for native asset manager
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>

#include "org_minibae_Sound.h"
#include "MiniBAE.h"
#include "GenSnd.h"
#include "GenSF2_FluidSynth.h"

#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <sys/stat.h>
#include <stdlib.h>

// Optional writable cache directory provided by Java layer. If empty, fallback to /data/local/tmp
static char g_miniBAE_cache_dir[512] = "";

// JNI setter to allow Java to provide a writable cache directory for temp files
JNIEXPORT void JNICALL Java_org_minibae_Mixer__1setNativeCacheDir(JNIEnv* env, jclass clazz, jstring path)
{
	if(!path) return;
	const char* cpath = (*env)->GetStringUTFChars(env, path, NULL);
	if(!cpath) return;
	// copy up to buffer size-1
	strncpy(g_miniBAE_cache_dir, cpath, sizeof(g_miniBAE_cache_dir)-1);
	g_miniBAE_cache_dir[sizeof(g_miniBAE_cache_dir)-1] = '\0';
	__android_log_print(ANDROID_LOG_DEBUG, "miniBAE", "g_miniBAE_cache_dir set to %s", g_miniBAE_cache_dir);
	(*env)->ReleaseStringUTFChars(env, path, cpath);
}

/*
 * Class:     org_minibae_Sound
 * Method:    _newNativeSound
 * Signature: (I)I
 */
JNIEXPORT jlong JNICALL Java_org_minibae_Sound__1newNativeSound
	(JNIEnv* env, jobject jsound, jlong mixerReference)
{
		BAEMixer mixer = (BAEMixer)(intptr_t)mixerReference;
	BAESound sound = NULL;

	if (mixer)
	{
		__android_log_print(ANDROID_LOG_DEBUG, "miniBAE", "hello sound %p", (void*)mixer);
		sound = BAESound_New(mixer);
	}
	return (jlong)(intptr_t)sound;
}

/*
 * Class:     org_minibae_Sound
 * Method:    _loadSound
 * Signature: (Ljava/nio/ByteBuffer;)I
 */
JNIEXPORT jint JNICALL Java_org_minibae_Sound__1loadSound__Ljava_nio_ByteBuffer_2
	(JNIEnv* env, jobject snd, jlong soundReference, jobject byteBuffer)
{
	BAESound sound = (BAESound)(intptr_t)soundReference;
	__android_log_print(ANDROID_LOG_DEBUG, "miniBAE", "_loadSound(ByteBuffer) sound=%p byteBuffer=%p", (void*)sound, (void*)byteBuffer);

	if(!sound){ __android_log_print(ANDROID_LOG_ERROR, "miniBAE", "_loadSound(ByteBuffer): invalid sound handle"); return (jint)BAE_PARAM_ERR; }
	if(!byteBuffer){ __android_log_print(ANDROID_LOG_ERROR, "miniBAE", "ByteBuffer is null"); return (jint)BAE_PARAM_ERR; }

	void* data = (*env)->GetDirectBufferAddress(env, byteBuffer);
	if(!data){ __android_log_print(ANDROID_LOG_ERROR, "miniBAE", "GetDirectBufferAddress returned NULL - perhaps not a direct ByteBuffer"); return (jint)BAE_PARAM_ERR; }
	jsize cap = (*env)->GetDirectBufferCapacity(env, byteBuffer);
	if(cap <= 0){ __android_log_print(ANDROID_LOG_ERROR, "miniBAE", "ByteBuffer capacity <= 0"); return (jint)BAE_BAD_FILE; }

	// Heuristic file type detection (extension-less memory buffer)
	const unsigned char *bytes = (const unsigned char*)data;
	BAEFileType ftype = BAE_INVALID_TYPE;
	if(cap >= 12 && (memcmp(bytes, "RIFF", 4) == 0) && memcmp(bytes+8, "WAVE", 4) == 0){
		ftype = BAE_WAVE_TYPE;
	}else if(cap >= 12 && (memcmp(bytes, "FORM", 4) == 0) && (memcmp(bytes+8, "AIFF", 4) == 0 || memcmp(bytes+8, "AIFC", 4) == 0)){
		ftype = BAE_AIFF_TYPE;
	}else if(cap >= 4 && (memcmp(bytes, ".snd", 4) == 0)){
		ftype = BAE_AU_TYPE;
	}else if(cap >= 3 && (memcmp(bytes, "ID3", 3) == 0)){
		ftype = BAE_MPEG_TYPE; // MP3 with ID3 tag
	}else if(cap >= 2 && bytes[0] == 0xFF && (bytes[1] & 0xE0) == 0xE0){
		// Likely MPEG frame (MP2/MP3). Distinguish Layer via bits but engine uses single MPEG type.
		ftype = BAE_MPEG_TYPE;
	}

	if(ftype == BAE_INVALID_TYPE){
		__android_log_print(ANDROID_LOG_ERROR, "miniBAE", "_loadSound(ByteBuffer) unknown/unsupported buffer format");
		return (jint)BAE_UNSUPPORTED_FORMAT;
	}

	// Copy to owned memory because underlying load routine may read asynchronously or expect stable pointer
	unsigned char *copy = (unsigned char*)malloc((size_t)cap);
	if(!copy){ return (jint)BAE_MEMORY_ERR; }
	memcpy(copy, data, (size_t)cap);
	BAEResult sr = BAESound_LoadMemorySample(sound, (void*)copy, (uint32_t)cap, ftype);
	// BAESound_LoadMemorySample copies/allocates its own internal Wave data; we can free our temp copy.
	free(copy);
	if(sr != BAE_NO_ERROR){ __android_log_print(ANDROID_LOG_ERROR, "miniBAE", "BAESound_LoadMemorySample failed %d", sr); return (jint)sr; }
	__android_log_print(ANDROID_LOG_DEBUG, "miniBAE", "Loaded sound from ByteBuffer (%d bytes) type=%d", (int)cap, ftype);
	return (jint)BAE_NO_ERROR;
}

/*
 * Class:     org_minibae_Sound
 * Method:    _loadSound
 * Signature: (Landroid/content/res/AssetManager;Ljava/lang/String;)I
 */
JNIEXPORT jint JNICALL Java_org_minibae_Sound__1loadSound__Landroid_content_res_AssetManager_2Ljava_lang_String_2
	(JNIEnv* env, jobject jsound, jlong soundReference, jobject assetManager, jstring filename)
{
		BAESound sound = (BAESound)(intptr_t)soundReference;
		__android_log_print(ANDROID_LOG_DEBUG, "miniBAE", "hello sound %p", (void*)sound);

	if(!assetManager || !filename) return (jint)BAE_PARAM_ERR;

	AAssetManager* mgr = AAssetManager_fromJava(env, assetManager);
	if(!mgr) return (jint)BAE_GENERAL_ERR;

	const char* fname = (*env)->GetStringUTFChars(env, filename, NULL);
	if(!fname) return (jint)BAE_PARAM_ERR;

	AAsset* asset = AAssetManager_open(mgr, fname, AASSET_MODE_STREAMING);
	if(!asset){ __android_log_print(ANDROID_LOG_ERROR, "miniBAE", "Failed to open asset %s", fname); (*env)->ReleaseStringUTFChars(env, filename, fname); return (jint)BAE_FILE_NOT_FOUND; }

	// Read entire asset into memory
	off_t asset_len = AAsset_getLength(asset);
	if(asset_len <= 0){ __android_log_print(ANDROID_LOG_ERROR, "miniBAE", "Asset has zero length %s", fname); AAsset_close(asset); (*env)->ReleaseStringUTFChars(env, filename, fname); return (jint)BAE_BAD_FILE; }
	unsigned char *mem = (unsigned char*)malloc((size_t)asset_len);
	if(!mem){ __android_log_print(ANDROID_LOG_ERROR, "miniBAE", "Out of memory reading asset %s", fname); AAsset_close(asset); (*env)->ReleaseStringUTFChars(env, filename, fname); return (jint)BAE_MEMORY_ERR; }
	int32_t read_total = 0;
	int32_t r = 0;
	while(read_total < asset_len && (r = AAsset_read(asset, mem + read_total, (size_t)(asset_len - read_total))) > 0){ read_total += r; }
	AAsset_close(asset);

	// Determine extension BEFORE releasing UTF chars
	const char *ext = strrchr(fname, '.');
	// copy extension for safety after release
	char extCopy[16];
	if(ext){
		strncpy(extCopy, ext, sizeof(extCopy)-1);
		extCopy[sizeof(extCopy)-1] = '\0';
		ext = extCopy;
	}
	(*env)->ReleaseStringUTFChars(env, filename, fname);
	if(ext){
		if(strcasecmp(ext, ".mid") == 0 || strcasecmp(ext, ".midi") == 0 || strcasecmp(ext, ".kar") == 0){
			// create BAESong and load from memory
			BAEMixer mixer = NULL;
			if(sound){ BAESound_GetMixer(sound, &mixer); }
			if(!mixer){ free(mem); __android_log_print(ANDROID_LOG_ERROR, "miniBAE", "No mixer available for sound load"); return (jint)BAE_NOT_SETUP; }

			BAESong song = BAESong_New(mixer);
			if(!song){ free(mem); return (jint)BAE_MEMORY_ERR; }
			BAEResult sr = BAESong_LoadMidiFromMemory(song, (void const*)mem, (uint32_t)read_total, TRUE);
			free(mem);
			if(sr != BAE_NO_ERROR){ BAESong_Delete(song); __android_log_print(ANDROID_LOG_ERROR, "miniBAE", "BAESong_LoadMidiFromMemory failed %d", sr); return (jint)sr; }
			BAESong_Preroll(song);
			sr = BAESong_Start(song, 0);
			if(sr != BAE_NO_ERROR){ BAESong_Stop(song, FALSE); BAESong_Delete(song); __android_log_print(ANDROID_LOG_ERROR, "miniBAE", "BAESong_Start failed %d", sr); return (jint)sr; }
			__android_log_print(ANDROID_LOG_DEBUG, "miniBAE", "Started song from asset memory %s", fname);
			return (jint)BAE_NO_ERROR;
		}
	}

	// Fallback: attempt to load as BAESound sample file from memory
	if(sound){
		BAEFileType ftype = BAE_INVALID_TYPE;
		if(ext){ if(strcasecmp(ext, ".wav") == 0) ftype = BAE_WAVE_TYPE; else if(strcasecmp(ext, ".aif") == 0 || strcasecmp(ext, ".aiff") == 0) ftype = BAE_AIFF_TYPE; else if(strcasecmp(ext, ".au") == 0) ftype = BAE_AU_TYPE; else if(strcasecmp(ext, ".mp3") == 0) ftype = BAE_MPEG_TYPE; }
		if(ftype != BAE_INVALID_TYPE){
			BAEResult sr = BAESound_LoadMemorySample(sound, (void*)mem, (uint32_t)read_total, ftype);
			free(mem);
			if(sr != BAE_NO_ERROR){ __android_log_print(ANDROID_LOG_ERROR, "miniBAE", "BAESound_LoadMemorySample failed %d", sr); return (jint)sr; }
			sr = BAESound_Start(sound, 0, FLOAT_TO_UNSIGNED_FIXED(1.0), 0);
			if(sr != BAE_NO_ERROR){ BAESound_Stop(sound, FALSE); __android_log_print(ANDROID_LOG_ERROR, "miniBAE", "BAESound_Start failed %d", sr); return (jint)sr; }
			return (jint)BAE_NO_ERROR;
		}
		free(mem);
	}

	free(mem);
	return (jint)BAE_UNSUPPORTED_FORMAT;
}

// Sound playback control JNI methods
JNIEXPORT jint JNICALL Java_org_minibae_Sound__1startSound
	(JNIEnv* env, jobject jsound, jlong soundReference, jint sampleFrames, jint fixedVolume)
{
	BAESound sound = (BAESound)(intptr_t)soundReference;
	if(!sound) return (jint)BAE_PARAM_ERR;
	
	// Use the volume passed from Java (already boosted by setVolumePercent)
	// BAESound_Start(sound, priority, volume, startOffsetFrame)
	// priority: 0 = normal priority
	// volume: current volume in 16.16 fixed point format (1.0 = 65536)
	// startOffsetFrame: starting frame offset (0 = beginning)
	BAE_UNSIGNED_FIXED volume = (BAE_UNSIGNED_FIXED)fixedVolume;
	BAEResult r = BAESound_Start(sound, 0, volume, (uint32_t)sampleFrames);
	return (jint)r;
}

JNIEXPORT jint JNICALL Java_org_minibae_Sound__1stopSound
	(JNIEnv* env, jobject jsound, jlong soundReference, jboolean deleteSound)
{
	BAESound sound = (BAESound)(intptr_t)soundReference;
	if(!sound) return (jint)BAE_PARAM_ERR;
	__android_log_print(ANDROID_LOG_DEBUG, "miniBAE", "_stopSound sound=%p deleteSound=%d", (void*)(intptr_t)soundReference, deleteSound);
	
	BAEResult r = BAESound_Stop(sound, FALSE);
	if(deleteSound && r == BAE_NO_ERROR) {
		BAESound_Delete(sound);
	}
	__android_log_print(ANDROID_LOG_DEBUG, "miniBAE", "BAESound_Stop returned %d", r);
	return (jint)r;
}

JNIEXPORT jint JNICALL Java_org_minibae_Sound__1pauseSound
	(JNIEnv* env, jobject jsound, jlong soundReference)
{
	BAESound sound = (BAESound)(intptr_t)soundReference;
	if(!sound) return (jint)BAE_PARAM_ERR;
	__android_log_print(ANDROID_LOG_DEBUG, "miniBAE", "_pauseSound sound=%p", (void*)(intptr_t)soundReference);
	
	BAEResult r = BAESound_Pause(sound);
	__android_log_print(ANDROID_LOG_DEBUG, "miniBAE", "BAESound_Pause returned %d", r);
	return (jint)r;
}

JNIEXPORT jint JNICALL Java_org_minibae_Sound__1resumeSound
	(JNIEnv* env, jobject jsound, jlong soundReference)
{
	BAESound sound = (BAESound)(intptr_t)soundReference;
	if(!sound) return (jint)BAE_PARAM_ERR;
	__android_log_print(ANDROID_LOG_DEBUG, "miniBAE", "_resumeSound sound=%p", (void*)(intptr_t)soundReference);
	
	BAEResult r = BAESound_Resume(sound);
	__android_log_print(ANDROID_LOG_DEBUG, "miniBAE", "BAESound_Resume returned %d", r);
	return (jint)r;
}

JNIEXPORT jboolean JNICALL Java_org_minibae_Sound__1isSoundPaused
	(JNIEnv* env, jobject jsound, jlong soundReference)
{
	BAESound sound = (BAESound)(intptr_t)soundReference;
	if(!sound) return JNI_FALSE;
	
	BAE_BOOL paused = FALSE;
	BAEResult r = BAESound_IsPaused(sound, &paused);
	if(r != BAE_NO_ERROR) {
		__android_log_print(ANDROID_LOG_DEBUG, "miniBAE", "_isSoundPaused error %d", r);
		return JNI_FALSE;
	}
	return paused ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL Java_org_minibae_Sound__1isSoundDone
	(JNIEnv* env, jobject jsound, jlong soundReference)
{
	BAESound sound = (BAESound)(intptr_t)soundReference;
	if(!sound) return JNI_TRUE;
	
	BAE_BOOL done = FALSE;
	BAEResult r = BAESound_IsDone(sound, &done);
	if(r != BAE_NO_ERROR) {
		__android_log_print(ANDROID_LOG_DEBUG, "miniBAE", "_isSoundDone error %d", r);
		return JNI_TRUE;
	}
	return done ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jint JNICALL Java_org_minibae_Sound__1setSoundVolume
	(JNIEnv* env, jclass clazz, jlong soundReference, jint fixedVolume)
{
	BAESound sound = (BAESound)(intptr_t)soundReference;
	if(!sound) return (jint)BAE_PARAM_ERR;
	BAEResult r = BAESound_SetVolume(sound, (BAE_UNSIGNED_FIXED)fixedVolume);
	return (jint)r;
}

JNIEXPORT jint JNICALL Java_org_minibae_Sound__1getSoundVolume
	(JNIEnv* env, jclass clazz, jlong soundReference)
{
	BAESound sound = (BAESound)(intptr_t)soundReference;
	if(!sound) return 0;
	BAE_UNSIGNED_FIXED v = 0;
	BAEResult r = BAESound_GetVolume(sound, &v);
	if(r == BAE_NO_ERROR){ return (jint)v; }
	return 0;
}

JNIEXPORT jint JNICALL Java_org_minibae_Sound__1getSoundPositionFrames
	(JNIEnv* env, jclass clazz, jlong soundReference)
{
	BAESound sound = (BAESound)(intptr_t)soundReference;
	if(!sound) return 0;
	uint32_t pos = 0;
	if(BAESound_GetSamplePlaybackPosition(sound, &pos) == BAE_NO_ERROR){ return (jint)pos; }
	return 0;
}

JNIEXPORT jint JNICALL Java_org_minibae_Sound__1getSoundLengthFrames
	(JNIEnv* env, jclass clazz, jlong soundReference)
{
	BAESound sound = (BAESound)(intptr_t)soundReference;
	if(!sound) return 0;
	uint32_t length = 0;
	BAESound_GetSamplePlaybackPointer(sound, &length);
	return (jint)length;
}

JNIEXPORT jint JNICALL Java_org_minibae_Sound__1getSoundSampleRate
	(JNIEnv* env, jclass clazz, jlong soundReference)
{
	// BAESound structure definition (from MiniBAE.c line 631)
	struct sBAESound_internal {
		int32_t mID;
		void* mixer; // BAEMixer
		GM_Waveform *pWave;
		// ... rest of structure not needed
	};
	
	struct sBAESound_internal* sound = (struct sBAESound_internal*)(intptr_t)soundReference;
	if(!sound || !sound->pWave) return 44100;
	
	// sampledRate is in 16.16 fixed point format (XFIXED)
	// Convert to integer by right-shifting 16 bits
	return (jint)(sound->pWave->sampledRate >> 16);
}

JNIEXPORT jint JNICALL Java_org_minibae_Sound__1setSoundPositionFrames
	(JNIEnv* env, jclass clazz, jlong soundReference, jint sampleFrames)
{
	BAESound sound = (BAESound)(intptr_t)soundReference;
	if(!sound) return (jint)BAE_PARAM_ERR;
	if(sampleFrames < 0) sampleFrames = 0;
	BAEResult r = BAESound_SetSamplePlaybackPosition(sound, (uint32_t)sampleFrames);
	if(r != BAE_NO_ERROR){ __android_log_print(ANDROID_LOG_WARN, "miniBAE", "BAESound_SetSamplePlaybackPosition(%d) err=%d", (int)sampleFrames, r); }
	return (jint)r;
}

JNIEXPORT jint JNICALL Java_org_minibae_Sound__1setSoundLoops
	(JNIEnv* env, jclass clazz, jlong soundReference, jint loopCount)
{
	BAESound sound = (BAESound)(intptr_t)soundReference;
	if(!sound) return (jint)BAE_PARAM_ERR;
	uint32_t loops = (uint32_t)loopCount;
	BAEResult r = BAESound_SetLoopCount(sound, loops);
	__android_log_print(ANDROID_LOG_DEBUG, "miniBAE", "BAESound_SetLoopCount(%u) err=%d", (unsigned)loops, r);
	if(r != BAE_NO_ERROR){ __android_log_print(ANDROID_LOG_WARN, "miniBAE", "BAESound_SetLoopCount(%u) err=%d", (unsigned)loops, r); }
	return (jint)r;
}

// Helper to get mixer from sound - forward declaration (if not present in header)
// Implement BAESound_GetMixer wrapper if missing

// SONG JNI bindings
JNIEXPORT jlong JNICALL Java_org_minibae_Song__1newNativeSong
	(JNIEnv* env, jobject jsong, jlong mixerReference)
{
		BAEMixer mixer = (BAEMixer)(intptr_t)mixerReference;
		BAESong song = NULL;
		if(mixer){ song = BAESong_New(mixer); }
		return (jlong)(intptr_t)song;
}

JNIEXPORT jint JNICALL Java_org_minibae_Song__1loadSong
	(JNIEnv* env, jobject jsong, jlong songReference, jstring path)
{
		if(!path) return (jint)BAE_PARAM_ERR;
		const char *cpath = (*env)->GetStringUTFChars(env, path, NULL);
		if(!cpath) return (jint)BAE_PARAM_ERR;
		__android_log_print(ANDROID_LOG_DEBUG, "miniBAE", "_loadSong path=%s song=%p", cpath, (void*)(intptr_t)songReference);
		BAESong song = (BAESong)(intptr_t)songReference;
		if(!song){ __android_log_print(ANDROID_LOG_ERROR, "miniBAE", "_loadSong: invalid song handle"); (*env)->ReleaseStringUTFChars(env, path, cpath); return (jint)BAE_PARAM_ERR; }
	BAEResult r = BAE_BAD_FILE;
	const char *ext = strrchr(cpath, '.');
	if(ext && (strcasecmp(ext, ".rmf") == 0)){
		// Try multiple RMF song indices (0..7)
		const int kMaxSongIndexProbe = 8;
		for(int idx = 0; idx < kMaxSongIndexProbe; ++idx){
			BAEResult tr = BAESong_LoadRmfFromFile(song, (BAEPathName)cpath, (int16_t)idx, TRUE);
			__android_log_print(ANDROID_LOG_DEBUG, "miniBAE", "BAESong_LoadRmfFromFile(index=%d) returned %d", idx, tr);
			if(tr == BAE_NO_ERROR){ r = tr; break; }
			if(tr != BAE_RESOURCE_NOT_FOUND){ r = tr; break; }
			if(idx == kMaxSongIndexProbe - 1){ r = tr; }
		}
	} else {
		r = BAESong_LoadMidiFromFile(song, (BAEPathName)cpath, TRUE);
		__android_log_print(ANDROID_LOG_DEBUG, "miniBAE", "BAESong_LoadMidiFromFile returned %d", r);
	}
	(*env)->ReleaseStringUTFChars(env, path, cpath);
	return (jint)r;
}

JNIEXPORT jint JNICALL Java_org_minibae_Song__1loadSongFromMemory
	(JNIEnv* env, jobject jsong, jlong songReference, jbyteArray data)
{
	if(!data) return (jint)BAE_PARAM_ERR;
	BAESong song = (BAESong)(intptr_t)songReference;
	if(!song) return (jint)BAE_PARAM_ERR;

	jsize len = (*env)->GetArrayLength(env, data);
	jbyte *bytes = (*env)->GetByteArrayElements(env, data, NULL);
	if(!bytes) return (jint)BAE_MEMORY_ERR;

	__android_log_print(ANDROID_LOG_DEBUG, "miniBAE", "_loadSongFromMemory song=%p len=%d", (void*)(intptr_t)songReference, (int)len);

	// RMF header probe (first 12 bytes: 'IREZ' mapID, version, totalResources). Always log to help diagnose 10030.
	if(len >= 12){
		unsigned char *ub = (unsigned char*)bytes;
		uint32_t be_mapID = (uint32_t)ub[0]<<24 | (uint32_t)ub[1]<<16 | (uint32_t)ub[2]<<8 | (uint32_t)ub[3];
		uint32_t be_version = (uint32_t)ub[4]<<24 | (uint32_t)ub[5]<<16 | (uint32_t)ub[6]<<8 | (uint32_t)ub[7];
		uint32_t be_total = (uint32_t)ub[8]<<24 | (uint32_t)ub[9]<<16 | (uint32_t)ub[10]<<8 | (uint32_t)ub[11];
		__android_log_print(ANDROID_LOG_DEBUG, "miniBAE", "RMF probe: raw12=%02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X mapID_be=0x%08X version_be=%u totalResources_be=%u", ub[0],ub[1],ub[2],ub[3],ub[4],ub[5],ub[6],ub[7],ub[8],ub[9],ub[10],ub[11], be_mapID, be_version, be_total);
		// If bytes look like 'IREZ' in ASCII order (big-endian), mapID should be 0x4952455A
		if(be_mapID == 0x4952455A){
			__android_log_print(ANDROID_LOG_DEBUG, "miniBAE", "RMF probe: detected IREZ header (potential RMF resource file)");
			// Enumerate resource entries for diagnostics (types, ids) if totalResources reasonable
			if(be_total > 0 && be_total < 256){
				uint32_t nextOffset = 12; // sizeof map
				for(uint32_t resIndex=0; resIndex < be_total; ++resIndex){
					if(nextOffset + 4*3 + 1 + 4 > (uint32_t)len){ // need at least minimal header
						__android_log_print(ANDROID_LOG_DEBUG, "miniBAE", "RMF enumerate: truncated before resource %u", resIndex);
						break;
					}
					unsigned char *base = ub + nextOffset;
					uint32_t rawNext = (uint32_t)base[0]<<24 | (uint32_t)base[1]<<16 | (uint32_t)base[2]<<8 | (uint32_t)base[3];
					uint32_t rawType = (uint32_t)base[4]<<24 | (uint32_t)base[5]<<16 | (uint32_t)base[6]<<8 | (uint32_t)base[7];
					uint32_t rawID   = (uint32_t)base[8]<<24 | (uint32_t)base[9]<<16 | (uint32_t)base[10]<<8 | (uint32_t)base[11];
					uint8_t nameLen = base[12];
					uint32_t minEntrySize = 4/*next*/+4/*type*/+4/*id*/+1/*len*/+nameLen+4/*resLen*/;
					if(nextOffset + minEntrySize > (uint32_t)len){
						__android_log_print(ANDROID_LOG_DEBUG, "miniBAE", "RMF enumerate: resource %u truncated (need %u bytes)", resIndex, (unsigned)minEntrySize);
						break;
					}
					uint32_t rawResLenOffset = nextOffset + 4+4+4+1+nameLen; // position of length field
					unsigned char *lenPtr = ub + rawResLenOffset;
					uint32_t rawResLen = (uint32_t)lenPtr[0]<<24 | (uint32_t)lenPtr[1]<<16 | (uint32_t)lenPtr[2]<<8 | (uint32_t)lenPtr[3];
					char typeStr[5]; typeStr[0]=(char)(rawType>>24); typeStr[1]=(char)(rawType>>16); typeStr[2]=(char)(rawType>>8); typeStr[3]=(char)(rawType); typeStr[4]='\0';
					char nameBuf[64];
					if(nameLen){
						uint32_t copyLen = (nameLen < sizeof(nameBuf)-1)? nameLen : (uint32_t)(sizeof(nameBuf)-1);
						memcpy(nameBuf, base+13, copyLen); nameBuf[copyLen]='\0';
					}else{ nameBuf[0]='\0'; }
					__android_log_print(ANDROID_LOG_DEBUG, "miniBAE", "RMF resource[%u]: type='%s' (0x%08X) id=%u nameLen=%u name='%s' dataLen=%u next=0x%08X", resIndex, typeStr, rawType, rawID, nameLen, nameBuf, rawResLen, rawNext);
					// compute start of data after len field
					uint32_t dataStart = rawResLenOffset + 4;
					if(rawResLen && dataStart + rawResLen <= (uint32_t)len){
						// log first up to 8 bytes hex for SONG or other interesting types
						if(rawType == 0x534F4E47 /*'SONG'*/){
							char hexPreview[3*8+1]; hexPreview[0]='\0';
							uint32_t preview = (rawResLen < 8)? rawResLen : 8;
							for(uint32_t pi=0; pi<preview; ++pi){
								char tmp[4]; snprintf(tmp, sizeof(tmp), "%02X ", ub[dataStart+pi]); strncat(hexPreview, tmp, sizeof(hexPreview)-strlen(hexPreview)-1);
							}
							__android_log_print(ANDROID_LOG_DEBUG, "miniBAE", "RMF resource[%u] SONG first bytes: %s", resIndex, hexPreview);
						}
					}
					if(rawNext == 0 || rawNext >= (uint32_t)len){
						// if rawNext seems invalid, infer next by skipping this entry
						rawNext = dataStart + rawResLen;
					}
					nextOffset = rawNext;
					if(nextOffset <= 12){
						__android_log_print(ANDROID_LOG_DEBUG, "miniBAE", "RMF enumerate: nextOffset stuck (%u), abort", nextOffset);
						break;
					}
					if(nextOffset >= (uint32_t)len){
						__android_log_print(ANDROID_LOG_DEBUG, "miniBAE", "RMF enumerate: nextOffset past end (%u >= %d)", nextOffset, (int)len);
						break;
					}
				}
			}
		}
	}

	BAEResult r;
	BAE_BOOL headerIsRMF = FALSE;
	if(len >= 4){
		unsigned char *ub = (unsigned char*)bytes;
		uint32_t be_mapID = (uint32_t)ub[0]<<24 | (uint32_t)ub[1]<<16 | (uint32_t)ub[2]<<8 | (uint32_t)ub[3];
		if(be_mapID == 0x4952455A){ headerIsRMF = TRUE; }
	}
	if(!headerIsRMF){
		// Try loading as MIDI first if not clearly RMF
		r = BAESong_LoadMidiFromMemory(song, (void const*)bytes, (uint32_t)len, TRUE);
		__android_log_print(ANDROID_LOG_DEBUG, "miniBAE", "BAESong_LoadMidiFromMemory returned %d", r);
		if(r == BAE_NO_ERROR){
			// Done
			(*env)->ReleaseByteArrayElements(env, data, bytes, JNI_ABORT);
			return (jint)r;
		}
	}
	// Either detected RMF header or MIDI load failed; attempt RMF indices.
	const int kMaxSongIndexProbe = 4; // only need small probe; SONG index is relative to SONG entries
	BAEResult lastErr = BAE_RESOURCE_NOT_FOUND;
	for(int idx = 0; idx < kMaxSongIndexProbe; ++idx){
		BAEResult tr = BAESong_LoadRmfFromMemory(song, (void const*)bytes, (uint32_t)len, (int16_t)idx, TRUE);
		__android_log_print(ANDROID_LOG_DEBUG, "miniBAE", "BAESong_LoadRmfFromMemory(index=%d) returned %d", idx, tr);
		if(tr == BAE_NO_ERROR){ r = tr; goto doneLoad; }
		if(tr != BAE_RESOURCE_NOT_FOUND){ lastErr = tr; break; }
		lastErr = tr; // keep last
	}
	r = lastErr;
doneLoad:

	(*env)->ReleaseByteArrayElements(env, data, bytes, JNI_ABORT);
	return (jint)r;
}

JNIEXPORT jboolean JNICALL Java_org_minibae_Song__1hasEmbeddedBank
	(JNIEnv* env, jobject jsong, jlong songReference)
{
	BAESong song = (BAESong)(intptr_t)songReference;
	if(!song) return JNI_FALSE;
	return (jboolean)BAESong_HasEmbeddedBank(song);
}

JNIEXPORT jint JNICALL Java_org_minibae_Song__1prerollSong
	(JNIEnv* env, jobject jsong, jlong songReference)
{
	BAESong song = (BAESong)(intptr_t)songReference;
	if(!song) return (jint)BAE_PARAM_ERR;
	__android_log_print(ANDROID_LOG_DEBUG, "miniBAE", "_prerollSong song=%p", (void*)(intptr_t)songReference);
	BAEResult r = BAESong_Preroll(song);
	__android_log_print(ANDROID_LOG_DEBUG, "miniBAE", "BAESong_Preroll returned %d", r);
	return (jint)r;
}

JNIEXPORT jint JNICALL Java_org_minibae_Song__1startSong
	(JNIEnv* env, jobject jsong, jlong songReference)
{
	BAESong song = (BAESong)(intptr_t)songReference;
	if(!song) return (jint)BAE_PARAM_ERR;
	// ensure song volume applied
	BAE_UNSIGNED_FIXED cur;
	if(BAESong_GetVolume(song, &cur) == BAE_NO_ERROR){ BAESong_SetVolume(song, cur); }
	__android_log_print(ANDROID_LOG_DEBUG, "miniBAE", "_startSong song=%p", (void*)(intptr_t)songReference);
	BAESong_SetMicrosecondPosition(song, 0);
	BAESong_Preroll(song);
	BAESong_SetMicrosecondPosition(song, 0);
	BAEResult r = BAESong_Start(song, 0);
	__android_log_print(ANDROID_LOG_DEBUG, "miniBAE", "BAESong_Start returned %d", r);
	return (jint)r;
}

JNIEXPORT void JNICALL Java_org_minibae_Song__1stopSong
	(JNIEnv* env, jobject jsong, jlong songReference, jboolean deleteSong)
{
		BAESong song = (BAESong)(intptr_t)songReference;
		if(song){ BAESong_Stop(song, FALSE); if(deleteSong) { BAESong_Delete(song); } }
}

JNIEXPORT jint JNICALL Java_org_minibae_Song__1loadRmiFromMemory
	(JNIEnv* env, jobject jsong, jlong songReference, jbyteArray data, jboolean useEmbeddedBank)
{
	BAESong song = (BAESong)(intptr_t)songReference;
	if(!song || !data) return (jint)BAE_PARAM_ERR;
	
	jsize len = (*env)->GetArrayLength(env, data);
	jbyte* bytes = (*env)->GetByteArrayElements(env, data, NULL);
	if(!bytes) return (jint)BAE_MEMORY_ERR;
	
	__android_log_print(ANDROID_LOG_DEBUG, "miniBAE", "Loading RMI from memory, size=%d, useEmbeddedBank=%d", len, useEmbeddedBank);
	
#if USE_SF2_SUPPORT == TRUE && _USING_FLUIDSYNTH == TRUE
	BAEResult r = BAESong_LoadRmiFromMemory(song, (void const*)bytes, (uint32_t)len, TRUE, (BAE_BOOL)useEmbeddedBank);
	__android_log_print(ANDROID_LOG_DEBUG, "miniBAE", "BAESong_LoadRmiFromMemory returned %d", r);
#else
	__android_log_print(ANDROID_LOG_ERROR, "miniBAE", "RMI loading not supported (FluidSynth required)");
	BAEResult r = BAE_NOT_SETUP;
#endif
	
	(*env)->ReleaseByteArrayElements(env, data, bytes, JNI_ABORT);
	return (jint)r;
}

JNIEXPORT jint JNICALL Java_org_minibae_Song__1setSongVolume
	(JNIEnv* env, jclass clazz, jlong songReference, jint fixedVolume)
{
	BAESong song = (BAESong)(intptr_t)songReference;
	if(!song) return (jint)BAE_PARAM_ERR;
	BAEResult r = BAESong_SetVolume(song, (BAE_UNSIGNED_FIXED)fixedVolume);
	return (jint)r;
}

JNIEXPORT jint JNICALL Java_org_minibae_Song__1getSongVolume
	(JNIEnv* env, jclass clazz, jlong songReference)
{
	BAESong song = (BAESong)(intptr_t)songReference;
	if(!song) return 0;
	BAE_UNSIGNED_FIXED v = 0;
	if(BAESong_GetVolume(song, &v) == BAE_NO_ERROR){ return (jint)v; }
	return 0;
}

// Export functionality
JNIEXPORT jint JNICALL Java_org_minibae_Mixer__1startOutputToFile
	(JNIEnv* env, jclass clazz, jlong mixerReference, jstring filePath, jint outputType, jint compressionType)
{
	BAEMixer mixer = (BAEMixer)(intptr_t)mixerReference;
	if(!mixer || !filePath) return (jint)BAE_PARAM_ERR;
	
	const char* path = (*env)->GetStringUTFChars(env, filePath, NULL);
	if(!path) return (jint)BAE_MEMORY_ERR;
	
	__android_log_print(ANDROID_LOG_DEBUG, "miniBAE", "Starting output to file: %s, type: %d, compression: %d", path, outputType, compressionType);
	
	BAEResult result = BAEMixer_StartOutputToFile(mixer, (BAEPathName)path, (BAEFileType)outputType, (BAECompressionType)compressionType);
	
	(*env)->ReleaseStringUTFChars(env, filePath, path);
	
	__android_log_print(ANDROID_LOG_DEBUG, "miniBAE", "BAEMixer_StartOutputToFile returned %d", result);
	return (jint)result;
}

JNIEXPORT jint JNICALL Java_org_minibae_Mixer__1serviceOutputToFile
	(JNIEnv* env, jclass clazz, jlong mixerReference)
{
	BAEMixer mixer = (BAEMixer)(intptr_t)mixerReference;
	if(!mixer) return (jint)BAE_PARAM_ERR;

	BAEResult result = BAEMixer_ServiceAudioOutputToFile(mixer);

	return (jint)result;
}

JNIEXPORT jint JNICALL Java_org_minibae_Mixer__1stopOutputToFile
	(JNIEnv* env, jclass clazz, jlong mixerReference)
{
	BAEMixer mixer = (BAEMixer)(intptr_t)mixerReference;
	if(!mixer) return (jint)BAE_PARAM_ERR;
	
	__android_log_print(ANDROID_LOG_DEBUG, "miniBAE", "Stopping output to file");
	
	BAEMixer_StopOutputToFile();
	
	__android_log_print(ANDROID_LOG_DEBUG, "miniBAE", "BAEMixer_StopOutputToFile completed");
	return (jint)BAE_NO_ERROR;
}

JNIEXPORT jint JNICALL Java_org_minibae_Mixer__1determineFileTypeByData
	(JNIEnv* env, jclass clazz, jbyteArray data, jint length)
{
	if(!data || length <= 0) return (jint)BAE_INVALID_TYPE;
	
	jsize arrayLen = (*env)->GetArrayLength(env, data);
	if(length > arrayLen) length = arrayLen;
	
	jbyte* bytes = (*env)->GetByteArrayElements(env, data, NULL);
	if(!bytes) return (jint)BAE_INVALID_TYPE;
	
	BAEFileType fileType = X_DetermineFileTypeByData((const unsigned char*)bytes, (int32_t)length);
	
	(*env)->ReleaseByteArrayElements(env, data, bytes, JNI_ABORT);
	
	__android_log_print(ANDROID_LOG_DEBUG, "miniBAE", "X_DetermineFileTypeByData returned %d", fileType);
	return (jint)fileType;
}

JNIEXPORT jint JNICALL Java_org_minibae_Mixer__1loadFromMemory
	(JNIEnv* env, jclass clazz, jlong mixerReference, jbyteArray data, jobject resultObj)
{
	BAEMixer mixer = (BAEMixer)(intptr_t)mixerReference;
	if(!mixer || !data || !resultObj) return (jint)BAE_PARAM_ERR;

#if USE_SF2_SUPPORT == TRUE && _USING_FLUIDSYNTH == TRUE && USE_XMF_SUPPORT == TRUE
	// If the previous song loaded an XMF embedded bank as a FluidSynth overlay,
	// make sure it is unloaded before loading the next file. We keep the mixer
	// alive for performance (bank caching), so we must explicitly clear overlays.
	if (GM_SF2_HasXmfEmbeddedBank())
	{
		__android_log_print(ANDROID_LOG_DEBUG, "miniBAE", "Clearing prior XMF overlay soundfont before LoadFromMemory");
		GM_UnloadXMFOverlaySoundFont();
	}
#endif
	
	jsize len = (*env)->GetArrayLength(env, data);
	jbyte* bytes = (*env)->GetByteArrayElements(env, data, NULL);
	if(!bytes) return (jint)BAE_MEMORY_ERR;
	
	BAELoadResult result = {0};
	BAEResult r = BAEMixer_LoadFromMemory(mixer, (void const*)bytes, (uint32_t)len, &result);
	
	(*env)->ReleaseByteArrayElements(env, data, bytes, JNI_ABORT);
	
	if(r == BAE_NO_ERROR)
	{
		// Get the LoadResult class and set its fields
		jclass resultClass = (*env)->GetObjectClass(env, resultObj);
		
		// Set type field
		jfieldID typeField = (*env)->GetFieldID(env, resultClass, "type", "I");
		(*env)->SetIntField(env, resultObj, typeField, (jint)result.type);
		
		// Set fileType field
		jfieldID fileTypeField = (*env)->GetFieldID(env, resultClass, "fileType", "I");
		(*env)->SetIntField(env, resultObj, fileTypeField, (jint)result.fileType);
		
		// Set result field
		jfieldID resultField = (*env)->GetFieldID(env, resultClass, "result", "I");
		(*env)->SetIntField(env, resultObj, resultField, (jint)result.result);
		
		// Set the appropriate object reference
		if(result.type == BAE_LOAD_TYPE_SONG && result.data.song)
		{
			jfieldID songField = (*env)->GetFieldID(env, resultClass, "songReference", "J");
			(*env)->SetLongField(env, resultObj, songField, (jlong)(intptr_t)result.data.song);
		}
		else if(result.type == BAE_LOAD_TYPE_SOUND && result.data.sound)
		{
			jfieldID soundField = (*env)->GetFieldID(env, resultClass, "soundReference", "J");
			(*env)->SetLongField(env, resultObj, soundField, (jlong)(intptr_t)result.data.sound);
		}
		
		__android_log_print(ANDROID_LOG_DEBUG, "miniBAE", "BAEMixer_LoadFromMemory succeeded: type=%d, fileType=%d", result.type, result.fileType);
	}
	else
	{
		__android_log_print(ANDROID_LOG_ERROR, "miniBAE", "BAEMixer_LoadFromMemory failed: %d", r);
	}
	
	return (jint)r;
}
