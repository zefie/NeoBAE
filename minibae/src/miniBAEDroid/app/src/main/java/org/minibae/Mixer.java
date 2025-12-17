package org.minibae;

import android.content.res.AssetManager;
import android.content.res.Resources;

public class Mixer
{
	AssetManager mAssetManager;
	long mReference;

    private static Mixer mMixer;

	static
	{
		System.loadLibrary("miniBAE");
	}

	private static native long _newMixer();
	private static native void _deleteMixer(long reference);
	private static native int _openMixer(long reference, int sampleRate, int terpMode, int maxSongVoices, int maxSoundVoices, int mixLevel);
	
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
	private static native int _addBankFromFile(long reference, String path);
	private static native int _addBankFromAsset(long reference, android.content.res.AssetManager assetManager, String assetName);
	private static native int _addBankFromMemory(long reference, byte[] data);
	private static native int _addBankFromMemoryWithFilename(long reference, byte[] data, String filename);
	private static native int _addBuiltInPatches(long reference);
	private static native void _setNativeCacheDir(String path);
	private static native int _setMasterVolume(long reference, int fixedVolume);
	private static native int _setDefaultVelocityCurve(int curveType);
	private static native String _getBankFriendlyName(long reference);
	private static native String _getVersion();
	private static native String _getCompileInfo();
	private static native String _getFeatureString();
	private static native int _determineFileTypeByData(byte[] data, int length);
	private static native int _loadFromMemory(long mixerReference, byte[] data, LoadResult result);
	
	// Export functionality
	private static native int _startOutputToFile(long reference, String filePath, int outputType, int compressionType);
	private static native int _serviceOutputToFile(long reference);
	private static native int _stopOutputToFile(long reference);

	public static int setDefaultReverb(int reverbType){ if(mMixer==null) return -1; return _setDefaultReverb(mMixer.mReference, reverbType); }
	public static int addBankFromFile(String path){ if(mMixer==null) return -1; return _addBankFromFile(mMixer.mReference, path); }
	public static int addBankFromAsset(String assetName){ if(mMixer==null) return -1; return _addBankFromAsset(mMixer.mReference, mMixer.mAssetManager, assetName); }
	public static int addBankFromMemory(byte[] data){ if(mMixer==null) return -1; return _addBankFromMemory(mMixer.mReference, data); }
	public static int addBankFromMemory(byte[] data, String filename){ if(mMixer==null) return -1; return _addBankFromMemoryWithFilename(mMixer.mReference, data, filename); }
	public static int addBuiltInPatches(){ if(mMixer==null) return -1; return _addBuiltInPatches(mMixer.mReference); }
	public static void setNativeCacheDir(String path){ _setNativeCacheDir(path); }
	public static int setMasterVolumePercent(int percent){
		if(mMixer==null) return -1;
		if(percent<0) percent=0; if(percent>100) percent=100;
		// 16.16 fixed where 1.0 == 65536. Percent needs scaling /100.
		int fixed = (int)( (percent * 65536L) / 100L );
		return _setMasterVolume(mMixer.mReference, fixed);
	}
	public static int setDefaultVelocityCurve(int curveType){ return _setDefaultVelocityCurve(curveType); }
	public static String getBankFriendlyName(){ if(mMixer==null) return null; return _getBankFriendlyName(mMixer.mReference); }
	public static String getVersion(){ return _getVersion(); }
	public static String getCompileInfo(){ return _getCompileInfo(); }
	public static String getFeatureString(){ return _getFeatureString(); }
	
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
	
	// Export functionality constants (from MiniBAE.h)
	public static final int BAE_INVALID_TYPE = 0;
	public static final int BAE_AIFF_TYPE = 1;
	public static final int BAE_WAVE_TYPE = 2;
	public static final int BAE_MPEG_TYPE = 3;
	public static final int BAE_AU_TYPE = 4;
	public static final int BAE_MIDI_TYPE = 5;
	public static final int BAE_FLAC_TYPE = 6;
	public static final int BAE_VORBIS_TYPE = 7;
	public static final int BAE_GROOVOID = 8;
	public static final int BAE_RMF = 9;
	public static final int BAE_XMF = 10;
	public static final int BAE_RMI = 11;
	public static final int BAE_RAW_PCM = 12;

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
