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
	private static native int _disengageAudio(long reference);
	private static native int _reengageAudio(long reference);
	private static native int _isAudioEngaged(long reference);
	
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

	// Suspend/resume the hardware audio output without destroying the mixer.
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

	// Settings & utility JNI methods
	private static native int _setMasterVolume(long reference, int fixedVolume);
	private static native int _setDefaultReverb(long reference, int reverbType);
	private static native int _setDefaultVelocityCurve(int curveType);
	private static native int _addBankFromFile(long reference, String path);
	private static native int _addBankFromAsset(long reference, android.content.res.AssetManager assetManager, String assetName);
	private static native int _addBankFromMemory(long reference, byte[] data);
	private static native int _addBankFromMemoryWithFilename(long reference, byte[] data, String filename);
	private static native void _setNativeCacheDir(String path);
	private static native String _getBankFriendlyName(long reference);

	public static int setDefaultReverb(int reverbType){ if(mMixer==null) return -1; return _setDefaultReverb(mMixer.mReference, reverbType); }
	public static int setDefaultVelocityCurve(int curveType){ return _setDefaultVelocityCurve(curveType); }
	public static int addBankFromFile(String path){ if(mMixer==null) return -1; return _addBankFromFile(mMixer.mReference, path); }
	public static int addBankFromAsset(String assetName){ if(mMixer==null) return -1; return _addBankFromAsset(mMixer.mReference, mMixer.mAssetManager, assetName); }
	public static int addBankFromMemory(byte[] data){ if(mMixer==null) return -1; return _addBankFromMemory(mMixer.mReference, data); }
	public static int addBankFromMemory(byte[] data, String filename){ if(mMixer==null) return -1; return _addBankFromMemoryWithFilename(mMixer.mReference, data, filename); }
	public static void setNativeCacheDir(String path){ _setNativeCacheDir(path); }
	public static String getBankFriendlyName(){ if(mMixer==null) return null; return _getBankFriendlyName(mMixer.mReference); }

	public static int setMasterVolumePercent(int percent){
		if(mMixer==null) return -1;
		if(percent<0) percent=0; if(percent>100) percent=100;
		int fixed = (int)( (percent * 65536L) / 100L );
		return _setMasterVolume(mMixer.mReference, fixed);
	}

	// Android-only helper: set master volume directly (16.16 fixed). Not clamped to 100%.
	public static int setMasterVolumeFixed(int fixedVolume){
		if(mMixer==null) return -1;
		return _setMasterVolume(mMixer.mReference, fixedVolume);
	}

	public static Mixer getMixer() { return mMixer; }

	public static Sound create()
	{
		Sound snd = null;
        if (mMixer.mReference != 0L)
	{
		snd = new Sound(mMixer);
	}
		return snd;
	}	
}
