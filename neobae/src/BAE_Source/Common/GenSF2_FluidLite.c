/*
 * © 2021–2026 zefie
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

/****************************************************************************
 *
 * GenSF2_FluidLite.c
 *
 * FluidSynth integration for NeoBAE
 * Provides SF2 soundfont support through FluidSynth when USE_SF2_SUPPORT is enabled
 *
 ****************************************************************************/

#ifndef _WIN32
#define _GNU_SOURCE  // Required for dl_iterate_phdr on Linux
#endif

#include "GenSF2_FluidLite.h"

#if USE_SF2_SUPPORT == TRUE && _USING_FLUIDLITE == TRUE
#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#elif defined(__linux__)
#include <link.h>
#endif

#ifndef PATH_MAX
#include <limits.h>
#endif

#include "fluidlite.h"
#include "fluid_sfont.h"
#include "fluid_defsfont.h"
#include "fluidsynth_priv.h"
#include "GenSnd.h"
#include "GenPriv.h"
#include "X_Assert.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include "NeoBAE.h"
#include "GenBankBalance.h"

#define SAMPLE_BLOCK_SIZE 512

// Channel activity tracking for amplitude estimation
typedef struct {
    float leftLevel;     // Current left channel amplitude estimate
    float rightLevel;    // Current right channel amplitude estimate
    int activeNotes;     // Number of currently active notes on this channel
    float noteVelocity;  // Average velocity of active notes
    int lastActivity;    // Frame counter since last activity (for decay)
} ChannelActivity;

/* Per-mixer FluidLite state (was process-global). Stored on GM_Mixer->pSF2State. */
typedef struct GM_SF2MixerState {
    fluid_settings_t* settings;
    fluid_synth_t* synth;
    BAE_Mutex mutex;
    int soundfont_id;
    bool initialized;
    bool mono_mode;
    float master_volume;
    uint16_t sample_rate;
    char sf2_path[256];
    volatile bool unloading;
    ChannelActivity channel_activity[BAE_MAX_MIDI_CHANNELS];
    int activity_frame_counter;
    float* mix_buffer;
    int32_t mix_buffer_frames;
    float* resample_buffer;
    int32_t resample_buffer_frames;
} GM_SF2MixerState;

static GM_SF2MixerState s_sf2_fallback;
static bool s_sf2_fallback_inited = FALSE;

static GM_SF2MixerState *PV_SF2_GetState(void)
{
    GM_Mixer *m = MusicGlobals;
    GM_SF2MixerState *s;

    if (!m)
    {
        if (!s_sf2_fallback_inited)
        {
            XSetMemory(&s_sf2_fallback, (int32_t)sizeof(s_sf2_fallback), 0);
            s_sf2_fallback.soundfont_id = -1;
            s_sf2_fallback.master_volume = 0.5f;
            s_sf2_fallback.sample_rate = BAE_DEFAULT_SAMPLE_RATE;
            s_sf2_fallback_inited = TRUE;
        }
        return &s_sf2_fallback;
    }
    if (!m->pSF2State)
    {
        s = (GM_SF2MixerState *)XNewPtr((int32_t)sizeof(GM_SF2MixerState));
        if (!s)
            return &s_sf2_fallback;
        s->soundfont_id = -1;
        s->master_volume = 0.5f;
        s->sample_rate = BAE_DEFAULT_SAMPLE_RATE;
        m->pSF2State = s;
    }
    return (GM_SF2MixerState *)m->pSF2State;
}

/* Map legacy global names to the current mixer's SF2 state. */
#define g_fluidsynth_settings           (PV_SF2_GetState()->settings)
#define g_fluidsynth_synth              (PV_SF2_GetState()->synth)
#define g_fluidsynth_mutex              (PV_SF2_GetState()->mutex)
#define g_fluidsynth_soundfont_id       (PV_SF2_GetState()->soundfont_id)
#define g_fluidsynth_initialized        (PV_SF2_GetState()->initialized)
#define g_fluidsynth_mono_mode          (PV_SF2_GetState()->mono_mode)
#define g_fluidsynth_master_volume      (PV_SF2_GetState()->master_volume)
#define g_fluidsynth_sample_rate        (PV_SF2_GetState()->sample_rate)
#define g_fluidsynth_sf2_path           (PV_SF2_GetState()->sf2_path)
#define g_fluidsynth_unloading          (PV_SF2_GetState()->unloading)
#define g_channel_activity              (PV_SF2_GetState()->channel_activity)
#define g_activity_frame_counter        (PV_SF2_GetState()->activity_frame_counter)
#define g_fluidsynth_mix_buffer         (PV_SF2_GetState()->mix_buffer)
#define g_fluidsynth_mix_buffer_frames  (PV_SF2_GetState()->mix_buffer_frames)
#define g_fluidsynth_resample_buffer    (PV_SF2_GetState()->resample_buffer)
#define g_fluidsynth_resample_buffer_frames (PV_SF2_GetState()->resample_buffer_frames)

// Minimal FluidLite log filter that routes all fluidlite FLUID_LOG output
// through NeoBAE's debug_message so it appears in the debug log.
static void pv_fluidsynth_log_filter(int level, char* message, void* data)
{
    (void)data;
    if (message)
    {
        const char *prefix;
        switch (level) {
            case FLUID_PANIC: prefix = "PANIC"; break;
            case FLUID_ERR:   prefix = "ERROR"; break;
            case FLUID_WARN:  prefix = "WARN "; break;
            case FLUID_INFO:  prefix = "INFO "; break;
            case FLUID_DBG:   prefix = "DEBUG"; break;
            default:          prefix = "LOG  "; break;
        }
        // strip trailing newline if present so debug_message controls formatting
        size_t len = strlen(message);
        while (len > 0 && (message[len-1] == '\n' || message[len-1] == '\r'))
            message[--len] = '\0';
        debug_message("[FluidLite %s] %s\n", prefix, message);
    }
}

#define FLUIDSYNTH_INTERNAL_RATE 44100

// Private function prototypes
static bool PV_SF2_CheckChannelMuted(GM_Song* pSong, int16_t channel);
static void PV_SF2_ConvertFloatToInt32(float* input, int32_t* output, int32_t* reverbOutput, int32_t* chorusOutput, 
                                        int32_t frameCount, float songVolumeScale, const float *channelScales,
                                        const uint8_t *reverbLevels, const uint8_t *chorusLevels);
static void PV_SF2_AllocateMixBuffer(int32_t frameCount);
static void PV_SF2_FreeMixBuffer(void);
static void PV_SF2_AllocateResampleBuffer(int32_t frameCount);
static void PV_SF2_FreeResampleBuffer(void);
static void PV_SF2_InitializeChannelActivity(void);
static void PV_SF2_UpdateChannelActivity(int16_t channel, int16_t velocity, bool noteOn);
static void PV_SF2_DecayChannelActivity(void);

static void PV_SF2_LockSynth(void)
{
    if (g_fluidsynth_mutex)
    {
        BAE_AcquireMutex(g_fluidsynth_mutex);
    }
}

static void PV_SF2_UnlockSynth(void)
{
    if (g_fluidsynth_mutex)
    {
        BAE_ReleaseMutex(g_fluidsynth_mutex);
    }
}

// Choose sane default presets per channel after loading a bank to avoid
// "No preset found on channel X" warnings. Prefer bank 128 on channel 10.
static void PV_SF2_SetValidDefaultProgramsForAllChannels(void);

// Helpers to validate and choose presets present in the current font
static bool PV_SF2_PresetExists(int bank, int prog)
{
    if (!g_fluidsynth_synth || g_fluidsynth_soundfont_id < 0) return FALSE;
    
    // Search through ALL loaded soundfonts (overlay + base)
    int sfcount = fluid_synth_sfcount(g_fluidsynth_synth);
    for (int i = 0; i < sfcount; i++) {
        fluid_sfont_t* sf = fluid_synth_get_sfont(g_fluidsynth_synth, i);
        if (!sf) continue;
        
        fluid_preset_t p;
        fluid_sfont_iteration_start(sf);
        while (fluid_sfont_iteration_next(sf, &p)) {
            if (fluid_preset_get_banknum(&p) == bank && fluid_preset_get_num(&p) == prog)
                return TRUE;
        }
    }
    return FALSE;
}

static bool PV_SF2_FindFirstPresetInBank(int bank, int *outProg)
{
    if (!g_fluidsynth_synth || g_fluidsynth_soundfont_id < 0 || !outProg) return FALSE;
    
    // Search through ALL loaded soundfonts (overlay + base)
    int sfcount = fluid_synth_sfcount(g_fluidsynth_synth);
    for (int i = 0; i < sfcount; i++) {
        fluid_sfont_t* sf = fluid_synth_get_sfont(g_fluidsynth_synth, i);
        if (!sf) continue;
        
        fluid_preset_t p;
        fluid_sfont_iteration_start(sf);
        while (fluid_sfont_iteration_next(sf, &p)) {
            if (fluid_preset_get_banknum(&p) == bank) { 
                *outProg = fluid_preset_get_num(&p); 
                return TRUE; 
            }
        }
    }
    return FALSE;
}

static bool PV_SF2_FindAnyPreset(int *outBank, int *outProg)
{
    if (!g_fluidsynth_synth || g_fluidsynth_soundfont_id < 0 || !outBank || !outProg) return FALSE;
    
    // Search through ALL loaded soundfonts (overlay + base)
    int sfcount = fluid_synth_sfcount(g_fluidsynth_synth);
    for (int i = 0; i < sfcount; i++) {
        fluid_sfont_t* sf = fluid_synth_get_sfont(g_fluidsynth_synth, i);
        if (!sf) continue;
        
        fluid_preset_t p;
        fluid_sfont_iteration_start(sf);
        if (fluid_sfont_iteration_next(sf, &p)) {
            *outBank = fluid_preset_get_banknum(&p);
            *outProg = fluid_preset_get_num(&p);
            return TRUE;
        }
    }
    return FALSE;
}

