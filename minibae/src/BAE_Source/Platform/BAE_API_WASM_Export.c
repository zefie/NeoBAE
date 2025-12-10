/*
 * bae_wasm_api.c
 *
 * WebAssembly API for Beatnik Audio Engine
 * Exports functions callable from JavaScript
 */

#include <emscripten.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>


#if USE_SF2_SUPPORT
    #if _USING_FLUIDSYNTH
        #include "GenSF2_FluidSynth.h"
        #if USE_XMF_SUPPORT
            #include "GenXMF.h"
        #endif
    #endif
#endif

// Include miniBAE headers
#include "MiniBAE.h"
#include "X_API.h"
#include "X_Assert.h"
#include "GenSnd.h"
#include "GenPriv.h"  // for MusicGlobals (mixer cache)
#include "GenPriv.h"

// Global mixer instance
static BAEMixer gMixer = NULL;
static BAESong gCurrentSong = NULL;
static BAESong gEffectSong = NULL;  // Second song for sound effects (plays on top)

#ifdef SUPPORT_KARAOKE
// Lyric callback support
typedef void (*JSLyricCallback)(const char* lyric, uint32_t timeUs);
static JSLyricCallback gJSLyricCallback = NULL;
static int gSuppressLyrics = 1;  // Start suppressed (1 = suppress during preroll, 0 = allow)
static uint32_t gLyricUnsuppressTime = 0;  // Time when lyrics should be unsuppressed (0 = inactive)
#endif

// Audio buffer for JS interop
#define AUDIO_BUFFER_FRAMES 6144
static int16_t gAudioBuffer[AUDIO_BUFFER_FRAMES * 2];  // Stereo

// Ring buffer for 250ms audio buffering
typedef struct {
    int16_t *buffer;          // Ring buffer data (stereo interleaved)
    size_t capacity;          // Total capacity in samples (stereo pairs)
    size_t writePos;          // Write position in samples
    size_t readPos;           // Read position in samples
    size_t available;         // Number of samples available to read
} RingBuffer;

static RingBuffer gRingBuffer = {NULL, 0, 0, 0, 0};
static int gCurrentSampleRate = 44100;  // Default sample rate

// External function from GenSynth.c
extern void BAE_BuildMixerSlice(void *threadContext, void *pAudioBuffer,
                                int32_t bufferByteLength, int32_t sampleFrames);

#ifdef SUPPORT_KARAOKE
static void PV_LyricCallback(struct GM_Song *songPtr, const char *lyric, uint32_t timeUs, void *ref);
#endif

// Ring buffer management functions
static void RingBuffer_Init(int sampleRate);
static void RingBuffer_Destroy(void);
static void RingBuffer_Clear(void);
static size_t RingBuffer_Write(const int16_t *data, size_t frames);
static size_t RingBuffer_Read(int16_t *dest, size_t frames);

/*
 * Initialize the Beatnik Audio Engine
 * Returns: 0 on success, error code on failure
 */
EMSCRIPTEN_KEEPALIVE
int BAE_WASM_Init(int sampleRate, int maxVoices) {
    BAEResult err;

    if (gMixer != NULL) {
        // Already initialized
        return 0;
    }

    // Create mixer
    gMixer = BAEMixer_New();
    if (gMixer == NULL) {
        return -1;
    }

    // Determine quality based on sample rate
    BAERate rate = BAE_RATE_44K;
    if (sampleRate <= 22050) {
        rate = BAE_RATE_22K;
    } else if (sampleRate <= 24000) {
        rate = BAE_RATE_24K;
    } else if (sampleRate >= 48000) {
        rate = BAE_RATE_48K;
    }

    // Cap voices so total (MIDI + effects) doesn't exceed MAX_VOICES (64)
    short int effectVoices = 4;
    short int midiVoices = (short int)maxVoices;
    if (midiVoices + effectVoices > 64) {
        midiVoices = 64 - effectVoices;  // Cap at 60 MIDI voices
    }
    if (midiVoices < 1) {
        midiVoices = 1;
    }

    // Store current sample rate for ring buffer
    gCurrentSampleRate = sampleRate;

    // Open mixer
    // Mix level controls internal gain scaling via L2Levels lookup table:
    //   mixLevel 16 = 1.22x amplification (causes clipping!)
    //   mixLevel 24 = 1.00x unity gain
    //   mixLevel 32 = 0.86x attenuation
    //   mixLevel 48 = 0.70x attenuation
    // Higher values = more headroom for multiple voices = less distortion
    err = BAEMixer_Open(gMixer,
                        rate,                           // sample rate
                        BAE_2_POINT_INTERPOLATION,      // interpolation
                        BAE_USE_16 | BAE_USE_STEREO,    // 16-bit stereo
                        midiVoices,                     // voices for music (max 60)
                        effectVoices,                   // voices for effects
                        64,                             // mix level (64 = 0.61x attenuation - TEST for volume reduction)
                        FALSE);                         // don't engage hardware audio

    if (err != BAE_NO_ERROR) {
        BAEMixer_Delete(gMixer);
        gMixer = NULL;
        return (int)err;
    }

    // Initialize ring buffer for 250ms
    RingBuffer_Init(sampleRate);

    return 0;
}


/*
 * Unload the current soundbank
 * Returns: 0 on success, -1 if mixer not initialized
 */
EMSCRIPTEN_KEEPALIVE
int BAE_WASM_UnloadSoundbank(void) {
    BAE_PRINTF("[BAE] UnloadSoundbank: gMixer=%p\n", (void*)gMixer);
    
    if (gMixer == NULL) {
        BAE_PRINTF("[BAE] UnloadSoundbank: ERROR - gMixer is NULL\n");
        return -1;  // Not initialized
    }

    // Unload existing banks
    BAE_PRINTF("[BAE] UnloadSoundbank: Unloading banks...\n");
    BAEMixer_UnloadBanks(gMixer);

#if USE_SF2_SUPPORT == TRUE
    GM_UnloadSF2Soundfont();
#endif

    BAE_PRINTF("[BAE] UnloadSoundbank: SUCCESS\n");
    return 0;
}

/*
 * Load a soundbank from memory
 * Returns: 0 on success, error code on failure
 */
