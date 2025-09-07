/****************************************************************************
 *
 * GenSF2_FluidSynth.c
 *
 * FluidSynth integration for miniBAE
 * Provides SF2 soundfont support through FluidSynth when USE_SF2_SUPPORT is enabled
 *
 ****************************************************************************/

#include "GenSF2_FluidSynth.h"
#if USE_SF2_SUPPORT == TRUE && _USING_FLUIDSYNTH == TRUE

#include "fluidsynth.h"
#include "GenSnd.h"
#include "GenPriv.h"
#include "X_Assert.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "MiniBAE.h"

#define SAMPLE_BLOCK_SIZE 512

// Channel activity tracking for amplitude estimation
typedef struct {
    float leftLevel;     // Current left channel amplitude estimate
    float rightLevel;    // Current right channel amplitude estimate
    int activeNotes;     // Number of currently active notes on this channel
    float noteVelocity;  // Average velocity of active notes
    int lastActivity;    // Frame counter since last activity (for decay)
} ChannelActivity;

// Global FluidSynth state
static fluid_settings_t* g_fluidsynth_settings = NULL;
static fluid_synth_t* g_fluidsynth_synth = NULL;
static int g_fluidsynth_soundfont_id = -1;
static XBOOL g_fluidsynth_initialized = FALSE;
static XBOOL g_fluidsynth_mono_mode = FALSE;
static XFIXED g_fluidsynth_master_volume = XFIXED_1 / 256;
static int16_t g_fluidsynth_max_voices = MAX_VOICES;
static uint16_t g_fluidsynth_sample_rate = 44100;
static char g_fluidsynth_sf2_path[256] = {0};

// Channel activity tracking
static ChannelActivity g_channel_activity[16];
static int g_activity_frame_counter = 0;

// Audio mixing buffer for FluidSynth output
static float* g_fluidsynth_mix_buffer = NULL;
static int32_t g_fluidsynth_mix_buffer_frames = 0;

// Private function prototypes
static XBOOL PV_SF2_CheckChannelMuted(GM_Song* pSong, int16_t channel);
static void PV_SF2_ConvertFloatToInt32(float* input, int32_t* output, int32_t frameCount, float songVolumeScale, const float *channelScales);
static void PV_SF2_AllocateMixBuffer(int32_t frameCount);
static void PV_SF2_FreeMixBuffer(void);
static void PV_SF2_InitializeChannelActivity(void);
static void PV_SF2_UpdateChannelActivity(int16_t channel, int16_t velocity, XBOOL noteOn);
static void PV_SF2_DecayChannelActivity(void);

// Initialize FluidSynth support for the mixer
OPErr GM_InitializeSF2(void)
{
    if (g_fluidsynth_initialized)
    {
        return NO_ERR;
    }
    
    // Derive mixer sample rate from outputRate enum
    GM_Mixer* pMixer = GM_GetCurrentMixer();
    if (pMixer)
    {
        pMixer->isSF2 = TRUE;
        g_fluidsynth_sample_rate = (uint16_t)GM_ConvertFromOutputRateToRate(pMixer->outputRate);
        if (g_fluidsynth_sample_rate <= 0)
        {
            g_fluidsynth_sample_rate = 44100; // fallback
        }
    }
    
    // Create FluidSynth settings
    g_fluidsynth_settings = new_fluid_settings();
    if (!g_fluidsynth_settings)
    {
        return MEMORY_ERR;
    }
    
    // Configure FluidSynth settings
    fluid_settings_setnum(g_fluidsynth_settings, "synth.sample-rate", g_fluidsynth_sample_rate);
    fluid_settings_setint(g_fluidsynth_settings, "synth.polyphony", g_fluidsynth_max_voices);
    fluid_settings_setnum(g_fluidsynth_settings, "synth.gain", XFIXED_TO_FLOAT(g_fluidsynth_master_volume));
    fluid_settings_setint(g_fluidsynth_settings, "synth.audio-channels", g_fluidsynth_mono_mode ? 1 : 2);
    
    // Create FluidSynth synthesizer
    g_fluidsynth_synth = new_fluid_synth(g_fluidsynth_settings);
    if (!g_fluidsynth_synth)
    {
        delete_fluid_settings(g_fluidsynth_settings);
        g_fluidsynth_settings = NULL;
        return MEMORY_ERR;
    }
    
    // Initialize channel activity tracking
    PV_SF2_InitializeChannelActivity();
    
    g_fluidsynth_initialized = TRUE;
    return NO_ERR;
}