// Initialize FluidSynth support for the mixer
OPErr GM_InitializeSF2(void)
{
    if (g_fluidsynth_initialized)
    {
        return NO_ERR;
    }

    if (!g_fluidsynth_mutex)
    {
        // Serialize calls into FluidSynth; resync/load may happen off the audio thread.
        // If allocation fails, we continue unlocked (worst-case: legacy behavior).
        BAE_NewMutex(&g_fluidsynth_mutex, "bae", "sf2", __LINE__);
    }
    
    // Derive mixer sample rate from outputRate enum
    GM_Mixer* pMixer = GM_GetCurrentMixer();
    if (pMixer)
    {
        pMixer->isSF2 = TRUE;
        g_fluidsynth_sample_rate = (uint16_t)GM_ConvertFromOutputRateToRate(pMixer->outputRate);
        if (g_fluidsynth_sample_rate <= 0)
        {
            g_fluidsynth_sample_rate = BAE_DEFAULT_SAMPLE_RATE; // fallback
        }
        
        // Sync our mono flag with the mixer's stereo setting
        g_fluidsynth_mono_mode = !pMixer->generateStereoOutput;
    }
    
    // FluidSynth always runs at 44100 Hz internally to avoid pitch shifting.
    // Resampling to the mixer's sample rate is handled in GM_SF2_RenderAudioSlice.
    float fsSampleRate = (float)FLUIDSYNTH_INTERNAL_RATE;
    
    // Create FluidSynth settings
    g_fluidsynth_settings = new_fluid_settings();
    if (!g_fluidsynth_settings)
    {
        return MEMORY_ERR;
    }
    
    // Configure FluidSynth settings
    fluid_settings_setnum(g_fluidsynth_settings, "synth.sample-rate", fsSampleRate);
    fluid_settings_setint(g_fluidsynth_settings, "synth.polyphony", BAE_MAX_VOICES);
    fluid_settings_setint(g_fluidsynth_settings, "synth.midi-channels", BAE_MAX_MIDI_CHANNELS);
    fluid_settings_setnum(g_fluidsynth_settings, "synth.gain", g_fluidsynth_master_volume);
    fluid_settings_setint(g_fluidsynth_settings, "synth.audio-channels", 1);  // Sets the number of stereo channel pairs. So 1 is actually 2 channels (a stereo pair).
    fluid_settings_setint(g_fluidsynth_settings, "synth.reverb.active", 0);
    fluid_settings_setint(g_fluidsynth_settings, "synth.chorus.active", 0);
    
    // Create FluidSynth synthesizer
    g_fluidsynth_synth = new_fluid_synth(g_fluidsynth_settings);
    if (!g_fluidsynth_synth)
    {
        delete_fluid_settings(g_fluidsynth_settings);
        g_fluidsynth_settings = NULL;
        return MEMORY_ERR;
    }

    // Route all FluidLite log output through NeoBAE's debug_message so that
    // FLUID_LOG(FLUID_DBG/WARN/ERR...) messages from fluidlite are visible
    // in the debug log.
    fluid_set_log_function(FLUID_PANIC, pv_fluidsynth_log_filter, NULL);
    fluid_set_log_function(FLUID_ERR,   pv_fluidsynth_log_filter, NULL);
    fluid_set_log_function(FLUID_WARN,  pv_fluidsynth_log_filter, NULL);
    fluid_set_log_function(FLUID_INFO,  pv_fluidsynth_log_filter, NULL);
    fluid_set_log_function(FLUID_DBG,   pv_fluidsynth_log_filter, NULL);

    // Initialize channel activity tracking
    PV_SF2_InitializeChannelActivity();
    // Establish safe default programs/controllers (will be refined after font load)
    PV_SF2_SetValidDefaultProgramsForAllChannels();
    
    g_fluidsynth_initialized = TRUE;
    return NO_ERR;
}

void GM_SetMixerSF2Mode(bool isSF2) 
{
    GM_Mixer* pMixer = GM_GetCurrentMixer();
    if (pMixer) 
    {
        pMixer->isSF2 = isSF2;
    }
}

bool GM_GetMixerSF2Mode() 
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
    GM_Mixer *pMixer = MusicGlobals;
    GM_SF2MixerState *state;

    if (!g_fluidsynth_initialized && !(pMixer && pMixer->pSF2State))
    {
        return;
    }
    
    PV_SF2_FreeMixBuffer();
    PV_SF2_FreeResampleBuffer();
    GM_UnloadSF2Soundfont();
    
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

    if (g_fluidsynth_mutex)
    {
        BAE_DestroyMutex(g_fluidsynth_mutex);
        g_fluidsynth_mutex = NULL;
    }
    
    g_fluidsynth_initialized = FALSE;

    if (pMixer && pMixer->pSF2State)
    {
        state = (GM_SF2MixerState *)pMixer->pSF2State;
        pMixer->pSF2State = NULL;
        if (state != &s_sf2_fallback)
            XDisposePtr((XPTR)state);
    }
}


// Check if all loaded instruments in a song are RMF-embedded (not from SF2)
// If so, disable SF2 mode for this song to avoid double-playback
static bool PV_SF2_AllInstrumentsAreRMFEmbedded(GM_Song* pSong)
{
    if (!pSong || !(pSong->songFlags & SONG_FLAG_IS_RMF))
        return FALSE;
    
    // Get count of RMF instruments from index 0
    uint32_t rmfInstCount = pSong->RMFInstrumentIDs[0];
    if (rmfInstCount == 0)
        return FALSE; // No RMF instruments, can't be all-embedded
    
    // Count how many instruments are actually loaded in the song
    int loadedCount = 0;
    for (int i = 1; i < (MAX_INSTRUMENTS * MAX_BANKS); i++) // Skip instrument 0 (default/fallback)
    {
        if (pSong->instrumentData[i] != NULL)
        {
            loadedCount++;
        }
    }
    if (loadedCount == 0)
        return FALSE; // No instruments loaded yet
    
    // Check if all loaded instruments match RMF embedded IDs
    int matchedCount = 0;
    for (int i = 1; i < (MAX_INSTRUMENTS * MAX_BANKS); i++) // Skip program 0 (default/fallback)
    {
        if (pSong->instrumentData[i] != NULL)
        {
            // Check if this instrument ID exists in the RMF embedded list
            bool found = FALSE;
            for (uint32_t j = 1; j <= rmfInstCount; j++)
            {
                if (pSong->RMFInstrumentIDs[j] == (uint32_t)i)
                {
                    found = TRUE;
                    break;
                }
            }
            if (found)
                matchedCount++;
            else
            {
                debug_message("[SF2-RMF] Loaded instrument %d is NOT RMF-embedded - SF2 needed\n", i);
                return FALSE; // Found a loaded instrument that is not RMF-embedded, SF2 is needed
            }
        }
    }
    
    // Check if all loaded wavetable instruments are RMF-embedded
    bool allLoadedEmbedded = (matchedCount == loadedCount && loadedCount > 0);
    
    if (!allLoadedEmbedded)
        return FALSE;
    
    // Check if there are any USED instruments that aren't loaded (would need SF2)
    // Strategy: Check all channels that have been programmed
    debug_message("[SF2-RMF] Checking RMF for non-embedded instruments...\n");
    for (int channel = 0; channel < MAX_CHANNELS; channel++)
    {
        // Skip channels that have never been programmed
        if (pSong->firstChannelProgram[channel] == -1)
            continue;
        
        int16_t program = pSong->channelProgram[channel];
        
        // Skip invalid program values
        if (program < 0 || program >= (MAX_INSTRUMENTS * MAX_BANKS))
            continue;
        
        // Skip bank 0 program 0 (default/fallback) only if all loaded instruments are embedded
        // This is often a default value on channels that may not actually play notes
        if (program == 0 && allLoadedEmbedded)
        {
            //debug_message("[SF2-RMF] Channel %d uses program 0 (bank 0 program 0) - skipping (default value)\n", channel);
            continue;
        }
        
        
        // Check if this instrument is loaded
        if (pSong->instrumentData[program] == NULL)
        {
            // Not loaded - check if it's in the RMF embedded list
            bool isEmbedded = FALSE;
            for (uint32_t j = 1; j <= rmfInstCount; j++)
            {
                if (pSong->RMFInstrumentIDs[j] == (uint32_t)program || pSong->RMFInstrumentIDs[j] == (uint32_t)(program  + 512))
                {
                    isEmbedded = TRUE;
                    break;
                }
            }
            
            // If programmed but not loaded and not embedded, SF2 must provide it
            if (!isEmbedded)
            {
                debug_message("[SF2-RMF] Channel %d program %d is not RMF-embedded - SF2 needed\n", 
                           channel, program);
                return FALSE;
            }
            else
            {
                debug_message("[SF2-RMF] Channel %d program %d is RMF-embedded\n",
                           channel, program);
            }
        }
    }
    
    if (allLoadedEmbedded)
    {
        debug_message("[SF2-RMF] All %d loaded instruments are RMF-embedded (out of %u declared in RMF)\n",
                   loadedCount, rmfInstCount);
    }
    
    return allLoadedEmbedded;
}

void GM_ResetSF2(void)
{
    if (!g_fluidsynth_synth)
        return;

    // Kill all notes currently playing
    GM_SF2_KillAllNotes();
    // Pick valid defaults again after reset
    PV_SF2_SetValidDefaultProgramsForAllChannels();
    return;
}

// Check if all instruments are RMF-embedded and disable SF2 mode if so
// This prevents double-playback when RMF has all instruments embedded
void GM_SF2_CheckAndDisableSF2ForRMFEmbedded(GM_Song* pSong)
{
    if (!pSong || !GM_SF2_IsActive())
        return;
    
    // Check if this song has all instruments embedded in RMF
    if (PV_SF2_AllInstrumentsAreRMFEmbedded(pSong))
    {
        debug_message("[SF2-RMF] RMF detected to use solely embedded instruments - disabling SF2 mode for this song\n");
        // Disable SF2 for this specific song
        pSong->songFlags &= ~SONG_FLAG_USE_SF2;
        GM_EnableSF2ForSong(pSong, FALSE);
    }
}

void GM_SoftResetSF2(void) {
    GM_Mixer *pMixer;
    int songIndex;

    if (!g_fluidsynth_synth)
        return;

    /* Soft reset: controllers without resetting programs. Called from
     * PV_ResetControlers on song start/loop. Many phone MIDIs fade CC11→0 at
     * the end and never restore it at the top — leaving FluidSynth + our
     * post-scale muted for the next loop. Reset expression to GM default 127.
     * CC7 is left alone; songs almost always re-send volume at bar 1. */
    PV_SF2_LockSynth();
    for (int ch = 0; ch < BAE_MAX_MIDI_CHANNELS; ch++) {
        fluid_synth_pitch_bend(g_fluidsynth_synth, ch, 8192);
        fluid_synth_cc(g_fluidsynth_synth, ch, 1, 0);
        fluid_synth_cc(g_fluidsynth_synth, ch, 11, 127);
        fluid_synth_cc(g_fluidsynth_synth, ch, 10, 64);
        fluid_synth_cc(g_fluidsynth_synth, ch, 64, 0);
        fluid_synth_cc(g_fluidsynth_synth, ch, 91, 0);
        fluid_synth_cc(g_fluidsynth_synth, ch, 93, 0);
        fluid_synth_cc(g_fluidsynth_synth, ch, 100, 127);
        fluid_synth_cc(g_fluidsynth_synth, ch, 101, 127);
        fluid_synth_cc(g_fluidsynth_synth, ch, 5, 0);
        fluid_synth_cc(g_fluidsynth_synth, ch, 65, 0);
    }
    PV_SF2_UnlockSynth();

    /* Post-scale uses info->channelExpression independently of FluidSynth CC11. */
    pMixer = GM_GetCurrentMixer();
    if (!pMixer)
    {
        return;
    }
    for (songIndex = 0; songIndex < MAX_SONGS; ++songIndex)
    {
        GM_Song *pSong = pMixer->pSongsToPlay[songIndex];
        GM_SF2Info *info;
        int ch;

        if (!pSong || !pSong->sf2Info)
        {
            continue;
        }
        info = (GM_SF2Info *)pSong->sf2Info;
        for (ch = 0; ch < BAE_MAX_MIDI_CHANNELS; ++ch)
        {
            info->channelExpression[ch] = 127;
        }
    }
}

