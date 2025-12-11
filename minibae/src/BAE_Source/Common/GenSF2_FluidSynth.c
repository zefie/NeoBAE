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
#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#else
#include <link.h>
#endif

#include "fluidsynth.h"
#include "GenSnd.h"
#include "GenPriv.h"
#include "X_Assert.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include "MiniBAE.h"
#ifdef _WIN32
#include <windows.h>
#endif

// For temporary file fallback when loading DLS banks (path-based load only)
#include <unistd.h>  // mkstemp, write, close, unlink, fsync

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
static XFIXED g_fluidsynth_master_volume = (XFIXED)(XFIXED_1 / 256);
static uint16_t g_fluidsynth_sample_rate = BAE_DEFAULT_SAMPLE_RATE;
static char g_fluidsynth_sf2_path[256] = {0};
// Track a temp file we create for DLS fallback so we can remove it on unload
static char g_temp_sf_path[256] = {0};
static XBOOL g_temp_sf_is_tempfile = FALSE;
// When loading DLS banks, FluidSynth will emit an error log
// "Not a SoundFont file". This is expected; ignore it.
static XBOOL g_suppress_not_sf2_error = FALSE;
// Flag to prevent audio thread from accessing synth during unload (prevents race condition crashes)
static volatile XBOOL g_fluidsynth_unloading = FALSE;

// Minimal FluidSynth log filter used for DLS loads to suppress the expected error
static void pv_fluidsynth_log_filter(int level, const char* message, void* data)
{
    (void)data;
    // Suppress only the noisy, expected error during DLS load
    if (g_suppress_not_sf2_error && level == FLUID_ERR && message && strstr(message, "Not a SoundFont file") != NULL)
    {
        return; // ignore
    }
    // Print via BAE_PRINTF to preserve other logs in debug mode
    if (message)
    {
        BAE_PRINTF("fluidsynth: %s", message);
    }
}

// Channel activity tracking
static ChannelActivity g_channel_activity[BAE_MAX_MIDI_CHANNELS];
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

// Choose sane default presets per channel after loading a bank to avoid
// "No preset found on channel X" warnings. Prefer bank 128 on channel 10.
static void PV_SF2_SetValidDefaultProgramsForAllChannels(void);

// Helpers to validate and choose presets present in the current font
static XBOOL PV_SF2_PresetExists(int bank, int prog)
{
    if (!g_fluidsynth_synth || g_fluidsynth_soundfont_id < 0) return FALSE;
    fluid_sfont_t* sf = fluid_synth_get_sfont(g_fluidsynth_synth, 0);
    if (!sf) return FALSE;
    fluid_preset_t* p = NULL;
    fluid_sfont_iteration_start(sf);
    while ((p = fluid_sfont_iteration_next(sf)) != NULL) {
        if (fluid_preset_get_banknum(p) == bank && fluid_preset_get_num(p) == prog)
            return TRUE;
    }
    return FALSE;
}

static XBOOL PV_SF2_FindFirstPresetInBank(int bank, int *outProg)
{
    if (!g_fluidsynth_synth || g_fluidsynth_soundfont_id < 0 || !outProg) return FALSE;
    fluid_sfont_t* sf = fluid_synth_get_sfont(g_fluidsynth_synth, 0);
    if (!sf) return FALSE;
    fluid_preset_t* p = NULL;
    fluid_sfont_iteration_start(sf);
    while ((p = fluid_sfont_iteration_next(sf)) != NULL) {
        if (fluid_preset_get_banknum(p) == bank) { *outProg = fluid_preset_get_num(p); return TRUE; }
    }
    return FALSE;
}

