/****************************************************************************
 *
 * GenBassMidi.c
 *
 * BassMidi integration for miniBAE
 * Provides SF2 soundfont support through BassMidi when USE_SF2_SUPPORT is enabled
 *
 ****************************************************************************/

#include "GenSF2_BassMidi.h"
#if USE_SF2_SUPPORT == TRUE && _USING_BASSMIDI == TRUE

#include "bass.h"
#include "bassmidi.h"
#include "GenSnd.h"
#include "GenPriv.h"
#include "X_Assert.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "MiniBAE.h"

#define SAMPLE_BLOCK_SIZE 512

// Global BassMidi state
static HSTREAM g_bassmidi_stream = 0;
static HSOUNDFONT g_bassmidi_soundfont = 0;
static XBOOL g_bassmidi_initialized = FALSE;
static XBOOL g_bassmidi_mono_mode = FALSE;
static XFIXED g_bassmidi_master_volume = XFIXED_1;
static int16_t g_bassmidi_max_voices = MAX_VOICES;
static uint16_t g_bassmidi_sample_rate = 44100;
static char g_bassmidi_sf2_path[256] = {0};
typedef struct {
    float left, right;
} ChannelLevel;

ChannelLevel g_midiLevels[MAX_CHANNELS];

// Audio mixing buffer for BassMidi output
static float* g_bassmidi_mix_buffer = NULL;
static int32_t g_bassmidi_mix_buffer_frames = 0;

// Private function prototypes
static XBOOL PV_SF2_CheckChannelMuted(GM_Song* pSong, int16_t channel);
static void PV_SF2_ConvertFloatToInt32(float* input, int32_t* output, int32_t frameCount, float songVolumeScale, const float *channelScales);
static void PV_SF2_AllocateMixBuffer(int32_t frameCount);
static void PV_SF2_FreeMixBuffer(void);

// this function is for getting and storing the levels for sf2_get_channel_amplitudes()
void CALLBACK LevelDSP(HDSP handle, DWORD channel, void *buffer, DWORD length, void *user) {
    ChannelLevel *lvl = (ChannelLevel *)user;
    float *samples = (float *)buffer;
    DWORD count = length / sizeof(float);

    float sumL = 0.0f, sumR = 0.0f;
    DWORD frames = count / 2;

    for (DWORD i = 0; i < count; i += 2) {
        sumL += samples[i] * samples[i];       // Left channel
        sumR += samples[i + 1] * samples[i + 1]; // Right channel
    }

    lvl->left = sqrtf(sumL / frames) * 10;
    lvl->right = sqrtf(sumR / frames) * 10;
}


// Initialize BassMidi support for the mixer
OPErr GM_InitializeSF2(void)
{
    if (g_bassmidi_initialized)
    {
        return NO_ERR;
    }
    
    // Derive mixer sample rate from outputRate enum
    GM_Mixer* pMixer = GM_GetCurrentMixer();
    if (pMixer)
    {
        pMixer->isSF2 = TRUE;
        g_bassmidi_sample_rate = (int16_t)GM_ConvertFromOutputRateToRate(pMixer->outputRate);
        if (g_bassmidi_sample_rate <= 0)
        {
            g_bassmidi_sample_rate = 44100; // fallback
        }
    }

    // Initialize BASS
    if (!BASS_Init(-1, g_bassmidi_sample_rate, 0, 0, NULL))
    {
        return GENERAL_BAD;
    }
    

    // Create BassMidi stream
    g_bassmidi_stream = BASS_MIDI_StreamCreate(16, BASS_SAMPLE_FLOAT | BASS_STREAM_DECODE, g_bassmidi_sample_rate);
    BASS_SetConfig(BASS_CONFIG_BUFFER, 100);
    if (!g_bassmidi_stream)
    {
        BASS_Free();
        return GENERAL_BAD;
    }

    // set up the channel level DSP logger
    for (int ch = 0; ch < (MAX_CHANNELS - 1); ++ch) {
        HSTREAM chanHandle = BASS_MIDI_StreamGetChannel(g_bassmidi_stream, ch);
        BASS_ChannelSetDSP(chanHandle, LevelDSP, &g_midiLevels[ch], 0);
    }

    g_bassmidi_initialized = TRUE;
    return NO_ERR;
}

void GM_SetMixerSF2Mode(XBOOL isSF2) {
    GM_Mixer* pMixer = GM_GetCurrentMixer();
    if (pMixer) {
        pMixer->isSF2 = isSF2;
    }
}

XBOOL GM_GetMixerSF2Mode() {
    GM_Mixer* pMixer = GM_GetCurrentMixer();
    if (pMixer) {
        return pMixer->isSF2;
    }
    return FALSE;
}