// FluidSynth default controller setup
void GM_SF2_SetDefaultControllers()
{
    if (!g_fluidsynth_synth)
        return;

    PV_SF2_LockSynth();
    fluid_synth_system_reset(g_fluidsynth_synth);
    PV_SF2_UnlockSynth();
    GM_SoftResetSF2();
}

static void PV_SF2_ResyncSongStateToSynth(GM_Song* pSong)
{
    if (!pSong || !g_fluidsynth_synth || g_fluidsynth_soundfont_id < 0)
    {
        return;
    }
    if (!GM_IsSF2Song(pSong))
    {
        return;
    }

    PV_SF2_LockSynth();

    GM_SF2Info* info = (GM_SF2Info*)pSong->sf2Info;
    if (info)
    {
        info->sf2_active = TRUE;
        info->sf2_synth = g_fluidsynth_synth;
        info->sf2_settings = g_fluidsynth_settings;
        info->sf2_soundfont_id = g_fluidsynth_soundfont_id;
        info->sf2_master_volume = g_fluidsynth_master_volume;
        info->sf2_sample_rate = g_fluidsynth_sample_rate;
        info->sf2_max_voices = BAE_MAX_VOICES;
        strncpy(info->sf2_path, g_fluidsynth_sf2_path, sizeof(info->sf2_path) - 1);
        info->sf2_path[sizeof(info->sf2_path) - 1] = '\0';
    }

    for (int ch = 0; ch < BAE_MAX_MIDI_CHANNELS; ++ch)
    {
        // Restore program/bank selection to match the song's current channel state.
        // IMPORTANT: GM_SF2_ProcessProgramChange expects NeoBAE's combined program encoding:
        // combinedProgram = (internalBank * 128) + program
        int32_t internalBank = (int32_t)pSong->channelRawBank[ch];
        int32_t program = (int32_t)pSong->channelProgram[ch];

        if (program < 0) { program = 0; }
        if (program > 127) { program = 127; }

        switch (pSong->channelBankMode[ch])
        {
            default:
            case USE_GM_DEFAULT:
                if (ch == BAE_PERCUSSION_CHANNEL)
                {
                    internalBank = (internalBank * 2) + 1; // odd banks are percussion
                }
                else
                {
                    internalBank = (internalBank * 2) + 0; // even banks are melodic
                }
                break;
            case USE_NON_GM_PERC_BANK:
            case USE_GM_PERC_BANK:
                internalBank = (internalBank * 2) + 1;
                break;
            case USE_NORM_BANK:
                internalBank = (internalBank * 2) + 0;
                break;
        }

        int32_t combinedProgram = (internalBank * 128) + program;
        // Resync must not mutate song state; GM_SF2_ProcessProgramChange may adjust
        // channel bank mode and raw bank as part of normal event processing.
        // Preserve those fields across this resync call.
        uint16_t savedRawBank = pSong->channelRawBank[ch];
        uint8_t savedBankMode = pSong->channelBankMode[ch];
        GM_SF2_ProcessProgramChange(pSong, (int16_t)ch, combinedProgram);
        pSong->channelRawBank[ch] = savedRawBank;
        pSong->channelBankMode[ch] = savedBankMode;

        // Restore core controllers.
        // NOTE: CC91/CC93 are tracked in sf2Info and intentionally NOT sent to FluidSynth.
        if (info)
        {
            info->channelVolume[ch] = pSong->channelVolume[ch];
            // Many MIDI files never send CC11; NeoBAE's internal default/reset state can
            // leave channelExpression at 0 which would fully mute SF2 output (both via
            // FluidSynth CC11 and our post-scale). Treat 0 as "unset" and default to GM 127.
            info->channelExpression[ch] = (pSong->channelExpression[ch] == 0) ? 127 : pSong->channelExpression[ch];
#if REVERB_USED != REVERB_DISABLED
            info->channelReverb[ch] = pSong->channelReverb[ch];
            info->channelChorus[ch] = pSong->channelChorus[ch];
#endif
        }

        int vol = (int)pSong->channelVolume[ch];
        int expr = (int)pSong->channelExpression[ch];
        int pan = (int)pSong->channelStereoPosition[ch];
        int mod = (int)pSong->channelModWheel[ch];
        if (vol < 0) { vol = 0; } if (vol > 127) { vol = 127; }
        if (expr < 0) { expr = 0; } if (expr > 127) { expr = 127; }
        if (pan < 0) { pan = 0; } if (pan > 127) { pan = 127; }
        if (mod < 0) { mod = 0; } if (mod > 127) { mod = 127; }

        if (expr == 0)
        {
            expr = 127;
        }
        debug_message("[SF2-Resync] Ch %d: Vol=%d Expr=%d Pan=%d Mod=%d Sustain=%d\n",
                   ch, vol, expr, pan, mod, pSong->channelSustain[ch] ? 127 : 0);
        fluid_synth_cc(g_fluidsynth_synth, ch, 7, vol);
        fluid_synth_cc(g_fluidsynth_synth, ch, 11, expr);
        fluid_synth_cc(g_fluidsynth_synth, ch, 10, pan);
        fluid_synth_cc(g_fluidsynth_synth, ch, 1, mod);
        fluid_synth_cc(g_fluidsynth_synth, ch, 64, pSong->channelSustain[ch] ? 127 : 0);

        // Restore pitch bend range (RPN 0,0) and current pitch bend.
        // This approximates the effects of "processing events from 0..current position".
        fluid_synth_cc(g_fluidsynth_synth, ch, 101, 0); // RPN MSB
        fluid_synth_cc(g_fluidsynth_synth, ch, 100, 0); // RPN LSB
        int pbr = (int)pSong->channelPitchBendRange[ch];
        if (pbr < 0) { pbr = 0; } if (pbr > 127) { pbr = 127; }
        fluid_synth_cc(g_fluidsynth_synth, ch, 6, pbr); // Data Entry MSB
        fluid_synth_cc(g_fluidsynth_synth, ch, 38, 0); // Data Entry LSB
        fluid_synth_cc(g_fluidsynth_synth, ch, 101, 127); // RPN MSB reset
        fluid_synth_cc(g_fluidsynth_synth, ch, 100, 127); // RPN LSB reset

        int bend = (int)pSong->channelBend[ch] + 8192;
        if (bend < 0) { bend = 0; } if (bend > 16383) { bend = 16383; }
        fluid_synth_pitch_bend(g_fluidsynth_synth, ch, bend);
    }

    PV_SF2_UnlockSynth();
}

static void PV_SF2_ResyncAllActiveSongsToSynth(void)
{
    GM_Mixer* pMixer = GM_GetCurrentMixer();
    if (!pMixer || !g_fluidsynth_synth || g_fluidsynth_soundfont_id < 0)
    {
        return;
    }
    for (int i = 0; i < MAX_SONGS; ++i)
    {
        GM_Song* pSong = pMixer->pSongsToPlay[i];
        if (pSong)
        {
            PV_SF2_ResyncSongStateToSynth(pSong);
        }
    }
}

// In-memory SF2/DLS loading via FluidSynth defsfloader + custom file callbacks
typedef struct {
    const unsigned char *data;
    size_t size;
    size_t pos;
} fs_mem_stream_t;

static const unsigned char *g_mem_sf_data = NULL;
static size_t g_mem_sf_size = 0;
static fluid_sfloader_t *g_mem_sf_loader = NULL;
static fluid_fileapi_t *g_mem_sf_fileapi = NULL; // fileapi with callbacks

static void *fs_mem_fopen(fluid_fileapi_t *fileapi, const char *filename) {
    (void)filename;
    (void)fileapi;
    if (!g_mem_sf_data || g_mem_sf_size == 0) {
        debug_message("[FluidMem] fs_mem_fopen: no buffer set (filename=%s)\n", filename ? filename : "(null)");
        return NULL;
    }
    fs_mem_stream_t *s = (fs_mem_stream_t *)malloc(sizeof(fs_mem_stream_t));
    if (!s) return NULL;
    s->data = g_mem_sf_data;
    s->size = g_mem_sf_size;
    s->pos = 0;
    debug_message("[FluidMem] fs_mem_fopen: %zu bytes (filename=%s)\n", s->size, filename ? filename : "(null)");
    return s;
}

static int fs_mem_fread(void *buf, int count, void *handle) {
    fs_mem_stream_t *s = (fs_mem_stream_t *)handle;
    if (!s || !buf || count <= 0) return FLUID_FAILED;
    size_t want = (size_t)count;
    if (s->pos + want > s->size) {
        return FLUID_FAILED;
    }
    memcpy(buf, s->data + s->pos, want);
    s->pos += want;
    return FLUID_OK;
}

static int fs_mem_fseek(void *handle, long offset, int origin) {
    fs_mem_stream_t *s = (fs_mem_stream_t *)handle;
    if (!s) return FLUID_FAILED;
    size_t new_pos = 0;
    switch (origin) {
        case SEEK_SET:
            if (offset < 0) return FLUID_FAILED;
            new_pos = (size_t)offset;
            break;
        case SEEK_CUR:
            if (offset < 0 && (size_t)(-offset) > s->pos) return FLUID_FAILED;
            new_pos = (size_t)(offset < 0) ? s->pos - (size_t)(-offset) : s->pos + (size_t)offset;
            break;
        case SEEK_END:
            if (offset < 0 && (size_t)(-offset) > s->size) return FLUID_FAILED;
            new_pos = (size_t)(offset < 0) ? s->size - (size_t)(-offset) : s->size + (size_t)offset;
            break;
        default:
            return FLUID_FAILED;
    }
    if (new_pos > s->size) return FLUID_FAILED;
    s->pos = new_pos;
    return FLUID_OK;
}

static long fs_mem_ftell(void *handle) {
    fs_mem_stream_t *s = (fs_mem_stream_t *)handle;
    if (!s) return (long)FLUID_FAILED;
    return (long)s->pos;
}

static int fs_mem_fclose(void *handle) {
    if (handle) free(handle);
    return FLUID_OK;
}

static int fs_mem_fileapi_free(fluid_fileapi_t *fileapi) {
    if (fileapi) free(fileapi);
    return FLUID_OK;
}