void GM_SetMixerSF2Mode(XBOOL isSF2) 
{
    GM_Mixer* pMixer = GM_GetCurrentMixer();
    if (pMixer) 
    {
        pMixer->isSF2 = isSF2;
    }
}

XBOOL GM_GetMixerSF2Mode() 
{
    GM_Mixer* pMixer = GM_GetCurrentMixer();
    if (pMixer) 
    {
        return pMixer->isSF2;
    }
    return FALSE;
}

void GM_CleanupSF2(void)
{
    if (!g_fluidsynth_initialized)
    {
        return;
    }
    
    GM_UnloadSF2Soundfont();
    PV_SF2_FreeMixBuffer();
    
    if (g_fluidsynth_synth)
    {
        delete_fluid_synth(g_fluidsynth_synth);
        g_fluidsynth_synth = NULL;
    }
    
    if (g_fluidsynth_settings)
    {
        delete_fluid_settings(g_fluidsynth_settings);
        g_fluidsynth_settings = NULL;
    }
    
    g_fluidsynth_initialized = FALSE;
}

void GM_ResetSF2(void) 
{
    if (!g_fluidsynth_synth)
        return;
        
    // Reset all channels and voices
    fluid_synth_system_reset(g_fluidsynth_synth);
    
    // Set Ch 10 to percussion by default
    fluid_synth_bank_select(g_fluidsynth_synth, 9, 128);
    fluid_synth_program_change(g_fluidsynth_synth, 9, 0);
}

// Load SF2 soundfont for FluidSynth rendering
OPErr GM_LoadSF2Soundfont(const char* sf2_path)
{
    if (!g_fluidsynth_initialized)
    {
        OPErr err = GM_InitializeSF2();
        if (err != NO_ERR)
        {
            return err;
        }
    }
    
    // Unload any existing soundfont
    GM_UnloadSF2Soundfont();
    

    // Load new soundfont
    g_fluidsynth_soundfont_id = fluid_synth_sfload(g_fluidsynth_synth, sf2_path, TRUE);
    if (g_fluidsynth_soundfont_id == FLUID_FAILED)
    {
        return GENERAL_BAD;
    }
    
    // Store path
    strncpy(g_fluidsynth_sf2_path, sf2_path, sizeof(g_fluidsynth_sf2_path) - 1);
    g_fluidsynth_sf2_path[sizeof(g_fluidsynth_sf2_path) - 1] = '\0';
    
    // Set Ch 10 to percussion by default
    fluid_synth_bank_select(g_fluidsynth_synth, 9, 128);
    fluid_synth_program_change(g_fluidsynth_synth, 9, 0);
    
    return NO_ERR;
}

void GM_UnloadSF2Soundfont(void)
{
    if (g_fluidsynth_synth && g_fluidsynth_soundfont_id >= 0)
    {
        fluid_synth_sfunload(g_fluidsynth_synth, g_fluidsynth_soundfont_id, TRUE);
        g_fluidsynth_soundfont_id = -1;
    }
    g_fluidsynth_sf2_path[0] = '\0';
}

// Check if a song should use FluidSynth rendering
XBOOL GM_IsSF2Song(GM_Song* pSong)
{
    if (!g_fluidsynth_initialized || g_fluidsynth_soundfont_id < 0 || !pSong)
    {
        return FALSE;
    }

    return pSong->isSF2Song;
}