EMSCRIPTEN_KEEPALIVE
int BAE_WASM_LoadSoundbank(const uint8_t* data, int length) {
    BAEResult err;
    BAEBankToken token = NULL;

    BAE_PRINTF("[BAE] LoadSoundbank: data=%p, length=%d\n", (void*)data, length);
    BAE_PRINTF("[BAE] LoadSoundbank: Header bytes: %02X %02X %02X %02X\n",
           data[0], data[1], data[2], data[3]);

    if (gMixer == NULL) {
        BAE_PRINTF("[BAE] LoadSoundbank: ERROR - gMixer is NULL\n");
        return -1;  // Not initialized
    }

    // Unload existing banks
    BAE_PRINTF("[BAE] LoadSoundbank: Unloading existing banks...\n");
    BAE_WASM_UnloadSoundbank();

#if USE_SF2_SUPPORT == TRUE
    // Detect soundbank type by header
    // RIFF format: bytes 0-3 = "RIFF", bytes 8-11 = format identifier
    // SF2: "RIFF" + "sfbk" at offset 8
    // DLS: "RIFF" + "DLS " at offset 8
    int isSF2 = 0;
    int isDLS = 0;
    
    if (length >= 12 && 
        data[0] == 'R' && data[1] == 'I' && data[2] == 'F' && data[3] == 'F') {
        // Check format identifier at offset 8
        if (data[8] == 's' && data[9] == 'f' && data[10] == 'b' && data[11] == 'k') {
            isSF2 = 1;
            BAE_PRINTF("[BAE] LoadSoundbank: Detected SF2/SF3 format\n");
        }
#if _USING_FLUIDSYNTH == TRUE
        else if (data[8] == 'D' && data[9] == 'L' && data[10] == 'S' && data[11] == ' ') {
            isDLS = 1;
            BAE_PRINTF("[BAE] LoadSoundbank: Detected DLS format\n");
        }
#endif
    }
    
    if (isSF2
#if _USING_FLUIDSYNTH == TRUE
        || isDLS
#endif
       )
    {
        BAE_PRINTF("[BAE] LoadSoundbank: Loading SF2/SF3/DLS via temp file (WASM workaround)...\n");
        
        // WASM workaround: Write to temp file and load from file
        // FluidSynth's memory loader seems to have issues in WASM
        const char *tmpfile = "/tmp/soundbank";
        FILE *fp = fopen(tmpfile, "wb");
        if (!fp) {
            BAE_PRINTF("[BAE] LoadSoundbank: Failed to create temp file\n");
            return -1;
        }
        
        size_t written = fwrite(data, 1, (size_t)length, fp);
        fclose(fp);
        
        if (written != (size_t)length) {
            BAE_PRINTF("[BAE] LoadSoundbank: Failed to write complete data (%zu/%d)\n", written, length);
            return -1;
        }
        
        BAE_PRINTF("[BAE] LoadSoundbank: Wrote %zu bytes to %s\n", written, tmpfile);
        
        // Load from file instead
        OPErr sfErr = GM_LoadSF2Soundfont(tmpfile);
        BAE_PRINTF("[BAE] LoadSoundbank: GM_LoadSF2Soundfont returned %d\n", sfErr);

        // Clean up temp file (use remove() instead of unlink() for WASM compatibility)
        // Note: In WASM/Emscripten, the file will be reused on next load anyway
        remove(tmpfile);

        if (sfErr != NO_ERR)
        {
            BAE_PRINTF("[BAE] LoadSoundbank: SF2/DLS bank load failed: %d\n", sfErr);
            return (int)sfErr;
        }

        // Enable SF2 mode for the mixer (GM_LoadSF2Soundfont already does this, but just to be sure)
        GM_SetMixerSF2Mode(TRUE);
        BAE_PRINTF("[BAE] LoadSoundbank: SF2/DLS SUCCESS\n");
        return 0;
    }
#endif

    // HSB Handling

    // Add bank from memory
    BAE_PRINTF("[BAE] LoadSoundbank: Adding bank from memory...\n");
    err = BAEMixer_AddBankFromMemory(gMixer, (void*)data, (unsigned long)length, &token);
    BAE_PRINTF("[BAE] LoadSoundbank: AddBankFromMemory result=%d\n", (int)err);
    if (err != BAE_NO_ERROR) {
        BAE_PRINTF("[BAE] LoadSoundbank: ERROR - AddBankFromMemory failed with code %d\n", (int)err);
        return (int)err;
    }

    // Ensure the newly loaded bank is active/at front
    if (token != NULL) {
        BAEMixer_BringBankToFront(gMixer, token);
        XFileUseThisResourceFile((XFILE)token);
    }

    BAE_PRINTF("[BAE] LoadSoundbank: SUCCESS\n");
    return 0;
}

/*
 * Detect if data is RMF format
 * RMF files start with "IREZ" magic
 */
static int isRmfFile(const uint8_t* data, int length) {
    if (length < 4) return 0;
    return (data[0] == 'I' && data[1] == 'R' && data[2] == 'E' && data[3] == 'Z');
}

/*
 * Load a MIDI or RMF file from memory
 * Returns: 0 on success, error code on failure
 */
EMSCRIPTEN_KEEPALIVE
int BAE_WASM_LoadSong(const uint8_t* data, int length) {
    BAEResult err;

    BAE_PRINTF("[BAE] LoadSong: data=%p, length=%d\n", (void*)data, length);

    if (gMixer == NULL) {
        BAE_PRINTF("[BAE] LoadSong: ERROR - gMixer is NULL\n");
        return -1;
    }
    BAE_PRINTF("[BAE] LoadSong: gMixer=%p\n", (void*)gMixer);

    // Stop and delete existing song
    if (gCurrentSong != NULL) {
        BAE_PRINTF("[BAE] LoadSong: Stopping/deleting existing song\n");
        BAESong_Stop(gCurrentSong, FALSE);
        BAESong_Delete(gCurrentSong);
        gCurrentSong = NULL;
    }

    // Create new song
    BAE_PRINTF("[BAE] LoadSong: Creating new BAESong...\n");
    gCurrentSong = BAESong_New(gMixer);
    if (gCurrentSong == NULL) {
        BAE_PRINTF("[BAE] LoadSong: ERROR - BAESong_New returned NULL\n");
        return -2;
    }
    BAE_PRINTF("[BAE] LoadSong: BAESong created=%p\n", (void*)gCurrentSong);

    // Load from memory - detect file type
    if (isRmfFile(data, length)) {
        BAE_PRINTF("[BAE] LoadSong: Detected RMF file, loading...\n");
        // RMF: song, data, size, songIndex, ignoreBadInstruments
        err = BAESong_LoadRmfFromMemory(gCurrentSong, (void*)data, (unsigned long)length, 0, TRUE);
    } else {
        BAE_PRINTF("[BAE] LoadSong: Detected MIDI file (magic: %02X %02X %02X %02X), loading...\n",
               data[0], data[1], data[2], data[3]);
        // MIDI: song, data, size, ignoreBadInstruments
        err = BAESong_LoadMidiFromMemory(gCurrentSong, (void*)data, (unsigned long)length, TRUE);
    }

    BAE_PRINTF("[BAE] LoadSong: Load result=%d\n", (int)err);

    if (err != BAE_NO_ERROR) {
        BAE_PRINTF("[BAE] LoadSong: ERROR - Load failed with code %d\n", (int)err);
        BAESong_Delete(gCurrentSong);
        gCurrentSong = NULL;
        return (int)err;
    }

#ifdef SUPPORT_KARAOKE
    // Reset lyric state and suppress lyrics during preroll
    extern BAEResult BAESong_ResetLyricState(BAESong song);
    BAESong_ResetLyricState(gCurrentSong);
#endif

    // Preroll (load instruments)
    BAE_PRINTF("[BAE] LoadSong: Prerolling...\n");
    err = BAESong_Preroll(gCurrentSong);
    BAE_PRINTF("[BAE] LoadSong: Preroll result=%d\n", (int)err);
    if (err != BAE_NO_ERROR) {
        BAE_PRINTF("[BAE] LoadSong: ERROR - Preroll failed with code %d\n", (int)err);
        BAESong_Delete(gCurrentSong);
        gCurrentSong = NULL;
        return (int)err;
    }

#ifdef SUPPORT_KARAOKE
    extern BAEResult BAESong_ResetLyricState(BAESong song);
    BAESong_ResetLyricState(gCurrentSong);
    BAE_PRINTF("[BAE] LoadSong: Lyric state reset\n");
#endif

    BAE_PRINTF("[BAE] LoadSong: SUCCESS\n");
    return 0;
}

/*
 * Unload the current song
 * Returns: 0 on success, -1 if no song loaded
 */
