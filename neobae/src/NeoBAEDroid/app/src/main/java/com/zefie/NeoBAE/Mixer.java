package com.zefie.NeoBAE;

import android.content.res.AssetManager;
import android.content.res.Resources;

public class Mixer
{
	AssetManager mAssetManager;
	long mReference;

    private static Mixer mMixer;

	static
	{
		System.loadLibrary("NeoBAE");
	}

	private static native long _newMixer();
	private static native void _deleteMixer(long reference);
	private static native int _openMixer(long reference, int sampleRate, int terpMode, int maxSongVoices, int maxSoundVoices, int mixLevel);
	private static native int _disengageAudio(long reference);
	private static native int _reengageAudio(long reference);
	private static native int _isAudioEngaged(long reference);
	private static native int _isAudioTailActive(long reference);
	
	// keep static constructor private.
	private Mixer(AssetManager assetManager)
	{
	mAssetManager = assetManager;
	mReference = _newMixer();
	}
	
	public static int create(AssetManager assetManager, int sampleRate, int terpMode, int maxSongVoices, int maxSoundVoices, int mixLevel)
	{
        int status = 0;

		mMixer = new Mixer(assetManager);
		if (mMixer.mReference != 0L)
		{
			status = _openMixer(mMixer.mReference, sampleRate, terpMode, maxSongVoices, maxSoundVoices, mixLevel);
		}
		return status;
    }

	public static void delete()
	{
        if (mMixer != null && mMixer.mReference != 0L)
		{
			_deleteMixer(mMixer.mReference);
			mMixer.mReference = 0L;
			mMixer = null;
		}
	}

	public static boolean exists() {
		return (mMixer != null && mMixer.mReference != 0L);
	}

	// Suspend/resume the hardware audio output without destroying the mixer.
	// This keeps the loaded bank(s) resident while stopping the audio thread.
	public static int disengageAudio() {
		if (mMixer == null || mMixer.mReference == 0L) return -1;
		return _disengageAudio(mMixer.mReference);
	}

	public static int reengageAudio() {
		if (mMixer == null || mMixer.mReference == 0L) return -1;
		return _reengageAudio(mMixer.mReference);
	}

	public static boolean isAudioEngaged() {
		if (mMixer == null || mMixer.mReference == 0L) return false;
		return _isAudioEngaged(mMixer.mReference) != 0;
	}

	public static boolean isAudioTailActive() {
		if (mMixer == null || mMixer.mReference == 0L) return false;
		return _isAudioTailActive(mMixer.mReference) != 0;
	}

	public static Sound create()
	{
		Sound snd = null;
		if (mMixer.mReference != 0L)
		{
			snd = new Sound(mMixer);
		}
		return snd;
	}    

	// Create a Song wrapper associated with the current Mixer
	public static Song createSong()
	{
		Song s = null;
		if (mMixer != null && mMixer.mReference != 0L)
		{
			s = new Song(mMixer);
		}
		return s;
	}

	// Settings & utility JNI methods
	private static native int _setDefaultReverb(long reference, int reverbType);
	private static native int _getActiveVoiceCount(long reference);
	// EQ JNI methods
	private static native void _setEQEnabled(long reference, boolean enabled);
	private static native boolean _getEQEnabled(long reference);
	private static native void _setEQGain(long reference, int bandIndex, float gain);
	private static native float _getEQGain(long reference, int bandIndex);
	// Custom Neo reverb parameter JNI methods
	private static native void _setNeoCustomReverbCombCount(long reference, int combCount);
	private static native int _getNeoCustomReverbCombCount(long reference);
	private static native void _setNeoCustomReverbCombDelay(long reference, int combIndex, int delayMs);
	private static native int _getNeoCustomReverbCombDelay(long reference, int combIndex);
	private static native void _setNeoCustomReverbCombFeedback(long reference, int combIndex, int feedback);
	private static native int _getNeoCustomReverbCombFeedback(long reference, int combIndex);
	private static native void _setNeoCustomReverbCombGain(long reference, int combIndex, int gain);
	private static native int _getNeoCustomReverbCombGain(long reference, int combIndex);
	private static native void _getNeoReverbPresetParams(long reference, int reverbType, int[] combCount, int[] delaysMs, int[] feedback, int[] gain, int[] lowpass, int[] mix);
	private static native void _setNeoCustomReverbLowpass(long reference, int lowpass);
	private static native void _setNeoReverbMix(long reference, int wetLevel);
	private static native int _getNeoReverbMix(long reference);
	private static native int _addBankFromFile(long reference, String path);
	private static native int _loadBuiltinBank(long reference);
	private static native int _addBankFromMemory(long reference, byte[] data);
	private static native int _addBankFromMemoryWithFilename(long reference, byte[] data, String filename);
	private static native void _setNativeCacheDir(String path);
    private static native int _setMasterVolume(long reference, int fixedVolume);
    private static native int _setGlobalVolume(long reference, int fixedVolume);
    private static native int _getGlobalVolume(long reference);
	private static native String _getBankFriendlyName(long reference);
	private static native String _getVersion();
	private static native String _getCompileInfo();
	private static native String _getFeatureString();
	private static native int _setSpanDCFix(boolean enable);
	private static native boolean _getSpanDCFix();
	private static native int _setClassicChorus(boolean enable);
	private static native boolean _getClassicChorus();
	private static native int _setWebTVMode(boolean enable);
	private static native boolean _getWebTVMode();
	private static native int _setSongNormalizeGain(long reference, int gainPct);
	private static native int _setDLSCompatibilityMode(boolean enable);
	private static native boolean _getDLSCompatibilityMode();
	private static native boolean _hasEggsDLSBank(long reference);
	private static native boolean _hasMobileBAEDLSBank(long reference);
	private static native boolean _hasMobileBAEMainBank(long reference);
	private static native boolean _hasXMFDLSOverlayBank(long reference);
	private static native int _getDLSBankLevel(long reference);
	private static native int _determineFileTypeByData(byte[] data, int length);
	private static native int _loadFromMemory(long mixerReference, byte[] data, LoadResult result);
	