void GM_SF2_SetDefaultControllers(int16_t channel)
{
    if (!g_bassmidi_initialized || !g_bassmidi_stream)
    {
        return;
    }    
    // Set default controllers with lower volumes
    BASS_MIDI_StreamEvent(g_bassmidi_stream, channel, MIDI_EVENT_VOLUME, 127); // Volume
    BASS_MIDI_StreamEvent(g_bassmidi_stream, channel, MIDI_EVENT_EXPRESSION, 127); // Expression
    BASS_MIDI_StreamEvent(g_bassmidi_stream, channel, MIDI_EVENT_PAN, 64); // Pan center
    BASS_MIDI_StreamEvent(g_bassmidi_stream, channel, MIDI_EVENT_MODULATION, 0); // Modulation wheel
    BASS_MIDI_StreamEvent(g_bassmidi_stream, channel, MIDI_EVENT_SUSTAIN, 0); // Sustain pedal off
    BASS_MIDI_StreamEvent(g_bassmidi_stream, channel, MIDI_EVENT_REVERB, 0); // Reverb off
    BASS_MIDI_StreamEvent(g_bassmidi_stream, channel, MIDI_EVENT_CHORUS, 0); // Chorus off
    BASS_MIDI_StreamEvent(g_bassmidi_stream, channel, MIDI_EVENT_PORTAMENTO, 0);    // Portamento off
    BASS_MIDI_StreamEvent(g_bassmidi_stream, channel, MIDI_EVENT_SOSTENUTO, 0);    // Sostenuto off
    BASS_MIDI_StreamEvent(g_bassmidi_stream, channel, MIDI_EVENT_SOFT,  0);    // Soft pedal off
    BASS_MIDI_StreamEvent(g_bassmidi_stream, channel, MIDI_EVENT_REVERB, 20);   // Reverb send (reduced)
    BASS_MIDI_StreamEvent(g_bassmidi_stream, channel, MIDI_EVENT_CHORUS, 0);    // Chorus send
    BASS_MIDI_StreamEvent(g_bassmidi_stream, channel, MIDI_EVENT_PITCH, 8192); // Center pitch bend
}

void GM_SF2_SetSampleRate(int32_t sampleRate)
{
    if (!g_bassmidi_initialized)
    {
        // Store for later initialization
        g_bassmidi_sample_rate = (uint16_t)sampleRate;
        return;
    }

    // Don't recreate if rate hasn't changed
    if (g_bassmidi_sample_rate == sampleRate)
    {
        return;
    }

    g_bassmidi_sample_rate = (uint16_t)sampleRate;
    
    // Need to recreate the BASSMIDI stream with new sample rate
    // Save current soundfont handle and path
    HSOUNDFONT oldSoundfont = g_bassmidi_soundfont;
    char oldPath[256];
    strcpy(oldPath, g_bassmidi_sf2_path);
    
    // Clean up old stream but keep soundfont
    if (g_bassmidi_stream)
    {
        BASS_StreamFree(g_bassmidi_stream);
        g_bassmidi_stream = 0;
    }
    
    // Recreate BASS with new sample rate
    BASS_Free();
    if (!BASS_Init(-1, g_bassmidi_sample_rate, 0, 0, NULL))
    {
        // Failed to reinitialize BASS - try to restore old state
        g_bassmidi_initialized = FALSE;
        return;
    }
    
    // Create new BassMidi stream with new sample rate
    g_bassmidi_stream = BASS_MIDI_StreamCreate(16, BASS_SAMPLE_FLOAT | BASS_STREAM_DECODE, g_bassmidi_sample_rate);
    BASS_SetConfig(BASS_CONFIG_MIDI_VOICES, g_bassmidi_max_voices);
    BASS_SetConfig(BASS_CONFIG_BUFFER, 100);
    
    if (!g_bassmidi_stream)
    {
        // Failed to create stream - cleanup
        BASS_Free();
        g_bassmidi_initialized = FALSE;
        g_bassmidi_soundfont = 0;
        return;
    }
    
    // Restore soundfont if we had one
    if (oldSoundfont && oldPath[0] != '\0')
    {
        // Need to reload the soundfont since we freed BASS
        g_bassmidi_soundfont = BASS_MIDI_FontInit(oldPath, 0);
        if (g_bassmidi_soundfont)
        {
            // Set the soundfont for the new stream
            BASS_MIDI_FONT font;
            font.font = g_bassmidi_soundfont;
            font.preset = -1;  // All presets
            font.bank = 0;
            
            if (BASS_MIDI_StreamSetFonts(g_bassmidi_stream, &font, 1))
            {
                // Reinitialize channels with defaults
                // Set Ch 10 to percussion by default
                BASS_MIDI_StreamEvent(g_bassmidi_stream, 9, MIDI_EVENT_BANK, 127);
                BASS_MIDI_StreamEvent(g_bassmidi_stream, 9, MIDI_EVENT_PROGRAM, 0);
                
                // Initialize all channels with reduced volumes
                for (int channel = 0; channel < 16; channel++)
                {
                    // Set default controllers with lower volumes
                    GM_SF2_SetDefaultControllers(channel);
                    // Set default programs
                    if (channel != 9) // Non-percussion channels
                    {
                        BASS_MIDI_StreamEvent(g_bassmidi_stream, channel, MIDI_EVENT_BANK, 0);
                        BASS_MIDI_StreamEvent(g_bassmidi_stream, channel, MIDI_EVENT_PROGRAM, 0);
                    }
                }
                
                BASS_MIDI_FontSetVolume(g_bassmidi_soundfont, 1.0f);
            }
            else
            {
                // Failed to set soundfont - cleanup
                BASS_MIDI_FontFree(g_bassmidi_soundfont);
                g_bassmidi_soundfont = 0;
            }
        }
    }
}

void GM_CleanupSF2(void)
{
    if (!g_bassmidi_initialized)
    {
        return;
    }
    
    PV_SF2_FreeMixBuffer();
    
    if (g_bassmidi_soundfont)
    {
        BASS_MIDI_FontFree(g_bassmidi_soundfont);
        g_bassmidi_soundfont = 0;
    }
    
    if (g_bassmidi_stream)
    {
        BASS_StreamFree(g_bassmidi_stream);
        g_bassmidi_stream = 0;
    }
    
    BASS_Free();
    g_bassmidi_initialized = FALSE;
}