EMSCRIPTEN_KEEPALIVE
int BAE_WASM_UnloadSong(void) {
    BAE_PRINTF("[BAE] UnloadSong: gCurrentSong=%p\n", (void*)gCurrentSong);
    
    if (gCurrentSong == NULL) {
        BAE_PRINTF("[BAE] UnloadSong: No song to unload\n");
        return -1;  // No song loaded
    }

    // Stop playback
    BAE_PRINTF("[BAE] UnloadSong: Stopping song...\n");
    BAESong_Stop(gCurrentSong, FALSE);
    
    // Delete song
    BAE_PRINTF("[BAE] UnloadSong: Deleting song...\n");
    BAESong_Delete(gCurrentSong);
    gCurrentSong = NULL;

    BAE_PRINTF("[BAE] UnloadSong: SUCCESS\n");
    return 0;
}

/*
 * Start playback
 */
EMSCRIPTEN_KEEPALIVE
int BAE_WASM_Play(void) {
    BAE_PRINTF("[BAE] Play: gCurrentSong=%p\n", (void*)gCurrentSong);
    if (gCurrentSong == NULL) {
        BAE_PRINTF("[BAE] Play: ERROR - no song loaded\n");
        return -1;
    }

#ifdef SUPPORT_KARAOKE
    if (gJSLyricCallback) {
        extern BAEResult BAESong_SetLyricCallback(BAESong song, GM_SongLyricCallbackProcPtr pCallback, void *callbackReference);
        BAESong_SetLyricCallback(gCurrentSong, PV_LyricCallback, NULL);
        BAE_PRINTF("[BAE] Play: Lyric callback installed\n");
    }
    // Schedule lyric unsuppression for 550ms from now (non-blocking)
    gLyricUnsuppressTime = BAE_GetMicroseconds() + 550000;
#endif
    GM_ResumeGeneralSound(NULL);
    BAEResult err = BAESong_Start(gCurrentSong, 0);
    if (err != BAE_NO_ERROR) {
        BAE_PRINTF("[BAE] Play: ERROR - BAESong_Start failed with code %d\n", (int)err);
        return (int)err;
    }
    
    // Clear ring buffer on play start - let normal playback fill it gradually
    // This avoids applying gain/limiting twice to pre-filled data
    RingBuffer_Clear();
    BAE_PRINTF("[BAE] Play: Ring buffer cleared, will fill during playback\n");
    
    BAE_PRINTF("[BAE] Play: SUCCESS\n");
    return 0;
}

/*
 * Pause playback
 */
EMSCRIPTEN_KEEPALIVE
int BAE_WASM_Pause(void) {
    if (gCurrentSong == NULL) {
        return -1;
    }

    GM_PauseGeneralSound(NULL);
    BAESong_Pause(gCurrentSong);
    return 0;
}

/*
 * Resume playback
 */
EMSCRIPTEN_KEEPALIVE
int BAE_WASM_Resume(void) {
    if (gCurrentSong == NULL) {
        return -1;
    }

    GM_ResumeGeneralSound(NULL);
    BAESong_Resume(gCurrentSong);
    return 0;
}

/*
 * Stop playback
 */
EMSCRIPTEN_KEEPALIVE
int BAE_WASM_Stop(void) {
    if (gCurrentSong == NULL) {
        return -1;
    }

#ifdef SUPPORT_KARAOKE
    // Suppress lyrics when stopped and cancel any pending unsuppression
    gSuppressLyrics = 1;
    gLyricUnsuppressTime = 0;
    
    // Uninstall lyric callback
    extern BAEResult BAESong_SetLyricCallback(BAESong song, GM_SongLyricCallbackProcPtr pCallback, void *callbackReference);
    BAESong_SetLyricCallback(gCurrentSong, NULL, NULL);
    BAE_PRINTF("[BAE] Stop: Lyric callback uninstalled\n");
#endif

    // Use FALSE to immediately stop and remove from mixer (no fade)
    BAESong_Stop(gCurrentSong, FALSE);
    
    // Clear ring buffer when stopped
    RingBuffer_Clear();
    BAE_PRINTF("[BAE] Stop: Ring buffer cleared\n");
    
    return 0;
}

/*
 * Check if playing
 */
EMSCRIPTEN_KEEPALIVE
int BAE_WASM_IsPlaying(void) {
    if (gCurrentSong == NULL) {
        return 0;
    }

    BAE_BOOL isDone = FALSE;
    BAESong_IsDone(gCurrentSong, &isDone);
    return isDone ? 0 : 1;
}

/*
 * Get current position in milliseconds
 */
EMSCRIPTEN_KEEPALIVE
int BAE_WASM_GetPosition(void) {
    if (gCurrentSong == NULL) {
        return 0;
    }

    unsigned long pos = 0;
    BAESong_GetMicrosecondPosition(gCurrentSong, (uint32_t*)&pos);
    return (int)(pos / 1000);  // Convert to milliseconds
}

/*
 * Set position in milliseconds
 */
EMSCRIPTEN_KEEPALIVE
int BAE_WASM_SetPosition(int positionMs) {
    if (gCurrentSong == NULL) {
        return -1;
    }

    BAEResult err = BAESong_SetMicrosecondPosition(gCurrentSong, (unsigned long)positionMs * 1000);
    return (int)err;
}

/*
 * Get duration in milliseconds
 */
EMSCRIPTEN_KEEPALIVE
int BAE_WASM_GetDuration(void) {
    if (gCurrentSong == NULL) {
        return 0;
    }

    unsigned long duration = 0;
    BAESong_GetMicrosecondLength(gCurrentSong, (uint32_t*)&duration);
    return (int)(duration / 1000);
}

/*
 * Set master volume (0-100)
 */
EMSCRIPTEN_KEEPALIVE
int BAE_WASM_SetVolume(int volume) {
    if (gMixer == NULL) {
        return -1;
    }

    // Clamp to valid range
    if (volume < 0) volume = 0;
    if (volume > 200) volume = 200;

    // Convert to BAE 16.16 fixed point format where BAE_FIXED_1 (0x10000 = 65536) = 1.0
    // volume 100% -> 0x10000, volume 50% -> 0x8000, volume 0% -> 0
    BAE_UNSIGNED_FIXED fixedVol = (BAE_UNSIGNED_FIXED)((volume * 0x10000L) / 100);
    
    // Set mixer master volume
    BAEMixer_SetMasterVolume(gMixer, fixedVol);
    
    // Also set song volume if a song is loaded
    if (gCurrentSong != NULL) {
        BAESong_SetVolume(gCurrentSong, fixedVol);
    }

#if _USING_FLUIDSYNTH == TRUE && USE_SF2_SUPPORT == TRUE
    // Also set FluidSynth global volume
    if (GM_GetMixerSF2Mode()) {
        if (volume > 100) {
            GM_SF2_SetGain(((float)volume / 10000.0f) - 0.005f);
        } else {
            GM_SF2_SetGain(0.005f);
        }
    }
#endif
    return 0;
}

/*
 * Set tempo as percentage (100 = normal)
 */
EMSCRIPTEN_KEEPALIVE
int BAE_WASM_SetTempo(int tempoPercent) {
    if (gCurrentSong == NULL) {
        return -1;
    }

    // Convert percentage to fixed point (0x10000 = 1.0)
    BAE_UNSIGNED_FIXED tempo = (BAE_UNSIGNED_FIXED)((tempoPercent * 0x10000L) / 100);
    BAESong_SetMasterTempo(gCurrentSong, tempo);
    return 0;
}

