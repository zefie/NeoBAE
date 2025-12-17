package org.minibae;

public class Song
{
	private long mReference;
	private Mixer mMixer;

	private native long _newNativeSong(long mixerReference);
	private native int _loadSong(long songReference, String path);
	private native int _loadSongFromMemory(long songReference, byte[] data);
	private native int _loadRmiFromMemory(long songReference, byte[] data, boolean useEmbeddedBank);
	private native int _prerollSong(long songReference);
	private native int _startSong(long songReference);
	private native void _stopSong(long songReference, boolean deleteSong);
	private native int _pauseSong(long songReference);
	private native int _resumeSong(long songReference);
	private native boolean _isSongPaused(long songReference);
	private native boolean _isSongDone(long songReference);
	private static native int _setSongVolume(long songReference, int fixedVolume);
	private static native int _getSongVolume(long songReference);
	// Extended position/length JNI (microseconds)
	private static native int _getSongPositionUS(long songReference);
	private static native int _setSongPositionUS(long songReference, int us);
	private static native int _getSongLengthUS(long songReference);
	private static native int _setSongLoops(long songReference, int numLoops);

	private static native long _setMetaEventCallback(long songReference, MetaEventListener listener);
	private static native void _cleanupMetaEventCallback(long callbackRef);

	public interface MetaEventListener {
		void onMetaEvent(int markerType, byte[] data);
	}

	private long mCallbackHandle = 0;

	public void setMetaEventListener(MetaEventListener listener) {
		if (mCallbackHandle != 0) {
			_cleanupMetaEventCallback(mCallbackHandle);
			mCallbackHandle = 0;
		}
		if (listener != null) {
			mCallbackHandle = _setMetaEventCallback(mReference, listener);
		} else {
			_setMetaEventCallback(mReference, null);
		}
	}

	Song(Mixer mixer)
	{
		mMixer = mixer;
		mReference = _newNativeSong(mMixer.mReference);
		// default full volume
		_setSongVolume(mReference, 1 * 65536); // 1.0 in unsigned fixed (16.16)
	}
	
	// Package-private constructor for LoadResult to wrap existing native song
	Song(Mixer mixer, long nativeReference)
	{
		mMixer = mixer;
		mReference = nativeReference;
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

	public int loadRmiFromMemory(byte[] data, boolean useEmbeddedBank)
	{
		return _loadRmiFromMemory(mReference, data, useEmbeddedBank);
	}

	public int preroll()
	{
		return _prerollSong(mReference);
	}

	public int start()
	{
		return _startSong(mReference);
	}

	public void stop(boolean deleteSong)
	{
		if (mCallbackHandle != 0) {
			_cleanupMetaEventCallback(mCallbackHandle);
			mCallbackHandle = 0;
			_setMetaEventCallback(mReference, null);
		}
		_stopSong(mReference, deleteSong);
	}

	public int pause()
	{
		return _pauseSong(mReference);
	}

	public int resume()
	{
		return _resumeSong(mReference);
	}

	public boolean isPaused()
	{
		return _isSongPaused(mReference);
	}

	public boolean isDone()
	{
		return _isSongDone(mReference);
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
	
	// Loop control
	public int setLoops(int numLoops){ return _setSongLoops(mReference, numLoops); }
	
	// Additional methods for export functionality
	public boolean isPlaying() {
		return !isPaused(); // If not paused, assume it's playing
	}
	
	public void close() {
		// Stop song if playing
		if (mReference != 0L) {
			stop(true);
			mReference = 0L;
		}
	}
}
