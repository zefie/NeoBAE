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

// Debug output control - set to 0 to disable all console logging
#define BAE_DEBUG 1

#if BAE_DEBUG
#define BAE_LOG(...) printf(__VA_ARGS__)
#else
#define BAE_LOG(...) ((void)0)
#endif

// Include miniBAE headers
#include "MiniBAE.h"
#include "X_API.h"
#include "GenSnd.h"
#include "GenPriv.h"  // for MusicGlobals (mixer cache)
#include "GenPriv.h"

// Global mixer instance
static BAEMixer gMixer = NULL;
static BAESong gCurrentSong = NULL;
static BAESong gEffectSong = NULL;  // Second song for sound effects (plays on top)

// Audio buffer for JS interop
#define AUDIO_BUFFER_FRAMES 512
static int16_t gAudioBuffer[AUDIO_BUFFER_FRAMES * 2];  // Stereo

// External function from GenSynth.c
extern void BAE_BuildMixerSlice(void *threadContext, void *pAudioBuffer,
                                int32_t bufferByteLength, int32_t sampleFrames);

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

    BAE_LOG("[BAE] LoadSoundbank: data=%p, length=%d\n", (void*)data, length);
    BAE_LOG("[BAE] LoadSoundbank: Header bytes: %02X %02X %02X %02X\n",
           data[0], data[1], data[2], data[3]);

    if (gMixer == NULL) {
        BAE_LOG("[BAE] LoadSoundbank: ERROR - gMixer is NULL\n");
        return -1;  // Not initialized
    }

    // Unload existing banks
    BAE_LOG("[BAE] LoadSoundbank: Unloading existing banks...\n");
    BAEMixer_UnloadBanks(gMixer);

    // Add bank from memory
    BAE_LOG("[BAE] LoadSoundbank: Adding bank from memory...\n");
    err = BAEMixer_AddBankFromMemory(gMixer, (void*)data, (unsigned long)length, &token);
    BAE_LOG("[BAE] LoadSoundbank: AddBankFromMemory result=%d\n", (int)err);
    if (err != BAE_NO_ERROR) {
        BAE_LOG("[BAE] LoadSoundbank: ERROR - AddBankFromMemory failed with code %d\n", (int)err);
        return (int)err;
    }

    // Ensure the newly loaded bank is active/at front
    if (token != NULL) {
        BAEMixer_BringBankToFront(gMixer, token);
        XFileUseThisResourceFile((XFILE)token);
    }

    BAE_LOG("[BAE] LoadSoundbank: SUCCESS\n");
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

    BAE_LOG("[BAE] LoadSong: data=%p, length=%d\n", (void*)data, length);

    if (gMixer == NULL) {
        BAE_LOG("[BAE] LoadSong: ERROR - gMixer is NULL\n");
        return -1;
    }
    BAE_LOG("[BAE] LoadSong: gMixer=%p\n", (void*)gMixer);

    // Stop and delete existing song
    if (gCurrentSong != NULL) {
        BAE_LOG("[BAE] LoadSong: Stopping/deleting existing song\n");
        BAESong_Stop(gCurrentSong, FALSE);
        BAESong_Delete(gCurrentSong);
        gCurrentSong = NULL;
    }

    // Create new song
    BAE_LOG("[BAE] LoadSong: Creating new BAESong...\n");
    gCurrentSong = BAESong_New(gMixer);
    if (gCurrentSong == NULL) {
        BAE_LOG("[BAE] LoadSong: ERROR - BAESong_New returned NULL\n");
        return -2;
    }
    BAE_LOG("[BAE] LoadSong: BAESong created=%p\n", (void*)gCurrentSong);

    // Load from memory - detect file type
    if (isRmfFile(data, length)) {
        BAE_LOG("[BAE] LoadSong: Detected RMF file, loading...\n");
        // RMF: song, data, size, songIndex, ignoreBadInstruments
        err = BAESong_LoadRmfFromMemory(gCurrentSong, (void*)data, (unsigned long)length, 0, TRUE);
    } else {
        BAE_LOG("[BAE] LoadSong: Detected MIDI file (magic: %02X %02X %02X %02X), loading...\n",
               data[0], data[1], data[2], data[3]);
        // MIDI: song, data, size, ignoreBadInstruments
        err = BAESong_LoadMidiFromMemory(gCurrentSong, (void*)data, (unsigned long)length, TRUE);
    }

    BAE_LOG("[BAE] LoadSong: Load result=%d\n", (int)err);

    if (err != BAE_NO_ERROR) {
        BAE_LOG("[BAE] LoadSong: ERROR - Load failed with code %d\n", (int)err);
        BAESong_Delete(gCurrentSong);
        gCurrentSong = NULL;
        return (int)err;
    }

    // Preroll (load instruments)
    BAE_LOG("[BAE] LoadSong: Prerolling...\n");
    err = BAESong_Preroll(gCurrentSong);
    BAE_LOG("[BAE] LoadSong: Preroll result=%d\n", (int)err);
    if (err != BAE_NO_ERROR) {
        BAE_LOG("[BAE] LoadSong: ERROR - Preroll failed with code %d\n", (int)err);
        BAESong_Delete(gCurrentSong);
        gCurrentSong = NULL;
        return (int)err;
    }

    BAE_LOG("[BAE] LoadSong: SUCCESS\n");
    return 0;
}

