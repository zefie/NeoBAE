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
}