// Load SF2 soundfont for SF2 rendering
OPErr GM_LoadSF2Soundfont(const char* sf2_path)
{
    if (!g_bassmidi_initialized)
    {
        OPErr result = GM_InitializeSF2();
        if (result != NO_ERR)
        {
            return result;
        }
    }
    
    // Unload previous soundfont if any
    GM_UnloadSF2Soundfont();

    // Load new soundfont
    g_bassmidi_soundfont = BASS_MIDI_FontInit(sf2_path, 0);
    if (!g_bassmidi_soundfont)
    {
        return FILE_NOT_FOUND;
    }
    
    // Set the soundfont for the stream
    BASS_MIDI_FONT font;
    font.font = g_bassmidi_soundfont;
    font.preset = -1;  // All presets
    font.bank = 0;
    
    if (!BASS_MIDI_StreamSetFonts(g_bassmidi_stream, &font, 1))
    {
        BASS_MIDI_FontFree(g_bassmidi_soundfont);
        g_bassmidi_soundfont = 0;
        return GENERAL_BAD;
    }
    
    // Store the path
    strncpy(g_bassmidi_sf2_path, sf2_path, sizeof(g_bassmidi_sf2_path) - 1);
    g_bassmidi_sf2_path[sizeof(g_bassmidi_sf2_path) - 1] = '\0';
    
    // Set Ch 10 to percussion by default
    BASS_MIDI_StreamEvent(g_bassmidi_stream, 9, MIDI_EVENT_BANK, 127);
    BASS_MIDI_StreamEvent(g_bassmidi_stream, 9, MIDI_EVENT_PROGRAM, 0);
    
    // Initialize all channels with GM defaults (reduced volumes)
    for (int channel = 0; channel < 16; channel++)
    {
        // Set default controllers with lower volumes
        BASS_MIDI_StreamEvent(g_bassmidi_stream, channel, MIDI_EVENT_VOLUME, 127);    // Volume (reduced from 100)
        BASS_MIDI_StreamEvent(g_bassmidi_stream, channel, MIDI_EVENT_EXPRESSION, 127);  // Expression (reduced from 127)
        BASS_MIDI_StreamEvent(g_bassmidi_stream, channel, MIDI_EVENT_PAN, 64);   // Pan (center)
        BASS_MIDI_StreamEvent(g_bassmidi_stream, channel, MIDI_EVENT_MODULATION, 0);     // Modulation wheel
        BASS_MIDI_StreamEvent(g_bassmidi_stream, channel, MIDI_EVENT_SUSTAIN, 0);    // Sustain pedal off
        BASS_MIDI_StreamEvent(g_bassmidi_stream, channel, MIDI_EVENT_REVERB, 0);   // Reverb send
        BASS_MIDI_StreamEvent(g_bassmidi_stream, channel, MIDI_EVENT_CHORUS, 0);    // Chorus send
        
        // Set default programs
        if (channel != 9) // Non-percussion channels
        {
            BASS_MIDI_StreamEvent(g_bassmidi_stream, channel, MIDI_EVENT_BANK, 0);     // Bank 0
            BASS_MIDI_StreamEvent(g_bassmidi_stream, channel, MIDI_EVENT_PROGRAM, 0);  // Acoustic Grand Piano
        }
        // Channel 9 (percussion) already set above
    }
    
    BASS_MIDI_FontSetVolume(g_bassmidi_soundfont, 0.35f);
    return NO_ERR;
}

// Check if a song should use BassMidi rendering (based on loaded soundfont bank type)
XBOOL GM_IsSF2Song(GM_Song* pSong)
{
    if (!g_bassmidi_initialized || !g_bassmidi_soundfont || !pSong)
    {
        return FALSE;
    }
    
    // Check if song is flagged for SF2 rendering
    // Use the built-in isSF2Song field in GM_Song structure
    return pSong->isSF2Song;
}

void GM_UnloadSF2Soundfont(void)
{
    if (g_bassmidi_soundfont)
    {
        BASS_MIDI_FontFree(g_bassmidi_soundfont);
        g_bassmidi_soundfont = 0;
        g_bassmidi_sf2_path[0] = '\0';
    }
}

// Check if a song should use BassMidi rendering
XBOOL GM_IsBassMidiSong(GM_Song* pSong)
{
    if (!pSong || !g_bassmidi_initialized || !g_bassmidi_soundfont)
    {
        return FALSE;
    }
    
    // For now, we'll use BassMidi for all MIDI songs when it's available
    // This could be made more sophisticated with song metadata checks
    return TRUE;
}

// Enable/disable BassMidi rendering for a song
OPErr GM_EnableSF2ForSong(GM_Song* pSong, XBOOL enable)
{
    if (!pSong)
    {
        return PARAM_ERR;
    }
    
    if (enable)
    {
        // Mark the song as an SF2 song
        pSong->isSF2Song = TRUE;
        
        // Allocate and initialize SF2Info structure if needed
        if (!pSong->sf2Info)
        {
            pSong->sf2Info = XNewPtr(sizeof(GM_SF2Info));
            if (!pSong->sf2Info)
            {
                return MEMORY_ERR;
            }
            
            // Initialize the SF2Info structure
            GM_SF2Info* sf2Info = (GM_SF2Info*)pSong->sf2Info;
            sf2Info->sf2_stream = g_bassmidi_stream;
            sf2Info->sf2_soundfont = g_bassmidi_soundfont;
            sf2Info->sf2_active = TRUE;
            strcpy(sf2Info->sf2_path, g_bassmidi_sf2_path);
            sf2Info->sf2_master_volume = g_bassmidi_master_volume;
            sf2Info->sf2_sample_rate = g_bassmidi_sample_rate;
            sf2Info->sf2_max_voices = g_bassmidi_max_voices;
            
            // Initialize channel volumes and expressions to reduced defaults
            for (int i = 0; i < 16; i++)
            {
                sf2Info->channelVolume[i] = 80;       // Reduced default volume
                sf2Info->channelExpression[i] = 100;  // Reduced default expression
                sf2Info->channelMuted[i] = FALSE;
            }
        }
    }
    else
    {
        // Disable SF2 for this song
        pSong->isSF2Song = FALSE;
        
        // Free SF2Info structure if allocated
        if (pSong->sf2Info)
        {
            XDisposePtr(pSong->sf2Info);
            pSong->sf2Info = NULL;
        }
    }
    
    return NO_ERR;
}