void sf2_get_channel_amplitudes(float channelAmplitudes[16][2])
{
    // Always zero-out destination first
    for (int ch = 0; ch < 16; ch++)
    {
        channelAmplitudes[ch][0] = 0.0f;
        channelAmplitudes[ch][1] = 0.0f;
    }
    
    if (!g_fluidsynth_synth || g_fluidsynth_soundfont_id < 0)
    {
        return;
    }
    
    // Use our tracked channel activity to estimate amplitudes
    for (int ch = 0; ch < 16; ch++)
    {
        ChannelActivity* activity = &g_channel_activity[ch];
        
        if (activity->activeNotes > 0)
        {
            // Calculate amplitude based on active notes and velocity
            float baseLevel = (float)activity->activeNotes / 8.0f; // Normalize to typical polyphony
            float velocityFactor = activity->noteVelocity / 127.0f;
            float amplitude = baseLevel * velocityFactor * 0.3f; // Scale for reasonable display levels
            
            // Apply decay based on time since last activity
            float decayFactor = 1.0f;
            if (activity->lastActivity > 0)
            {
                // Decay over roughly 1 second (assuming 44.1kHz, 512 frame slices = ~86 frames/sec)
                float decayTime = (float)activity->lastActivity / 86.0f;
                decayFactor = expf(-decayTime * 2.0f); // Exponential decay
            }
            
            amplitude *= decayFactor;
            
            if (g_fluidsynth_mono_mode)
            {
                // Mono mode: same level for both channels
                channelAmplitudes[ch][0] = amplitude;
                channelAmplitudes[ch][1] = amplitude;
            }
            else
            {
                // Stereo mode: use tracked left/right levels if available
                channelAmplitudes[ch][0] = activity->leftLevel * amplitude;
                channelAmplitudes[ch][1] = activity->rightLevel * amplitude;
                
                // If no specific left/right tracking, distribute evenly
                if (activity->leftLevel == 0.0f && activity->rightLevel == 0.0f)
                {
                    channelAmplitudes[ch][0] = amplitude;
                    channelAmplitudes[ch][1] = amplitude;
                }
            }
        }
    }
}

// Enable/disable FluidSynth rendering for a song
OPErr GM_EnableSF2ForSong(GM_Song* pSong, XBOOL enable)
{
    if (!pSong)
    {
        return PARAM_ERR;
    }
    
    if (enable && g_fluidsynth_soundfont_id < 0)
    {
        return GENERAL_BAD; // No soundfont loaded
    }
    
    // Allocate SF2Info if needed
    if (!pSong->sf2Info && enable)
    {
        pSong->sf2Info = XNewPtr(sizeof(GM_SF2Info));
        if (!pSong->sf2Info)
        {
            return MEMORY_ERR;
        }
        XBlockMove(pSong->sf2Info, 0, sizeof(GM_SF2Info));
    }
    
    if (pSong->sf2Info)
    {
        GM_SF2Info* sf2Info = (GM_SF2Info*)pSong->sf2Info;
        sf2Info->sf2_active = enable;
        sf2Info->sf2_synth = enable ? g_fluidsynth_synth : NULL;
        sf2Info->sf2_settings = enable ? g_fluidsynth_settings : NULL;
        sf2Info->sf2_soundfont_id = enable ? g_fluidsynth_soundfont_id : -1;
        sf2Info->sf2_master_volume = g_fluidsynth_master_volume;
        sf2Info->sf2_sample_rate = g_fluidsynth_sample_rate;
        sf2Info->sf2_max_voices = g_fluidsynth_max_voices;
        
        // Init per-channel volume/expression defaults (GM defaults: volume 127, expression 127)
        for (int i = 0; i < 16; i++) 
        { 
            sf2Info->channelVolume[i] = 127;
            sf2Info->channelExpression[i] = 127;
            sf2Info->channelMuted[i] = FALSE;
        }
        
        if (enable)
        {
            strncpy(sf2Info->sf2_path, g_fluidsynth_sf2_path, sizeof(sf2Info->sf2_path) - 1);
            sf2Info->sf2_path[sizeof(sf2Info->sf2_path) - 1] = '\0';
        }
        
        if (!enable)
        {
            // Stop all FluidSynth notes when disabling
            GM_SF2_AllNotesOff(pSong);
        }
    }
    pSong->isSF2Song = enable;
    
    return NO_ERR;
}