static XBOOL PV_SF2_FindAnyPreset(int *outBank, int *outProg)
{
    if (!g_fluidsynth_synth || g_fluidsynth_soundfont_id < 0 || !outBank || !outProg) return FALSE;
    fluid_sfont_t* sf = fluid_synth_get_sfont(g_fluidsynth_synth, 0);
    if (!sf) return FALSE;
    fluid_preset_t* p = NULL;
    fluid_sfont_iteration_start(sf);
    if ((p = fluid_sfont_iteration_next(sf)) != NULL) {
        *outBank = fluid_preset_get_banknum(p);
        *outProg = fluid_preset_get_num(p);
        return TRUE;
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
    
    // Create FluidSynth settings
    g_fluidsynth_settings = new_fluid_settings();
    if (!g_fluidsynth_settings)
    {
        return MEMORY_ERR;
    }
    
    // Configure FluidSynth settings
    fluid_settings_setnum(g_fluidsynth_settings, "synth.sample-rate", g_fluidsynth_sample_rate);
    fluid_settings_setint(g_fluidsynth_settings, "synth.polyphony", BAE_MAX_VOICES);
    fluid_settings_setint(g_fluidsynth_settings, "synth.midi-channels", BAE_MAX_MIDI_CHANNELS);
    fluid_settings_setnum(g_fluidsynth_settings, "synth.gain", XFIXED_TO_FLOAT(g_fluidsynth_master_volume));
    fluid_settings_setint(g_fluidsynth_settings, "synth.audio-channels", 1);  // Sets the number of stereo channel pairs. So 1 is actually 2 channels (a stereo pair).
    
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
    // Establish safe default programs/controllers (will be refined after font load)
    PV_SF2_SetValidDefaultProgramsForAllChannels();
    
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
    
    PV_SF2_FreeMixBuffer();
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
    
    g_fluidsynth_initialized = FALSE;
}


void GM_ResetSF2(void) 
{
    if (!g_fluidsynth_synth)
        return;

    // Kill all notes currently playing
    GM_SF2_KillAllNotes();
    // Reset all channels and voices
    fluid_synth_system_reset(g_fluidsynth_synth);
    // Pick valid defaults again after reset
    PV_SF2_SetValidDefaultProgramsForAllChannels();
}


// In-memory SF2/DLS loading via FluidSynth defsfloader + custom file callbacks
typedef struct {
    const unsigned char *data;
    size_t size;
    size_t pos;
} fs_mem_stream_t;

static const unsigned char *g_mem_sf_data = NULL;
static size_t g_mem_sf_size = 0;
static fluid_sfloader_t *g_mem_sf_loader = NULL; // persistent loader with callbacks

static void *fs_mem_open(const char *filename) {
    (void)filename; // unused
    if (!g_mem_sf_data || g_mem_sf_size == 0) {
        BAE_PRINTF("[FluidMem] fs_mem_open: no buffer set (filename=%s)\n", filename ? filename : "(null)");
        return NULL;
    }
    fs_mem_stream_t *s = (fs_mem_stream_t *)malloc(sizeof(fs_mem_stream_t));
    if (!s) return NULL;
    s->data = g_mem_sf_data;
    s->size = g_mem_sf_size;
    s->pos = 0;
    BAE_PRINTF("[FluidMem] fs_mem_open: %zu bytes (filename=%s)\n", s->size, filename ? filename : "(null)");
    return s;
}

static int fs_mem_read(void *buf, fluid_long_long_t count, void *handle) {
    fs_mem_stream_t *s = (fs_mem_stream_t *)handle;
    if (!s || !buf || count <= 0) return FLUID_FAILED;
    // clamp count to available bytes and to size_t
    size_t want = (size_t)count;
    if (s->pos + want > s->size) {
        // not enough data to satisfy exactly count bytes
        return FLUID_FAILED;
    }
    memcpy(buf, s->data + s->pos, want);
    s->pos += want;
    return FLUID_OK;
}

static int fs_mem_seek(void *handle, fluid_long_long_t offset, int origin) {
    fs_mem_stream_t *s = (fs_mem_stream_t *)handle;
    if (!s) return FLUID_FAILED;
    // compute new position
    size_t new_pos = 0;
    switch (origin) {
        case SEEK_SET:
            if (offset < 0) return FLUID_FAILED;
            new_pos = (size_t)offset;
            break;
        case SEEK_CUR:
            if ((offset < 0 && (size_t)(-offset) > s->pos)) return FLUID_FAILED;
            new_pos = s->pos + (size_t)offset;
            break;
        case SEEK_END:
            if ((offset < 0 && (size_t)(-offset) > s->size)) return FLUID_FAILED;
            new_pos = s->size + (size_t)offset;
            break;
        default:
            return FLUID_FAILED;
    }
    if (new_pos > s->size) return FLUID_FAILED;
    s->pos = new_pos;
    return FLUID_OK;
}

static fluid_long_long_t fs_mem_tell(void *handle) {
    fs_mem_stream_t *s = (fs_mem_stream_t *)handle;
    if (!s) return (fluid_long_long_t)FLUID_FAILED;
    return (fluid_long_long_t)s->pos;
}

static int fs_mem_close(void *handle) {
    if (handle) free(handle);
    return FLUID_OK;
}

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
#ifdef __EMSCRIPTEN__
    return false; // dl_iterate_phdr not supported in Emscripten
#else
    struct ctx { int found; } context = {0};

    int callback(struct dl_phdr_info *info, size_t size, void *data) {
        struct ctx *ctx = (struct ctx *)data;
        if (info->dlpi_name && strstr(info->dlpi_name, "libinstpatch")) {
            ctx->found = 1;
            return true; // stop iteration
        }
        return false;
    }

    dl_iterate_phdr(callback, &context);
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

    // Detect container type
    XBOOL isRIFF = (size >= 12 && data[0]=='R' && data[1]=='I' && data[2]=='F' && data[3]=='F');
    //XBOOL isSF2 = FALSE;
    XBOOL isDLS = FALSE;
    if (isRIFF) {
        const unsigned char *type = data + 8;
        //isSF2 = (type[0]=='s' && type[1]=='f' && type[2]=='b' && type[3]=='k');
        isDLS = (type[0]=='D' && type[1]=='L' && type[2]=='S' && type[3]==' ');
    }
    //if (isDLS && is_libinstpatch_loaded()) {
    if (isDLS) {    
        // fluidsynth requires a path-based load for DLS files
        GM_UnloadSF2Soundfont();
        char tmpl[] = "/tmp/minibae_dls_XXXXXX.dls"; // keep .dls suffix
#if defined(_WIN32) || defined(_WIN64)
        int fd = mkstemp(tmpl);
#else
        int fd = mkstemps(tmpl, 4);
#endif
        if (fd < 0) {
            return GENERAL_BAD;
        }
        ssize_t written = 0;
        while ((size_t)written < size) {
            ssize_t w = write(fd, data + written, size - (size_t)written);
            if (w <= 0) { close(fd); unlink(tmpl); return GENERAL_BAD; }
            written += w;
        }
#if _WIN32
        HANDLE h = (HANDLE)_get_osfhandle(fd);
        FlushFileBuffers(h);
#else        
        fsync(fd);
#endif
        close(fd);
        // Temporarily suppress the expected FluidSynth error log for DLS
        g_suppress_not_sf2_error = TRUE;
        fluid_log_function_t prev_err = fluid_set_log_function(FLUID_ERR, pv_fluidsynth_log_filter, NULL);
        OPErr perr = GM_LoadSF2Soundfont(tmpl);
        // Restore previous logging behavior
        fluid_set_log_function(FLUID_ERR, prev_err, NULL);
        g_suppress_not_sf2_error = FALSE;
        if (perr == NO_ERR) {
            strncpy(g_temp_sf_path, tmpl, sizeof(g_temp_sf_path)-1);
            g_temp_sf_path[sizeof(g_temp_sf_path)-1] = '\0';
            g_temp_sf_is_tempfile = TRUE;
        } else {
            unlink(tmpl);
        }
        return perr;
    }

    // Prepare global memory buffer for callbacks (SF2)
    g_mem_sf_data = data;
    g_mem_sf_size = size;

    // Ensure we have a defsfloader with our callbacks installed once
    if (!g_mem_sf_loader) {
        g_mem_sf_loader = new_fluid_defsfloader(g_fluidsynth_settings);
        if (!g_mem_sf_loader) {
            g_mem_sf_data = NULL; g_mem_sf_size = 0;
            return MEMORY_ERR;
        }
        // Install callbacks as per FluidSynth 2.x API
        fluid_sfloader_set_callbacks(
            g_mem_sf_loader,
            fs_mem_open,
            fs_mem_read,
            fs_mem_seek,
            fs_mem_tell,
            fs_mem_close
        );
        // Add our loader to the synth
        fluid_synth_add_sfloader(g_fluidsynth_synth, g_mem_sf_loader);
        BAE_PRINTF("[FluidMem] defsfloader registered\n");
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
            fluid_synth_process(g_fluidsynth_synth, SAMPLE_BLOCK_SIZE, 0, 0, 0, NULL);
        }
        
        // Now safe to unload
        fluid_synth_sfunload(g_fluidsynth_synth, g_fluidsynth_soundfont_id, TRUE);
        g_fluidsynth_soundfont_id = -1;
        
        // Clear the unloading flag
        g_fluidsynth_unloading = FALSE;
    }
    g_fluidsynth_sf2_path[0] = '\0';
    if (g_temp_sf_is_tempfile) {
        unlink(g_temp_sf_path);
        g_temp_sf_path[0] = '\0';
        g_temp_sf_is_tempfile = FALSE;
    }
    GM_SetMixerSF2Mode(FALSE);
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