	// Export functionality
	private static native int _makeCurrent(long reference);
	private static native int _startOutputToFile(long reference, String filePath, int outputType, int compressionType);
	private static native int _serviceOutputToFile(long reference);
	private static native int _stopOutputToFile(long reference);

	public static int setDefaultReverb(int reverbType){ if(mMixer==null) return -1; return _setDefaultReverb(mMixer.mReference, reverbType); }
	public static int getActiveVoiceCount(){ if(mMixer==null) return 0; return _getActiveVoiceCount(mMixer.mReference); }

	// EQ Helpers
	public static void setEQEnabled(boolean enabled){ if(mMixer==null) return; _setEQEnabled(mMixer.mReference, enabled); }
	public static boolean getEQEnabled(){ if(mMixer==null) return false; return _getEQEnabled(mMixer.mReference); }
	public static void setEQGain(int bandIndex, float gain){ if(mMixer==null) return; _setEQGain(mMixer.mReference, bandIndex, gain); }
	public static float getEQGain(int bandIndex){ if(mMixer==null) return 0.0f; return _getEQGain(mMixer.mReference, bandIndex); }

	// Custom Neo reverb parameter helpers
	public static void setNeoCustomReverbCombCount(int combCount){ if(mMixer==null) return; _setNeoCustomReverbCombCount(mMixer.mReference, combCount); }
	public static int getNeoCustomReverbCombCount(){ if(mMixer==null) return 0; return _getNeoCustomReverbCombCount(mMixer.mReference); }
	public static void setNeoCustomReverbCombDelay(int combIndex, int delayMs){ if(mMixer==null) return; _setNeoCustomReverbCombDelay(mMixer.mReference, combIndex, delayMs); }
	public static int getNeoCustomReverbCombDelay(int combIndex){ if(mMixer==null) return 0; return _getNeoCustomReverbCombDelay(mMixer.mReference, combIndex); }
	public static void setNeoCustomReverbCombFeedback(int combIndex, int feedback){ if(mMixer==null) return; _setNeoCustomReverbCombFeedback(mMixer.mReference, combIndex, feedback); }
	public static int getNeoCustomReverbCombFeedback(int combIndex){ if(mMixer==null) return 0; return _getNeoCustomReverbCombFeedback(mMixer.mReference, combIndex); }
	public static void setNeoCustomReverbCombGain(int combIndex, int gain){ if(mMixer==null) return; _setNeoCustomReverbCombGain(mMixer.mReference, combIndex, gain); }
	public static int getNeoCustomReverbCombGain(int combIndex){ if(mMixer==null) return 0; return _getNeoCustomReverbCombGain(mMixer.mReference, combIndex); }
	public static void setNeoCustomReverbLowpass(int lowpass){ if(mMixer==null) return; _setNeoCustomReverbLowpass(mMixer.mReference, lowpass); }
	public static void setNeoReverbMix(int wetLevel){ if(mMixer==null) return; _setNeoReverbMix(mMixer.mReference, wetLevel); }
	public static int getNeoReverbMix(){ if(mMixer==null) return 255; return _getNeoReverbMix(mMixer.mReference); }
	public static void getNeoReverbPresetParams(int reverbType, int[] combCount, int[] delaysMs, int[] feedback, int[] gain, int[] lowpass, int[] mix){ if(mMixer==null) return; _getNeoReverbPresetParams(mMixer.mReference, reverbType, combCount, delaysMs, feedback, gain, lowpass, mix); }
	public static int addBankFromFile(String path){ if(mMixer==null) return -1; return _addBankFromFile(mMixer.mReference, path); }
	/** Unload other instrument banks and load the compile-time embedded patch bank. */
	public static int loadBuiltinBank(){ if(mMixer==null) return -1; return _loadBuiltinBank(mMixer.mReference); }
	public static int addBankFromMemory(byte[] data){ if(mMixer==null) return -1; return _addBankFromMemory(mMixer.mReference, data); }
	public static int addBankFromMemory(byte[] data, String filename){ if(mMixer==null) return -1; return _addBankFromMemoryWithFilename(mMixer.mReference, data, filename); }
	public static void setNativeCacheDir(String path){ _setNativeCacheDir(path); }
	public static int setMasterVolumePercent(int percent){
		if(mMixer==null) return -1;
		if(percent<0) percent=0; if(percent>100) percent=100;
		// 16.16 fixed where 1.0 == 65536. Percent needs scaling /100.
		int fixed = (int)( (percent * 65536L) / 100L );
		return _setMasterVolume(mMixer.mReference, fixed);
	}

