/****************************************************************************
 *
 * GenTSF.c
 *
 * TSF (TinySoundFont) integration for miniBAE
 * Provides SF2 soundfont support through TSF when USE_TSF_SUPPORT is enabled
 *
 ****************************************************************************/

 

#include "GenTSF.h"
#if USE_SF2_SUPPORT == TRUE && defined(_USING_TSF)
#if USE_VORBIS_DECODER == TRUE
#include "stb_vorbis.c"
#endif

#define TSF_IMPLEMENTATION
#include "tsf.h"
#include "GenSnd.h"
#include "GenPriv.h"
#include "X_Assert.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "MiniBAE.h"

#define SAMPLE_BLOCK_SIZE 512

// Global TSF state
static tsf* g_tsf_soundfont = NULL;
static XBOOL g_tsf_initialized = FALSE;
static XBOOL g_tsf_mono_mode = FALSE;
static XFIXED g_tsf_master_volume = XFIXED_1;
static int16_t g_tsf_max_voices = MAX_VOICES;
static uint16_t g_tsf_sample_rate = 44100;
static char g_tsf_sf2_path[256] = {0};

// Audio mixing buffer for TSF output
static float* g_tsf_mix_buffer = NULL;
static int32_t g_tsf_mix_buffer_frames = 0;

// Private function prototypes
static XBOOL PV_SF2_CheckChannelMuted(GM_Song* pSong, int16_t channel);
static void PV_SF2_ConvertFloatToInt32(float* input, int32_t* output, int32_t frameCount, float songVolumeScale, const float *channelScales);
static void PV_SF2_AllocateMixBuffer(int32_t frameCount);
static void PV_SF2_FreeMixBuffer(void);