#if !defined(_WIN32) && !defined(__EMSCRIPTEN__) && !defined(__APPLE__)
int is_libinstpatch_loaded_callback(struct dl_phdr_info *info, size_t size, void *data) {
    if (info->dlpi_name && strstr(info->dlpi_name, "libinstpatch")) {
        return true; // stop iteration
    }
    return false;
}
#endif


bool is_libinstpatch_loaded(void) {
#ifdef _WIN32
    HMODULE hMods[1024];
    DWORD cbNeeded;

    if (EnumProcessModules(GetCurrentProcess(), hMods, sizeof(hMods), &cbNeeded)) {
        for (unsigned i = 0; i < (cbNeeded / sizeof(HMODULE)); i++) {
            char szModName[MAX_PATH];
            if (GetModuleFileNameA(hMods[i], szModName, sizeof(szModName))) {
                if (strstr(szModName, "libinstpatch")) {
                    return true; // Found
                }
            }
        }
    }
    return false;
#else
#if defined(__EMSCRIPTEN__) || defined(__APPLE__)
    return false; // dl_iterate_phdr not supported in Emscripten or macOS
#else
    struct ctx { int found; } context = {0};

    dl_iterate_phdr(is_libinstpatch_loaded_callback, &context);
    return context.found != 0 ? true : false;
#endif
#endif
}

OPErr GM_LoadSF2SoundfontFromMemory(const unsigned char *data, size_t size) {
    if (!g_fluidsynth_initialized) {
        OPErr err = GM_InitializeSF2();
        if (err != NO_ERR) {
            return err;
        }
    }

    if (!data || size == 0 || !g_fluidsynth_synth) {
        return PARAM_ERR;
    }

    // Debug: Print first 16 bytes of the buffer
    debug_message("[FluidMem] Loading %zu bytes from memory", size);

    // Prepare global memory buffer for callbacks (SF2)
    g_mem_sf_data = data;
    g_mem_sf_size = size;

    // Ensure we have a defsfloader with our fileapi callbacks installed once
    if (!g_mem_sf_loader) {
        g_mem_sf_loader = new_fluid_defsfloader();
        if (!g_mem_sf_loader) {
            g_mem_sf_data = NULL; g_mem_sf_size = 0;
            return MEMORY_ERR;
        }
        // In fluidlite, callbacks are set via fluid_fileapi_t on the loader's fileapi field
        g_mem_sf_fileapi = (fluid_fileapi_t *)malloc(sizeof(fluid_fileapi_t));
        if (!g_mem_sf_fileapi) {
            delete_fluid_defsfloader(g_mem_sf_loader);
            g_mem_sf_loader = NULL;
            g_mem_sf_data = NULL; g_mem_sf_size = 0;
            return MEMORY_ERR;
        }
        g_mem_sf_fileapi->data = NULL;
        g_mem_sf_fileapi->free = fs_mem_fileapi_free;
        g_mem_sf_fileapi->fopen = fs_mem_fopen;
        g_mem_sf_fileapi->fread = fs_mem_fread;
        g_mem_sf_fileapi->fseek = fs_mem_fseek;
        g_mem_sf_fileapi->fclose = fs_mem_fclose;
        g_mem_sf_fileapi->ftell = fs_mem_ftell;
        g_mem_sf_loader->fileapi = g_mem_sf_fileapi;
        // Add our loader to the synth
        fluid_synth_add_sfloader(g_fluidsynth_synth, g_mem_sf_loader);
        debug_message("[FluidMem] defsfloader registered\n");
    }

    // Unload any existing font first
    GM_UnloadSF2Soundfont();

    // Trigger load; the filename is ignored by our open callback
    int sfid = fluid_synth_sfload(g_fluidsynth_synth, "__mem_sf2__", TRUE);
    // Clear buffer reference regardless of result (loader holds no state)
    g_mem_sf_data = NULL; g_mem_sf_size = 0;
    if (sfid == FLUID_FAILED) {
        return GENERAL_BAD;
    }

    g_fluidsynth_soundfont_id = sfid;
    
    strncpy(g_fluidsynth_sf2_path, "__memory__", sizeof(g_fluidsynth_sf2_path) - 1);
    g_fluidsynth_sf2_path[sizeof(g_fluidsynth_sf2_path) - 1] = '\0';

    // Choose valid default presets to avoid warnings
    PV_SF2_SetValidDefaultProgramsForAllChannels();
    GM_SetMixerSF2Mode(TRUE);
    PV_SF2_ResyncAllActiveSongsToSynth();
    GM_BankBalance_OnSf2Loaded();
    return NO_ERR;
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

    // Try to open the file and read first 16 bytes
    FILE *sf2_file = fopen(sf2_path, "rb");
    if (!sf2_file) {
        debug_message("[FluidMem] Failed to open SF2 file: %s\n", sf2_path);
        return BAD_FILE;
    }
    unsigned char sf2_header[16] = {0};
    size_t bytes_read = fread(sf2_header, 1, 16, sf2_file);
    fclose(sf2_file);
    if (bytes_read < 16) 
    {
        debug_message("[FluidMem] Could not read 16 bytes from SF2 file: %s\n", sf2_path);
        return BAD_FILE;
    }

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
    PV_SF2_SetValidDefaultProgramsForAllChannels();
    GM_SetMixerSF2Mode(TRUE);
    PV_SF2_ResyncAllActiveSongsToSynth();
    GM_BankBalance_OnSf2Loaded();
    return NO_ERR;
}

void GM_UnloadSF2Soundfont(void)
{
    if (g_fluidsynth_synth && g_fluidsynth_soundfont_id >= 0)
    {
        // Set flag to prevent audio thread from rendering during unload
        // This prevents race conditions between rendering and unloading
        g_fluidsynth_unloading = TRUE;
        
        // Kill all notes and reset
        GM_ResetSF2();

        while (GM_SF2_GetActiveVoiceCount() > 0)
        {
            // Wait for voices to finish
            PV_SF2_LockSynth();
            fluid_synth_process(g_fluidsynth_synth, SAMPLE_BLOCK_SIZE, 0, 0, 0, NULL);
            PV_SF2_UnlockSynth();
        }
        
        // Now safe to unload
        PV_SF2_LockSynth();
        fluid_synth_sfunload(g_fluidsynth_synth, g_fluidsynth_soundfont_id, TRUE);
        PV_SF2_UnlockSynth();
        g_fluidsynth_soundfont_id = -1;
        
        // Clear the unloading flag
        g_fluidsynth_unloading = FALSE;
    }
    g_fluidsynth_sf2_path[0] = '\0';
    GM_ResetSF2();
    GM_SetMixerSF2Mode(FALSE);
    GM_BankBalance_OnSf2Unloaded();
}

// Check if a song should use FluidSynth rendering
bool GM_IsSF2Song(GM_Song* pSong)
{
    if (!g_fluidsynth_initialized || g_fluidsynth_soundfont_id < 0 || !pSong)
    {
        return FALSE;
    }

    return pSong->isSF2Song;
}

void sf2_get_channel_amplitudes(float channelAmplitudes[BAE_MAX_MIDI_CHANNELS][2])
{
    for (int ch = 0; ch < BAE_MAX_MIDI_CHANNELS; ch++)
    {
        channelAmplitudes[ch][0] = 0.0f;
        channelAmplitudes[ch][1] = 0.0f;
    }
    
    if (!g_fluidsynth_synth || g_fluidsynth_soundfont_id < 0)
        return;
    
    fluid_synth_get_channel_amplitudes(g_fluidsynth_synth, channelAmplitudes);
}