// BassMidi MIDI event processing
void GM_SF2_ProcessNoteOn(GM_Song* pSong, int16_t channel, int16_t note, int16_t velocity)
{
    if (!g_bassmidi_initialized || !g_bassmidi_stream)
    {
        return;
    }
    //velocity = (int)(((float)velocity / (float)MAX_MASTER_VOLUME) * (float)MAX_NOTE_VOLUME);
    // I hate clamping but here we are
    if (velocity > 127) {
        velocity = 127;
    }
    BAE_PRINTF("note on: channel %i, note %i, velocity %i\n", channel, note, velocity);
    // Check if channel is muted
    if (PV_SF2_CheckChannelMuted(pSong, channel))
    {
        return;
    }
    
    // Send MIDI note on event to BassMidi
    BASS_MIDI_StreamEvent(g_bassmidi_stream, channel, MIDI_EVENT_NOTE, MAKEWORD(note, velocity));
}

void GM_SF2_ProcessNoteOff(GM_Song* pSong, int16_t channel, int16_t note, int16_t velocity)
{
    if (!g_bassmidi_initialized || !g_bassmidi_stream)
    {
        return;
    }
    
    // Send MIDI note off event to BassMidi (velocity 0)
    BASS_MIDI_StreamEvent(g_bassmidi_stream, channel, MIDI_EVENT_NOTE, MAKEWORD(note, 0));
}

void GM_SF2_ProcessProgramChange(GM_Song* pSong, int16_t channel, int16_t program)
{
    if (!g_bassmidi_initialized || !g_bassmidi_stream)
    {
        return;
    }
    BAE_PRINTF("raw request: program: %i, channel %i\n", program, channel);    
    // Convert program ID to MIDI bank/program
    // miniBAE uses: instrument = (bank * 128) + program + note
    // For percussion: bank = (bank * 2) + 1, note is included
    // For melodic: bank = bank * 2, note = 0
    int16_t midiBank = (uint16_t)(program / 128);    // Bank number (internal mapping)
    int16_t midiProgram = (uint16_t)(program % 128); // Program number or note depending on mapping

    // Determine percussion intent from two signals:
    // 1) Internal odd-bank mapping (legacy miniBAE percussion mapping)
    // 2) Direct MIDI bank MSB 128 (SF2 percussion bank convention)
    XBOOL isOddBankPerc = ((midiBank % 2) == 1);
    XBOOL isMSB128Perc = FALSE;

    if (!isOddBankPerc)
    {
        // If not odd mapping, treat direct bank 128 as percussion
        // Convert back to MIDI bank first to test the external value
        uint16_t extBank = midiBank / 2; // internal even bank encodes extBank*2
        if (extBank == 127)
            isMSB128Perc = TRUE;
    }

    if (isOddBankPerc)
    {
        // Odd banks are percussion in miniBAE mapping
        midiBank = (midiBank - 1) / 2;     // Convert back to external MIDI bank
        // Route to SF2 percussion bank
        midiProgram = 0; // Standard drum kit preset
        midiBank = 127;  // SF2 percussion bank
    }
    else if (isMSB128Perc)
    {
        // Treat explicit MIDI bank 128 as percussion
        // Keep requested kit program if provided; use note from low 7 bits if present
        uint16_t extProgram = midiProgram; // may indicate kit variant
        midiBank = 127;                    // enforce SF2 percussion bank
        midiProgram = extProgram;          // try requested kit first, fall back later if needed
    }
    else
    {
        // Melodic mapping
        midiBank = midiBank / 2; // Convert back to external MIDI bank
        // midiProgram stays as-is for melodic instruments
    }
    // hack for dumb midis
    if (midiBank == 0 && channel == 9) {
        // ch 10, percussions
        midiBank = 127;
    }

    if (pSong->channelBankMode[channel] == USE_GM_PERC_BANK) {
        if (midiProgram == 0 && midiBank == 0) {
            BASS_MIDI_StreamEvent(g_bassmidi_stream, channel, MIDI_EVENT_DRUMS, 1);
            midiBank = 127;
        } else {
            // change back to normal channel if the program is not a percussion program
            pSong->channelBankMode[channel] = USE_GM_DEFAULT;
            BASS_MIDI_StreamEvent(g_bassmidi_stream, channel, MIDI_EVENT_DRUMS, 0);
            midiBank = midiBank / 2;
        }

    }

    BAE_PRINTF("final intepretation: midiBank: %i, midiProgram: %i, channel: %i\n", midiBank, midiProgram, channel);
    if (midiBank == 2) {
        pSong->channelType[channel] = CHANNEL_TYPE_RMF;
    } else {
        // Send MIDI program change event to BassMidi
        BASS_MIDI_StreamEvent(g_bassmidi_stream, channel, MIDI_EVENT_BANK, midiBank);
        BASS_MIDI_StreamEvent(g_bassmidi_stream, channel, MIDI_EVENT_PROGRAM, midiProgram);
    }
}