// Initialize TSF support for the mixer
OPErr GM_InitializeSF2(void)
{
    if (g_tsf_initialized)
    {
        return NO_ERR;
    }
    
    // Derive mixer sample rate from outputRate enum
    GM_Mixer* pMixer = GM_GetCurrentMixer();
    if (pMixer)
    {
        pMixer->isSF2 = TRUE;
        g_tsf_sample_rate = (int16_t)GM_ConvertFromOutputRateToRate(pMixer->outputRate);
        if (g_tsf_sample_rate <= 0)
        {
            g_tsf_sample_rate = 44100; // fallback
        }
    }
    
    g_tsf_initialized = TRUE;
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

void GM_SF2_SetSampleRate(int32_t sampleRate)
{
    if (!g_tsf_initialized)
    {
        return;
    }

    g_tsf_sample_rate = sampleRate;
    if (g_tsf_soundfont)
    {
        tsf_set_output(g_tsf_soundfont, (g_tsf_mono_mode) ? TSF_MONO : TSF_STEREO_INTERLEAVED, g_tsf_sample_rate, 0.0f);
    }
}   

void GM_CleanupSF2(void)
{
    if (!g_tsf_initialized)
    {
        return;
    }
    
    GM_UnloadSF2Soundfont();
    PV_SF2_FreeMixBuffer();
    g_tsf_initialized = FALSE;
}

void GM_ResetSF2(void) {
    // Reset all channels' MODs and stuff
    tsf_reset(g_tsf_soundfont);
    // That resets EVERYTHING, so we have to set Ch 10 to percussion by default, again
    tsf_channel_set_bank_preset(g_tsf_soundfont, 9, 128, 0);
}

// Load SF2 soundfont for TSF rendering
OPErr GM_LoadSF2Soundfont(const char* sf2_path)
{
    if (!g_tsf_initialized)
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
    g_tsf_soundfont = tsf_load_filename(sf2_path);
    if (!g_tsf_soundfont)
    {
        return GENERAL_BAD;
    }
    
    // Configure TSF
    tsf_set_output(g_tsf_soundfont, g_tsf_mono_mode ? TSF_MONO : TSF_STEREO_INTERLEAVED, g_tsf_sample_rate, 0.0f);
    tsf_set_max_voices(g_tsf_soundfont, g_tsf_max_voices);
    tsf_set_volume(g_tsf_soundfont, XFIXED_TO_FLOAT(g_tsf_master_volume));
    
    // Store path
    strncpy(g_tsf_sf2_path, sf2_path, sizeof(g_tsf_sf2_path) - 1);
    g_tsf_sf2_path[sizeof(g_tsf_sf2_path) - 1] = '\0';
    
    // Set Ch 10 to percussion by default
    tsf_channel_set_bank_preset(g_tsf_soundfont, 9, 128, 0);
    return NO_ERR;
}

void GM_UnloadSF2Soundfont(void)
{
    if (g_tsf_soundfont)
    {
        tsf_close(g_tsf_soundfont);
        g_tsf_soundfont = NULL;
    }
    g_tsf_sf2_path[0] = '\0';
}


// Check if a song should use TSF rendering (based on loaded soundfont bank type)
XBOOL GM_IsSF2Song(GM_Song* pSong)
{
    if (!g_tsf_initialized || !g_tsf_soundfont || !pSong)
    {
        return FALSE;
    }

    return pSong->isSF2Song;
}

void sf2_get_channel_amplitudes(float channelAmplitudes[16][2])
{
    // Always zero-out destination first
    for (int ch = 0; ch < 16; ++ch) {
        channelAmplitudes[ch][0] = 0.0f;
        channelAmplitudes[ch][1] = 0.0f;
    }

    if (!g_tsf_soundfont || g_tsf_soundfont->voiceNum <= 0)
    {
        return; // nothing to do
    }

    // Use a smaller frame count for real-time responsiveness
    const int frames = 256;
    const int outChannels = g_tsf_mono_mode ? 1 : 2;
    
    // Per-channel accumulation buffers - allocate all at once for efficiency
    size_t bufSamples = (size_t)frames * (size_t)outChannels;
    size_t totalSize = bufSamples * 16 * sizeof(float); // 16 channels worth
    float *channelBuffers = (float*)XNewPtr((long)totalSize);
    if (!channelBuffers) {
        return; // allocation failure
    }
    
    // Clear all channel buffers
    memset(channelBuffers, 0, totalSize);
    
    // Render each voice directly into its channel's buffer
    struct tsf_voice *v = g_tsf_soundfont->voices;
    struct tsf_voice *vEnd = v + g_tsf_soundfont->voiceNum;
    
    for (; v != vEnd; ++v)
    {
        if (v->playingPreset == -1 || v->playingChannel < 0 || v->playingChannel >= 16)
            continue;
            
        int ch = v->playingChannel;
        float *chBuffer = channelBuffers + (ch * bufSamples);
        
        // Create a temporary voice copy and render into this channel's buffer
        struct tsf_voice tempVoice = *v;
        
        // Render voice with additive mixing
        float tempOutput[256 * 2]; // max stereo samples
        memset(tempOutput, 0, frames * outChannels * sizeof(float));
        tsf_voice_render(g_tsf_soundfont, &tempVoice, tempOutput, frames);
        
        // Add to channel buffer
        for (int i = 0; i < frames * outChannels; ++i) {
            chBuffer[i] += tempOutput[i];
        }
    }
    
    // Compute RMS for each channel
    for (int ch = 0; ch < 16; ++ch)
    {
        float *chBuffer = channelBuffers + (ch * bufSamples);
        double sumL = 0.0, sumR = 0.0;
        
        if (g_tsf_mono_mode)
        {
            for (int i = 0; i < frames; ++i)
            {
                double s = (double)chBuffer[i];
                sumL += s * s;
            }
            float rms = (float)sqrt(sumL / (double)frames);
            channelAmplitudes[ch][0] = rms * 8.0f;
            channelAmplitudes[ch][1] = rms * 8.0f;
        }
        else
        {
            for (int i = 0; i < frames; ++i)
            {
                double left = (double)chBuffer[i * 2];
                double right = (double)chBuffer[i * 2 + 1];
                sumL += left * left;
                sumR += right * right;
            }
            channelAmplitudes[ch][0] = (float)sqrt(sumL / (double)frames) * 8.0f;
            channelAmplitudes[ch][1] = (float)sqrt(sumR / (double)frames) * 8.0f;
        }
    }

    XDisposePtr(channelBuffers);
}


// Enable/disable TSF rendering for a song
OPErr GM_EnableSF2ForSong(GM_Song* pSong, XBOOL enable)
{
    if (!pSong)
    {
        return PARAM_ERR;
    }
    
    if (enable && !g_tsf_soundfont)
    {
        return GENERAL_BAD; // No soundfont loaded
    }
    GM_SF2_ProcessProgramChange(pSong, 9, 129);
    // Allocate TSF info if needed
    if (!pSong->tsfInfo && enable)
    {
        pSong->tsfInfo = XNewPtr(sizeof(GM_SF2Info));
        if (!pSong->tsfInfo)
        {
            return MEMORY_ERR;
        }
        XBlockMove(pSong->tsfInfo, 0, sizeof(GM_SF2Info));
    }
    
    if (pSong->tsfInfo)
    {
        GM_SF2Info* tsfInfo = (GM_SF2Info*)pSong->tsfInfo;
        tsfInfo->tsf_active = enable;
        tsfInfo->tsf_soundfont = enable ? g_tsf_soundfont : NULL;
        tsfInfo->tsf_master_volume = g_tsf_master_volume;
        tsfInfo->tsf_sample_rate = g_tsf_sample_rate;
        tsfInfo->tsf_max_voices = g_tsf_max_voices;
    // Init per-channel volume/expression defaults (GM defaults: volume 100? we'll use 127, expression 127)
    for (int i=0;i<16;i++) { tsfInfo->channelVolume[i]=127; tsfInfo->channelExpression[i]=127; }
        
        if (enable)
        {
            strncpy(tsfInfo->tsf_sf2_path, g_tsf_sf2_path, sizeof(tsfInfo->tsf_sf2_path) - 1);
            tsfInfo->tsf_sf2_path[sizeof(tsfInfo->tsf_sf2_path) - 1] = '\0';
        }
        
        if (!enable)
        {
            // Stop all TSF notes when disabling
            GM_SF2_AllNotesOff(pSong);
        }
    }
    
    return NO_ERR;
}

// TSF MIDI event processing
void GM_SF2_ProcessNoteOn(GM_Song* pSong, int16_t channel, int16_t note, int16_t velocity)
{
    GM_Mixer* pMixer = GM_GetCurrentMixer();
    if (!pMixer)
        return;

    velocity = (int)(((float)velocity / (float)MAX_MASTER_VOLUME) * (float)MAX_NOTE_VOLUME);
    // I hate clamping but here we are
    if (velocity > 127) {
        velocity = 127;
    }
    float tsfVelocity = (float)velocity / (float)MAX_NOTE_VOLUME;
    if (!GM_IsSF2Song(pSong) || !g_tsf_soundfont)
    {
        return;
    }
    
    // Check if channel is muted using miniBAE's mute logic
    if (PV_SF2_CheckChannelMuted(pSong, channel))
    {
        return;
    }
    
    // Apply song pitch shift if enabled for this channel
    if (GM_DoesChannelAllowPitchOffset(pSong, (unsigned short)channel))
    {
        note += pSong->songPitchShift;
        // Clamp to valid MIDI range
        if (note < 0) note = 0;
        if (note > 127) note = 127;
    }
    
    // Send to TSF
    if (velocity > 0)
    {
        tsf_channel_note_on(g_tsf_soundfont, channel, note, tsfVelocity);
    }
    else
    {
        // Velocity 0 is note off
        tsf_channel_note_off(g_tsf_soundfont, channel, note);
    }
}

void GM_SF2_ProcessNoteOff(GM_Song* pSong, int16_t channel, int16_t note, int16_t velocity)
{
    (void)velocity; // unused in TSF

    if (!GM_IsSF2Song(pSong) || !g_tsf_soundfont)
    {
        return;
    }
    
    // Apply song pitch shift if enabled for this channel
    if (GM_DoesChannelAllowPitchOffset(pSong, (unsigned short)channel))
    {
        note += pSong->songPitchShift;
        // Clamp to valid MIDI range
        if (note < 0) note = 0;
        if (note > 127) note = 127;
    }
    
    tsf_channel_note_off(g_tsf_soundfont, channel, note);
}

void GM_SF2_ProcessProgramChange(GM_Song* pSong, int16_t channel, int16_t program)
{
    if (!GM_IsSF2Song(pSong) || !g_tsf_soundfont)
    {
        return;
    }
    
    // Check if channel is muted
    /*
    if (PV_SF2_CheckChannelMuted(pSong, channel))
    {
        return;
    }
    */
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
            midiBank = 127;
        } else {
            // change back to normal channel if the program is not a percussion program
            pSong->channelBankMode[channel] == USE_GM_DEFAULT;
            midiBank = midiBank / 2;
        }

    }

    BAE_PRINTF("final intepretation: midiBank: %i, midiProgram: %i, channel: %i\n", midiBank, midiProgram, channel);
    if (midiBank == 2) {
        pSong->channelType[channel] = CHANNEL_TYPE_RMF;
    } else {
        // TSF uses preset index, so we use bank_preset method
        tsf_channel_set_bank_preset(g_tsf_soundfont, channel, midiBank, midiProgram);
    }
}

