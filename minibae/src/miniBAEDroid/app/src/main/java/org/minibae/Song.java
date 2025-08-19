package org.minibae;

public class Song
{
	private long mReference;
	private Mixer mMixer;

	private native long _newNativeSong(long mixerReference);
	private native int _loadSong(long songReference, String path);
	private native int _loadSongFromMemory(long songReference, byte[] data);
	private native int _startSong(long songReference);
	private native void _stopSong(long songReference);
	private static native int _setSongVolume(long songReference, int fixedVolume);
	private static native int _getSongVolume(long songReference);
	// Extended position/length JNI (microseconds)
	private static native int _getSongPositionUS(long songReference);
	private static native int _setSongPositionUS(long songReference, int us);
	private static native int _getSongLengthUS(long songReference);

	Song(Mixer mixer)
	{
		mMixer = mixer;
	mReference = _newNativeSong(mMixer.mReference);
		// default full volume
		_setSongVolume(mReference, 1 * 65536); // 1.0 in unsigned fixed (16.16)
	}

	public int load(String path)
	{
		return _loadSong(mReference, path);
	}

	public int loadFromMemory(byte[] data)
	{
		return _loadSongFromMemory(mReference, data);
	}

	public int start()
	{
		return _startSong(mReference);
	}

	public void stop()
	{
		_stopSong(mReference);
	}

	public int setVolumePercent(int percent){
		if(percent<0) percent=0; if(percent>100) percent=100;
		int fixed = (int)((percent * 65536L) / 100L);
		return _setSongVolume(mReference, fixed);
	}
	public int getVolumePercent(){ int fixed = _getSongVolume(mReference); // fixed 16.16
		if(fixed <= 0) return 0;
		return (int)((fixed * 100L) / 65536L); }

	// Position helpers (milliseconds granularity at call site)
	public int getPositionMs(){ int us = _getSongPositionUS(mReference); return us / 1000; }
	public void seekToMs(int ms){ if(ms < 0) ms = 0; _setSongPositionUS(mReference, ms * 1000); }
	public int getLengthMs(){ int us = _getSongLengthUS(mReference); return us / 1000; }
}