// Enable/disable FluidSynth rendering for a song
OPErr GM_EnableSF2ForSong(GM_Song* pSong, bool enable)
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
    bool newlyAllocated = FALSE;
    if (!pSong->sf2Info && enable)
    {
        pSong->sf2Info = XNewPtr(sizeof(GM_SF2Info));
        if (!pSong->sf2Info)
        {
            return MEMORY_ERR;
        }
        XBlockMove(pSong->sf2Info, 0, sizeof(GM_SF2Info));
        newlyAllocated = TRUE;
    }
    
    if (pSong->sf2Info)
    {
        GM_SF2Info* sf2Info = (GM_SF2Info*)pSong->sf2Info;
        bool wasActive = sf2Info->sf2_active;
        sf2Info->sf2_active = enable;
        sf2Info->sf2_synth = enable ? g_fluidsynth_synth : NULL;
        sf2Info->sf2_settings = enable ? g_fluidsynth_settings : NULL;
        sf2Info->sf2_soundfont_id = enable ? g_fluidsynth_soundfont_id : -1;
        sf2Info->sf2_master_volume = g_fluidsynth_master_volume;
        sf2Info->sf2_sample_rate = g_fluidsynth_sample_rate;
        sf2Info->sf2_max_voices = BAE_MAX_VOICES;
        
        // Verify the synth pointer is valid before using
        if (enable && !g_fluidsynth_synth)
        {
            // Synth is not available, disable SF2 for this song
            sf2Info->sf2_active = FALSE;
            sf2Info->sf2_synth = NULL;
            enable = FALSE;
        }
        
        // Only initialize defaults the first time we enable SF2 for this song.
        // Repeated calls (e.g., after swapping soundfonts) must not clobber the song's
        // live controller state; that state will be resynced separately.
        if (enable && (newlyAllocated || !wasActive))
        {
            for (int i = 0; i < BAE_MAX_MIDI_CHANNELS; i++)
            {
                sf2Info->channelVolume[i] = 127;
                sf2Info->channelExpression[i] = 127;
                sf2Info->channelReverb[i] = 40;  // Default reverb level
                sf2Info->channelChorus[i] = 0;   // Default chorus level (off)
                sf2Info->channelMuted[i] = FALSE;
            }
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

    // If enabling SF2 while the song is not yet playing (or after a seek), the
    // sequencer may not replay historical setup events (bank/program/CCs) from 0.
    // Push the song's current channel state into FluidSynth now so the first NoteOn
    // after a mid-song start uses the correct preset.
    if (enable)
    {
        PV_SF2_ResyncSongStateToSynth(pSong);
    }
    
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
    
    uint32_t scaledVelocity = velocity;

    if (scaledVelocity <= 0)
        return;
    if (scaledVelocity > MAX_NOTE_VOLUME)
        scaledVelocity = MAX_NOTE_VOLUME;
    
    // Check what preset is selected on this channel
    PV_SF2_LockSynth();
    fluid_preset_t* preset = fluid_synth_get_channel_preset(g_fluidsynth_synth, channel);
    if (!preset) {
        debug_message("[SF2 NoteOn] Channel %d has NO PRESET selected!\n", channel);
    }

    fluid_synth_noteon(g_fluidsynth_synth, channel, note, scaledVelocity);
    PV_SF2_UnlockSynth();
    
    // Update channel activity tracking with original velocity
    PV_SF2_UpdateChannelActivity(channel, scaledVelocity, TRUE);
}

void GM_SF2_ProcessNoteOff(GM_Song* pSong, int16_t channel, int16_t note, int16_t velocity)
{
    if (!GM_IsSF2Song(pSong) || !g_fluidsynth_synth)
    {
        return;
    }
    
    PV_SF2_LockSynth();
    fluid_synth_noteoff(g_fluidsynth_synth, channel, note);
    PV_SF2_UnlockSynth();
    
    // Update channel activity tracking
    PV_SF2_UpdateChannelActivity(channel, 0, FALSE);
}

void GM_SF2_ProcessProgramChange(GM_Song* pSong, int16_t channel, int32_t program)
{
    if (!GM_IsSF2Song(pSong) || !g_fluidsynth_synth)
    {
        return;
    }
    debug_message("[SF2 ProcessProgramChange] Raw Request: program: %i, channel %i\n", program, channel);    
    // Convert program ID to MIDI bank/program
    // NeoBAE uses: instrument = (bank * 128) + program + note
    // For percussion: bank = (bank * 2) + 1, note is included
    // For melodic: bank = bank * 2, note = 0
    int32_t midiBank = (int32_t)(program / 128);    // Bank number (internal mapping)
    int16_t midiProgram = (uint16_t)(program % 128); // Program number or note depending on mapping

    // Determine percussion intent from two signals:
    // 1) Internal odd-bank mapping (legacy NeoBAE percussion mapping)
    // 2) Direct MIDI bank MSB 128 (SF2 percussion bank convention)
    bool isOddBankPerc = ((midiBank % 2) == 1);
    bool isMSB128Perc = FALSE;

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
        if (pSong->songFlags & SONG_FLAG_IS_RMF) {
            // Odd banks are percussion in NeoBAE mapping
            midiBank = (midiBank - 1) / 2;     // Convert back to external MIDI bank
            // Route to SF2 percussion bank
            midiProgram = 0; // Standard drum kit preset
        }
        midiBank = 128;  // SF2 percussion bank
    }
    else if (isMSB128Perc)
    {
        // Treat explicit MIDI bank 128 as percussion
        // Keep requested kit program if provided; use note from low 7 bits if present
        uint16_t extProgram = midiProgram; // may indicate kit variant
        midiBank = 128;  // SF2 percussion bank
        midiProgram = extProgram;          // try requested kit first, fall back later if needed
    }
    else
    {
        // Melodic mapping
        midiBank = midiBank / 2; // Convert back to external MIDI bank
        // midiProgram stays as-is for melodic instruments
    }
    // hack for dumb midis
    if (midiBank == 0 && channel == BAE_PERCUSSION_CHANNEL) {
        // ch 10, percussions
        midiBank = 128;  // SF2 percussion bank
    }

    if (pSong->channelBankMode[channel] == USE_GM_PERC_BANK) {
        if (midiBank == 128 || (midiProgram == 0 && midiBank == 0)) {
            // Already resolved to the percussion bank above (odd-bank / MSB128 mapping),
            // e.g. a non-ch10 channel switched to percussion via NRPN.
            midiBank = 128;  // SF2 percussion bank
        } else {
            // change back to normal channel if the program is not a percussion program
            pSong->channelBankMode[channel] = USE_GM_DEFAULT;
            midiBank = midiBank / 2;
        }

    }

    PV_SF2_LockSynth();

    // mobileBAE MIDI quirk: bank 121 program 124:125 are used for motor vibration.
    // Best behavior is to give the channel no preset at all.
    if (midiBank == 121 && (midiProgram == 124 || midiProgram == 125))
    {
        debug_message("[SF2 ProcessProgramChange] Denying preset request %i:%i on channel %d (unsetting program)\n", midiBank, midiProgram, channel);
        fluid_synth_all_sounds_off(g_fluidsynth_synth, channel);
        fluid_synth_all_notes_off(g_fluidsynth_synth, channel);
        fluid_synth_unset_program(g_fluidsynth_synth, channel);
        PV_SF2_UnlockSynth();
        return;
    }

    // Validate bank/program exist in current font; apply fallback if not
    int useBank = midiBank;
    int useProg = midiProgram;

#if USE_J2ME_PATCH
    if ((useBank * 2) == 128 || (useBank * 2) == 120) {
        pSong->channelBankMode[channel] = USE_GM_PERC_BANK;
        useBank = 128;
    }
#endif    

    if (pSong->channelBankMode[channel] == USE_GM_PERC_BANK) {
        fluid_synth_set_channel_type(g_fluidsynth_synth, channel, CHANNEL_TYPE_DRUM);
    }

    // Alias bank 121 -> bank 0 if bank 121 preset doesn't exist but bank 0 does
    // This handles MIDI files that request bank 121 but the soundfont only has bank 0
    if (useBank == 121 && !PV_SF2_PresetExists(121, useProg) && PV_SF2_PresetExists(0, useProg)) {
        debug_message("[SF2 ProcessProgramChange] Aliasing bank 121 prog %d -> bank 0 prog %d (121:%d not found)\n", useProg, useProg, useProg);
        useBank = 0;
    }

    if (!PV_SF2_PresetExists(useBank, useProg)) {
        bool percIntent = (channel == BAE_PERCUSSION_CHANNEL) || (useBank == 128 || useBank == 120);
        bool found = FALSE;

        // 1. Try fallback to Bank 0 (Capital Tone) with same program
        // This is the standard GM fallback: if variation is missing, use standard instrument
        if (!percIntent && useBank != 0) {
            if (PV_SF2_PresetExists(0, useProg)) {
                debug_message("[SF2 ProcessProgramChange] Fallback: bank %d prog %d not found; using bank 0 prog %d\n", useBank, useProg, useProg);
                useBank = 0;
                found = TRUE;
            }
        }


        // 2. If still not found, try bank 0 (or 128) default
        if (!found) {
            int fbBank = -1, fbProg = 0;
            if (percIntent && PV_SF2_FindFirstPresetInBank(128, &fbProg)) {
                fbBank = 128;
            } else if (!percIntent && PV_SF2_FindFirstPresetInBank(0, &fbProg)) {
                fbBank = 0;
            } else if (PV_SF2_FindAnyPreset(&fbBank, &fbProg)) {
                // leave as found
            }
            if (fbBank >= 0) {
                debug_message("[SF2 ProcessProgramChange] Fallback: no preset for bank %d:%d; selecting %d:%d\n", useBank, useProg, fbBank, fbProg);
                useBank = fbBank; useProg = fbProg;
            }
        }
    }

    pSong->channelRawBank[channel] = useBank;

    // If this soundfont has no canonical drum kit preset, don't load any bank for channel 10.
    // (Avoid incorrectly falling back to melodic bank 0 on the percussion channel.)
    if (channel == BAE_PERCUSSION_CHANNEL || pSong->channelBankMode[channel] == USE_GM_PERC_BANK)
    {
        int drumBank = 128;
        if (!PV_SF2_PresetExists(drumBank, 0))
        {
            // try DLS percussion bank
            drumBank = 120;
            if (!PV_SF2_PresetExists(drumBank, 0)) {        
                debug_message("[SF2 ProcessProgramChange] No drum kit preset 128:0 or 120:0 found; unsetting program on percussion channel %d\n", channel);
                fluid_synth_all_sounds_off(g_fluidsynth_synth, channel);
                fluid_synth_all_notes_off(g_fluidsynth_synth, channel);
        fluid_synth_unset_program(g_fluidsynth_synth, channel);
                PV_SF2_UnlockSynth();
                return;
            }
        }
    }

    // Send MIDI program change event to FluidSynth
    fluid_synth_bank_select(g_fluidsynth_synth, channel, useBank);
    fluid_synth_program_change(g_fluidsynth_synth, channel, useProg);
    debug_message("[SF2 ProcessProgramChange] Final Interpretation: midiBank: %i, midiProgram: %i, channel: %i\n", useBank, useProg, channel);

    PV_SF2_UnlockSynth();
}