void GM_SF2_ProcessController(GM_Song* pSong, int16_t channel, int16_t controller, int16_t value)
{
    if (!GM_IsSF2Song(pSong) || !g_tsf_soundfont)
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
        GM_SF2Info* info = (GM_SF2Info*)pSong->tsfInfo;
        if (info)
        {
            if (controller == 7) { if (value<0) value=0; if (value>127) value=127; info->channelVolume[channel]=(uint8_t)value; }
            else { if (value<0) value=0; if (value>127) value=127; info->channelExpression[channel]=(uint8_t)value; }
        }
    }
    tsf_channel_midi_control(g_tsf_soundfont, channel, controller, value);
}

void GM_SF2_ProcessPitchBend(GM_Song* pSong, int16_t channel, int16_t bendMSB, int16_t bendLSB)
{
    if (!GM_IsSF2Song(pSong) || !g_tsf_soundfont)
    {
        return;
    }
    
    // Check if channel is muted
    if (PV_SF2_CheckChannelMuted(pSong, channel))
    {
        return;
    }
    
    // Convert MSB/LSB to TSF pitch wheel value (0-16383)
    int pitchWheel = (bendMSB << 7) | bendLSB;
    tsf_channel_set_pitchwheel(g_tsf_soundfont, channel, pitchWheel);
}