// FluidSynth MIDI event processing
void GM_SF2_ProcessNoteOn(GM_Song* pSong, int16_t channel, int16_t note, int16_t velocity)
{
    if (!GM_IsSF2Song(pSong) || !g_fluidsynth_synth)
    {
        return;
    }
    
    // Check if channel is muted
    if (PV_SF2_CheckChannelMuted(pSong, channel))
    {
        return;
    }
    
    fluid_synth_noteon(g_fluidsynth_synth, channel, note, velocity);
    
    // Update channel activity tracking
    PV_SF2_UpdateChannelActivity(channel, velocity, TRUE);
}

void GM_SF2_ProcessNoteOff(GM_Song* pSong, int16_t channel, int16_t note, int16_t velocity)
{
    if (!GM_IsSF2Song(pSong) || !g_fluidsynth_synth)
    {
        return;
    }
    
    fluid_synth_noteoff(g_fluidsynth_synth, channel, note);
    
    // Update channel activity tracking
    PV_SF2_UpdateChannelActivity(channel, 0, FALSE);
}

void GM_SF2_ProcessProgramChange(GM_Song* pSong, int16_t channel, int16_t program)
{
    if (!GM_IsSF2Song(pSong) || !g_fluidsynth_synth)
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
        if (extBank == 128)
            isMSB128Perc = TRUE;
    }

    if (isOddBankPerc)
    {
        // Odd banks are percussion in miniBAE mapping
        midiBank = (midiBank - 1) / 2;     // Convert back to external MIDI bank
        // Route to SF2 percussion bank
        midiProgram = 0; // Standard drum kit preset
        midiBank = 128;  // SF2 percussion bank
    }
    else if (isMSB128Perc)
    {
        // Treat explicit MIDI bank 128 as percussion
        // Keep requested kit program if provided; use note from low 7 bits if present
        uint16_t extProgram = midiProgram; // may indicate kit variant
        midiBank = 128;                    // enforce SF2 percussion bank
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
        midiBank = 128;
    }

    if (pSong->channelBankMode[channel] == USE_GM_PERC_BANK) {
        if (midiProgram == 0 && midiBank == 0) {
            midiBank = 128;
        } else {
            // change back to normal channel if the program is not a percussion program
            pSong->channelBankMode[channel] = USE_GM_DEFAULT;
            midiBank = midiBank / 2;
        }

    }

    BAE_PRINTF("final intepretation: midiBank: %i, midiProgram: %i, channel: %i\n", midiBank, midiProgram, channel);
    if (midiBank == 2) {
        pSong->channelType[channel] = CHANNEL_TYPE_RMF;
    } else {
        // Send MIDI program change event to FluidSynth
        fluid_synth_bank_select(g_fluidsynth_synth, channel, midiBank);
        fluid_synth_program_change(g_fluidsynth_synth, channel, midiProgram);
    }
    
}