void GM_SF2_ProcessController(GM_Song* pSong, int16_t channel, int16_t controller, int16_t value)
{
    if (!g_bassmidi_initialized || !g_bassmidi_stream)
    {
        return;
    }
    
    // Track important controllers in song structure and handle special cases
    if (pSong && channel >= 0 && channel < 16)
    {
        switch (controller)
        {
            case 0:   // Bank Select MSB
                BASS_MIDI_StreamEvent(g_bassmidi_stream, channel, MIDI_EVENT_BANK, value);
                break;
                
            case 1:   // Modulation Wheel
                BASS_MIDI_StreamEvent(g_bassmidi_stream, channel, MIDI_EVENT_MODULATION, value);
                break;
                
            case 7:   // Volume level (0-127).
                pSong->channelVolume[channel] = (UBYTE)value;
                // Also update SF2Info if available
                if (pSong->sf2Info)
                {
                    ((GM_SF2Info*)pSong->sf2Info)->channelVolume[channel] = (uint8_t)value;
                }
                BASS_MIDI_StreamEvent(g_bassmidi_stream, channel, MIDI_EVENT_VOLUME, value);
                break;
                
            case 10:  // Pan position (0-128, 0=left, 64=middle, 127=right, 128=random).
                BASS_MIDI_StreamEvent(g_bassmidi_stream, channel, MIDI_EVENT_PAN, value);
                break;
                
            case 11:  // Expression
                pSong->channelExpression[channel] = (UBYTE)value;
                // Also update SF2Info if available
                if (pSong->sf2Info)
                {
                    ((GM_SF2Info*)pSong->sf2Info)->channelExpression[channel] = (uint8_t)value;
                }
                BASS_MIDI_StreamEvent(g_bassmidi_stream, channel, MIDI_EVENT_EXPRESSION, value);
                break;
                
            case 32:  // Bank Select LSB
                BASS_MIDI_StreamEvent(g_bassmidi_stream, channel, MIDI_EVENT_BANK_LSB, value);
                break;
                
            case 64:  // Sustain Pedal
                BASS_MIDI_StreamEvent(g_bassmidi_stream, channel, MIDI_EVENT_SUSTAIN, value);
                break;
                
            case 65:  // Portamento On/Off
                BASS_MIDI_StreamEvent(g_bassmidi_stream, channel, MIDI_EVENT_PORTAMENTO, value);
                break;
                
            case 66:  // Sostenuto Pedal
                BASS_MIDI_StreamEvent(g_bassmidi_stream, channel, MIDI_EVENT_SOSTENUTO, value);
                break;
                
            case 67:  // Soft Pedal
                BASS_MIDI_StreamEvent(g_bassmidi_stream, channel, MIDI_EVENT_SOFT, value);
                break;
                
            case 68:  // Legato Footswitch
                // BASSMIDI handles legato automatically
                break;
                
            case 69:  // Hold 2 Pedal
                // BASSMIDI handles hold 2 automatically
                break;
                
            case 71:  // Sound Controller 2 (Timbre/Harmonic Intensity)
                BASS_MIDI_StreamEvent(g_bassmidi_stream, channel, MIDI_EVENT_RESONANCE, value);
                break;
                
            case 72:  // Sound Controller 3 (Release Time)
                BASS_MIDI_StreamEvent(g_bassmidi_stream, channel, MIDI_EVENT_RELEASE, value);
                break;
                
            case 73:  // Sound Controller 4 (Attack Time)
                BASS_MIDI_StreamEvent(g_bassmidi_stream, channel, MIDI_EVENT_ATTACK, value);
                break;
                
            case 74:  // Sound Controller 5 (Brightness/Cutoff Frequency)
                BASS_MIDI_StreamEvent(g_bassmidi_stream, channel, MIDI_EVENT_CUTOFF, value);
                break;
                
            case 75:  // Sound Controller 6 (Decay Time)
                BASS_MIDI_StreamEvent(g_bassmidi_stream, channel, MIDI_EVENT_DECAY, value);
                break;
                
            case 76:  // Sound Controller 7 (Vibrato Rate)
                BASS_MIDI_StreamEvent(g_bassmidi_stream, channel, MIDI_EVENT_VIBRATO_RATE, value);
                break;
                
            case 77:  // Sound Controller 8 (Vibrato Depth)
                BASS_MIDI_StreamEvent(g_bassmidi_stream, channel, MIDI_EVENT_VIBRATO_DEPTH, value);
                break;
                
            case 78:  // Sound Controller 9 (Vibrato Delay)
                BASS_MIDI_StreamEvent(g_bassmidi_stream, channel, MIDI_EVENT_VIBRATO_DELAY, value);
                break;
                
            case 84:  // Portamento Control
                BASS_MIDI_StreamEvent(g_bassmidi_stream, channel, MIDI_EVENT_PORTAMENTO,  value);
                break;
                
            case 91:  // Reverb Send Level
                BASS_MIDI_StreamEvent(g_bassmidi_stream, channel, MIDI_EVENT_REVERB, value);
                break;
                
            case 92:  // Tremolo Depth
                // BASSMIDI handles tremolo automatically
                break;
                
            case 93:  // Chorus Send Level
                BASS_MIDI_StreamEvent(g_bassmidi_stream, channel, MIDI_EVENT_CHORUS, value);
                break;
                
            case 94:  // Set the user effect send level (MIDI controller 94). This will have no audible effect unless custom processing is applied to the user effect mix via BASS_MIDI_StreamGetChannel.
                BASS_MIDI_StreamEvent(g_bassmidi_stream, channel, MIDI_EVENT_USERFX, value);
                break;
                
            case 95:  // Phaser Depth
                // TODO
                break;
                
            case 96:  // Data Entry Increment
                // TODO
                break;
                
            case 97:  // Data Entry Decrement
                // TODO
                break;
                
            case 98:  // Non-Registered Parameter LSB
                // TODO
                break;
                
            case 99:  // Non-Registered Parameter MSB
                // TODO
                break;
                
            case 100: // Registered Parameter LSB
                // TODO
                break;
                
            case 101: // Registered Parameter MSB
                // TODO
                break;
                
            // Channel Mode Messages (120-127)
            case 120: // All Sound Off
                GM_SF2_AllNotesOffChannel(pSong, channel);
                BASS_MIDI_StreamEvent(g_bassmidi_stream, channel, MIDI_EVENT_SOUNDOFF, value);
                break;
                
            case 121: // Reset All Controllers
                // Reset important controller values to reduced defaults
                pSong->channelVolume[channel] = 80;   // Reduced from 100
                pSong->channelExpression[channel] = 100;  // Reduced from 127
                if (pSong->sf2Info)
                {
                    ((GM_SF2Info*)pSong->sf2Info)->channelVolume[channel] = 80;
                    ((GM_SF2Info*)pSong->sf2Info)->channelExpression[channel] = 100;
                }
                BASS_MIDI_StreamEvent(g_bassmidi_stream, channel, MIDI_EVENT_RESET,  value);
                break;
                
            case 123: // All Notes Off
                GM_SF2_AllNotesOffChannel(pSong, channel);
                BASS_MIDI_StreamEvent(g_bassmidi_stream, channel, MIDI_EVENT_NOTESOFF, value);
                break;
                
            case 124: // Omni Mode Off
                // TODO
                break;
                
            case 125: // Omni Mode On
                // TODO
                break;
                
            case 126: // Mono Mode On (Poly Off)
                BASS_MIDI_StreamEvent(g_bassmidi_stream, channel, MIDI_EVENT_MODE, MAKEWORD(controller, value));
                break;
                
            case 127: // Poly Mode On (Mono Off)
                BASS_MIDI_StreamEvent(g_bassmidi_stream, channel, MIDI_EVENT_MODE, MAKEWORD(controller, value));
                break;
                
            default:
                // Handle any other controller
                BAE_PRINTF("Controller: CC%d = %d (channel %d)\n", controller, value, channel);
                BASS_MIDI_StreamEvent(g_bassmidi_stream, channel, MIDI_EVENT_CONTROL, MAKEWORD(controller, value));
                break;
        }
    }
}