// TSF audio rendering - this gets called during mixer slice processing
void GM_SF2_RenderAudioSlice(GM_Song* pSong, int32_t* mixBuffer, int32_t frameCount)
{
    if (!GM_IsSF2Song(pSong) || !g_tsf_soundfont || !mixBuffer || frameCount <= 0)
    {
        return;
    }
    
    // Allocate mix buffer if needed
    PV_SF2_AllocateMixBuffer(frameCount);
    if (!g_tsf_mix_buffer)
    {
        return;
    }
    
    // Clear the float buffer (mono or stereo)
    int channels = g_tsf_mono_mode ? 1 : 2;
    XBlockMove(g_tsf_mix_buffer, 0, frameCount * channels * sizeof(float));
    
    // Render TSF audio (TSF will output mono or stereo depending on tsf_set_output)
    tsf_render_float(g_tsf_soundfont, g_tsf_mix_buffer, frameCount, 0);
    
    // Song volume scaling (0..127 typical). Clamp defensively.
    float songScale = 1.0f;
    if (pSong)
    {
        int sv = pSong->songVolume;
        if (sv < 0) {
            sv = 0; 
        }
        if (sv > 127) {
            sv = 127;
        }
        songScale = (float)sv / 127.0f;
        // Apply fade if active (songFixedVolume holds current volume during fades)
        if (pSong->songFadeRate != 0)
        {
            // songFixedVolume appears to be raw volume; normalize similarly
            int fv = pSong->songFixedVolume >> 16; // songFixedVolume is XFIXED? (defensive)
            if (fv < 0) {
                fv = 0;
            }
            if (fv > 127) {
                fv = 127;
            }
            songScale *= (float)fv / 127.0f;
        }
    }
    // Apply per-channel volume/expression: we post-scale the rendered buffer per frame
    float channelScales[16];
    GM_SF2Info* info = (GM_SF2Info*)pSong->tsfInfo;
    for(int c=0;c<16;c++)
    {
        float vol = 1.0f;
        if (info)
        {
            vol = (info->channelVolume[c] / 127.0f) * (info->channelExpression[c] / 127.0f);
        }
        channelScales[c]=vol;
    }
    // Apply song + channel scaling inside conversion
    // For simplicity pass songScale now; channel scaling applied per sample pair inside conversion helper (modify helper signature?)
    PV_SF2_ConvertFloatToInt32(g_tsf_mix_buffer, mixBuffer, frameCount, songScale, channelScales);
}