void GM_SF2_ProcessController(GM_Song* pSong, int16_t channel, int16_t controller, int16_t value)
{
    if (!GM_IsSF2Song(pSong) || !g_fluidsynth_synth)
    {
        return;
    }
    
    // Check if channel is muted for non-critical controllers
    if (PV_SF2_CheckChannelMuted(pSong, channel))
    {
        // Allow certain controllers even when muted (sustain pedal, all notes off, etc.)
        if (controller != 64 && controller != 120 && controller != 123)
        {
            return;
        }
    }
    
    // Intercept volume (7) and expression (11) to update per-channel scaling
    if (controller == 7 || controller == 11)
    {
        GM_SF2Info* info = (GM_SF2Info*)pSong->sf2Info;
        if (info)
        {
            if (controller == 7)
            {
                info->channelVolume[channel] = value;
            }
            else if (controller == 11)
            {
                info->channelExpression[channel] = value;
            }
        }
    }
    
    fluid_synth_cc(g_fluidsynth_synth, channel, controller, value);
}

void GM_SF2_ProcessPitchBend(GM_Song* pSong, int16_t channel, int16_t bendMSB, int16_t bendLSB)
{
    if (!GM_IsSF2Song(pSong) || !g_fluidsynth_synth)
    {
        return;
    }
    
    // Check if channel is muted
    if (PV_SF2_CheckChannelMuted(pSong, channel))
    {
        return;
    }
    
    int pitchWheel = (bendMSB << 7) | bendLSB;
    fluid_synth_pitch_bend(g_fluidsynth_synth, channel, pitchWheel);
}

// FluidSynth audio rendering - this gets called during mixer slice processing
void GM_SF2_RenderAudioSlice(GM_Song* pSong, int32_t* mixBuffer, int32_t frameCount)
{
    if (!GM_IsSF2Song(pSong) || !g_fluidsynth_synth || !mixBuffer || frameCount <= 0)
    {
        return;
    }
    
    // Update channel activity decay
    PV_SF2_DecayChannelActivity();
    
    // Allocate mix buffer if needed
    PV_SF2_AllocateMixBuffer(frameCount);
    if (!g_fluidsynth_mix_buffer)
    {
        return;
    }
    
    // Clear the float buffer (stereo)
    int channels = g_fluidsynth_mono_mode ? 1 : 2;
    memset(g_fluidsynth_mix_buffer, 0, frameCount * channels * sizeof(float));
    
    // Render FluidSynth audio
    if (g_fluidsynth_mono_mode)
    {
        // Mono rendering
        fluid_synth_write_float(g_fluidsynth_synth, frameCount, 
                               g_fluidsynth_mix_buffer, 0, 1,
                               g_fluidsynth_mix_buffer, 0, 1);
    }
    else
    {
        // Stereo rendering
        fluid_synth_write_float(g_fluidsynth_synth, frameCount,
                               g_fluidsynth_mix_buffer, 0, 2,
                               g_fluidsynth_mix_buffer, 1, 2);
    }
    
    // Apply song volume scaling
    float songScale = 1.0f;
    GM_Mixer* pMixer = GM_GetCurrentMixer();
    if (pMixer)
    {
        int32_t fv = pSong->songVolume;
        if (fv >= 0 && fv <= MAX_SONG_VOLUME)
        {
            songScale *= (float)fv / 127.0f;
        }
    }
    
    // Apply per-channel volume/expression: we post-scale the rendered buffer per frame
    float channelScales[16];
    GM_SF2Info* info = (GM_SF2Info*)pSong->sf2Info;
    for(int c = 0; c < 16; c++)
    {
        float vol = 1.0f;
        if (info)
        {
            vol = (info->channelVolume[c] / 127.0f) * (info->channelExpression[c] / 127.0f);
        }
        channelScales[c] = vol;
    }
    
    // Convert float to int32 and mix with existing buffer
    PV_SF2_ConvertFloatToInt32(g_fluidsynth_mix_buffer, mixBuffer, frameCount, songScale, channelScales);
}

// FluidSynth channel management (respects miniBAE mute/solo states)
void GM_SF2_MuteChannel(GM_Song* pSong, int16_t channel)
{
    if (!pSong || !pSong->sf2Info)
        return;
        
    GM_SF2Info* info = (GM_SF2Info*)pSong->sf2Info;
    info->channelMuted[channel] = TRUE;
    
    // Stop any playing notes on this channel
    GM_SF2_KillChannelNotes(channel);
}