/*
 * Set transpose in semitones (-12 to +12)
 */
EMSCRIPTEN_KEEPALIVE
int BAE_WASM_SetTranspose(int semitones) {
    if (gCurrentSong == NULL) {
        return -1;
    }

    // Clamp
    if (semitones < -12) semitones = -12;
    if (semitones > 12) semitones = 12;

    BAESong_SetTranspose(gCurrentSong, (long)semitones);
    return 0;
}

/*
 * Set loop count (0 = no loop, -1 or high value like 32767 = infinite loop)
 */
EMSCRIPTEN_KEEPALIVE
int BAE_WASM_SetLoops(int loopCount) {
    if (gCurrentSong == NULL) {
        return -1;
    }

    // Follow GUI convention: 0 = no loop, 32767 = infinite loop
    BAESong_SetLoops(gCurrentSong, (short)loopCount);
    return 0;
}

/*
 * Mute/unmute a MIDI channel (0-15)
 */
EMSCRIPTEN_KEEPALIVE
int BAE_WASM_MuteChannel(int channel, int muted) {
    if (gCurrentSong == NULL || channel < 0 || channel > 15) {
        return -1;
    }

    // BAE uses 0-based channels internally (PERCUSSION_CHANNEL = 9)
    if (muted) {
        BAESong_MuteChannel(gCurrentSong, (unsigned short)channel);
    } else {
        BAESong_UnmuteChannel(gCurrentSong, (unsigned short)channel);
    }
    return 0;
}

/*
 * Set reverb type (0-11)
 */
EMSCRIPTEN_KEEPALIVE
int BAE_WASM_SetReverbType(int reverbType) {
    if (gMixer == NULL) {
        return -1;
    }

    if (reverbType < 0 || reverbType > 11) {
        reverbType = 0;  // None
    }

    BAEMixer_SetDefaultReverb(gMixer, (BAEReverbType)(reverbType + 1)); // BAE reverb types start at 1
    return 0;
}

// Debug counter for GenerateAudio
static int gGenerateAudioCallCount = 0;

// Output gain control (256 = unity gain, 128 = -6dB, 64 = -12dB)
// Default to 230 (~90%) - good balance with OUTPUT_SCALAR=11
static int gOutputGain = 230;

/*
 * Set output gain (0-256, where 256 = unity, 128 = -6dB, 64 = -12dB)
 * This is applied after mixing, before output to JavaScript
 */
EMSCRIPTEN_KEEPALIVE
int BAE_WASM_SetOutputGain(int gain) {
    if (gain < 0) gain = 0;
    if (gain > 512) gain = 512;  // Allow some boost if needed
    gOutputGain = gain;
    return 0;
}

/*
 * Get current output gain
 */
EMSCRIPTEN_KEEPALIVE
int BAE_WASM_GetOutputGain(void) {
    return gOutputGain;
}

/*
 * Generate audio samples into buffer
 * Called from JavaScript audio callback
 * Returns: pointer to audio buffer (interleaved stereo 16-bit)
 */
EMSCRIPTEN_KEEPALIVE
int16_t* BAE_WASM_GenerateAudio(int frames) {
    gGenerateAudioCallCount++;

#ifdef SUPPORT_KARAOKE
    // Check if it's time to unsuppress lyrics (non-blocking)
    if (gLyricUnsuppressTime > 0 && BAE_GetMicroseconds() >= gLyricUnsuppressTime) {
        gSuppressLyrics = 0;
        gLyricUnsuppressTime = 0;  // Clear the timer
        BAE_PRINTF("[BAE] GenerateAudio: Lyrics unsuppressed\n");
    }
#endif

    if (gMixer == NULL) {
        // Fill with silence
        memset(gAudioBuffer, 0, frames * 2 * sizeof(int16_t));
        return gAudioBuffer;
    }

    // Clamp frames to buffer size
    if (frames > AUDIO_BUFFER_FRAMES) {
        frames = AUDIO_BUFFER_FRAMES;
    }

    // Try to read from ring buffer first (if available)
    size_t framesRead = 0;
    if (gRingBuffer.buffer != NULL && gRingBuffer.available > 0) {
        framesRead = RingBuffer_Read(gAudioBuffer, frames);
    }
    
    // If ring buffer didn't have enough data, generate more
    if (framesRead < (size_t)frames) {
        // Generate fresh audio into temporary buffer
        size_t framesToGenerate = frames - framesRead;
        int16_t *destPtr = &gAudioBuffer[framesRead * 2];
        long bufferByteLength = framesToGenerate * 2 * sizeof(int16_t);
        BAE_BuildMixerSlice(NULL, (void*)destPtr, bufferByteLength, framesToGenerate);
    }
    
    // Refill ring buffer to maintain 250ms buffer
    // Generate as many frames as we consumed (or as much as will fit)
    if (gRingBuffer.buffer != NULL && framesRead > 0) {
        size_t spaceAvailable = gRingBuffer.capacity - gRingBuffer.available;
        size_t framesToRefill = (framesRead < spaceAvailable) ? framesRead : spaceAvailable;
        
        if (framesToRefill > 0) {
            // Clamp to our temp buffer size
            if (framesToRefill > AUDIO_BUFFER_FRAMES) {
                framesToRefill = AUDIO_BUFFER_FRAMES;
            }
            
            int16_t *tempBuf = (int16_t*)malloc(framesToRefill * 2 * sizeof(int16_t));
            if (tempBuf != NULL) {
                long refillBytes = framesToRefill * 2 * sizeof(int16_t);
                BAE_BuildMixerSlice(NULL, (void*)tempBuf, refillBytes, framesToRefill);
                RingBuffer_Write(tempBuf, framesToRefill);
                free(tempBuf);
            }
        }
    }

    // Apply output gain and soft limiting in a single optimized pass
    int totalSamples = frames * 2;  // Stereo
    
    if (gOutputGain != 256) {
        // Non-unity gain path with soft limiting
        for (int i = 0; i < totalSamples; i++) {
            int32_t sample = (gAudioBuffer[i] * gOutputGain) >> 8;
            
            // Fast soft limiter with reduced branching
            // Most samples won't need limiting, so optimize for that case
            if (sample >= -28000 && sample <= 28000) {
                // Fast path: no limiting needed
                gAudioBuffer[i] = (int16_t)sample;
            } else {
                // Slow path: apply soft knee or hard limit
                if (sample > 32767) {
                    sample = 32767;
                } else if (sample < -32768) {
                    sample = -32768;
                } else if (sample > 28000) {
                    // Soft knee: use shift instead of division
                    int32_t excess = sample - 28000;
                    // Approximate: 28000 + excess * (4767/4767+x) ≈ 28000 + excess*0.8
                    sample = 28000 + ((excess * 13) >> 4);  // *0.8125 via shift
                } else {  // sample < -28000
                    int32_t excess = -28000 - sample;
                    sample = -28000 - ((excess * 13) >> 4);
                }
                gAudioBuffer[i] = (int16_t)sample;
            }
        }
    } else {
        // Unity gain - minimal processing, most samples won't clip
        for (int i = 0; i < totalSamples; i++) {
            int32_t sample = gAudioBuffer[i];
            // Branch prediction friendly: most values are in range
            if (sample < -32768 || sample > 32767) {
                sample = (sample > 32767) ? 32767 : -32768;
                gAudioBuffer[i] = (int16_t)sample;
            }
            // No write needed if already in range (already int16_t)
        }
    }

    return gAudioBuffer;
}

