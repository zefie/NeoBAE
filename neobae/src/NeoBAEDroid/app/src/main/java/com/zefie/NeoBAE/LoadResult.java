package com.zefie.NeoBAE;

public class LoadResult {
    // Load type constants
    public static final int BAE_LOAD_TYPE_NONE = 0;
    public static final int BAE_LOAD_TYPE_SONG = 1;
    public static final int BAE_LOAD_TYPE_SOUND = 2;
    
    // Public fields (set by JNI)
    public int type = BAE_LOAD_TYPE_NONE;
    public int fileType = Mixer.BAE_INVALID_TYPE;
    public int result = 0;
    public long songReference = 0;
    public long soundReference = 0;
    
    // References to wrapper objects
    private Song song;
    private Sound sound;
    private Mixer mixer;
    
    public LoadResult() {
        // Default constructor required for JNI
    }
    
    /**
     * Get the loaded Song object (if type == BAE_LOAD_TYPE_SONG)
     */
    public Song getSong() {
        if (song == null && type == BAE_LOAD_TYPE_SONG && songReference != 0) {
            song = new Song(mixer, songReference);
        }
        return song;
    }
    
    /**
     * Get the loaded Sound object (if type == BAE_LOAD_TYPE_SOUND)
     */
    public Sound getSound() {
        if (sound == null && type == BAE_LOAD_TYPE_SOUND && soundReference != 0) {
            sound = new Sound(mixer, soundReference);
        }
        return sound;
    }
    
    /**
     * Check if a song was loaded
     */
    public boolean isSong() {
        return type == BAE_LOAD_TYPE_SONG;
    }
    
    /**
     * Check if a sound was loaded
     */
    public boolean isSound() {
        return type == BAE_LOAD_TYPE_SOUND;
    }
    
    /**
     * Get a human-readable file type string
     */
    public String getFileTypeString() {
        switch (fileType) {
            case Mixer.BAE_MIDI_TYPE: return "MIDI";
            case Mixer.BAE_RMF: return "RMF";
            case Mixer.BAE_RMI: return "RMI";
            case Mixer.BAE_AIFF_TYPE: return "AIFF";
            case Mixer.BAE_WAVE_TYPE: return "WAVE";
            case Mixer.BAE_AU_TYPE: return "AU";
            case Mixer.BAE_MPEG_TYPE: return "MP3";
            case Mixer.BAE_FLAC_TYPE: return "FLAC";
            case Mixer.BAE_VORBIS_TYPE: return "OGG Vorbis";
            default: return "Unknown";
        }
    }
    
    /**
     * Clean up resources (deletes song/sound if loaded)
     */
    public void cleanup() {
        if (song != null) {
            song.close();
            song = null;
        }
        if (sound != null) {
            // Sound cleanup if needed
            sound = null;
        }
        songReference = 0;
        soundReference = 0;
        type = BAE_LOAD_TYPE_NONE;
    }
    
    // Package-private method to set mixer reference
    void setMixer(Mixer m) {
        this.mixer = m;
    }
}