void GM_SF2_UnmuteChannel(GM_Song* pSong, int16_t channel)
{
    if (!pSong || !pSong->sf2Info)
        return;
        
    GM_SF2Info* info = (GM_SF2Info*)pSong->sf2Info;
    info->channelMuted[channel] = FALSE;
}

void GM_SF2_KillChannelNotes(int16_t channel)
{
    if (!g_fluidsynth_synth)
        return;
        
    // Turn off all notes on the specified channel
    for (int note = 0; note < 128; note++)
    {
        fluid_synth_noteoff(g_fluidsynth_synth, channel, note);
    }
}

void GM_SF2_AllNotesOff(GM_Song* pSong)
{
    if (!g_fluidsynth_synth)
    {
        return;
    }
    
    for (int i = 0; i < 16; i++) 
    {
        GM_SF2_KillChannelNotes(i);
    }
}

// FluidSynth configuration
void GM_SF2_SetMasterVolume(XFIXED volume)
{
    g_fluidsynth_master_volume = volume;
    if (g_fluidsynth_settings)
    {
        fluid_settings_setnum(g_fluidsynth_settings, "synth.gain", XFIXED_TO_FLOAT(volume));
    }
}

XFIXED GM_SF2_GetMasterVolume(void)
{
    return g_fluidsynth_master_volume;
}

void GM_SF2_SetMaxVoices(int16_t maxVoices)
{
    g_fluidsynth_max_voices = maxVoices;
    if (g_fluidsynth_settings)
    {
        fluid_settings_setint(g_fluidsynth_settings, "synth.polyphony", maxVoices);
    }
}

int16_t GM_SF2_GetMaxVoices(void)
{
    return g_fluidsynth_max_voices;
}

void GM_SF2_SetStereoMode(XBOOL stereo, XBOOL applyNow)
{
    g_fluidsynth_mono_mode = !stereo;
    
    // FluidSynth stereo mode would require recreating the synth
    // For now, we'll just store the setting
    if (applyNow && g_fluidsynth_settings)
    {
        fluid_settings_setint(g_fluidsynth_settings, "synth.audio-channels", stereo ? 2 : 1);
    }
}

void GM_SF2_SetSampleRate(int32_t sampleRate)
{
    if (!g_fluidsynth_initialized)
    {
        return;
    }

    g_fluidsynth_sample_rate = sampleRate;
    if (g_fluidsynth_settings)
    {
        fluid_settings_setnum(g_fluidsynth_settings, "synth.sample-rate", sampleRate);
    }
}

void GM_SF2_KillAllNotes(void) 
{
    if (!g_fluidsynth_synth)
    {
        return;
    }
    
    for (int i = 0; i < 16; i++) 
    {
        GM_SF2_KillChannelNotes(i);
    }
}

// FluidSynth status queries
uint16_t GM_SF2_GetActiveVoiceCount(void)
{
    if (!g_fluidsynth_initialized || !g_fluidsynth_synth)
    {
        return 0;
    }
    
    return (uint16_t)fluid_synth_get_active_voice_count(g_fluidsynth_synth);
}

XBOOL GM_SF2_IsActive(void)
{
    return g_fluidsynth_initialized && g_fluidsynth_synth && g_fluidsynth_soundfont_id >= 0;
}