/*
 * Get the audio buffer pointer (for JS to read)
 */
EMSCRIPTEN_KEEPALIVE
int16_t* BAE_WASM_GetAudioBuffer(void) {
    return gAudioBuffer;
}

/*
 * Get audio buffer size in frames
 */
EMSCRIPTEN_KEEPALIVE
int BAE_WASM_GetBufferFrames(void) {
    return AUDIO_BUFFER_FRAMES;
}

/*
 * Get library version
 * Returns: version encoded as (major << 16) | (minor << 8) | subminor
 * Example: version 1.6.0 returns 0x010600 (65536)
 */
EMSCRIPTEN_KEEPALIVE
int BAE_WASM_GetVersion(void) {
    return (BAE_VERSION_MAJOR << 16) | (BAE_VERSION_MINOR << 8) | BAE_VERSION_SUB_MINOR;
}

/*
 * Get detailed library version string (uses BAE_GetVersion from MiniBAE.c)
 * Returns: pointer to version string (e.g., "1.6.0" or "built on Dec  7 2025")
 * Note: String is allocated by BAE_GetVersion and should be freed by caller
 */
EMSCRIPTEN_KEEPALIVE
const char* BAE_WASM_GetVersionString(void) {
    return BAE_GetVersion();
}

EMSCRIPTEN_KEEPALIVE
const char *BAE_WASM_GetCompileInfo(void) {
    return BAE_GetCompileInfo();
}

EMSCRIPTEN_KEEPALIVE
const char *BAE_WASM_GetFeatureString(void) {
    return BAE_GetFeatureString();
}

/*
 * Cleanup
 */
EMSCRIPTEN_KEEPALIVE
void BAE_WASM_Shutdown(void) {
    if (gCurrentSong != NULL) {
        BAESong_Stop(gCurrentSong, FALSE);
        BAESong_Delete(gCurrentSong);
        gCurrentSong = NULL;
    }

    if (gMixer != NULL) {
        BAEMixer_Close(gMixer);
        BAEMixer_Delete(gMixer);
        gMixer = NULL;
    }
}

/*
 * Get song title
 * Returns: length of string, or 0 if not available
 * infoType is ignored for now (only title is available in miniBAE)
 */
EMSCRIPTEN_KEEPALIVE
int BAE_WASM_GetSongInfo(int infoType, char* buffer, int bufferSize) {
    (void)infoType;  // Only title is available in miniBAE

    if (gCurrentSong == NULL || buffer == NULL || bufferSize <= 0) {
        return 0;
    }

    BAEResult err = BAESong_GetTitle(gCurrentSong, buffer, bufferSize);

    if (err != BAE_NO_ERROR) {
        buffer[0] = '\0';
        return 0;
    }

    return (int)strlen(buffer);
}

/*
 * Mute/unmute a MIDI track (1-65535)
 * Track numbers are 1-based (track 1 is the first track)
 */
EMSCRIPTEN_KEEPALIVE
int BAE_WASM_MuteTrack(int track, int muted) {
    if (gCurrentSong == NULL || track < 1) {
        return -1;
    }

    BAEResult err;
    if (muted) {
        err = BAESong_MuteTrack(gCurrentSong, (unsigned short)track);
    } else {
        err = BAESong_UnmuteTrack(gCurrentSong, (unsigned short)track);
    }
    return (int)err;
}

/*
 * Solo/unsolo a MIDI track (1-65535)
 * When a track is soloed, only soloed tracks produce sound
 */
EMSCRIPTEN_KEEPALIVE
int BAE_WASM_SoloTrack(int track, int soloed) {
    if (gCurrentSong == NULL || track < 1) {
        return -1;
    }

    BAEResult err;
    if (soloed) {
        err = BAESong_SoloTrack(gCurrentSong, (unsigned short)track);
    } else {
        err = BAESong_UnSoloTrack(gCurrentSong, (unsigned short)track);
    }
    return (int)err;
}

/*
 * Get track mute status
 * Returns: 1 if muted, 0 if not muted, -1 on error
 * Note: BAESong_GetTrackMuteStatus returns an array of 16 track statuses
 */
EMSCRIPTEN_KEEPALIVE
int BAE_WASM_GetTrackMuteStatus(int track) {
    if (gCurrentSong == NULL || track < 1 || track > 16) {
        return -1;
    }

    BAE_BOOL trackMutes[16];
    BAEResult err = BAESong_GetTrackMuteStatus(gCurrentSong, trackMutes);
    if (err != BAE_NO_ERROR) {
        return -1;
    }
    return trackMutes[track - 1] ? 1 : 0;
}

/*
 * Get track solo status
 * Returns: 1 if soloed, 0 if not soloed, -1 on error
 * Note: BAESong_GetSoloTrackStatus returns an array of 16 track statuses
 */
EMSCRIPTEN_KEEPALIVE
int BAE_WASM_GetTrackSoloStatus(int track) {
    if (gCurrentSong == NULL || track < 1 || track > 16) {
        return -1;
    }

    BAE_BOOL trackSolos[16];
    BAEResult err = BAESong_GetSoloTrackStatus(gCurrentSong, trackSolos);
    if (err != BAE_NO_ERROR) {
        return -1;
    }
    return trackSolos[track - 1] ? 1 : 0;
}

/*
 * Change the program (instrument) on a MIDI channel and load the instrument
 * channel: 0-15 (MIDI channel number, 0-based)
 * program: 0-127 (General MIDI program number)
 *
 * Uses NoteOnWithLoad to ensure the instrument samples are actually loaded,
 * then immediately sends NoteOff so no sound is heard.
 */
EMSCRIPTEN_KEEPALIVE
int BAE_WASM_ProgramChange(int channel, int program) {
    BAE_PRINTF("[BAE] ProgramChange: channel=%d program=%d song=%p\n", channel, program, (void*)gCurrentSong);
    if (gCurrentSong == NULL || channel < 0 || channel > 15 || program < 0 || program > 127) {
        BAE_PRINTF("[BAE] ProgramChange: INVALID - returning -1\n");
        return -1;
    }

    // BAE uses 0-based channel numbers internally (PERCUSSION_CHANNEL = 9)
    unsigned char baeChannel = (unsigned char)channel;
    BAEResult err;

    // First do the program change
    err = BAESong_ProgramChange(gCurrentSong, baeChannel, (unsigned char)program, 0);
    BAE_PRINTF("[BAE] ProgramChange: ProgramChange result=%d\n", (int)err);

    // Now use NoteOnWithLoad to force the instrument to load
    // Use middle C (note 60) with velocity 0 - this loads but doesn't play
    // Actually, velocity 0 is treated as NoteOff, so use velocity 1 then NoteOff
    err = BAESong_NoteOnWithLoad(gCurrentSong, baeChannel, 60, 1, 0);
    BAE_PRINTF("[BAE] ProgramChange: NoteOnWithLoad result=%d\n", (int)err);

    // Immediately send NoteOff so we don't hear a blip
    // NoteOff takes: song, channel, note, velocity, time
    err = BAESong_NoteOff(gCurrentSong, baeChannel, 60, 0, 0);
    BAE_PRINTF("[BAE] ProgramChange: NoteOff result=%d\n", (int)err);

    return 0;  // Return success if we got this far
}

/*
 * Change the program (instrument) on a MIDI channel with bank selection
 * channel: 0-15 (MIDI channel number, 0-based)
 * bank: 0-127 (bank number, 0=GM, 1=Beatnik Special, 2=User)
 * program: 0-127 (General MIDI program number)
 */
