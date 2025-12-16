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
        if (mMixer.mReference != 0L)
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
	private static native int _addBuiltInPatches(long reference);
	private static native void _setNativeCacheDir(String path);
	private static native int _setMasterVolume(long reference, int fixedVolume);
	private static native int _setDefaultVelocityCurve(int curveType);
	private static native String _getBankFriendlyName(long reference);
	private static native String _getVersion();
	private static native String _getCompileInfo();
	private static native String _getFeatureString();

	public static int setDefaultReverb(int reverbType){ if(mMixer==null) return -1; return _setDefaultReverb(mMixer.mReference, reverbType); }
	public static int addBankFromFile(String path){ if(mMixer==null) return -1; return _addBankFromFile(mMixer.mReference, path); }
	public static int addBankFromAsset(String assetName){ if(mMixer==null) return -1; return _addBankFromAsset(mMixer.mReference, mMixer.mAssetManager, assetName); }
	public static int addBankFromMemory(byte[] data){ if(mMixer==null) return -1; return _addBankFromMemory(mMixer.mReference, data); }
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
}