/*
 * Start playback
 */
EMSCRIPTEN_KEEPALIVE
int BAE_WASM_Play(void) {
    BAE_LOG("[BAE] Play: gCurrentSong=%p\n", (void*)gCurrentSong);
    if (gCurrentSong == NULL) {
        BAE_LOG("[BAE] Play: ERROR - no song loaded\n");
        return -1;
    }

    // Resume mixer/sequencer if paused
    GM_ResumeGeneralSound(NULL);

    BAEResult err = BAESong_Start(gCurrentSong, 0);
    BAE_LOG("[BAE] Play: BAESong_Start returned %d\n", (int)err);
    return (int)err;
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

    BAESong_Stop(gCurrentSong, TRUE);
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
    BAESong_GetMicrosecondPosition(gCurrentSong, &pos);
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
    BAESong_GetMicrosecondLength(gCurrentSong, &duration);
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
    if (volume > 100) volume = 100;

    // Convert to BAE 16.16 fixed point format where BAE_FIXED_1 (0x10000 = 65536) = 1.0
    // volume 100% -> 0x10000, volume 50% -> 0x8000, volume 0% -> 0
    BAE_UNSIGNED_FIXED fixedVol = (BAE_UNSIGNED_FIXED)((volume * 0x10000L) / 100);
    BAEMixer_SetMasterVolume(gMixer, fixedVol);
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
 * Called from AudioWorklet
 * Returns: pointer to audio buffer (interleaved stereo 16-bit)
 */
EMSCRIPTEN_KEEPALIVE
int16_t* BAE_WASM_GenerateAudio(int frames) {
    gGenerateAudioCallCount++;

    // Log every 100 calls to avoid spam
    if (BAE_DEBUG && (gGenerateAudioCallCount <= 5 || (gGenerateAudioCallCount % 100) == 0)) {
        BAE_LOG("[BAE] GenerateAudio: call #%d, frames=%d, gMixer=%p\n",
               gGenerateAudioCallCount, frames, (void*)gMixer);
    }

    if (gMixer == NULL) {
        // Fill with silence
        memset(gAudioBuffer, 0, frames * 2 * sizeof(int16_t));
        return gAudioBuffer;
    }

    // Clamp frames to buffer size
    if (frames > AUDIO_BUFFER_FRAMES) {
        frames = AUDIO_BUFFER_FRAMES;
    }

    // Calculate buffer byte length (stereo 16-bit)
    long bufferByteLength = frames * 2 * sizeof(int16_t);

    // Generate audio using BAE's mixer slice function
    BAE_BuildMixerSlice(NULL, (void*)gAudioBuffer, bufferByteLength, frames);

    // Apply output gain and soft limiting to prevent harsh clipping
    if (gOutputGain != 256) {
        int totalSamples = frames * 2;  // Stereo
        for (int i = 0; i < totalSamples; i++) {
            int32_t sample = gAudioBuffer[i];
            // Apply gain
            sample = (sample * gOutputGain) >> 8;
            // Soft clip to prevent harsh distortion
            if (sample > 32767) sample = 32767;
            else if (sample < -32768) sample = -32768;
            gAudioBuffer[i] = (int16_t)sample;
        }
    }

    // Log first few samples for debugging (only first 5 calls)
    if (BAE_DEBUG && gGenerateAudioCallCount <= 5) {
        BAE_LOG("[BAE] GenerateAudio: first samples: %d %d %d %d\n",
               gAudioBuffer[0], gAudioBuffer[1], gAudioBuffer[2], gAudioBuffer[3]);
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
    BAE_LOG("[BAE] ProgramChange: channel=%d program=%d song=%p\n", channel, program, (void*)gCurrentSong);
    if (gCurrentSong == NULL || channel < 0 || channel > 15 || program < 0 || program > 127) {
        BAE_LOG("[BAE] ProgramChange: INVALID - returning -1\n");
        return -1;
    }

    // BAE uses 0-based channel numbers internally (PERCUSSION_CHANNEL = 9)
    unsigned char baeChannel = (unsigned char)channel;
    BAEResult err;

    // First do the program change
    err = BAESong_ProgramChange(gCurrentSong, baeChannel, (unsigned char)program, 0);
    BAE_LOG("[BAE] ProgramChange: ProgramChange result=%d\n", (int)err);

    // Now use NoteOnWithLoad to force the instrument to load
    // Use middle C (note 60) with velocity 0 - this loads but doesn't play
    // Actually, velocity 0 is treated as NoteOff, so use velocity 1 then NoteOff
    err = BAESong_NoteOnWithLoad(gCurrentSong, baeChannel, 60, 1, 0);
    BAE_LOG("[BAE] ProgramChange: NoteOnWithLoad result=%d\n", (int)err);

    // Immediately send NoteOff so we don't hear a blip
    // NoteOff takes: song, channel, note, velocity, time
    err = BAESong_NoteOff(gCurrentSong, baeChannel, 60, 0, 0);
    BAE_LOG("[BAE] ProgramChange: NoteOff result=%d\n", (int)err);

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
    BAE_LOG("[BAE] ProgramBankChange: channel=%d bank=%d program=%d song=%p\n", channel, bank, program, (void*)gCurrentSong);
    if (gCurrentSong == NULL || channel < 0 || channel > 15 ||
        bank < 0 || bank > 127 || program < 0 || program > 127) {
        BAE_LOG("[BAE] ProgramBankChange: INVALID - returning -1\n");
        return -1;
    }

    // BAE uses 0-based channel numbers internally
    // Time parameter 0 means immediate effect
    BAEResult err = BAESong_ProgramBankChange(gCurrentSong,
                                               (unsigned char)channel,
                                               (unsigned char)program,
                                               (unsigned char)bank,
                                               0);  // time = 0 for immediate
    BAE_LOG("[BAE] ProgramBankChange: result=%d\n", (int)err);
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
        BAE_LOG("[BAE] LoadEffect: ERROR - mixer not initialized\n");
        return -1;
    }

    BAE_LOG("[BAE] LoadEffect: data=%p, length=%d\n", (void*)data, length);

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
        BAE_LOG("[BAE] LoadEffect: Adding RMF as bank for embedded samples...\n");
        err = BAEMixer_AddBankFromMemory(gMixer, (void*)data, (unsigned long)length, &gEffectBankToken);
        if (err != BAE_NO_ERROR) {
            BAE_LOG("[BAE] LoadEffect: AddBankFromMemory warning=%d (continuing anyway)\n", (int)err);
        } else {
            // Bring to front so it's searched first
            BAEMixer_BringBankToFront(gMixer, gEffectBankToken);
            BAE_LOG("[BAE] LoadEffect: Bank added and brought to front\n");
        }
    }

    // Create new effect song
    gEffectSong = BAESong_New(gMixer);
    if (gEffectSong == NULL) {
        BAE_LOG("[BAE] LoadEffect: ERROR - failed to create BAESong\n");
        return -2;
    }

    // Detect format and load (use TRUE for ignoreBadInstruments like main song)
    if (length >= 4 && data[0] == 'I' && data[1] == 'R' && data[2] == 'E' && data[3] == 'Z') {
        BAE_LOG("[BAE] LoadEffect: Loading RMF song...\n");
        err = BAESong_LoadRmfFromMemory(gEffectSong, (void*)data, (unsigned long)length, 0, TRUE);
    } else {
        BAE_LOG("[BAE] LoadEffect: Loading MIDI song...\n");
        err = BAESong_LoadMidiFromMemory(gEffectSong, (void*)data, (unsigned long)length, TRUE);
    }

    if (err != BAE_NO_ERROR) {
        BAE_LOG("[BAE] LoadEffect: Load error=%d\n", (int)err);
        BAESong_Delete(gEffectSong);
        gEffectSong = NULL;
        return (int)err;
    }

    // Preroll to prepare for playback
    err = BAESong_Preroll(gEffectSong);
    if (err != BAE_NO_ERROR) {
        BAE_LOG("[BAE] LoadEffect: Preroll warning=%d (continuing anyway)\n", (int)err);
    }

    BAE_LOG("[BAE] LoadEffect: SUCCESS\n");
    return 0;
}