EMSCRIPTEN_KEEPALIVE
int BAE_WASM_ProgramBankChange(int channel, int bank, int program) {
    BAE_PRINTF("[BAE] ProgramBankChange: channel=%d bank=%d program=%d song=%p\n", channel, bank, program, (void*)gCurrentSong);
    if (gCurrentSong == NULL || channel < 0 || channel > 15 ||
        bank < 0 || bank > 127 || program < 0 || program > 127) {
        BAE_PRINTF("[BAE] ProgramBankChange: INVALID - returning -1\n");
        return -1;
    }

    // BAE uses 0-based channel numbers internally
    // Time parameter 0 means immediate effect
    BAEResult err = BAESong_ProgramBankChange(gCurrentSong,
                                               (unsigned char)channel,
                                               (unsigned char)program,
                                               (unsigned char)bank,
                                               0);  // time = 0 for immediate
    BAE_PRINTF("[BAE] ProgramBankChange: result=%d\n", (int)err);
    return (int)err;
}

/*
 * ============================================
 * Effect Song Functions (for layered playback)
 * ============================================
 * These allow a second RMF/MIDI to play on top of the main song
 */

// Token for effect's embedded bank (for RMFs that contain their own samples)
static BAEBankToken gEffectBankToken = NULL;

/*
 * Load an RMF/MIDI as a sound effect (plays on top of main song)
 * For RMF files, also adds the RMF as a bank so its embedded samples can be found
 */
EMSCRIPTEN_KEEPALIVE
int BAE_WASM_LoadEffect(const uint8_t* data, int length) {
    BAEResult err;

    if (gMixer == NULL) {
        BAE_PRINTF("[BAE] LoadEffect: ERROR - mixer not initialized\n");
        return -1;
    }

    BAE_PRINTF("[BAE] LoadEffect: data=%p, length=%d\n", (void*)data, length);

    // Clean up any previous effect
    if (gEffectSong != NULL) {
        BAESong_Stop(gEffectSong, FALSE);
        BAESong_Delete(gEffectSong);
        gEffectSong = NULL;
    }
    if (gEffectBankToken != NULL) {
        BAEMixer_UnloadBank(gMixer, gEffectBankToken);
        gEffectBankToken = NULL;
    }

    // For RMF files, first add as a bank so embedded samples can be found
    if (length >= 4 && data[0] == 'I' && data[1] == 'R' && data[2] == 'E' && data[3] == 'Z') {
        BAE_PRINTF("[BAE] LoadEffect: Adding RMF as bank for embedded samples...\n");
        err = BAEMixer_AddBankFromMemory(gMixer, (void*)data, (unsigned long)length, &gEffectBankToken);
        if (err != BAE_NO_ERROR) {
            BAE_PRINTF("[BAE] LoadEffect: AddBankFromMemory warning=%d (continuing anyway)\n", (int)err);
        } else {
            // Bring to front so it's searched first
            BAEMixer_BringBankToFront(gMixer, gEffectBankToken);
            BAE_PRINTF("[BAE] LoadEffect: Bank added and brought to front\n");
        }
    }

    // Create new effect song
    gEffectSong = BAESong_New(gMixer);
    if (gEffectSong == NULL) {
        BAE_PRINTF("[BAE] LoadEffect: ERROR - failed to create BAESong\n");
        return -2;
    }

    // Detect format and load (use TRUE for ignoreBadInstruments like main song)
    if (length >= 4 && data[0] == 'I' && data[1] == 'R' && data[2] == 'E' && data[3] == 'Z') {
        BAE_PRINTF("[BAE] LoadEffect: Loading RMF song...\n");
        err = BAESong_LoadRmfFromMemory(gEffectSong, (void*)data, (unsigned long)length, 0, TRUE);
    } else {
        BAE_PRINTF("[BAE] LoadEffect: Loading MIDI song...\n");
        err = BAESong_LoadMidiFromMemory(gEffectSong, (void*)data, (unsigned long)length, TRUE);
    }

    if (err != BAE_NO_ERROR) {
        BAE_PRINTF("[BAE] LoadEffect: Load error=%d\n", (int)err);
        BAESong_Delete(gEffectSong);
        gEffectSong = NULL;
        return (int)err;
    }

    // Preroll to prepare for playback
    err = BAESong_Preroll(gEffectSong);
    if (err != BAE_NO_ERROR) {
        BAE_PRINTF("[BAE] LoadEffect: Preroll warning=%d (continuing anyway)\n", (int)err);
    }

    BAE_PRINTF("[BAE] LoadEffect: SUCCESS\n");
    return 0;
}

/*
 * Play the loaded effect (overlaid on main song)
 */
EMSCRIPTEN_KEEPALIVE
int BAE_WASM_PlayEffect(void) {
    if (gEffectSong == NULL) {
        BAE_PRINTF("[BAE] PlayEffect: ERROR - no effect loaded\n");
        return -1;
    }

    BAE_PRINTF("[BAE] PlayEffect: Starting effect song\n");
    BAEResult err = BAESong_Start(gEffectSong, 0);
    BAE_PRINTF("[BAE] PlayEffect: result=%d\n", (int)err);
    return (int)err;
}

/*
 * Stop the effect song
 */
EMSCRIPTEN_KEEPALIVE
int BAE_WASM_StopEffect(void) {
    if (gEffectSong == NULL) {
        return 0;  // Nothing to stop
    }

    BAE_PRINTF("[BAE] StopEffect: Stopping effect song\n");
    BAEResult err = BAESong_Stop(gEffectSong, FALSE);
    return (int)err;
}

/*
 * Check if effect is playing
 */
EMSCRIPTEN_KEEPALIVE
int BAE_WASM_IsEffectPlaying(void) {
    if (gEffectSong == NULL) {
        return 0;
    }

    BAE_BOOL isDone = FALSE;
    BAESong_IsDone(gEffectSong, &isDone);
    return isDone ? 0 : 1;
}

/*
 * Get the current program (instrument) on a MIDI channel
 * channel: 0-15 (MIDI channel number, 0-based)
 * Returns: program number (0-127), or -1 on error
 */
EMSCRIPTEN_KEEPALIVE
int BAE_WASM_GetProgram(int channel) {
    if (gCurrentSong == NULL || channel < 0 || channel > 15) {
        return -1;
    }

    unsigned char program = 0;
    unsigned char bank = 0;
    // BAE uses 0-based channels internally
    BAEResult err = BAESong_GetProgramBank(gCurrentSong, (unsigned char)channel, &program, &bank);
    if (err != BAE_NO_ERROR) {
        return -1;
    }

    BAE_PRINTF("[BAE] GetProgram: channel=%d program=%d bank=%d\n", channel, program, bank);
    return (int)program;
}

/*
 * ============================================
 * Sample Bank Functions (for RMF samples like voice files)
 * ============================================
 * Used for RMF files that contain only samples (snd resources)
 * without embedded MIDI. These need to be loaded as banks and
 * triggered with NoteOn.
 */

// Token for the sample bank (tell-me-about.rmf etc)
static BAEBankToken gSampleBankToken = NULL;

/*
 * Load an RMF as a sample bank (for voice/sample RMFs without MIDI)
 * This adds the RMF as a soundbank so its instruments can be triggered via NoteOn
 */