void GM_SF2_ProcessPitchBend(GM_Song* pSong, int16_t channel, int16_t bendMSB, int16_t bendLSB)
{
    if (!g_bassmidi_initialized || !g_bassmidi_stream)
    {
        return;
    }
    
    // Combine MSB and LSB into 14-bit pitch bend value
    int16_t pitchBend = (bendMSB << 7) | bendLSB;
    
    // Send MIDI pitch bend event to BassMidi
    BASS_MIDI_StreamEvent(g_bassmidi_stream, channel, MIDI_EVENT_PITCH, pitchBend);
}

// BassMidi audio rendering
void GM_SF2_RenderAudioSlice(GM_Song* pSong, int32_t* mixBuffer, int32_t frameCount)
{
    if (!g_bassmidi_initialized || !g_bassmidi_stream || !mixBuffer)
    {
        return;
    }
    
    // Allocate mix buffer if needed
    PV_SF2_AllocateMixBuffer(frameCount);
    
    if (!g_bassmidi_mix_buffer)
    {
        return;
    }
    
    // Get audio data from BassMidi stream
    DWORD bytesRead = BASS_ChannelGetData(g_bassmidi_stream, g_bassmidi_mix_buffer, frameCount * sizeof(float) * 2);
    if (bytesRead == (DWORD)-1)
    {
        return;
    }
    
    // Convert float samples to int32 and mix into output buffer
    float songVolumeScale = (float)g_bassmidi_master_volume / XFIXED_1;
    if (pSong)
    {
        // Apply song volume as well
        songVolumeScale *= (float)pSong->songVolume / 127.0f;
    }
    
    float channelScales[16];
    
    // Calculate per-channel scaling factors
    for (int i = 0; i < 16; i++)
    {
        channelScales[i] = 1.0f;
        if (pSong)
        {
            // Use volume and expression from song structure
            float volume = (float)pSong->channelVolume[i] / 127.0f;
            float expression = (float)pSong->channelExpression[i] / 127.0f;
            channelScales[i] = volume * expression;
        }
    }
    
    PV_SF2_ConvertFloatToInt32(g_bassmidi_mix_buffer, mixBuffer, frameCount, songVolumeScale, channelScales);
}

// BassMidi channel management
void GM_SF2_MuteChannel(GM_Song* pSong, int16_t channel)
{
    if (pSong && channel >= 0 && channel < 16)
    {
        // Set channel mute bit in GM_Song structure
        XSetBit((XDWORD*)pSong->channelMuted, channel);
        
        // Also update SF2Info if available
        if (pSong->sf2Info)
        {
            ((GM_SF2Info*)pSong->sf2Info)->channelMuted[channel] = TRUE;
        }
        
        // Turn off all notes on this channel
        GM_SF2_AllNotesOffChannel(pSong, channel);
    }
}

void GM_SF2_UnmuteChannel(GM_Song* pSong, int16_t channel)
{
    if (pSong && channel >= 0 && channel < 16)
    {
        // Clear channel mute bit in GM_Song structure
        XClearBit((XDWORD*)pSong->channelMuted, channel);
        
        // Also update SF2Info if available
        if (pSong->sf2Info)
        {
            ((GM_SF2Info*)pSong->sf2Info)->channelMuted[channel] = FALSE;
        }
    }
}