void GM_SF2_ProcessController(GM_Song* pSong, int16_t channel, int16_t controller, int16_t value)
{
    if (!GM_IsSF2Song(pSong) || !g_fluidsynth_synth)
    {
        return;
    }

    // Keep SF2-side controller state in sync even during preroll/scan modes so
    // CC-derived effect sends are accurate once playback enters SCAN_NORMAL.
    // We only block forwarding to FluidSynth while not in SCAN_NORMAL.
    // Intercept reverb (91) and chorus (93) to track levels for NeoBAE effects engine
    if (controller == 91 || controller == 93)
    {
        GM_SF2Info* info = (GM_SF2Info*)pSong->sf2Info;
        if (info)
        {
            if (controller == 91)
            {
                info->channelReverb[channel] = value;
            }
            else if (controller == 93)
            {
                info->channelChorus[channel] = value;
            }
        }
        // Don't send to FluidSynth - reverb and chorus are handled by NeoBAE engine
        return;
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

    if (pSong->AnalyzeMode != SCAN_NORMAL) {
        return;
    }
    
    PV_SF2_LockSynth();
    fluid_synth_cc(g_fluidsynth_synth, channel, controller, value);
    PV_SF2_UnlockSynth();
}

void GM_SF2_ProcessPitchBend(GM_Song* pSong, int16_t channel, int16_t bendMSB, int16_t bendLSB)
{
    if (!GM_IsSF2Song(pSong) || !g_fluidsynth_synth)
    {
        return;
    }

    // Only apply pitch bend during normal playback to avoid scan/preroll phases
    // leaving channels in a bent state.
    if (pSong->AnalyzeMode != SCAN_NORMAL)
    {
        return;
    }
    
    int pitchWheel = (bendMSB << 7) | bendLSB;
    PV_SF2_LockSynth();
    fluid_synth_pitch_bend(g_fluidsynth_synth, channel, pitchWheel);
    PV_SF2_UnlockSynth();
}

void GM_SF2_ProcessSysEx(GM_Song* pSong, const unsigned char* message, int32_t length)
{
    if (!GM_IsSF2Song(pSong) || !g_fluidsynth_synth)
    {
        return;
    }
    if (!message || length <= 0)
    {
        return;
    }

    // Only forward SysEx during normal playback. This avoids scan/preroll phases
    // mutating FluidSynth's global state (tuning, resets, etc.).
    if (pSong->AnalyzeMode != SCAN_NORMAL)
    {
        return;
    }

    // FluidSynth may optionally return a response for some SysEx messages.
    // We don't currently use it, but providing a buffer avoids NULL handling edge cases.
    char response[256];
    int response_len = (int)sizeof(response);
    int handled = 0;

    PV_SF2_LockSynth();
    fluid_synth_sysex(g_fluidsynth_synth,
                      (const char*)message,
                      (int)length,
                      response,
                      &response_len,
                      &handled,
                      0);
    PV_SF2_UnlockSynth();
}

// FluidSynth audio rendering - this gets called during mixer slice processing
void GM_SF2_RenderAudioSlice(GM_Song* pSong, int32_t* mixBuffer, int32_t* reverbBuffer, int32_t* chorusBuffer, int32_t frameCount)
{
    if (!GM_IsSF2Song(pSong) || !g_fluidsynth_synth || !mixBuffer || frameCount <= 0)
    {
        return;
    }
    
    // Additional safety check during synth recreation.
    if (!g_fluidsynth_initialized || g_fluidsynth_soundfont_id < 0)
    {
        return;
    }
    
    // CRITICAL: Do not render if we're in the process of unloading the soundfont
    // This prevents race condition crashes when switching soundfonts
    if (g_fluidsynth_unloading)
    {
        return;
    }

    {
        GM_Mixer *pMixer = pSong->pMixer ? pSong->pMixer : GM_GetCurrentMixer();
        if (pMixer)
            g_fluidsynth_mono_mode = !pMixer->generateStereoOutput;
    }

    
    // Update channel activity decay
    PV_SF2_DecayChannelActivity();
    
    // Mixer sample rate for resampling decisions (cached at SF2 open)
    uint32_t mixerRate = g_fluidsynth_sample_rate;

    // FluidSynth always renders at 44100 Hz internally to avoid pitch shifting.
    // When the mixer rate differs, we render at 44100 and downsample via linear interpolation.
    int32_t fsFrames = frameCount;
    
    if (mixerRate != FLUIDSYNTH_INTERNAL_RATE)
    {
        // Calculate how many 44100 Hz input frames produce `frameCount` output frames
        // at the mixer's rate: input_frames = output_frames * (44100 / mixerRate)
        fsFrames = (int32_t)((int64_t)frameCount * FLUIDSYNTH_INTERNAL_RATE / mixerRate) + 1;
        PV_SF2_AllocateMixBuffer(fsFrames);
        if (!g_fluidsynth_mix_buffer)
        {
            return;
        }
    }
    else
    {
        PV_SF2_AllocateMixBuffer(frameCount);
        if (!g_fluidsynth_mix_buffer)
        {
            return;
        }
    }
    
    // Clear the float buffer (always stereo now)
    memset(g_fluidsynth_mix_buffer, 0, fsFrames * 2 * sizeof(float));

    // Render FluidSynth audio (always stereo - we simulate mono in conversion)
    PV_SF2_LockSynth();
    fluid_synth_write_float(g_fluidsynth_synth, fsFrames,
                           g_fluidsynth_mix_buffer, 0, 2,
                           g_fluidsynth_mix_buffer, 1, 2);
    PV_SF2_UnlockSynth();
    
    // Apply BankBalance before peak-limit so a >1x scale cannot re-blow the buffer.
    // Song volume stays in the convert path (after limit) as before.
    {
        float bankScale = GM_BankBalance_GetMixScale(GM_BANK_ENGINE_SF2);
        if (bankScale != 1.0f)
        {
            int32_t totalSamples = fsFrames * 2;
            for (int32_t s = 0; s < totalSamples; s++)
                g_fluidsynth_mix_buffer[s] *= bankScale;
        }
    }
    {
        int32_t totalSamples = fsFrames * 2;
        float peak = 0.0f;
        for (int32_t s = 0; s < totalSamples; s++)
        {
            float v = g_fluidsynth_mix_buffer[s];
            if (v < 0.0f) v = -v;
            if (v > peak) peak = v;
        }
        if (peak > 1.0f)
        {
            float scale = 1.0f / peak;
            for (int32_t s = 0; s < totalSamples; s++)
                g_fluidsynth_mix_buffer[s] *= scale;
        }
    }

    float songScale = 1.0f;
    {
        int32_t fv = pSong->songVolume;
        if (fv >= 0 && fv <= MAX_SONG_VOLUME)
            songScale *= (float)fv / 127.0f;
    }
    // Apply per-channel volume/expression: we post-scale the rendered buffer per frame
    float channelScales[BAE_MAX_MIDI_CHANNELS];
    float channelSendWeights[BAE_MAX_MIDI_CHANNELS];
    GM_SF2Info* info = (GM_SF2Info*)pSong->sf2Info;
    for(int c = 0; c < BAE_MAX_MIDI_CHANNELS; c++)
    {
        float vol = 1.0f;
        if (info)
        {
            vol = (info->channelVolume[c] / 127.0f) * (info->channelExpression[c] / 127.0f);
        }
        channelScales[c] = vol;

        // Activity-aware send weighting: only channels that are currently active
        // (or recently active) contribute meaningfully to shared FX sends.
        // This avoids averaging toward a fixed default from inactive channels.
        {
            ChannelActivity* activity = &g_channel_activity[c];
            float activityWeight = 0.0f;

            if (activity->activeNotes > 0)
            {
                float noteWeight = (float)activity->activeNotes / 4.0f;
                if (noteWeight > 1.0f)
                {
                    noteWeight = 1.0f;
                }
                activityWeight = noteWeight;
            }
            else if (activity->lastActivity > 0)
            {
                // Preserve a small decaying send contribution for release tails.
                float decayTime = (float)activity->lastActivity / 86.0f;
                activityWeight = 0.15f * expf(-decayTime * 2.0f);
            }

            channelSendWeights[c] = vol * activityWeight;
        }
    }
    
    // Get reverb and chorus levels (default to 0 if no info)
    uint8_t reverbLevels[BAE_MAX_MIDI_CHANNELS];
    uint8_t chorusLevels[BAE_MAX_MIDI_CHANNELS];
    for (int c = 0; c < BAE_MAX_MIDI_CHANNELS; c++)
    {
        reverbLevels[c] = info ? info->channelReverb[c] : 0;
        chorusLevels[c] = info ? info->channelChorus[c] : 0;
    }
    
    // When the mixer rate differs from FluidSynth's internal 44100 Hz,
    // downsample via linear interpolation before converting to int32.
    float* finalBuffer = g_fluidsynth_mix_buffer;
    int32_t  finalFrames = fsFrames;
    
    if (mixerRate != FLUIDSYNTH_INTERNAL_RATE)
    {
        PV_SF2_AllocateResampleBuffer(frameCount);
        if (!g_fluidsynth_resample_buffer)
        {
            return;
        }
        
        // Linear interpolation downsample: resample from 44100 Hz to mixerRate
        double ratio = (double)FLUIDSYNTH_INTERNAL_RATE / (double)mixerRate;
        
        for (int32_t outFrame = 0; outFrame < frameCount; outFrame++)
        {
            double srcIndex = (double)outFrame * ratio;
            int32_t srcFrame = (int32_t)srcIndex;
            double frac = srcIndex - (double)srcFrame;
            
            int32_t nextFrame = srcFrame + 1;
            if (nextFrame >= fsFrames) nextFrame = fsFrames - 1;
            
            // Interpolate left channel
            g_fluidsynth_resample_buffer[outFrame * 2] =
                g_fluidsynth_mix_buffer[srcFrame * 2] * (float)(1.0 - frac) +
                g_fluidsynth_mix_buffer[nextFrame * 2] * (float)frac;
            // Interpolate right channel
            g_fluidsynth_resample_buffer[outFrame * 2 + 1] =
                g_fluidsynth_mix_buffer[srcFrame * 2 + 1] * (float)(1.0 - frac) +
                g_fluidsynth_mix_buffer[nextFrame * 2 + 1] * (float)frac;
        }
        finalBuffer = g_fluidsynth_resample_buffer;
        finalFrames = frameCount;
    }
    
    // Convert float to int32 and mix with existing buffer (including reverb/chorus sends)
    PV_SF2_ConvertFloatToInt32(finalBuffer, mixBuffer, reverbBuffer, chorusBuffer,
                               finalFrames, songScale, channelSendWeights, reverbLevels, chorusLevels);
}

// FluidSynth channel management (respects NeoBAE mute/solo states)
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

    PV_SF2_LockSynth();
    fluid_synth_all_sounds_off(g_fluidsynth_synth, channel);
    PV_SF2_UnlockSynth();
}

void GM_SF2_AllNotesOff(GM_Song* pSong)
{
    if (!g_fluidsynth_synth)
        return;
    
    for (int i = 0; i < BAE_MAX_MIDI_CHANNELS; i++) 
        GM_SF2_KillChannelNotes(i);
}

// FluidSynth configuration
void GM_SF2_SetGain(float volume)
{
    PV_SF2_LockSynth();
    fluid_synth_set_gain(g_fluidsynth_synth, volume);
    PV_SF2_UnlockSynth();
}

float GM_SF2_GetGain() {
    PV_SF2_LockSynth();
    float gain = fluid_synth_get_gain(g_fluidsynth_synth);
    PV_SF2_UnlockSynth();
    return gain;
}

float GM_SF2_GetMasterVolume(void)
{
    return g_fluidsynth_master_volume;
}

int16_t GM_SF2_GetMaxVoices(void)
{
    return BAE_MAX_VOICES;
}

void GM_SF2_SetStereoMode(bool stereo, bool applyNow)
{
    // Just set the flag - we'll simulate mono in the conversion function
    // instead of recreating the FluidSynth synth which can cause crashes
    g_fluidsynth_mono_mode = !stereo;
    
    // No need to recreate the synth - FluidSynth stays in stereo mode always
    // We handle mono simulation in PV_SF2_ConvertFloatToInt32
}

void GM_SF2_SetSampleRate(int32_t sampleRate)
{
    if (!g_fluidsynth_initialized)
    {
        g_fluidsynth_sample_rate = sampleRate;
        return;
    }

    // Only update the cached mixer sample rate used for the resampling ratio.
    // FluidSynth always runs at 44100 Hz internally to avoid pitch shifting.
    // Resampling to the mixer's rate happens in GM_SF2_RenderAudioSlice.
    g_fluidsynth_sample_rate = sampleRate;
}