// TSF channel management (respects miniBAE mute/solo states)
void GM_SF2_MuteChannel(GM_Song* pSong, int16_t channel)
{
    if (!GM_IsSF2Song(pSong) || !g_tsf_soundfont)
    {
        return;
    }
    
    // Stop all notes on this channel
    GM_SF2_AllNotesOffChannel(pSong, channel);
}

void GM_SF2_UnmuteChannel(GM_Song* pSong, int16_t channel)
{
    (void)pSong;
    (void)channel;
    // Nothing special needed for unmuting - new notes will play normally
}

void GM_SF2_AllNotesOff(GM_Song* pSong)
{
    if (!GM_IsSF2Song(pSong) || !g_tsf_soundfont)
    {
        return;
    }
    
    // Send all notes off to all channels
    for (int channel = 0; channel < 16; channel++)
    {
        tsf_channel_midi_control(g_tsf_soundfont, channel, 123, 0); // All Notes Off
    }

    // Ensure all notes are released
    tsf_note_off_all(g_tsf_soundfont);

    for (int ch = 0; ch < 16; ++ch)
    {
        GM_SF2_AllNotesOffChannel(pSong, ch);
    }
}

void GM_SF2_AllNotesOffChannel(GM_Song* pSong, int16_t channel)
{
    if (!GM_IsSF2Song(pSong) || !g_tsf_soundfont)
    {
        return;
    }
    
    tsf_channel_midi_control(g_tsf_soundfont, channel, 123, 0); // All Notes Off
    tsf_channel_note_off_all(g_tsf_soundfont, channel); // Ensure all notes off on this channel

    // Safety controls first
    PV_ProcessController(pSong, channel, 64, 0, 0);  // Sustain Off
    PV_ProcessController(pSong, channel, 120, 0, 0); // All Sound Off
    PV_ProcessController(pSong, channel, 123, 0, 0); // All Notes Off
    for (int n = 0; n < 128; ++n)
    {
        GM_NoteOff(pSong, channel, n, 0);
        PV_StopMIDINote(pSong, n, channel, 0, 0);
    }
}

// Force immediate silence for a TSF-backed song (used on pause to avoid hanging tails)
void GM_SF2_SilenceSong(GM_Song* pSong)
{
    if (!GM_IsSF2Song(pSong) || !g_tsf_soundfont)
    {
        return;
    }
    
    // Stop all notes immediately
    GM_SF2_AllNotesOff(pSong);
    
    // Ensure any (legacy) voices allocated before TSF activation enter release
    GM_EndSongNotes(pSong);
}

void GM_SF2_KillChannelNotes(int ch) {
    if (!g_tsf_soundfont)
    {
        return;
    }
    tsf_channel_sounds_off_all(g_tsf_soundfont, ch);
}

void GM_SF2_KillAllNotes() {
    if (!g_tsf_soundfont)
    {
        return;
    }
    for (int i=0; i<16; i++) {
        GM_SF2_KillChannelNotes(i);
    }
}

// TSF configuration
void GM_SF2_SetMasterVolume(XFIXED volume)
{
    g_tsf_master_volume = volume;
    if (g_tsf_soundfont)
    {
        tsf_set_volume(g_tsf_soundfont, XFIXED_TO_FLOAT(volume));
    }
}

XFIXED GM_SF2_GetMasterVolume(void)
{
    return g_tsf_master_volume;
}

void GM_SF2_SetMaxVoices(int16_t maxVoices)
{
    g_tsf_max_voices = maxVoices;
    if (g_tsf_soundfont)
    {
        tsf_set_max_voices(g_tsf_soundfont, maxVoices);
    }
}

int16_t GM_SF2_GetMaxVoices(void)
{
    return g_tsf_max_voices;
}