// FluidSynth default controller setup
void GM_SF2_SetDefaultControllers(int16_t channel)
{
    if (!g_fluidsynth_synth)
        return;
        
    // Set default GM controller values with reduced volumes for better balance
    fluid_synth_cc(g_fluidsynth_synth, channel, 7, 80);   // Volume (reduced from 100)
    fluid_synth_cc(g_fluidsynth_synth, channel, 10, 64);  // Pan (center)
    fluid_synth_cc(g_fluidsynth_synth, channel, 11, 100); // Expression (reduced from 127)
    fluid_synth_cc(g_fluidsynth_synth, channel, 64, 0);   // Sustain pedal off
    fluid_synth_cc(g_fluidsynth_synth, channel, 91, 0);   // Reverb depth
    fluid_synth_cc(g_fluidsynth_synth, channel, 93, 0);   // Chorus depth
    
    // Set default programs
    if (channel == 9) // Percussion channel
    {
        fluid_synth_bank_select(g_fluidsynth_synth, channel, 128);
        fluid_synth_program_change(g_fluidsynth_synth, channel, 0);
    }
    else // Normal channels
    {
        fluid_synth_bank_select(g_fluidsynth_synth, channel, 0);
        fluid_synth_program_change(g_fluidsynth_synth, channel, 0); // Acoustic Grand Piano
    }
}

void PV_SF2_SetBankPreset(GM_Song* pSong, int16_t channel, int16_t bank, int16_t preset) 
{
    if (!GM_IsSF2Song(pSong) || !g_fluidsynth_synth)
    {
        return;
    }
    
    fluid_synth_bank_select(g_fluidsynth_synth, channel, bank);
    fluid_synth_program_change(g_fluidsynth_synth, channel, preset);
}

void GM_SF2_AllNotesOffChannel(GM_Song* pSong, int16_t channel)
{
    if (!GM_IsSF2Song(pSong) || !g_fluidsynth_synth)
    {
        return;
    }
    
    // Turn off all notes on this channel using MIDI all notes off controller
    fluid_synth_cc(g_fluidsynth_synth, channel, 123, 0); // All Notes Off
    
    // Also manually turn off all notes for safety
    for (int note = 0; note < 128; note++)
    {
        fluid_synth_noteoff(g_fluidsynth_synth, channel, note);
    }
    
    // Reset sustain and other controllers
    fluid_synth_cc(g_fluidsynth_synth, channel, 64, 0);  // Sustain Off
    fluid_synth_cc(g_fluidsynth_synth, channel, 120, 0); // All Sound Off
}

void GM_SF2_SilenceSong(GM_Song* pSong)
{
    if (!GM_IsSF2Song(pSong) || !g_fluidsynth_synth)
    {
        return;
    }
    
    // Stop all notes immediately
    GM_SF2_AllNotesOff(pSong);
    
    // Ensure any (legacy) voices allocated before FluidSynth activation enter release
    GM_EndSongNotes(pSong);
}

// Private helper functions
static XBOOL PV_SF2_CheckChannelMuted(GM_Song* pSong, int16_t channel)
{
    if (!pSong || !pSong->sf2Info)
        return FALSE;
        
    GM_SF2Info* info = (GM_SF2Info*)pSong->sf2Info;
    return info->channelMuted[channel];
}

static void PV_SF2_ConvertFloatToInt32(float* input, int32_t* output, int32_t frameCount, float songVolumeScale, const float *channelScales)
{
    // For now, apply a simple global scaling since we don't have per-channel separation in the final mix
    // A more sophisticated implementation would require per-channel rendering
    float globalScale = songVolumeScale;
    
    // Average the channel scales for a rough approximation
    float avgChannelScale = 0.0f;
    for (int c = 0; c < 16; c++)
    {
        avgChannelScale += channelScales[c];
    }
    avgChannelScale /= 16.0f;
    globalScale *= avgChannelScale;
    
    if (g_fluidsynth_mono_mode)
    {
        // Mono conversion
        for (int32_t i = 0; i < frameCount; i++)
        {
            float sample = input[i] * globalScale;
            // Convert to 32-bit fixed point and add to existing buffer
            int32_t intSample = (int32_t)(sample * 2147483647.0f);
            output[i * 2] += intSample;     // Left
            output[i * 2 + 1] += intSample; // Right (duplicate for mono)
        }
    }
    else
    {
        // Stereo conversion
        for (int32_t i = 0; i < frameCount; i++)
        {
            float leftSample = input[i * 2] * globalScale;
            float rightSample = input[i * 2 + 1] * globalScale;
            
            // Convert to 32-bit fixed point and add to existing buffer
            output[i * 2] += (int32_t)(leftSample * 2147483647.0f);     // Left
            output[i * 2 + 1] += (int32_t)(rightSample * 2147483647.0f); // Right
        }
    }
}