	public static int setGlobalVolumePercent(int percent){
		if(mMixer==null) return -1;
		if(percent<0) percent=0; if(percent>100) percent=100;
		// 16.16 fixed where 1.0 == 65536. Percent needs scaling /100.
		int fixed = (int)( (percent * 65536L) / 100L );
		return _setGlobalVolume(mMixer.mReference, fixed);
	}

    // Android-only helper: set master volume directly (16.16 fixed).
	// Unlike setMasterVolumePercent(), this does NOT clamp at 100%.
	public static int setMasterVolumeFixed(int fixedVolume){
		if(mMixer==null) return -1;
		return _setMasterVolume(mMixer.mReference, fixedVolume);
	}

	public static String getBankFriendlyName(){ if(mMixer==null) return null; return _getBankFriendlyName(mMixer.mReference); }
	public static String getVersion(){ return _getVersion(); }
	public static String getCompileInfo(){ return _getCompileInfo(); }
	public static String getFeatureString(){ return _getFeatureString(); }
	public static int setSpanDCFix(boolean enable){ return _setSpanDCFix(enable); }
	public static boolean getSpanDCFix(){ return _getSpanDCFix(); }
	public static int setClassicChorus(boolean enable){ return _setClassicChorus(enable); }
	public static boolean getClassicChorus(){ return _getClassicChorus(); }
	// WebTV Mode: WebTV velocity curve, LFO shapes, default pans, filter glide.
	// Returns BAE_NOT_SETUP when the library was built without USE_WEBTV_MODE.
	// Default pans apply to songs started after enabling (reload/seek to apply now).
	public static int setWebTVMode(boolean enable){ return _setWebTVMode(enable); }
	public static boolean getWebTVMode(){ return _getWebTVMode(); }
	public static int setSongNormalizeGain(int gainPct){
		if(mMixer==null) return -1;
		return _setSongNormalizeGain(mMixer.mReference, gainPct);
	}
	public static int setDLSCompatibilityMode(boolean enable){ return _setDLSCompatibilityMode(enable); }
	public static boolean getDLSCompatibilityMode(){ return _getDLSCompatibilityMode(); }
	/** True if the loaded native DLS bank is a microQ "eggs" (scrambled) bank. */
	public static boolean hasEggsDLSBank(){
		if (mMixer == null || mMixer.mReference == 0L) return false;
		return _hasEggsDLSBank(mMixer.mReference);
	}
	/** True if the loaded native DLS bank is treated as MobileBAE (pgal or bankinfo). */
	public static boolean hasMobileBAEDLSBank(){
		if (mMixer == null || mMixer.mReference == 0L) return false;
		return _hasMobileBAEDLSBank(mMixer.mReference);
	}
	/** True if the main (non-overlay) DLS bank is MobileBAE — for host badges with XMF. */
	public static boolean hasMobileBAEMainBank(){
		if (mMixer == null || mMixer.mReference == 0L) return false;
		return _hasMobileBAEMainBank(mMixer.mReference);
	}
	/** True if an XMF/MXMF DLS overlay bank is loaded. */
	public static boolean hasXMFDLSOverlayBank(){
		if (mMixer == null || mMixer.mReference == 0L) return false;
		return _hasXMFDLSOverlayBank(mMixer.mReference);
	}
	/** 2 for Level-2 DLS, 1 for Level-1, 0 if no native DLS bank. */
	public static int getDLSBankLevel(){
		if (mMixer == null || mMixer.mReference == 0L) return 0;
		return _getDLSBankLevel(mMixer.mReference);
	}
	