EMSCRIPTEN_KEEPALIVE
int BAE_WASM_LoadSampleBank(const uint8_t* data, int length) {
    BAEResult err;

    if (gMixer == NULL) {
        BAE_PRINTF("[BAE] LoadSampleBank: ERROR - mixer not initialized\n");
        return -1;
    }

    BAE_PRINTF("[BAE] LoadSampleBank: data=%p, length=%d\n", (void*)data, length);

    // Unload previous sample bank if any
    if (gSampleBankToken != NULL) {
        BAEMixer_UnloadBank(gMixer, gSampleBankToken);
        gSampleBankToken = NULL;
    }

    // Add this RMF as a bank
    err = BAEMixer_AddBankFromMemory(gMixer, (void*)data, (unsigned long)length, &gSampleBankToken);
    if (err != BAE_NO_ERROR) {
        BAE_PRINTF("[BAE] LoadSampleBank: AddBankFromMemory failed with error %d\n", (int)err);
        return (int)err;
    }

    // Bring to front so it's searched first
    if (gSampleBankToken != NULL) {
        BAEMixer_BringBankToFront(gMixer, gSampleBankToken);
    }

    BAE_PRINTF("[BAE] LoadSampleBank: SUCCESS, token=%p\n", (void*)gSampleBankToken);
    return 0;
}

// Dedicated song for triggering samples (separate from main song)
static BAESong gSampleTriggerSong = NULL;

/*
 * Trigger a sample from the loaded sample bank using NoteOn
 * Uses a SEPARATE song object to avoid interfering with main song
 * bank: bank number (2 for tell-me-about.rmf based on its header)
 * program: program number (0 for tell-me-about.rmf)
 * note: MIDI note number (60 = middle C is typical)
 * velocity: 0-127
 */
EMSCRIPTEN_KEEPALIVE
int BAE_WASM_TriggerSample(int bank, int program, int note, int velocity) {
    BAEResult err;

    if (gMixer == NULL) {
        BAE_PRINTF("[BAE] TriggerSample: ERROR - mixer not initialized\n");
        return -1;
    }

    BAE_PRINTF("[BAE] TriggerSample: bank=%d program=%d note=%d velocity=%d\n",
           bank, program, note, velocity);

    // Create a dedicated song for sample triggering if needed
    if (gSampleTriggerSong == NULL) {
        BAE_PRINTF("[BAE] TriggerSample: Creating dedicated sample trigger song\n");
        gSampleTriggerSong = BAESong_New(gMixer);
        if (gSampleTriggerSong == NULL) {
            BAE_PRINTF("[BAE] TriggerSample: ERROR - failed to create trigger song\n");
            return -2;
        }
        // Start the song so it can process MIDI events
        BAESong_Start(gSampleTriggerSong, 0);
    }

    // Use channel 1 on the dedicated song (won't interfere with main song)
    unsigned char channel = 1;  // 1-indexed for BAE API

    // Set bank select (CC 0 = bank MSB)
    err = BAESong_ControlChange(gSampleTriggerSong, channel, 0, (unsigned char)bank, 0);
    BAE_PRINTF("[BAE] TriggerSample: Bank select result=%d\n", (int)err);

    // Program change
    err = BAESong_ProgramChange(gSampleTriggerSong, channel, (unsigned char)program, 0);
    BAE_PRINTF("[BAE] TriggerSample: Program change result=%d\n", (int)err);

    // Send NoteOn with load (will load instrument if needed)
    err = BAESong_NoteOnWithLoad(gSampleTriggerSong, channel, (unsigned char)note, (unsigned char)velocity, 0);
    BAE_PRINTF("[BAE] TriggerSample: NoteOnWithLoad result=%d\n", (int)err);

    return (int)err;
}

/*
 * Get channel activity level (0-255) for a specific MIDI channel
 * Returns sum of active note velocities clamped to 255
 * channel: 0-15 for MIDI channels
 */
EMSCRIPTEN_KEEPALIVE
int BAE_WASM_GetChannelActivity(int channel) {
    if (gCurrentSong == NULL || channel < 0 || channel > 15) {
        return 0;
    }

    unsigned char notes[128];
    BAEResult err = BAESong_GetActiveNotes(gCurrentSong, (unsigned char)channel, notes);
    if (err != BAE_NO_ERROR) {
        return 0;
    }

    // Sum velocities of all active notes
    int activity = 0;
    for (int i = 0; i < 128; i++) {
        activity += notes[i];
    }

    // Clamp to 255
    if (activity > 255) activity = 255;

    return activity;
}

/*
 * Get active notes for a specific channel
 * channel: 0-15 (MIDI channel)
 * outNotes: pointer to 128-byte buffer that will receive note velocities (0 = off, 1-127 = velocity)
 * Returns 0 on success, -1 on error
 */
EMSCRIPTEN_KEEPALIVE
int BAE_WASM_GetActiveNotesForChannel(int channel, uint8_t* outNotes) {
    if (gCurrentSong == NULL || channel < 0 || channel > 15 || outNotes == NULL) {
        return -1;
    }

    unsigned char notes[128];
    BAEResult err = BAESong_GetActiveNotes(gCurrentSong, (unsigned char)channel, notes);
    if (err != BAE_NO_ERROR) {
        memset(outNotes, 0, 128);
        return -1;
    }

    // Copy note velocities to output buffer
    for (int i = 0; i < 128; i++) {
        outNotes[i] = notes[i];
    }

    return 0;
}

/*
 * Get activity levels for all 16 MIDI channels at once
 * outActivities: pointer to 16-byte buffer that will receive activity levels (0-255 each)
 * Returns 0 on success, -1 on error
 */
EMSCRIPTEN_KEEPALIVE
int BAE_WASM_GetAllChannelActivities(uint8_t* outActivities) {
    if (gCurrentSong == NULL || outActivities == NULL) {
        return -1;
    }

    unsigned char notes[128];

    for (int ch = 0; ch < 16; ch++) {
        BAEResult err = BAESong_GetActiveNotes(gCurrentSong, (unsigned char)ch, notes);
        if (err != BAE_NO_ERROR) {
            outActivities[ch] = 0;
            continue;
        }

        // Sum velocities of all active notes
        int activity = 0;
        for (int i = 0; i < 128; i++) {
            activity += notes[i];
        }

        // Clamp to 255
        if (activity > 255) activity = 255;
        outActivities[ch] = (uint8_t)activity;
    }

    return 0;
}

/*
 * Send a MIDI NoteOn event
 * channel: 0-15 (MIDI channel)
 * note: 0-127 (MIDI note number)
 * velocity: 1-127 (note velocity, 0 is treated as NoteOff)
 */
EMSCRIPTEN_KEEPALIVE
int BAE_WASM_NoteOn(int channel, int note, int velocity) {
    if (gCurrentSong == NULL || channel < 0 || channel > 15 ||
        note < 0 || note > 127 || velocity < 0 || velocity > 127) {
        return -1;
    }

    BAEResult err = BAESong_NoteOnWithLoad(gCurrentSong,
                                            (unsigned char)channel,
                                            (unsigned char)note,
                                            (unsigned char)velocity,
                                            0);  // time = 0 for immediate
    return (int)err;
}

/*
 * Send a MIDI NoteOff event
 * channel: 0-15 (MIDI channel)
 * note: 0-127 (MIDI note number)
 */
