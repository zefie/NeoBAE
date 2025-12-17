package org.minibae;

public class Song
{
	private long mReference;
	private Mixer mMixer;

	private native long _newNativeSong(long mixerReference);
	private native int _loadSong(long songReference, String path);
	private native int _startSong(long songReference);
	private native void _stopSong(long songReference);
	private static native int _setSongLoops(long songReference, int numLoops);

	Song(Mixer mixer)
	{
		mMixer = mixer;
	mReference = _newNativeSong(mMixer.mReference);
	}

	public int load(String path)
	{
		return _loadSong(mReference, path);
	}

	public int start()
	{
		return _startSong(mReference);
	}

	public void stop()
	{
		_stopSong(mReference);
	}
	
	public int setLoops(int numLoops)
	{
		return _setSongLoops(mReference, numLoops);
	}
}