// TSF status queries
uint16_t GM_SF2_GetActiveVoiceCount(void)
{
    if (!g_tsf_soundfont)
    {
        return 0;
    }
    return (uint16_t)tsf_active_voice_count(g_tsf_soundfont);
}

XBOOL GM_SF2_IsActive(void)
{
    return g_tsf_initialized && (g_tsf_soundfont != NULL);
}

// Private helper functions
static XBOOL PV_SF2_CheckChannelMuted(GM_Song* pSong, int16_t channel)
{
    if (!pSong || channel < 0 || channel >= MAX_CHANNELS)
    {
        return TRUE; // invalid parameters, treat as muted
    }
    
    // Use the same mute logic as the regular MIDI processor
    // This checks both channel mute and solo states
    return PV_IsMuted(pSong, channel, -1); // -1 for track means channel-only check
}

static void PV_SF2_ConvertFloatToInt32(float* input, int32_t* output, int32_t frameCount, float songVolumeScale, const float *channelScales)
{

    // Map float -1..1 to internal mixer scale (~16-bit range << OUTPUT_SCALAR)
    const float kScale = 32767.0f; // internal base 16-bit peak
    float masterVol = XFIXED_TO_FLOAT(g_tsf_master_volume);

    if (g_tsf_mono_mode)
    {
        // Collapse stereo input to mono by averaging L and R and write a true single-channel
        // PCM stream: one int32 result per frame (output[frame]). Caller/mixer must be
        // configured to accept mono mixBuffer when g_tsf_mono_mode is enabled.
        for (int32_t frame = 0; frame < frameCount; ++frame)
        {
            // TSF is configured to output mono floats when g_tsf_mono_mode is set,
            // so the float buffer is [frameCount] long (one sample per frame).
            float mono = input[frame] * masterVol * songVolumeScale;
            if (mono > 1.0f) mono = 1.0f;
            else if (mono < -1.0f) mono = -1.0f;
            int32_t s = (int32_t)(mono * kScale);
            s <<= OUTPUT_SCALAR; // match engine internal scaling
            // Write single-channel PCM (one sample per frame)
            output[frame] += s;
        }
    }
    else
    {
        for (int32_t i = 0; i < frameCount * 2; i++)
        {
            float sample = input[i] * masterVol * songVolumeScale;
            if (sample > 1.0f) sample = 1.0f;
            else if (sample < -1.0f) sample = -1.0f;
            int32_t s = (int32_t)(sample * kScale);
            s <<= OUTPUT_SCALAR; // match engine internal scaling
            output[i] += s;
        }
    }
}

void GM_SF2_SetStereoMode(XBOOL stereo, XBOOL applyNow) {
    g_tsf_mono_mode = !stereo;
    if (applyNow) {
        tsf_set_output(g_tsf_soundfont, g_tsf_mono_mode ? TSF_MONO : TSF_STEREO_INTERLEAVED, g_tsf_sample_rate, 0.0f);
    }
}

static void PV_SF2_AllocateMixBuffer(int32_t frameCount)
{
    if (g_tsf_mix_buffer && g_tsf_mix_buffer_frames >= frameCount)
    {
        return; // Already have sufficient buffer
    }
    
    PV_SF2_FreeMixBuffer();
    
    g_tsf_mix_buffer_frames = frameCount;
    int channels = g_tsf_mono_mode ? 1 : 2;
    g_tsf_mix_buffer = (float*)XNewPtr(frameCount * channels * sizeof(float));
}

static void PV_SF2_FreeMixBuffer(void)
{
    if (g_tsf_mix_buffer)
    {
        XDisposePtr(g_tsf_mix_buffer);
        g_tsf_mix_buffer = NULL;
        g_tsf_mix_buffer_frames = 0;
    }
}

void PV_SF2_SetBankPreset(GM_Song* pSong, int16_t channel, int16_t bank, int16_t preset) {
    if (!GM_IsSF2Song(pSong) || !g_tsf_soundfont)
    {
        return;
    }
    tsf_channel_set_bank_preset(g_tsf_soundfont, channel, bank, preset);
}

#endif // USE_SF2_SUPPORT