/*
 * Play the loaded effect (overlaid on main song)
 */
EMSCRIPTEN_KEEPALIVE
int BAE_WASM_PlayEffect(void) {
    if (gEffectSong == NULL) {
        BAE_LOG("[BAE] PlayEffect: ERROR - no effect loaded\n");
        return -1;
    }

    BAE_LOG("[BAE] PlayEffect: Starting effect song\n");
    BAEResult err = BAESong_Start(gEffectSong, 0);
    BAE_LOG("[BAE] PlayEffect: result=%d\n", (int)err);
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

    BAE_LOG("[BAE] StopEffect: Stopping effect song\n");
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

    BAE_LOG("[BAE] GetProgram: channel=%d program=%d bank=%d\n", channel, program, bank);
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
        BAE_LOG("[BAE] LoadSampleBank: ERROR - mixer not initialized\n");
        return -1;
    }

    BAE_LOG("[BAE] LoadSampleBank: data=%p, length=%d\n", (void*)data, length);

    // Unload previous sample bank if any
    if (gSampleBankToken != NULL) {
        BAEMixer_UnloadBank(gMixer, gSampleBankToken);
        gSampleBankToken = NULL;
    }

    // Add this RMF as a bank
    err = BAEMixer_AddBankFromMemory(gMixer, (void*)data, (unsigned long)length, &gSampleBankToken);
    if (err != BAE_NO_ERROR) {
        BAE_LOG("[BAE] LoadSampleBank: AddBankFromMemory failed with error %d\n", (int)err);
        return (int)err;
    }

    // Bring to front so it's searched first
    if (gSampleBankToken != NULL) {
        BAEMixer_BringBankToFront(gMixer, gSampleBankToken);
    }

    BAE_LOG("[BAE] LoadSampleBank: SUCCESS, token=%p\n", (void*)gSampleBankToken);
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
        BAE_LOG("[BAE] TriggerSample: ERROR - mixer not initialized\n");
        return -1;
    }

    BAE_LOG("[BAE] TriggerSample: bank=%d program=%d note=%d velocity=%d\n",
           bank, program, note, velocity);

    // Create a dedicated song for sample triggering if needed
    if (gSampleTriggerSong == NULL) {
        BAE_LOG("[BAE] TriggerSample: Creating dedicated sample trigger song\n");
        gSampleTriggerSong = BAESong_New(gMixer);
        if (gSampleTriggerSong == NULL) {
            BAE_LOG("[BAE] TriggerSample: ERROR - failed to create trigger song\n");
            return -2;
        }
        // Start the song so it can process MIDI events
        BAESong_Start(gSampleTriggerSong, 0);
    }

    // Use channel 1 on the dedicated song (won't interfere with main song)
    unsigned char channel = 1;  // 1-indexed for BAE API

    // Set bank select (CC 0 = bank MSB)
    err = BAESong_ControlChange(gSampleTriggerSong, channel, 0, (unsigned char)bank, 0);
    BAE_LOG("[BAE] TriggerSample: Bank select result=%d\n", (int)err);

    // Program change
    err = BAESong_ProgramChange(gSampleTriggerSong, channel, (unsigned char)program, 0);
    BAE_LOG("[BAE] TriggerSample: Program change result=%d\n", (int)err);

    // Send NoteOn with load (will load instrument if needed)
    err = BAESong_NoteOnWithLoad(gSampleTriggerSong, channel, (unsigned char)note, (unsigned char)velocity, 0);
    BAE_LOG("[BAE] TriggerSample: NoteOnWithLoad result=%d\n", (int)err);

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