static void PV_SF2_AllocateMixBuffer(int32_t frameCount)
{
    int channels = g_fluidsynth_mono_mode ? 1 : 2;
    int32_t requiredSize = frameCount * channels;
    
    if (g_fluidsynth_mix_buffer_frames < requiredSize)
    {
        PV_SF2_FreeMixBuffer();
        g_fluidsynth_mix_buffer = (float*)XNewPtr(requiredSize * sizeof(float));
        if (g_fluidsynth_mix_buffer)
        {
            g_fluidsynth_mix_buffer_frames = requiredSize;
        }
    }
}

static void PV_SF2_FreeMixBuffer(void)
{
    if (g_fluidsynth_mix_buffer)
    {
        XDisposePtr(g_fluidsynth_mix_buffer);
        g_fluidsynth_mix_buffer = NULL;
        g_fluidsynth_mix_buffer_frames = 0;
    }
}

static void PV_SF2_InitializeChannelActivity(void)
{
    for (int ch = 0; ch < 16; ch++)
    {
        g_channel_activity[ch].leftLevel = 0.0f;
        g_channel_activity[ch].rightLevel = 0.0f;
        g_channel_activity[ch].activeNotes = 0;
        g_channel_activity[ch].noteVelocity = 0.0f;
        g_channel_activity[ch].lastActivity = 0;
    }
    g_activity_frame_counter = 0;
}

static void PV_SF2_UpdateChannelActivity(int16_t channel, int16_t velocity, XBOOL noteOn)
{
    if (channel < 0 || channel >= 16)
        return;
        
    ChannelActivity* activity = &g_channel_activity[channel];
    
    if (noteOn)
    {
        // Note on: increment active notes and update velocity average
        activity->activeNotes++;
        if (activity->activeNotes == 1)
        {
            activity->noteVelocity = (float)velocity;
        }
        else
        {
            // Running average of note velocities
            activity->noteVelocity = (activity->noteVelocity * 0.8f) + ((float)velocity * 0.2f);
        }
        
        // Reset activity timer
        activity->lastActivity = 0;
        
        // Set default stereo levels (can be enhanced later with pan information)
        activity->leftLevel = 1.0f;
        activity->rightLevel = 1.0f;
    }
    else
    {
        // Note off: decrement active notes
        if (activity->activeNotes > 0)
        {
            activity->activeNotes--;
        }
        
        // If no more notes, start decay timer
        if (activity->activeNotes == 0)
        {
            activity->lastActivity = 1; // Start decay countdown
        }
    }
}

static void PV_SF2_DecayChannelActivity(void)
{
    g_activity_frame_counter++;
    
    for (int ch = 0; ch < 16; ch++)
    {
        ChannelActivity* activity = &g_channel_activity[ch];
        
        // If no active notes but we have recent activity, increment decay timer
        if (activity->activeNotes == 0 && activity->lastActivity > 0)
        {
            activity->lastActivity++;
            
            // After sufficient decay time, reset the channel
            if (activity->lastActivity > 200) // ~2.3 seconds at 86 fps
            {
                activity->leftLevel = 0.0f;
                activity->rightLevel = 0.0f;
                activity->noteVelocity = 0.0f;
                activity->lastActivity = 0;
            }
        }
    }
}

#endif // USE_SF2_SUPPORT && defined(_USING_FLUIDSYNTH)
