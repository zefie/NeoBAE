package org.minibae;
import java.nio.ByteBuffer;
import android.content.res.AssetManager;

public class Sound
{
    private long mReference;
    private ByteBuffer mFile;
    Mixer mMixer;

    private native long _newNativeSound(long mixerReference);
    private native int _loadSound(long soundReference, ByteBuffer fileData);
    private native int _loadSound(long soundReference, AssetManager assetManager, String file);
    private native int _startSound(long soundReference, int sampleFrames, int fixedVolume);
    private native int _stopSound(long soundReference, boolean deleteSound);
    private native int _pauseSound(long soundReference);
    private native int _resumeSound(long soundReference);
    private native boolean _isSoundPaused(long soundReference);
    private native boolean _isSoundDone(long soundReference);
    private static native int _setSoundVolume(long soundReference, int fixedVolume);
    private static native int _getSoundVolume(long soundReference);
    private static native int _getSoundPositionFrames(long soundReference);
    private static native int _getSoundLengthFrames(long soundReference);
    private static native int _getSoundSampleRate(long soundReference);

	Sound(Mixer mixer)
	{
        mMixer = mixer;
		mReference = _newNativeSound(mMixer.mReference);
        if (mReference == 0L)
		{
            // good
		}
        else
        {
            // bad
        }
	}
	
	// Package-private constructor for LoadResult to wrap existing native sound
	Sound(Mixer mixer, long nativeReference)
	{
		mMixer = mixer;
		mReference = nativeReference;
	}

    int load(String resourceName)
    {
        mFile = ByteBuffer.allocateDirect(10000);
        int status = _loadSound(mReference, mMixer.mAssetManager, resourceName);
        return status;
    }

    int load()
    {
        mFile = ByteBuffer.allocateDirect(10000);
        int status = _loadSound(mReference, mFile);
        return status;
    }
    
    public int start()
    {
        return start(0);
    }
    
    public int start(int sampleFrames)
    {
        // Get current boosted volume to pass to native start method
        int currentVolume = _getSoundVolume(mReference);
        if (currentVolume == 0 || currentVolume == 0x10000) {
            // If volume hasn't been set yet (0) or is still default (0x10000), use 100% boosted
            double engineGain = 1.0;
            double soundMultiplier = 12.0 * (1.0 + 1.0);
            double soundGain = engineGain * soundMultiplier;
            currentVolume = (int)(soundGain * 65536L);
        }
        int r = _startSound(mReference, sampleFrames, currentVolume);
        // Apply volume again immediately after starting to ensure it persists
        if (r == 0 && currentVolume != 0) {
            _setSoundVolume(mReference, currentVolume);
        }
        return r;
    }

    public void stop(boolean deleteSound)
    {
        _stopSound(mReference, deleteSound);
    }

    public int pause()
    {
        return _pauseSound(mReference);
    }

    public int resume()
    {
        return _resumeSound(mReference);
    }

    public boolean isPaused()
    {
        return _isSoundPaused(mReference);
    }

    public boolean isDone()
    {
        return _isSoundDone(mReference);
    }

    public int setVolumePercent(int percent){
        if(percent<0) percent=0; if(percent>100) percent=100;
        // Audio files need a boost multiplier to match MIDI loudness
        // GUI uses: soundMultiplier = 3.0 * (1.0 + percent/100.0)
        // We use 12.0 for even more boost
        double engineGain = percent / 100.0;
        double soundMultiplier = 12.0 * (1.0 + percent / 100.0);
        double soundGain = engineGain * soundMultiplier;
        int fixed = (int)(soundGain * 65536L);
        return _setSoundVolume(mReference, fixed);
    }
    
    public int getVolumePercent(){ 
        int fixed = _getSoundVolume(mReference); // fixed 16.16
        if(fixed <= 0) return 0;
        return (int)((fixed * 100L) / 65536L); 
    }
    
    public int getPositionMs() {
        int frames = _getSoundPositionFrames(mReference);
        int sampleRate = _getSoundSampleRate(mReference);
        if (sampleRate <= 0) return 0;
        return (int)((frames * 1000L) / sampleRate);
    }
    
    public int getLengthMs() {
        int frames = _getSoundLengthFrames(mReference);
        int sampleRate = _getSoundSampleRate(mReference);
        if (sampleRate <= 0) return 0;
        return (int)((frames * 1000L) / sampleRate);
    }
    
    public boolean isPlaying() {
        return !isPaused(); // If not paused, assume it's playing
    }
    
    public void close() {
        // Stop sound if playing
        if (mReference != 0L) {
            stop(true);
            mReference = 0L;
        }
    }
}