	// Determine file type from raw data (returns BAEFileType constant)
	public static int determineFileTypeByData(byte[] data, int length) {
		if (data == null || length <= 0) return BAE_INVALID_TYPE;
		return _determineFileTypeByData(data, length);
	}
	
	// Universal memory loader - automatically detects file type and loads appropriate Song or Sound
	public static int loadFromMemory(byte[] data, LoadResult result) {
		if (mMixer == null || data == null || result == null) return -1;
		result.setMixer(mMixer);
		return _loadFromMemory(mMixer.mReference, data, result);
	}
	
	// Get the global mixer instance
	public static Mixer getMixer() { return mMixer; }

	/** Bind this mixer's GM_Mixer into TLS MusicGlobals for the calling thread.
	 *  Required on export worker threads after the multi-mixer change. */
	public int makeCurrent() {
		if (mReference == 0L) return -1;
		return _makeCurrent(mReference);
	}

	public static int makeCurrentMixer() {
		if (mMixer == null || mMixer.mReference == 0L) return -1;
		return _makeCurrent(mMixer.mReference);
	}
	
	// Export functionality constants (from MiniBAE.h)
	public static final int BAE_INVALID_TYPE = 0;
	public static final int BAE_AIFF_TYPE = 1;
	public static final int BAE_WAVE_TYPE = 2;
	public static final int BAE_MPEG_TYPE = 3;
	public static final int BAE_AU_TYPE = 4;
	public static final int BAE_MIDI_TYPE = 5;
	public static final int BAE_FLAC_TYPE = 6;
	public static final int BAE_VORBIS_TYPE = 7;
	public static final int BAE_OPUS_TYPE = 8;
	public static final int BAE_GROOVOID = 9;
	public static final int BAE_RMF = 10;
	public static final int BAE_XMF = 11;
	public static final int BAE_MTHC = 12;
	public static final int BAE_RMI = 13;
	public static final int BAE_ADP_TYPE = 14;
	public static final int BAE_ADX_TYPE = 15;
	public static final int BAE_QOA_TYPE = 16;
	public static final int BAE_WMA_TYPE = 17;
	public static final int BAE_RINGTONE_IMY = 18;
	public static final int BAE_RINGTONE_RNG = 19;
	public static final int BAE_RINGTONE_RTX = 20;
	public static final int BAE_RAW_PCM = 21;

	public static final int BAE_COMPRESSION_NONE = 0;
	public static final int BAE_COMPRESSION_LOSSLESS = 1;
	public static final int BAE_COMPRESSION_VORBIS_128 = 21;
	
	// Create a new mixer instance (not the singleton)
	public static Mixer createMixer(int sampleRate, int bitDepth, int channels, boolean engageAudio) {
		Mixer mixer = new Mixer(null); // No asset manager needed for export mixers
		if (mixer.mReference != 0L) {
			// For export mixers, use simplified parameters
			int terpMode = 2; // Linear interpolation
			int maxSongVoices = 64;
			int maxSoundVoices = 8;
			int mixLevel = 11; // Default mix level
			
			int status = _openMixer(mixer.mReference, sampleRate, terpMode, maxSongVoices, maxSoundVoices, mixLevel);
			if (status != 0) {
				mixer.close();
				return null;
			}
		}
		return mixer;
	}
	
	// Instance method to create a song for this mixer
	public Song newSong() {
		if (mReference != 0L) {
			return new Song(this);
		}
		return null;
	}
	
	// Instance method to close this mixer
	public void close() {
		if (mReference != 0L) {
			_deleteMixer(mReference);
			mReference = 0L;
		}
	}
	
	// Instance export methods
	public int startOutputToFile(String filePath, int outputType, int compressionType) {
		if (mReference == 0L) return -1;
		return _startOutputToFile(mReference, filePath, outputType, compressionType);
	}
	
	public int serviceOutputToFile() {
		if (mReference == 0L) return -1;
		return _serviceOutputToFile(mReference);
	}
	
	public int stopOutputToFile() {
		if (mReference == 0L) return -1;
		return _stopOutputToFile(mReference);
	}

}