void sf2_get_channel_amplitudes(float channelAmplitudes[BAE_MAX_MIDI_CHANNELS][2])
{
    // Always zero-out destination first
    for (int ch = 0; ch < BAE_MAX_MIDI_CHANNELS; ch++)
    {
        channelAmplitudes[ch][0] = 0.0f;
        channelAmplitudes[ch][1] = 0.0f;
    }
    
    if (!g_fluidsynth_synth || g_fluidsynth_soundfont_id < 0)
    {
        return;
    }
    
    // Method 1: Voice-based amplitude monitoring (more accurate)
    // Get list of active voices from FluidSynth
    const int maxVoices = BAE_MAX_VOICES;  // FluidSynth default max polyphony
    fluid_voice_t* voiceList[maxVoices];
    
    // Get all active voices
    fluid_synth_get_voicelist(g_fluidsynth_synth, voiceList, maxVoices, -1);
    
    // Accumulate amplitude estimates per channel based on active voices
    float channelVoiceCounts[BAE_MAX_MIDI_CHANNELS] = {0};
    float channelVelocitySum[BAE_MAX_MIDI_CHANNELS] = {0};
    
    for (int i = 0; i < maxVoices && voiceList[i] != NULL; i++)
    {
        fluid_voice_t* voice = voiceList[i];
        
        // Skip if voice is not playing
        if (!fluid_voice_is_playing(voice))
            continue;
            
        int channel = fluid_voice_get_channel(voice);
        if (channel < 0 || channel >= BAE_MAX_MIDI_CHANNELS)
            continue;
            
        int velocity = fluid_voice_get_actual_velocity(voice);
        
        // Accumulate voice information per channel
        channelVoiceCounts[channel] += 1.0f;
        channelVelocitySum[channel] += velocity;
        
        // Get voice envelope/amplitude level (if available)
        // Note: FluidSynth doesn't directly expose voice amplitude,
        // so we estimate based on velocity and voice state
        float voiceAmplitude = 0.0f;
        
        if (fluid_voice_is_on(voice))
        {
            // Voice is in attack/sustain phase
            voiceAmplitude = (float)velocity / 127.0f * 0.8f;
        }
        else
        {
            // Voice is in release phase - assume lower amplitude
            voiceAmplitude = (float)velocity / 127.0f * 0.3f;
        }
        
        // Add to channel amplitude (simplified stereo assumption)
        channelAmplitudes[channel][0] += voiceAmplitude * 0.1f; // Scale down for multiple voices
        channelAmplitudes[channel][1] += voiceAmplitude * 0.1f;
    }
    
    // Method 2: Fallback to note tracking for channels with no voice data
    for (int ch = 0; ch < BAE_MAX_MIDI_CHANNELS; ch++)
    {
        if (channelVoiceCounts[ch] == 0.0f)
        {
            // No voices found, use our note tracking as fallback
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
        else
        {
            // Apply mono/stereo mode to voice-based amplitudes
            if (g_fluidsynth_mono_mode)
            {
                // Convert stereo to mono
                float mono = (channelAmplitudes[ch][0] + channelAmplitudes[ch][1]) * 0.5f;
                channelAmplitudes[ch][0] = mono;
                channelAmplitudes[ch][1] = mono;
            }
            
            // Clamp to reasonable display ranges
            if (channelAmplitudes[ch][0] > 1.0f) channelAmplitudes[ch][0] = 1.0f;
            if (channelAmplitudes[ch][1] > 1.0f) channelAmplitudes[ch][1] = 1.0f;
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
        sf2Info->sf2_max_voices = BAE_MAX_VOICES;
        
        // Verify the synth pointer is valid before using
        if (enable && !g_fluidsynth_synth)
        {
            // Synth is not available, disable SF2 for this song
            sf2Info->sf2_active = FALSE;
            sf2Info->sf2_synth = NULL;
            enable = FALSE;
        }
        
        // Init per-channel volume/expression defaults (GM defaults: volume 127, expression 127)
        for (int i = 0; i < BAE_MAX_MIDI_CHANNELS; i++) 
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
    if (midiBank == 0 && channel == BAE_PERCUSSION_CHANNEL) {
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
        // Validate bank/program exist in current font; apply fallback if not
        int useBank = midiBank;
        int useProg = midiProgram;
        if (!PV_SF2_PresetExists(useBank, useProg)) {
            int altProg;
            if (PV_SF2_FindFirstPresetInBank(useBank, &altProg)) {
                // Use first program available in requested bank
                BAE_PRINTF("[FluidMem] Fallback: bank %d has no prog %d; using prog %d\n", useBank, useProg, altProg);
                useProg = altProg;
            } else {
                // If percussion intent, try bank 128; else try bank 0; finally pick any preset
                XBOOL percIntent = (channel == BAE_PERCUSSION_CHANNEL) || (useBank == 128);
                int fbBank = -1, fbProg = 0;
                if (percIntent && PV_SF2_FindFirstPresetInBank(128, &fbProg)) {
                    fbBank = 128;
                } else if (!percIntent && PV_SF2_FindFirstPresetInBank(0, &fbProg)) {
                    fbBank = 0;
                } else if (PV_SF2_FindAnyPreset(&fbBank, &fbProg)) {
                    // leave as found
                }
                if (fbBank >= 0) {
                    BAE_PRINTF("[FluidMem] Fallback: no presets in bank %d; selecting %d:%d\n", useBank, fbBank, fbProg);
                    useBank = fbBank; useProg = fbProg;
                }
            }
        }

        // Send MIDI program change event to FluidSynth
        fluid_synth_bank_select(g_fluidsynth_synth, channel, useBank);
        fluid_synth_program_change(g_fluidsynth_synth, channel, useProg);
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
    
    // Only send controller changes to FluidSynth during normal playback
    // This prevents preroll/scanning phases from applying controller changes
    // that should only take effect during actual playback
    if (pSong->AnalyzeMode == SCAN_NORMAL)
    {
        fluid_synth_cc(g_fluidsynth_synth, channel, controller, value);
    }
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
    
    // Additional safety check during synth recreation
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
    
    // Update channel activity decay
    PV_SF2_DecayChannelActivity();
    
    // Allocate mix buffer if needed
    PV_SF2_AllocateMixBuffer(frameCount);
    if (!g_fluidsynth_mix_buffer)
    {
        return;
    }
    
    // Clear the float buffer (always stereo now)
    memset(g_fluidsynth_mix_buffer, 0, frameCount * 2 * sizeof(float));
    
    // Render FluidSynth audio (always stereo - we simulate mono in conversion)
    fluid_synth_write_float(g_fluidsynth_synth, frameCount,
                           g_fluidsynth_mix_buffer, 0, 2,
                           g_fluidsynth_mix_buffer, 1, 2);
    
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
    float channelScales[BAE_MAX_MIDI_CHANNELS];
    GM_SF2Info* info = (GM_SF2Info*)pSong->sf2Info;
    for(int c = 0; c < BAE_MAX_MIDI_CHANNELS; c++)
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

    fluid_synth_all_notes_off(g_fluidsynth_synth, channel);
    fluid_synth_all_sounds_off(g_fluidsynth_synth, channel);
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
    fluid_synth_set_gain(g_fluidsynth_synth, volume);
}

float GM_SF2_GetGain() {
    return fluid_synth_get_gain(g_fluidsynth_synth);
}

XFIXED GM_SF2_GetMasterVolume(void)
{
    return g_fluidsynth_master_volume;
}

int16_t GM_SF2_GetMaxVoices(void)
{
    return BAE_MAX_VOICES;
}

void GM_SF2_SetStereoMode(XBOOL stereo, XBOOL applyNow)
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
        // Just store the sample rate for later initialization
        g_fluidsynth_sample_rate = sampleRate;
        return;
    }

    g_fluidsynth_sample_rate = sampleRate;
    
    // FluidSynth requires recreating the synth to change sample rate
    // Store current state
    char currentSF2Path[256];
    strncpy(currentSF2Path, g_fluidsynth_sf2_path, sizeof(currentSF2Path) - 1);
    currentSF2Path[sizeof(currentSF2Path) - 1] = '\0';
    
    // Cleanup current synth
    GM_UnloadSF2Soundfont();
    if (g_fluidsynth_synth)
    {
        delete_fluid_synth(g_fluidsynth_synth);
        g_fluidsynth_synth = NULL;
    }
    
    // Update settings
    fluid_settings_setnum(g_fluidsynth_settings, "synth.sample-rate", sampleRate);
    
    // Recreate synth with new settings
    g_fluidsynth_synth = new_fluid_synth(g_fluidsynth_settings);
    if (g_fluidsynth_synth && currentSF2Path[0] != '\0')
    {
        // Reload soundfont
        GM_LoadSF2Soundfont(currentSF2Path);
    }
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

XBOOL GM_SF2_IsActive(void)
{
    return g_fluidsynth_initialized && g_fluidsynth_synth && g_fluidsynth_soundfont_id >= 0;
}

XBOOL GM_SF2_CurrentFontHasAnyPreset(int *outPresetCount)
{
    if (!g_fluidsynth_synth || g_fluidsynth_soundfont_id < 0) {
        if (outPresetCount) *outPresetCount = 0;
        return FALSE;
    }
    int count = 0;
    fluid_sfont_t* sf = fluid_synth_get_sfont(g_fluidsynth_synth, 0);
    if (sf) {
        fluid_preset_t* p = NULL;
        fluid_sfont_iteration_start(sf);
        while ((p = fluid_sfont_iteration_next(sf)) != NULL) {
            count++;
            if (count > 0) break; // early out once we know it's non-empty
        }
    }
    if (outPresetCount) *outPresetCount = count;
    return (count > 0) ? TRUE : FALSE;
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

    fluid_synth_system_reset(g_fluidsynth_synth);
    
    // Program selection handled globally in PV_SF2_SetValidDefaultProgramsForAllChannels()
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
    
    // Temporarily disable effects to clear their buffers
    fluid_synth_reverb_on(g_fluidsynth_synth, -1, 0);  // Turn off reverb for all fx groups
    fluid_synth_chorus_on(g_fluidsynth_synth, -1, 0);  // Turn off chorus for all fx groups
    
    // Re-enable effects (they'll start with clean buffers)
    fluid_synth_reverb_on(g_fluidsynth_synth, -1, 1);  // Turn reverb back on
    fluid_synth_chorus_on(g_fluidsynth_synth, -1, 1);  // Turn chorus back on
    
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
    const float kScale = 2147483647.0f;
    
    // For now, apply a simple global scaling since we don't have per-channel separation in the final mix
    // A more sophisticated implementation would require per-channel rendering
    float globalScale = songVolumeScale;
    
    // Average the channel scales for a rough approximation
    float avgChannelScale = 0.0f;
    for (int c = 0; c < BAE_MAX_MIDI_CHANNELS; c++)
    {
        avgChannelScale += channelScales[c];
    }
    avgChannelScale /= 16.0f;
    globalScale *= avgChannelScale;

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
            
            // Clamp to prevent overflow
            if (mono > 1.0f) mono = 1.0f;
            else if (mono < -1.0f) mono = -1.0f;
            
            // Convert to 32-bit fixed point
            int32_t intSample = (int32_t)(mono * kScale);
            
            // Write single-channel PCM (one sample per frame, true mono layout)
            output[frame] += intSample;
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
            
            // Clamp to prevent overflow
            if (leftSample > 1.0f) leftSample = 1.0f;
            else if (leftSample < -1.0f) leftSample = -1.0f;
            if (rightSample > 1.0f) rightSample = 1.0f;
            else if (rightSample < -1.0f) rightSample = -1.0f;
            
            // Convert to 32-bit fixed point and add to existing buffer
            output[frame * 2] += (int32_t)(leftSample * kScale);     // Left
            output[frame * 2 + 1] += (int32_t)(rightSample * kScale); // Right
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

static void PV_SF2_UpdateChannelActivity(int16_t channel, int16_t velocity, XBOOL noteOn)
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

    // Controller defaults first
    for (int ch = 0; ch < BAE_MAX_MIDI_CHANNELS; ch++) {
        fluid_synth_cc(g_fluidsynth_synth, ch, 7, 80);
        fluid_synth_cc(g_fluidsynth_synth, ch, 10, 64);
        fluid_synth_cc(g_fluidsynth_synth, ch, 11, 100);
        fluid_synth_cc(g_fluidsynth_synth, ch, 64, 0);
        fluid_synth_cc(g_fluidsynth_synth, ch, 91, 0);
        fluid_synth_cc(g_fluidsynth_synth, ch, 93, 0);
    }

    fluid_synth_system_reset(g_fluidsynth_synth);

    // If no font loaded, nothing else to do
    if (g_fluidsynth_soundfont_id < 0)
        return;

    // Try to find a default melodic preset and a drum kit preset
    // We prefer: melodic -> bank 0, drums -> bank 128. If those don't exist, fall back to first preset found.
    int foundMelodicBank = -1, foundMelodicProg = 0;
    int foundDrumBank = -1, foundDrumProg = 0; // look for bank 128 if available
    int firstBank = -1, firstProg = 0;         // fallback to the very first preset seen
    fluid_sfont_t* sf = fluid_synth_get_sfont(g_fluidsynth_synth, 0);
    if (sf) {
        fluid_preset_t* p = NULL;
        fluid_sfont_iteration_start(sf);
        while ((p = fluid_sfont_iteration_next(sf)) != NULL) {
            int bank = fluid_preset_get_banknum(p);
            int prog = fluid_preset_get_num(p);
            if (firstBank < 0) { firstBank = bank; firstProg = prog; }
            if (bank == 128 && foundDrumBank < 0) {
                foundDrumBank = bank; foundDrumProg = prog;
            }
            if (bank == 0 && foundMelodicBank < 0) { // capture first bank 0 as a generic melodic default
                foundMelodicBank = bank; foundMelodicProg = prog;
            }
        }
    }

    // Fallbacks if preferred banks not found
    if (foundMelodicBank < 0 && firstBank >= 0) { foundMelodicBank = firstBank; foundMelodicProg = firstProg; }
    if (foundDrumBank   < 0 && firstBank >= 0)   { foundDrumBank   = firstBank; foundDrumProg   = firstProg; }

    BAE_PRINTF("[FluidMem] Default presets: melodic bank=%d prog=%d, drums bank=%d prog=%d (first=%d:%d)\n",
               foundMelodicBank, foundMelodicProg, foundDrumBank, foundDrumProg, firstBank, firstProg);

    // Apply per-channel defaults
    for (int ch = 0; ch < BAE_MAX_MIDI_CHANNELS; ch++) {
        if (ch == BAE_PERCUSSION_CHANNEL) {
            if (foundDrumBank >= 0) {
                fluid_synth_bank_select(g_fluidsynth_synth, ch, foundDrumBank);
                fluid_synth_program_change(g_fluidsynth_synth, ch, foundDrumProg);
            }
        } else {
            if (foundMelodicBank >= 0) {
                fluid_synth_bank_select(g_fluidsynth_synth, ch, foundMelodicBank);
                fluid_synth_program_change(g_fluidsynth_synth, ch, foundMelodicProg);
            }
        }
    }
}

#endif // USE_SF2_SUPPORT && defined(_USING_FLUIDSYNTH)