void GM_SF2_AllNotesOff(GM_Song* pSong)
{
    if (!g_bassmidi_initialized || !g_bassmidi_stream)
    {
        return;
    }
    
    // Send all notes off to all channels
    for (int channel = 0; channel < 16; channel++)
    {
        BASS_MIDI_StreamEvent(g_bassmidi_stream, channel, MIDI_EVENT_NOTESOFF, 0); // All notes off
    }
}

void GM_SF2_AllNotesOffChannel(GM_Song* pSong, int16_t channel)
{
    if (!g_bassmidi_initialized || !g_bassmidi_stream)
    {
        return;
    }
    
    // Send all notes off to specific channel
    BASS_MIDI_StreamEvent(g_bassmidi_stream, channel, MIDI_EVENT_NOTESOFF, 0); // All notes off
}

void GM_SF2_SilenceSong(GM_Song* pSong)
{
    GM_SF2_AllNotesOff(pSong);
    
    // Also send sustain pedal off to all channels
    for (int channel = 0; channel < 16; channel++)
    {
        BASS_MIDI_StreamEvent(g_bassmidi_stream, channel, MIDI_EVENT_SUSTAIN, 0); // Sustain off
    }
}

void GM_SF2_StoreRMFInstrumentIDs(uint32_t* rmf_instrumentIDs)
{
    // This function would store RMF instrument mappings
    // Implementation depends on how RMF instruments are handled
    // For now, we'll leave it as a stub
}

// BassMidi configuration functions
void GM_SF2_SetMasterVolume(XFIXED volume)
{
    g_bassmidi_master_volume = volume;
    
    // Apply volume to BassMidi stream if available
    if (g_bassmidi_stream)
    {
        float bassVolume = (float)volume / XFIXED_1;
        BASS_ChannelSetAttribute(g_bassmidi_stream, BASS_ATTRIB_VOL, bassVolume);
    }
}

XFIXED GM_SF2_GetMasterVolume(void)
{
    return g_bassmidi_master_volume;
}

void GM_SF2_SetMaxVoices(int16_t maxVoices)
{
    g_bassmidi_max_voices = maxVoices;
    
    // Set BassMidi voice limit
    BASS_SetConfig(BASS_CONFIG_MIDI_VOICES, maxVoices);
}

int16_t GM_SF2_GetMaxVoices(void)
{
    return g_bassmidi_max_voices;
}

void PV_SF2_SetBankPreset(GM_Song* pSong, int16_t channel, int16_t bank, int16_t preset)
{
    if (!g_bassmidi_initialized || !g_bassmidi_stream)
    {
        return;
    }
        
    // Send bank select MSB and LSB if needed
    if (bank >= 0)
    {
        if (bank >= 128) {
            bank = 127;
        }
        BASS_MIDI_StreamEvent(g_bassmidi_stream, channel, MIDI_EVENT_BANK, bank);   // Bank MSB
        BASS_MIDI_StreamEvent(g_bassmidi_stream, channel, MIDI_EVENT_BANK_LSB, preset);         // Bank LSB
    }
    
    // Send program change
    if (preset >= 0)
    {
        BASS_MIDI_StreamEvent(g_bassmidi_stream, channel, MIDI_EVENT_PROGRAM, preset);
    }
}

void GM_SF2_SetStereoMode(XBOOL stereo, XBOOL applyNow)
{
    g_bassmidi_mono_mode = !stereo;
    
    if (applyNow && g_bassmidi_initialized && g_bassmidi_stream)
    {
        // BassMidi doesn't have direct mono output - we handle mono in the conversion
        // The stream itself remains stereo, but we convert to mono in PV_SF2_ConvertFloatToInt32
        // No stream recreation needed for BASS, unlike TSF
        
        // If we need to force stream recreation for some reason in the future, 
        // we would need to save current state and recreate like in GM_SF2_SetSampleRate
        BAE_PRINTF("SF2 stereo mode set to: %s\n", stereo ? "stereo" : "mono");
    }
}

XBOOL GM_SF2_GetStereoMode(void)
{
    return !g_bassmidi_mono_mode;
}

// BassMidi status queries
uint16_t GM_SF2_GetActiveVoiceCount(void)
{
    if (!g_bassmidi_initialized || !g_bassmidi_stream)
    {
        return 0;
    }
    float totalVoices = 0;
    BASS_ChannelGetAttribute(
        g_bassmidi_stream,
        BASS_ATTRIB_MIDI_VOICES_ACTIVE,
        &totalVoices
    );

    return (uint16_t)totalVoices;
}

XBOOL GM_SF2_IsActive(void)
{
    return g_bassmidi_initialized && g_bassmidi_stream && g_bassmidi_soundfont;
}