EMSCRIPTEN_KEEPALIVE
int BAE_WASM_NoteOff(int channel, int note) {
    if (gCurrentSong == NULL || channel < 0 || channel > 15 ||
        note < 0 || note > 127) {
        return -1;
    }

    BAEResult err = BAESong_NoteOff(gCurrentSong,
                                     (unsigned char)channel,
                                     (unsigned char)note,
                                     0,   // velocity (unused for NoteOff)
                                     0);  // time = 0 for immediate
    return (int)err;
}


#ifdef SUPPORT_KARAOKE
/*
 * Internal lyric callback that forwards to JavaScript
 */
static void PV_LyricCallback(struct GM_Song *songPtr, const char *lyric, uint32_t timeUs, void *ref) {
    (void)songPtr;
    (void)ref;
    
    // Suppress lyrics during preroll
    if (gSuppressLyrics) {
        return;
    }
    
    if (gJSLyricCallback && lyric) {
        gJSLyricCallback(lyric, timeUs);
    }
}

/*
 * Set JavaScript lyric callback function
 * callback: JavaScript function pointer that accepts (lyric: string, timeUs: number)
 * Returns: 0 on success, -1 on error
 */
EMSCRIPTEN_KEEPALIVE
int BAE_WASM_SetLyricCallback(JSLyricCallback callback) {
    gJSLyricCallback = callback;
    
    if (gCurrentSong == NULL) {
        return -1;
    }
    
    // External function from MiniBAE.c
    extern BAEResult BAESong_SetLyricCallback(BAESong song, GM_SongLyricCallbackProcPtr pCallback, void *callbackReference);
    
    BAEResult err = BAESong_SetLyricCallback(gCurrentSong, callback ? PV_LyricCallback : NULL, NULL);
    return (int)err;
}

/*
 * Reset lyric state (clear accumulated lyrics)
 * Returns: 0 on success, -1 on error
 */
EMSCRIPTEN_KEEPALIVE
int BAE_WASM_ResetLyricState(void) {
    if (gCurrentSong == NULL) {
        return -1;
    }
    
    // External function from MiniBAE.c
    extern BAEResult BAESong_ResetLyricState(BAESong song);
    
    BAEResult err = BAESong_ResetLyricState(gCurrentSong);
    return (int)err;
}
#endif // SUPPORT_KARAOKE

/*
 * Ring Buffer Implementation
 * Provides 250ms audio buffering for smooth playback
 */

/*
 * Initialize ring buffer for 250ms of audio at given sample rate
 */
static void RingBuffer_Init(int sampleRate) {
    // Clean up existing buffer if any
    RingBuffer_Destroy();
    
    // Calculate buffer size for 250ms (0.25 seconds)
    // frames = sampleRate * 0.25
    size_t bufferFrames = (size_t)(sampleRate * 0.25);
    
    // Allocate buffer (stereo = 2 channels)
    gRingBuffer.buffer = (int16_t*)malloc(bufferFrames * 2 * sizeof(int16_t));
    if (gRingBuffer.buffer == NULL) {
        BAE_PRINTF("[BAE] RingBuffer_Init: ERROR - Failed to allocate buffer\n");
        return;
    }
    
    gRingBuffer.capacity = bufferFrames;
    gRingBuffer.writePos = 0;
    gRingBuffer.readPos = 0;
    gRingBuffer.available = 0;
    
    // Clear buffer
    memset(gRingBuffer.buffer, 0, bufferFrames * 2 * sizeof(int16_t));
    
    BAE_PRINTF("[BAE] RingBuffer_Init: Initialized %zu frames (%.1fms at %d Hz)\n",
           bufferFrames, (bufferFrames * 1000.0) / sampleRate, sampleRate);
}

/*
 * Destroy ring buffer and free memory
 */
static void RingBuffer_Destroy(void) {
    if (gRingBuffer.buffer != NULL) {
        free(gRingBuffer.buffer);
        gRingBuffer.buffer = NULL;
    }
    gRingBuffer.capacity = 0;
    gRingBuffer.writePos = 0;
    gRingBuffer.readPos = 0;
    gRingBuffer.available = 0;
}

/*
 * Clear ring buffer (reset to empty state)
 */
static void RingBuffer_Clear(void) {
    if (gRingBuffer.buffer != NULL) {
        memset(gRingBuffer.buffer, 0, gRingBuffer.capacity * 2 * sizeof(int16_t));
    }
    gRingBuffer.writePos = 0;
    gRingBuffer.readPos = 0;
    gRingBuffer.available = 0;
}

/*
 * Write audio frames to ring buffer
 * Returns number of frames actually written
 */
static size_t RingBuffer_Write(const int16_t *data, size_t frames) {
    if (gRingBuffer.buffer == NULL || data == NULL) {
        return 0;
    }
    
    // Calculate how much space is available
    size_t spaceAvailable = gRingBuffer.capacity - gRingBuffer.available;
    size_t framesToWrite = (frames < spaceAvailable) ? frames : spaceAvailable;
    
    if (framesToWrite == 0) {
        return 0;  // Buffer is full
    }
    
    // Write in two parts if wrapping around
    size_t firstPart = gRingBuffer.capacity - gRingBuffer.writePos;
    if (firstPart > framesToWrite) {
        firstPart = framesToWrite;
    }
    
    // Copy first part (stereo = 2 samples per frame)
    memcpy(&gRingBuffer.buffer[gRingBuffer.writePos * 2],
           data,
           firstPart * 2 * sizeof(int16_t));
    
    // Copy second part if wrapping
    if (framesToWrite > firstPart) {
        size_t secondPart = framesToWrite - firstPart;
        memcpy(gRingBuffer.buffer,
               &data[firstPart * 2],
               secondPart * 2 * sizeof(int16_t));
    }
    
    // Update write position and available count
    gRingBuffer.writePos = (gRingBuffer.writePos + framesToWrite) % gRingBuffer.capacity;
    gRingBuffer.available += framesToWrite;
    
    return framesToWrite;
}

/*
 * Read audio frames from ring buffer
 * Returns number of frames actually read
 */
static size_t RingBuffer_Read(int16_t *dest, size_t frames) {
    if (gRingBuffer.buffer == NULL || dest == NULL) {
        return 0;
    }
    
    // Can't read more than available
    size_t framesToRead = (frames < gRingBuffer.available) ? frames : gRingBuffer.available;
    
    if (framesToRead == 0) {
        // Buffer is empty - fill with silence
        memset(dest, 0, frames * 2 * sizeof(int16_t));
        return 0;
    }
    
    // Read in two parts if wrapping around
    size_t firstPart = gRingBuffer.capacity - gRingBuffer.readPos;
    if (firstPart > framesToRead) {
        firstPart = framesToRead;
    }
    
    // Copy first part (stereo = 2 samples per frame)
    memcpy(dest,
           &gRingBuffer.buffer[gRingBuffer.readPos * 2],
           firstPart * 2 * sizeof(int16_t));
    
    // Copy second part if wrapping
    if (framesToRead > firstPart) {
        size_t secondPart = framesToRead - firstPart;
        memcpy(&dest[firstPart * 2],
               gRingBuffer.buffer,
               secondPart * 2 * sizeof(int16_t));
    }
    
    // Fill remaining with silence if not enough data
    if (framesToRead < frames) {
        memset(&dest[framesToRead * 2], 0, (frames - framesToRead) * 2 * sizeof(int16_t));
    }
    
    // Update read position and available count
    gRingBuffer.readPos = (gRingBuffer.readPos + framesToRead) % gRingBuffer.capacity;
    gRingBuffer.available -= framesToRead;
    
    return framesToRead;
}