void GM_SF2_KillAllNotes(void) 
{
    if (!g_fluidsynth_synth)
    {
        return;
    }

    fluid_synth_reverb_on(g_fluidsynth_synth, -1, 0);  // Turn off reverb for all fx groups
    fluid_synth_chorus_on(g_fluidsynth_synth, -1, 0);  // Turn off chorus for all fx groups

    for (int i = 0; i < BAE_MAX_MIDI_CHANNELS; i++) 
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

bool GM_SF2_IsActive(void)
{
    return g_fluidsynth_initialized && g_fluidsynth_synth && g_fluidsynth_soundfont_id >= 0;
}

bool GM_SF2_CurrentFontHasAnyPreset(int *outPresetCount)
{
    if (!g_fluidsynth_synth || g_fluidsynth_soundfont_id < 0) {
        if (outPresetCount) *outPresetCount = 0;
        return FALSE;
    }
    int count = 0;
    
    // Search through ALL loaded soundfonts (overlay + base)
    int sfcount = fluid_synth_sfcount(g_fluidsynth_synth);
    for (int i = 0; i < sfcount; i++) {
        fluid_sfont_t* sf = fluid_synth_get_sfont(g_fluidsynth_synth, i);
        if (sf) {
            fluid_preset_t p;
            fluid_sfont_iteration_start(sf);
            while (fluid_sfont_iteration_next(sf, &p)) {
                count++; 
                if (count > 0) break; // early out once we know it's non-empty
            }
            if (count > 0) break; // found presets, no need to check other soundfonts
        }
    }
    
    if (outPresetCount) *outPresetCount = count;
    return (count > 0) ? TRUE : FALSE;
}

static float PV_SF2_LoudnessInt16(const short *pcm, unsigned int frames, int stride)
{
    double sumSq = 0.0;
    double peak = 0.0;
    unsigned int n = 0;
    unsigned int f;
    double thresh;

    if (!pcm || frames == 0)
        return 0.0f;
    if (stride < 1)
        stride = 1;

    for (f = 0; f < frames; f += (unsigned int)stride)
    {
        double s = (double)pcm[f] * (1.0 / 32768.0);
        double a = (s < 0.0) ? -s : s;
        if (a > peak)
            peak = a;
    }

    thresh = peak * 0.10;
    for (f = 0; f < frames; f += (unsigned int)stride)
    {
        double s = (double)pcm[f] * (1.0 / 32768.0);
        double a = (s < 0.0) ? -s : s;
        if (a >= thresh)
        {
            sumSq += s * s;
            n++;
        }
    }

    if (n == 0)
        return (float)peak;
    {
        float activeRms = (float)sqrt(sumSq / (double)n);
        float peakFloor = (float)(peak * 0.35);
        return (activeRms > peakFloor) ? activeRms : peakFloor;
    }
}

static float PV_SF2_MedianInPlace(float *values, int count)
{
    int i, j;
    if (count <= 0)
        return 0.0f;
    for (i = 1; i < count; i++)
    {
        float key = values[i];
        j = i - 1;
        while (j >= 0 && values[j] > key)
        {
            values[j + 1] = values[j];
            j--;
        }
        values[j + 1] = key;
    }
    if (count & 1)
        return values[count / 2];
    return 0.5f * (values[count / 2 - 1] + values[count / 2]);
}

float GM_SF2_MeasureBankLoudness(void)
{
    enum { kMaxValues = 128, kProbeKey = 60, kProbeVel = 64, kStride = 8 };
    float values[kMaxValues];
    int valueCount = 0;
    fluid_sfont_t *sfont;
    fluid_defsfont_t *defsfont;
    fluid_defpreset_t *preset;
    float pathGain;

    if (!g_fluidsynth_synth || g_fluidsynth_soundfont_id < 0)
        return 0.0f;

    PV_SF2_LockSynth();
    sfont = fluid_synth_get_sfont_by_id(g_fluidsynth_synth, g_fluidsynth_soundfont_id);
    if (!sfont || !sfont->data)
    {
        PV_SF2_UnlockSynth();
        return 0.0f;
    }

    defsfont = (fluid_defsfont_t *)sfont->data;
    pathGain = g_fluidsynth_master_volume;
    if (pathGain <= 0.0f)
        pathGain = 0.5f;

    for (preset = defsfont->preset; preset && valueCount < kMaxValues; preset = preset->next)
    {
        fluid_preset_zone_t *pzone;
        float best = 0.0f;

        /* Skip percussion bank for melodic level matching. */
        if (preset->bank == 128)
            continue;

        for (pzone = preset->zone; pzone; pzone = pzone->next)
        {
            fluid_inst_t *inst;
            fluid_inst_zone_t *izone;
            double presetAtten = 0.0;

            if (pzone->keylo > kProbeKey || pzone->keyhi < kProbeKey)
                continue;
            if (pzone->vello > kProbeVel || pzone->velhi < kProbeVel)
                continue;

            if (pzone->gen[GEN_ATTENUATION].flags)
                presetAtten = pzone->gen[GEN_ATTENUATION].val;

            inst = pzone->inst;
            if (!inst)
                continue;

            for (izone = inst->zone; izone; izone = izone->next)
            {
                fluid_sample_t *sample;
                double atten;
                unsigned int start, end, frames;
                float rms;
                float loud;

                if (izone->keylo > kProbeKey || izone->keyhi < kProbeKey)
                    continue;
                if (izone->vello > kProbeVel || izone->velhi < kProbeVel)
                    continue;

                sample = izone->sample;
                if (!sample || !sample->data || !sample->valid)
                    continue;
                if (sample->sampletype & FLUID_SAMPLETYPE_ROM)
                    continue;
                /* Prefer mono / left; skip lone right channel of stereo pairs. */
                if (sample->sampletype & FLUID_SAMPLETYPE_RIGHT)
                    continue;

                atten = presetAtten;
                if (inst->global_zone && inst->global_zone->gen[GEN_ATTENUATION].flags)
                    atten += inst->global_zone->gen[GEN_ATTENUATION].val;
                if (izone->gen[GEN_ATTENUATION].flags)
                    atten += izone->gen[GEN_ATTENUATION].val;

                start = sample->start;
                end = sample->end;
                if (end <= start)
                    continue;
                frames = end - start + 1;
                rms = PV_SF2_LoudnessInt16(sample->data + start, frames, kStride);
                /* FluidLite applies only ALT_ATTENUATION_SCALE (0.4) of GEN_ATTENUATION
                 * (see fluid_voice.c). Full-scale atten here under-read loudness and
                 * made BankBalance over-boost SF2. */
                loud = rms * (float)pow(10.0, -(atten * 0.4) / 200.0);
                if (loud > best)
                    best = loud;
            }
        }

        if (best > 1.0e-6f)
            values[valueCount++] = best;
    }

    PV_SF2_UnlockSynth();

    if (valueCount <= 0)
        return 0.0f;
    return PV_SF2_MedianInPlace(values, valueCount) * pathGain;
}

float GM_SF2_EstimateNoteLoudness(int bank, int program, int key, int velocity)
{
    enum { kStride = 8 };
    fluid_sfont_t *sfont;
    fluid_defsfont_t *defsfont;
    fluid_defpreset_t *preset;
    float pathGain;
    float best = 0.0f;
    int wantBank = bank & 0x3FFF;
    int wantProg = program & 0x7F;

    if (key < 0)
        key = 0;
    if (key > 127)
        key = 127;
    if (velocity < 1)
        velocity = 1;
    if (velocity > 127)
        velocity = 127;

    if (!g_fluidsynth_synth || g_fluidsynth_soundfont_id < 0)
        return 0.0f;

    PV_SF2_LockSynth();
    sfont = fluid_synth_get_sfont_by_id(g_fluidsynth_synth, g_fluidsynth_soundfont_id);
    if (!sfont || !sfont->data)
    {
        PV_SF2_UnlockSynth();
        return 0.0f;
    }

    defsfont = (fluid_defsfont_t *)sfont->data;
    pathGain = g_fluidsynth_master_volume;
    if (pathGain <= 0.0f)
        pathGain = 0.5f;

    for (preset = defsfont->preset; preset; preset = preset->next)
    {
        fluid_preset_zone_t *pzone;

        if ((int)preset->bank != wantBank || (int)preset->num != wantProg)
            continue;

        for (pzone = preset->zone; pzone; pzone = pzone->next)
        {
            fluid_inst_t *inst;
            fluid_inst_zone_t *izone;
            double presetAtten = 0.0;

            if (pzone->keylo > key || pzone->keyhi < key)
                continue;
            if (pzone->vello > velocity || pzone->velhi < velocity)
                continue;

            if (pzone->gen[GEN_ATTENUATION].flags)
                presetAtten = pzone->gen[GEN_ATTENUATION].val;

            inst = pzone->inst;
            if (!inst)
                continue;

            for (izone = inst->zone; izone; izone = izone->next)
            {
                fluid_sample_t *sample;
                double atten;
                unsigned int start, end, frames;
                float rms;
                float loud;

                if (izone->keylo > key || izone->keyhi < key)
                    continue;
                if (izone->vello > velocity || izone->velhi < velocity)
                    continue;

                sample = izone->sample;
                if (!sample || !sample->data || !sample->valid)
                    continue;
                if (sample->sampletype & FLUID_SAMPLETYPE_ROM)
                    continue;
                if (sample->sampletype & FLUID_SAMPLETYPE_RIGHT)
                    continue;

                atten = presetAtten;
                if (inst->global_zone && inst->global_zone->gen[GEN_ATTENUATION].flags)
                    atten += inst->global_zone->gen[GEN_ATTENUATION].val;
                if (izone->gen[GEN_ATTENUATION].flags)
                    atten += izone->gen[GEN_ATTENUATION].val;

                start = sample->start;
                end = sample->end;
                if (end <= start)
                    continue;
                frames = end - start + 1;
                rms = PV_SF2_LoudnessInt16(sample->data + start, frames, kStride);
                /* Match FluidLite ALT_ATTENUATION_SCALE (0.4). */
                loud = rms * (float)pow(10.0, -(atten * 0.4) / 200.0);
                if (loud > best)
                    best = loud;
            }
        }
        break;
    }

    PV_SF2_UnlockSynth();
    return best * pathGain;
}

void GM_SF2_SetChannelMode(int16_t channel, int16_t mode)
{
    if (!g_fluidsynth_synth)
        return;

    PV_SF2_LockSynth();
    if (mode == USE_GM_PERC_BANK) {
        fluid_synth_set_channel_type(g_fluidsynth_synth, channel, CHANNEL_TYPE_DRUM);
    } else {
        fluid_synth_set_channel_type(g_fluidsynth_synth, channel, CHANNEL_TYPE_MELODIC);
    }
    PV_SF2_UnlockSynth();
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
    
    // Clear FluidSynth's internal effects buffers that can cause lingering audio.
    // This is much lighter than full reinitialization but should clear reverb/chorus tails.
    
    // Ensure FluidSynth's internal effects remain completely disabled
    fluid_synth_reverb_on(g_fluidsynth_synth, -1, 0);  // Keep reverb off for all fx groups
    fluid_synth_chorus_on(g_fluidsynth_synth, -1, 0);  // Keep chorus off for all fx groups
    
    // Ensure any (legacy) voices allocated before FluidSynth activation enter release
    GM_EndSongNotes(pSong);
}

// Private helper functions
static bool PV_SF2_CheckChannelMuted(GM_Song* pSong, int16_t channel)
{
    if (!pSong || !pSong->sf2Info)
        return FALSE;
        
    GM_SF2Info* info = (GM_SF2Info*)pSong->sf2Info;
    return info->channelMuted[channel];
}

static void PV_SF2_ConvertFloatToInt32(float* input, int32_t* output, int32_t* reverbOutput, int32_t* chorusOutput, 
                                        int32_t frameCount, float songVolumeScale, const float *channelScales,
                                        const uint8_t *reverbLevels, const uint8_t *chorusLevels)
{
    //const float kScale = 2147483647.0f;
    const double kScale = 32767.0 * (1L << OUTPUT_SCALAR);

    
    // Note: Channel volume/expression are handled by FluidSynth via CC7/CC11.
    // We only apply song-level volume here. `channelScales` are used only for weighting reverb/chorus
    // amounts across active channels.
    float globalScale = songVolumeScale;
    
    // Average reverb/chorus levels only across channels with non-zero volume
    // This prevents inactive channels from affecting the reverb mix
    // Apply additional scaling to match the perceived reverb level of built-in instruments
    float totalWeight = 0.0f;
    float weightedReverb = 0.0f;
    float weightedChorus = 0.0f;
    
    for (int c = 0; c < BAE_MAX_MIDI_CHANNELS; c++)
    {
        float weight = channelScales[c];
        if (weight > 0.01f)  // Only count channels with audible volume
        {
            totalWeight += weight;
            weightedReverb += reverbLevels[c] * weight;
            weightedChorus += chorusLevels[c] * weight;
        }
    }
    
    float reverbScale = 0.0f;
    float chorusScale = 0.0f;
    if (totalWeight > 0.0f)
    {
        // Average by active channel weight, then normalize by 128 (>> 7)
        // Apply 0.5x factor to reduce intensity for SF2's richer sound
        reverbScale = (weightedReverb / totalWeight) / 128.0f;
        chorusScale = (weightedChorus / totalWeight) / 128.0f;
    }

    if (g_fluidsynth_mono_mode)
    {
        // True mono mode: FluidSynth renders stereo, but we downconvert to mono
        // Output true mono format - one sample per frame (output[frame])
        // The mixer expects mono buffer layout when mono mode is enabled
        for (int32_t frame = 0; frame < frameCount; frame++)
        {
            float leftSample = input[frame * 2] * globalScale;
            float rightSample = input[frame * 2 + 1] * globalScale;
            
            // Mix stereo to mono (average L+R)
            float mono = (leftSample + rightSample) * 0.5f;
            
            // Convert to 32-bit fixed point (no clamp needed - int32 provides
            // ~128x headroom above 1.0 and the output limiter handles peaks)
            int32_t intSample = (int32_t)(mono * kScale);
            
            // Write single-channel PCM (one sample per frame, true mono layout)
            output[frame] += intSample;
            
            // Mix into reverb and chorus buffers if they exist
            // Use the scaled sample directly (reverb is a percentage of the dry signal)
            // Apply 20/128 wet mix factor to match DLS reverb send level
            if (reverbOutput && reverbScale > 0.0f)
            {
                int32_t reverbSample = (int32_t)(intSample * reverbScale * 20.0f / 64.0f);
                reverbOutput[frame] += reverbSample;
            }
            if (chorusOutput && chorusScale > 0.0f)
            {
                int32_t chorusSample = (int32_t)(intSample * chorusScale * 20.0f / 64.0f);
                chorusOutput[frame] += chorusSample;
            }
        }
    }
    else
    {
        // Stereo conversion: input buffer has frameCount * 2 samples (interleaved L,R)
        // Output interleaved stereo format - two samples per frame (output[frame * 2], output[frame * 2 + 1])
        for (int32_t frame = 0; frame < frameCount; frame++)
        {
            float leftSample = input[frame * 2] * globalScale;
            float rightSample = input[frame * 2 + 1] * globalScale;
            
            // Convert to 32-bit fixed point and add to existing buffer
            // (no clamp needed - int32 provides ~128x headroom above 1.0
            // and the output limiter handles peaks)
            int32_t leftInt = (int32_t)(leftSample * kScale);
            int32_t rightInt = (int32_t)(rightSample * kScale);
            output[frame * 2] += leftInt;     // Left
            output[frame * 2 + 1] += rightInt; // Right
            
            // Mix into reverb and chorus buffers if they exist (stereo interleaved)
            // Use the scaled samples directly (reverb/chorus are percentages of the dry signal)
            // Apply 20/128 wet mix factor to match DLS reverb send level
            if (reverbOutput && reverbScale > 0.0f)
            {
                int32_t monoSend = (leftInt / 2) + (rightInt / 2);
                reverbOutput[frame] += (int32_t)(monoSend * reverbScale * 20.0f / 64.0f);
            }
            if (chorusOutput && chorusScale > 0.0f)
            {
                int32_t monoSend = (leftInt / 2) + (rightInt / 2);
                chorusOutput[frame] += (int32_t)(monoSend * chorusScale * 20.0f / 64.0f);
            }
        }
    }
}

static void PV_SF2_AllocateMixBuffer(int32_t frameCount)
{
    // Always allocate for stereo since FluidSynth stays in stereo mode
    int32_t requiredSize = frameCount * 2;
    
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

static void PV_SF2_AllocateResampleBuffer(int32_t frameCount)
{
    if (g_fluidsynth_resample_buffer_frames < frameCount)
    {
        if (g_fluidsynth_resample_buffer)
        {
            XDisposePtr(g_fluidsynth_resample_buffer);
            g_fluidsynth_resample_buffer = NULL;
            g_fluidsynth_resample_buffer_frames = 0;
        }
        g_fluidsynth_resample_buffer = (float*)XNewPtr(frameCount * 2 * sizeof(float));
        if (g_fluidsynth_resample_buffer)
        {
            g_fluidsynth_resample_buffer_frames = frameCount;
        }
    }
}

static void PV_SF2_FreeResampleBuffer(void)
{
    if (g_fluidsynth_resample_buffer)
    {
        XDisposePtr(g_fluidsynth_resample_buffer);
        g_fluidsynth_resample_buffer = NULL;
        g_fluidsynth_resample_buffer_frames = 0;
    }
}

static void PV_SF2_InitializeChannelActivity(void)
{
    for (int ch = 0; ch < BAE_MAX_MIDI_CHANNELS; ch++)
    {
        g_channel_activity[ch].leftLevel = 0.0f;
        g_channel_activity[ch].rightLevel = 0.0f;
        g_channel_activity[ch].activeNotes = 0;
        g_channel_activity[ch].noteVelocity = 0.0f;
        g_channel_activity[ch].lastActivity = 0;
    }
    g_activity_frame_counter = 0;
}

static void PV_SF2_UpdateChannelActivity(int16_t channel, int16_t velocity, bool noteOn)
{
    if (channel < 0 || channel >= BAE_MAX_MIDI_CHANNELS)
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
    
    for (int ch = 0; ch < BAE_MAX_MIDI_CHANNELS; ch++)
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

// Iterate presets and pick one that exists. Prefer any preset on bank 128 for channel 10.
static void PV_SF2_SetValidDefaultProgramsForAllChannels(void)
{
    if (!g_fluidsynth_synth)
        return;

    GM_SF2_SetDefaultControllers();

    // If no font loaded, nothing else to do
    if (g_fluidsynth_soundfont_id < 0)
        return;

    // Try to find a default melodic preset and a drum kit preset
    // We prefer: melodic -> bank 0, drums -> bank 128:0 (SF2) or 120:0 (DLS).
    // If the canonical drum kit preset doesn't exist, do NOT fall back to any other bank on the percussion channel.
    int foundMelodicBank = -1, foundMelodicProg = 0;
    int foundDrumBank = -1, foundDrumProg = 0; // look for bank 128 if available
    int firstBank = -1, firstProg = 0;         // fallback to the very first preset seen
    
    // Search through ALL loaded soundfonts (overlay + base)
    int sfcount = fluid_synth_sfcount(g_fluidsynth_synth);
    for (int i = 0; i < sfcount; i++) {
        fluid_sfont_t* sf = fluid_synth_get_sfont(g_fluidsynth_synth, i);
        if (sf) {
            fluid_preset_t p;
            fluid_sfont_iteration_start(sf);
            while (fluid_sfont_iteration_next(sf, &p)) {
                int bank = fluid_preset_get_banknum(&p);
                if (firstBank < 0 && bank != 120 && bank != 128) { firstBank = bank; firstProg = 0; }
                if (bank == 0 && foundMelodicBank < 0) { // capture first bank 0 as a generic melodic default
                    foundMelodicBank = bank; foundMelodicProg = 0;
                }
                if (bank == 128 && foundDrumBank < 0) { // canonical SF2 drum kit
                    foundDrumBank = bank; foundDrumProg = 0;
                }
                if (bank != 128 && bank != 120 && foundMelodicBank < 0) { // first non-drum melodic preset
                    foundMelodicBank = bank; foundMelodicProg = 0;
                }
                if (foundMelodicBank >= 0 && foundDrumBank >= 0) {
                    break; // found both preferred presets
                }
            }
        }
        if (foundMelodicBank >= 0 && !PV_SF2_PresetExists(foundMelodicBank, foundMelodicProg)) {
            PV_SF2_FindFirstPresetInBank(foundMelodicBank, &firstProg);
            foundMelodicProg = firstProg;
        }
        if (foundDrumBank >= 0 && !PV_SF2_PresetExists(foundDrumBank, foundDrumProg)) {
            PV_SF2_FindFirstPresetInBank(foundDrumBank, &foundDrumProg);
        }
    }

    // Only accept the canonical drum kit preset.
    if (foundDrumBank < 0) {
        // try SF2 percussion bank
        int preferredDrumBank = 128;
        if (PV_SF2_PresetExists(preferredDrumBank, 0))
        {
            foundDrumBank = preferredDrumBank;
            foundDrumProg = 0;
        } else {
            // try DLS percussion bank
            preferredDrumBank = 120;
            if (PV_SF2_PresetExists(preferredDrumBank, 0))
            {
                foundDrumBank = preferredDrumBank;
                foundDrumProg = 0;
            }
        }
    }

    // Fallbacks if preferred banks not found
    if (foundMelodicBank < 0 && firstBank >= 0) { foundMelodicBank = firstBank; foundMelodicProg = firstProg; }

    debug_message("[FluidMem] Default presets: melodic bank=%d prog=%d, drums bank=%d prog=%d (first=%d:%d)\n",
               foundMelodicBank, foundMelodicProg, foundDrumBank, foundDrumProg, firstBank, firstProg);

    // Apply per-channel defaults
    for (int ch = 0; ch < BAE_MAX_MIDI_CHANNELS; ch++) {
        if (ch == BAE_PERCUSSION_CHANNEL) {
            if (foundDrumBank >= 0) {
                fluid_synth_bank_select(g_fluidsynth_synth, ch, foundDrumBank);
                fluid_synth_program_change(g_fluidsynth_synth, ch, foundDrumProg);
            } else {
                fluid_synth_unset_program(g_fluidsynth_synth, ch);
            }
        } else {
            if (foundMelodicBank >= 0) {
                fluid_synth_bank_select(g_fluidsynth_synth, ch, foundMelodicBank);
                fluid_synth_program_change(g_fluidsynth_synth, ch, foundMelodicProg);
            }
        }
    }
}

#endif // USE_SF2_SUPPORT && defined(_USING_FLUIDLITE)