void GM_ResetSF2(void)
{
    if (!g_bassmidi_initialized || !g_bassmidi_stream)
    {
        return;
    }
    
    // Send comprehensive reset to all channels
    for (int channel = 0; channel < 16; channel++)
    {
        // All notes off first
        BASS_MIDI_StreamEvent(g_bassmidi_stream, channel, MIDI_EVENT_NOTESOFF, 0); // All notes off

        // Reset all controllers
        BASS_MIDI_StreamEvent(g_bassmidi_stream, channel, MIDI_EVENT_CONTROL, MAKEWORD(121, 0)); // Reset all controllers
        
        // Explicitly reset important controllers to reduced defaults
        GM_SF2_SetDefaultControllers(channel);
        
        // Reset program to 0 (Acoustic Grand Piano) for non-percussion channels
        if (channel != 9) // Channel 10 is percussion
        {
            BASS_MIDI_StreamEvent(g_bassmidi_stream, channel, MIDI_EVENT_BANK, 0);     // Bank 0
            BASS_MIDI_StreamEvent(g_bassmidi_stream, channel, MIDI_EVENT_PROGRAM, 0);  // Program 0
        }
        else
        {
            // Set channel 10 to standard drum kit
            BASS_MIDI_StreamEvent(g_bassmidi_stream, channel, MIDI_EVENT_BANK, 127);   // Percussion bank
            BASS_MIDI_StreamEvent(g_bassmidi_stream, channel, MIDI_EVENT_PROGRAM, 0);  // Standard kit
        }
        if (channel == 9) // Channel 10 is percussion
        { 
            BASS_MIDI_StreamEvent(g_bassmidi_stream, channel, MIDI_EVENT_DRUMS, 1);
        }
        else
        {
            BASS_MIDI_StreamEvent(g_bassmidi_stream, channel, MIDI_EVENT_DRUMS, 0);
        }
    }
}

void sf2_get_channel_amplitudes(float channelAmplitudes[16][2])
{
    if (!g_bassmidi_initialized || !g_bassmidi_stream)
    {
        return;
    }
    
    // Fill channel amplitudes
    for (int i = 0; i < 16; i++)
    {
        if (g_bassmidi_mono_mode)
        {
            // In mono mode, average left and right channels
            float monoLevel = (g_midiLevels[i].left + g_midiLevels[i].right) * 0.5f;
            channelAmplitudes[i][0] = monoLevel;
            channelAmplitudes[i][1] = monoLevel;
        }
        else
        {
            // Stereo mode: use original left/right levels
            channelAmplitudes[i][0] = g_midiLevels[i].left;
            channelAmplitudes[i][1] = g_midiLevels[i].right;
        }
    }
}

void GM_SF2_KillChannelNotes(int ch)
{
    GM_SF2_AllNotesOffChannel(NULL, ch);
}

void GM_SF2_KillAllNotes(void)
{
    GM_SF2_AllNotesOff(NULL);
}

// Private helper functions
static XBOOL PV_SF2_CheckChannelMuted(GM_Song* pSong, int16_t channel)
{
    if (!pSong || channel < 0 || channel >= 16)
    {
        return FALSE;
    }
    
    // Check if the channel is muted using the built-in GM_Song channel mute bits
    return XTestBit((XDWORD*)pSong->channelMuted, channel);
}

static void PV_SF2_ConvertFloatToInt32(float* input, int32_t* output, int32_t frameCount, float songVolumeScale, const float *channelScales)
{
    if (!input || !output)
    {
        return;
    }
    
    // Apply significant volume reduction to prevent BASSMIDI from being too loud
    const float volumeReduction = 0.01f;  // Further reduced to 1%   
    
    if (g_bassmidi_mono_mode)
    {
        // Convert stereo input to mono output
        for (int32_t i = 0; i < frameCount; i++)
        {
            // Mix left and right channels for mono output
            float leftSample = input[i * 2] * songVolumeScale * volumeReduction;
            float rightSample = input[i * 2 + 1] * songVolumeScale * volumeReduction;
            float monoSample = (leftSample + rightSample) * 0.5f;
            
            // Convert to int32 and clamp
            int32_t monoInt = (int32_t)(monoSample * 2147483647.0f);
            
            // Clamp to prevent overflow
            if (monoInt > 2147483647) monoInt = 2147483647;
            if (monoInt < -2147483648) monoInt = -2147483648;
            
            // Output mono to both left and right channels
            output[i * 2] += monoInt;     // Left
            output[i * 2 + 1] += monoInt; // Right
        }
    }
    else
    {
        // Convert float samples to int32 format used by miniBAE mixer (stereo)
        for (int32_t i = 0; i < frameCount; i++)
        {
            // Apply volume scaling with reduction
            float leftSample = input[i * 2] * songVolumeScale * volumeReduction;
            float rightSample = input[i * 2 + 1] * songVolumeScale * volumeReduction;
            
            // Convert to int32 and clamp
            int32_t leftInt = (int32_t)(leftSample * 2147483647.0f);
            int32_t rightInt = (int32_t)(rightSample * 2147483647.0f);
            
            // Clamp to prevent overflow
            if (leftInt > 2147483647) leftInt = 2147483647;
            if (leftInt < -2147483648) leftInt = -2147483648;
            if (rightInt > 2147483647) rightInt = 2147483647;
            if (rightInt < -2147483648) rightInt = -2147483648;
            
            // Mix into output buffer (stereo interleaved)
            output[i * 2] += leftInt;
            output[i * 2 + 1] += rightInt;
        }
    }
}

static void PV_SF2_AllocateMixBuffer(int32_t frameCount)
{
    if (g_bassmidi_mix_buffer_frames < frameCount)
    {
        PV_SF2_FreeMixBuffer();
        
        // Allocate buffer for stereo float samples
        // Note: BASS always outputs stereo, mono conversion is handled in PV_SF2_ConvertFloatToInt32
        g_bassmidi_mix_buffer = (float*)malloc(frameCount * sizeof(float) * 2);
        if (g_bassmidi_mix_buffer)
        {
            g_bassmidi_mix_buffer_frames = frameCount;
        }
    }
}

static void PV_SF2_FreeMixBuffer(void)
{
    if (g_bassmidi_mix_buffer)
    {
        free(g_bassmidi_mix_buffer);
        g_bassmidi_mix_buffer = NULL;
        g_bassmidi_mix_buffer_frames = 0;
    }
}

#endif // USE_SF2_SUPPORT && defined(_USING_BASSMIDI)
