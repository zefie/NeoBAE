/****************************************************************************
 *
 * GenSF2_FluidSynth.c
 *
 * FluidSynth integration for NeoBAE
 * Provides SF2 soundfont support through FluidSynth when USE_SF2_SUPPORT is enabled
 *
 ****************************************************************************/

#ifndef _WIN32
#define _GNU_SOURCE  // Required for dl_iterate_phdr on Linux
#endif

#include "GenSF2_FluidSynth.h"

#if USE_SF2_SUPPORT == TRUE && _USING_FLUIDSYNTH == TRUE
#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#else
#include <link.h>
#endif

#ifndef PATH_MAX
#include <limits.h>
#endif

#include "fluidsynth.h"
#include "GenSnd.h"
#include "GenPriv.h"
#include "X_Assert.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include "NeoBAE.h"


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
static int g_fluidsynth_base_soundfont_id = -1;  // Base GM soundfont (e.g., user-loaded SF2)
static int g_fluidsynth_xmf_overlay_id = -1;     // XMF embedded bank overlay
static int g_fluidsynth_xmf_overlay_bank_offset = 0;  // Bank offset for XMF overlay (0 or 2)
static XBOOL g_fluidsynth_initialized = FALSE;
static XBOOL g_fluidsynth_mono_mode = FALSE;
static XFIXED g_fluidsynth_master_volume = (XFIXED)(XFIXED_1 / 512);
static uint16_t g_fluidsynth_sample_rate = BAE_DEFAULT_SAMPLE_RATE;
static char g_fluidsynth_sf2_path[256] = {0};
// Track a temp file we create for DLS fallback so we can remove it on unload
static char g_temp_sf_path[256] = {0};
static XBOOL g_temp_sf_is_tempfile = FALSE;
// Track temp file for XMF overlay DLS banks
static char g_temp_xmf_overlay_path[256] = {0};
static XBOOL g_temp_xmf_overlay_is_tempfile = FALSE;
// When loading DLS banks, FluidSynth will emit an error log
// "Not a SoundFont file". This is expected; ignore it.
static XBOOL g_suppress_not_sf2_error = FALSE;
static XBOOL g_fluidsynth_soundfont_is_dls = FALSE;
// Flag to prevent audio thread from accessing synth during unload (prevents race condition crashes)
static volatile XBOOL g_fluidsynth_unloading = FALSE;
static XBOOL g_hasBank121Presets = FALSE;

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
static void PV_SF2_ConvertFloatToInt32(float* input, int32_t* output, int32_t* reverbOutput, int32_t* chorusOutput, 
                                        int32_t frameCount, float songVolumeScale, const float *channelScales,
                                        const uint8_t *reverbLevels, const uint8_t *chorusLevels);
static void PV_SF2_AllocateMixBuffer(int32_t frameCount);
static void PV_SF2_FreeMixBuffer(void);
static void PV_SF2_InitializeChannelActivity(void);
static void PV_SF2_UpdateChannelActivity(int16_t channel, int16_t velocity, XBOOL noteOn);
static void PV_SF2_DecayChannelActivity(void);

// Choose sane default presets per channel after loading a bank to avoid
// "No preset found on channel X" warnings. Prefer bank 128 on channel 10.
static void PV_SF2_SetValidDefaultProgramsForAllChannels(void);

// Helpers to validate and choose presets present in the current font
// Check if a preset exists in a specific soundfont by ID
static XBOOL PV_SF2_PresetExistsInSoundFont(int sfid, int bank, int prog)
{
    if (!g_fluidsynth_synth || sfid < 0) return FALSE;
    
    fluid_sfont_t* sf = fluid_synth_get_sfont_by_id(g_fluidsynth_synth, sfid);
    if (!sf) return FALSE;
    
    fluid_preset_t* p = NULL;
    fluid_sfont_iteration_start(sf);
    while ((p = fluid_sfont_iteration_next(sf)) != NULL) {
        if (fluid_preset_get_banknum(p) == bank && fluid_preset_get_num(p) == prog)
            return TRUE;
    }
    return FALSE;
}

static XBOOL PV_SF2_PresetExists(int bank, int prog)
{
    if (!g_fluidsynth_synth || g_fluidsynth_soundfont_id < 0) return FALSE;
    
    // Search through ALL loaded soundfonts (overlay + base)
    int sfcount = fluid_synth_sfcount(g_fluidsynth_synth);
    for (int i = 0; i < sfcount; i++) {
        fluid_sfont_t* sf = fluid_synth_get_sfont(g_fluidsynth_synth, i);
        if (!sf) continue;
        
        fluid_preset_t* p = NULL;
        fluid_sfont_iteration_start(sf);
        while ((p = fluid_sfont_iteration_next(sf)) != NULL) {
            if (fluid_preset_get_banknum(p) == bank && fluid_preset_get_num(p) == prog)
                return TRUE;
        }
    }
    return FALSE;
}

static XBOOL PV_SF2_FindFirstPresetInBank(int bank, int *outProg)
{
    if (!g_fluidsynth_synth || g_fluidsynth_soundfont_id < 0 || !outProg) return FALSE;
    
    // Search through ALL loaded soundfonts (overlay + base)
    int sfcount = fluid_synth_sfcount(g_fluidsynth_synth);
    for (int i = 0; i < sfcount; i++) {
        fluid_sfont_t* sf = fluid_synth_get_sfont(g_fluidsynth_synth, i);
        if (!sf) continue;
        
        fluid_preset_t* p = NULL;
        fluid_sfont_iteration_start(sf);
        while ((p = fluid_sfont_iteration_next(sf)) != NULL) {
            if (fluid_preset_get_banknum(p) == bank) { 
                *outProg = fluid_preset_get_num(p); 
                return TRUE; 
            }
        }
    }
    return FALSE;
}

static XBOOL PV_SF2_FindAnyPreset(int *outBank, int *outProg)
{
    if (!g_fluidsynth_synth || g_fluidsynth_soundfont_id < 0 || !outBank || !outProg) return FALSE;
    
    // Search through ALL loaded soundfonts (overlay + base)
    int sfcount = fluid_synth_sfcount(g_fluidsynth_synth);
    for (int i = 0; i < sfcount; i++) {
        fluid_sfont_t* sf = fluid_synth_get_sfont(g_fluidsynth_synth, i);
        if (!sf) continue;
        
        fluid_preset_t* p = NULL;
        fluid_sfont_iteration_start(sf);
        if ((p = fluid_sfont_iteration_next(sf)) != NULL) {
            *outBank = fluid_preset_get_banknum(p);
            *outProg = fluid_preset_get_num(p);
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
    fluid_settings_setint(g_fluidsynth_settings, "synth.reverb.active", 0);
    
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


// Check if all loaded instruments in a song are RMF-embedded (not from SF2)
// If so, disable SF2 mode for this song to avoid double-playback
static XBOOL PV_SF2_AllInstrumentsAreRMFEmbedded(GM_Song* pSong)
{
    if (!pSong || !(pSong->songFlags & SONG_FLAG_IS_RMF))
        return FALSE;
    
    // Get count of RMF instruments from index 0
    uint32_t rmfInstCount = pSong->RMFInstrumentIDs[0];
    if (rmfInstCount == 0)
        return FALSE; // No RMF instruments, can't be all-embedded
    
    // Count how many instruments are actually loaded in the song
    int loadedCount = 0;
    for (int i = 0; i < (MAX_INSTRUMENTS * MAX_BANKS); i++)
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
    for (int i = 0; i < (MAX_INSTRUMENTS * MAX_BANKS); i++)
    {
        if (pSong->instrumentData[i] != NULL)
        {
            // Check if this instrument ID exists in the RMF embedded list
            XBOOL found = FALSE;
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
        }
    }
    
    // Check if all loaded wavetable instruments are RMF-embedded
    XBOOL allLoadedEmbedded = (matchedCount == loadedCount && loadedCount > 0);
    
    if (!allLoadedEmbedded)
        return FALSE;
    
    // Check if there are any USED instruments that aren't loaded (would need SF2)
    // Strategy: Check all channels that have been programmed
    BAE_PRINTF("[SF2] Checking programmed channels for non-embedded instruments...\n");
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
            BAE_PRINTF("[SF2] Channel %d uses program 0 (bank 0 program 0) - skipping (default value, all loaded are embedded)\n", channel);
            continue;
        }
        
        BAE_PRINTF("[SF2] Channel %d uses program %d\n", channel, program);
        
        // Check if this instrument is loaded
        if (pSong->instrumentData[program] == NULL)
        {
            // Not loaded - check if it's in the RMF embedded list
            XBOOL isEmbedded = FALSE;
            for (uint32_t j = 1; j <= rmfInstCount; j++)
            {
                if (pSong->RMFInstrumentIDs[j] == (uint32_t)program)
                {
                    isEmbedded = TRUE;
                    break;
                }
            }
            
            // If programmed but not loaded and not embedded, SF2 must provide it
            if (!isEmbedded)
            {
                BAE_PRINTF("[SF2] Channel %d program %d is not loaded and NOT RMF-embedded - SF2 needed\n", 
                           channel, program);
                return FALSE;
            }
            else
            {
                BAE_PRINTF("[SF2] Channel %d program %d is not loaded but IS RMF-embedded (will load on demand)\n",
                           channel, program);
            }
        }
        else
        {
            BAE_PRINTF("[SF2] Channel %d program %d is loaded\n", channel, program);
        }
    }
    
    if (allLoadedEmbedded)
    {
        BAE_PRINTF("[SF2] All %d loaded instruments are RMF-embedded (out of %u declared in RMF)\n",
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
        BAE_PRINTF("[SF2] RMF has all instruments embedded - disabling SF2 mode for this song\n");
        // Disable SF2 for this specific song
        pSong->songFlags &= ~SONG_FLAG_USE_SF2;
        GM_EnableSF2ForSong(pSong, FALSE);
    }
}

void GM_SoftResetSF2(void) {
    if (!g_fluidsynth_synth)
        return;

    // Soft reset: Reset controllers without resetting programs
    // This resets pitch bend, modulation, sustain, etc.
    // but preserves the currently selected instrument on each channel
    // NOTE: We do NOT reset CC7 (volume) or CC11 (expression) here because
    // MIDI files set these during preroll and we don't want to overwrite them
    
    for (int ch = 0; ch < BAE_MAX_MIDI_CHANNELS; ch++) {
        // Reset pitch bend to center (8192 = 0x2000)
        fluid_synth_pitch_bend(g_fluidsynth_synth, ch, 8192);
        
        // Reset modulation wheel (CC 1)
        fluid_synth_cc(g_fluidsynth_synth, ch, 1, 0);
        
        // DO NOT reset CC7 (volume) - let MIDI file control it
        // DO NOT reset CC11 (expression) - let MIDI file control it
        
        // Reset pan (CC 10) to center
        fluid_synth_cc(g_fluidsynth_synth, ch, 10, 64);
        
        // Reset sustain pedal (CC 64) to off
        fluid_synth_cc(g_fluidsynth_synth, ch, 64, 0);
        
        // Disable reverb (CC 91)
        fluid_synth_cc(g_fluidsynth_synth, ch, 91, 0);
        
        // Disable chorus (CC 93)
        fluid_synth_cc(g_fluidsynth_synth, ch, 93, 0);
        
        // Reset RPN parameters
        fluid_synth_cc(g_fluidsynth_synth, ch, 100, 127); // RPN LSB
        fluid_synth_cc(g_fluidsynth_synth, ch, 101, 127); // RPN MSB
    }
}

// FluidSynth default controller setup
void GM_SF2_SetDefaultControllers(int16_t channel)
{
    if (!g_fluidsynth_synth)
        return;
    
    fluid_synth_system_reset(g_fluidsynth_synth);
    GM_SoftResetSF2();
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


#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
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
#ifdef __EMSCRIPTEN__
    return false; // dl_iterate_phdr not supported in Emscripten
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
    BAE_PRINTF("[FluidMem] Loading %zu bytes from memory", size);
    
    // Detect container type
    XBOOL isRIFF = (size >= 12 && data[0]=='R' && data[1]=='I' && data[2]=='F' && data[3]=='F');
    //XBOOL isSF2 = FALSE;
    XBOOL isDLS = FALSE;
    if (isRIFF) {
        const unsigned char *type = data + 8;
        //isSF2 = (type[0]=='s' && type[1]=='f' && type[2]=='b' && type[3]=='k');
        isDLS = (type[0]=='D' && type[1]=='L' && type[2]=='S' && type[3]==' ');
    }
    g_fluidsynth_soundfont_is_dls = FALSE;
    //if (isDLS && is_libinstpatch_loaded()) {
    if (isDLS) {
        g_fluidsynth_soundfont_is_dls = TRUE;    
        // fluidsynth requires a path-based load for DLS files
        GM_UnloadSF2Soundfont();
        int fd;
        char tmpl[PATH_MAX]; // keep .dls suffix
#if defined(_WIN32) || defined(_WIN64)
        // Get %TEMP% directory
        char tempPath[MAX_PATH];
        DWORD len = GetTempPathA(MAX_PATH, tempPath);
        if (len == 0 || len > MAX_PATH) {
            BAE_PRINTF("[FluidMem] Failed to get TEMP path for DLS temp file\n");
            return 1;
        }

        snprintf(tmpl, sizeof(tmpl), "%sneobae_dls_XXXXXX", tempPath);
        fd = mkstemp(tmpl);
#elif defined(__ANDROID__)
        // On Android, use cache directory from TMPDIR environment variable
        // or fallback to app-specific cache path
        const char* tmpdir = getenv("TMPDIR");
        if (!tmpdir || strlen(tmpdir) == 0) {
            // Fallback to common Android cache locations
            tmpdir = getenv("EXTERNAL_STORAGE");
            if (!tmpdir || strlen(tmpdir) == 0) {
                tmpdir = "/data/local/tmp";
            }
        }
        snprintf(tmpl, sizeof(tmpl), "%s/neobae_dls_XXXXXX.dls", tmpdir);
        fd = mkstemps(tmpl, 4);
#else
        // On Linux/Unix, just use /tmp
        strcpy(tmpl, "/tmp/neobae_dls_XXXXXX.dls");
        fd = mkstemps(tmpl, 4);
#endif

        if (fd < 0) {
            BAE_PRINTF("[FluidMem] Failed to create temporary file for DLS load (%s)\n", tmpl);
            return GENERAL_BAD;
        }
        ssize_t written = 0;
        while ((size_t)written < size) {
            ssize_t w = write(fd, data + written, size - (size_t)written);
            if (w <= 0) { close(fd); unlink(tmpl); BAE_PRINTF("[FluidMem] Failed to write to temporary file for DLS load\n"); return GENERAL_BAD; }
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
            BAE_PRINTF("[FluidMem] Failed to load temp DLS file into FluidSynth\n");
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
    // Track this as the base soundfont (not an XMF overlay)
    g_fluidsynth_base_soundfont_id = g_fluidsynth_soundfont_id;
    
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

    // Try to open the file and read first 16 bytes
    FILE *sf2_file = fopen(sf2_path, "rb");
    if (!sf2_file) {
        BAE_PRINTF("[FluidMem] Failed to open SF2 file: %s\n", sf2_path);
        return BAD_FILE;
    }
    unsigned char sf2_header[16] = {0};
    size_t bytes_read = fread(sf2_header, 1, 16, sf2_file);
    fclose(sf2_file);
    if (bytes_read < 16) 
    {
        BAE_PRINTF("[FluidMem] Could not read 16 bytes from SF2 file: %s\n", sf2_path);
        return BAD_FILE;
    }

    g_fluidsynth_soundfont_is_dls = FALSE;
    XBOOL isRIFF = (sf2_header[0]=='R' && sf2_header[1]=='I' && sf2_header[2]=='F' && sf2_header[3]=='F');
    if (isRIFF) {
        const unsigned char *type = sf2_header + 8;
        g_fluidsynth_soundfont_is_dls = (type[0]=='D' && type[1]=='L' && type[2]=='S' && type[3]==' ') ? TRUE : FALSE;
    }

    // Load new soundfont
    g_fluidsynth_soundfont_id = fluid_synth_sfload(g_fluidsynth_synth, sf2_path, TRUE);
    if (g_fluidsynth_soundfont_id == FLUID_FAILED)
    {
        return GENERAL_BAD;
    }
    
    // Track this as the base soundfont (not an XMF overlay)
    g_fluidsynth_base_soundfont_id = g_fluidsynth_soundfont_id;
    
    // Store path
    strncpy(g_fluidsynth_sf2_path, sf2_path, sizeof(g_fluidsynth_sf2_path) - 1);
    g_fluidsynth_sf2_path[sizeof(g_fluidsynth_sf2_path) - 1] = '\0';
    
    // Set Ch 10 to percussion by default
    PV_SF2_SetValidDefaultProgramsForAllChannels();
    GM_SetMixerSF2Mode(TRUE);
    return NO_ERR;
}

#if USE_XMF_SUPPORT == TRUE
// Load a soundfont as an XMF overlay (does not unload base soundfont)
// The overlay soundfont takes priority for instruments it contains
OPErr GM_LoadSF2SoundfontAsXMFOverlay(const unsigned char *data, size_t size) {
    if (!g_fluidsynth_initialized) {
        OPErr err = GM_InitializeSF2();
        if (err != NO_ERR) {
            return err;
        }
    }

    if (!data || size == 0 || !g_fluidsynth_synth) {
        return PARAM_ERR;
    }

    BAE_PRINTF("[XMF] Loading embedded bank as overlay (%zu bytes)\n", size);
    
    // Detect container type
    XBOOL isRIFF = (size >= 12 && data[0]=='R' && data[1]=='I' && data[2]=='F' && data[3]=='F');
    XBOOL isDLS = FALSE;
    if (isRIFF) {
        const unsigned char *type = data + 8;
        isDLS = (type[0]=='D' && type[1]=='L' && type[2]=='S' && type[3]==' ');
    }
    
    // Unload any existing XMF overlay first
    GM_UnloadXMFOverlaySoundFont();
    
    if (isDLS) {
        // DLS requires path-based load via temp file
        int fd;
        char tmpl[PATH_MAX];
#if defined(_WIN32) || defined(_WIN64)
        char tempPath[MAX_PATH];
        DWORD len = GetTempPathA(MAX_PATH, tempPath);
        if (len == 0 || len > MAX_PATH) {
            BAE_PRINTF("[XMF] Failed to get TEMP path for XMF DLS overlay\n");
            return GENERAL_BAD;
        }
        snprintf(tmpl, sizeof(tmpl), "%sneobae_xmf_dls_XXXXXX", tempPath);
        fd = mkstemp(tmpl);
#elif defined(__ANDROID__)
        const char* tmpdir = getenv("TMPDIR");
        if (!tmpdir || strlen(tmpdir) == 0) {
            tmpdir = getenv("EXTERNAL_STORAGE");
            if (!tmpdir || strlen(tmpdir) == 0) {
                tmpdir = "/data/local/tmp";
            }
        }
        snprintf(tmpl, sizeof(tmpl), "%s/neobae_xmf_dls_XXXXXX.dls", tmpdir);
        fd = mkstemps(tmpl, 4);
#else
        strcpy(tmpl, "/tmp/neobae_xmf_dls_XXXXXX.dls");
        fd = mkstemps(tmpl, 4);
#endif

        if (fd < 0) {
            BAE_PRINTF("[XMF] Failed to create temporary file for XMF DLS overlay (%s)\n", tmpl);
            return GENERAL_BAD;
        }
        ssize_t written = 0;
        while ((size_t)written < size) {
            ssize_t w = write(fd, data + written, size - (size_t)written);
            if (w <= 0) { close(fd); unlink(tmpl); BAE_PRINTF("[XMF] Failed to write to temp XMF DLS file\n"); return GENERAL_BAD; }
            written += w;
        }
#if _WIN32
        HANDLE h = (HANDLE)_get_osfhandle(fd);
        FlushFileBuffers(h);
#else        
        fsync(fd);
#endif
        close(fd);
        
        // Suppress the expected FluidSynth error log for DLS
        g_suppress_not_sf2_error = TRUE;
        fluid_log_function_t prev_err = fluid_set_log_function(FLUID_ERR, pv_fluidsynth_log_filter, NULL);
        
        // Load the DLS as XMF overlay (does NOT reset presets - keeps existing instruments)
        g_fluidsynth_xmf_overlay_id = fluid_synth_sfload(g_fluidsynth_synth, tmpl, TRUE);
        
        // Restore previous logging behavior
        fluid_set_log_function(FLUID_ERR, prev_err, NULL);
        g_suppress_not_sf2_error = FALSE;
        
        if (g_fluidsynth_xmf_overlay_id == FLUID_FAILED) {
            BAE_PRINTF("[XMF] Failed to load XMF DLS overlay from temp file\n");
            unlink(tmpl);
            return GENERAL_BAD;
        }
        
        strncpy(g_temp_xmf_overlay_path, tmpl, sizeof(g_temp_xmf_overlay_path)-1);
        g_temp_xmf_overlay_path[sizeof(g_temp_xmf_overlay_path)-1] = '\0';
        g_temp_xmf_overlay_is_tempfile = TRUE;
        fluid_sfont_t* sf = fluid_synth_get_sfont_by_id(g_fluidsynth_synth, g_fluidsynth_xmf_overlay_id);
        fluid_sfont_iteration_start(sf);

        // Check if any presets exist in bank 0, if so we'll apply an offset
        g_hasBank121Presets = FALSE;
        XBOOL hasBank0Presets = FALSE;
        fluid_preset_t* preset;
        while ((preset = fluid_sfont_iteration_next(sf))) {
            int bankNum = fluid_preset_get_banknum(preset);
            if (bankNum == 0) {
                hasBank0Presets = TRUE;
            } else if (bankNum == 121) {
                g_hasBank121Presets = TRUE;
            }
        }

        // Apply bank offset if bank 0 presets exist (offset to bank 2 in HSB mode)
        g_fluidsynth_xmf_overlay_bank_offset = hasBank0Presets ? 2 : 0;
        // Bank 121 presets stay at bank 121 - we'll alias bank 0 requests to check bank 121 first
#if _DEBUG
        if (g_fluidsynth_xmf_overlay_bank_offset > 0) {
            BAE_PRINTF("[XMF] XMF DLS overlay has bank 0 presets, will apply bank offset +%d\n", 
                       g_fluidsynth_xmf_overlay_bank_offset);
        } else {
            // Debug: Dump all presets in the loaded soundfont
            if (g_fluidsynth_synth && g_fluidsynth_xmf_overlay_id >= 0) {
                fluid_sfont_t* sf = fluid_synth_get_sfont_by_id(g_fluidsynth_synth, g_fluidsynth_xmf_overlay_id);
                 if (sf) {
                    fluid_preset_t* p = NULL;
                    fluid_sfont_iteration_start(sf);
                    while ((p = fluid_sfont_iteration_next(sf)) != NULL) {
                        int bank = fluid_preset_get_banknum(p);
                        int prog = fluid_preset_get_num(p);
                        const char* name = fluid_preset_get_name(p);
                        BAE_PRINTF("[XMF]  Bank %d, Program %d: %s\n", bank, prog, name ? name : "(null)");
                    }
                } else {
                    BAE_PRINTF("[XMF] Could not get sfont for sfid=%d\n", g_fluidsynth_xmf_overlay_id);
                }
            }
        }

        BAE_PRINTF("[XMF] XMF DLS overlay loaded successfully (id=%d)\n", g_fluidsynth_xmf_overlay_id);
#endif
        return NO_ERR;
    }

    // SF2 path: load from memory
    // Prepare global memory buffer for callbacks
    g_mem_sf_data = data;
    g_mem_sf_size = size;

    // Ensure we have a defsfloader with our callbacks installed once
    if (!g_mem_sf_loader) {
        g_mem_sf_loader = new_fluid_defsfloader(g_fluidsynth_settings);
        if (!g_mem_sf_loader) {
            g_mem_sf_data = NULL; g_mem_sf_size = 0;
            return MEMORY_ERR;
        }
        fluid_sfloader_set_callbacks(
            g_mem_sf_loader,
            fs_mem_open,
            fs_mem_read,
            fs_mem_seek,
            fs_mem_tell,
            fs_mem_close
        );
        fluid_synth_add_sfloader(g_fluidsynth_synth, g_mem_sf_loader);
        BAE_PRINTF("[XMF] defsfloader registered\n");
    }

    // Load as XMF overlay (FALSE = do not reset presets, allows overlay behavior)
    int sfid = fluid_synth_sfload(g_fluidsynth_synth, "__xmf_overlay__", FALSE);
    g_mem_sf_data = NULL; g_mem_sf_size = 0;
    
    if (sfid == FLUID_FAILED) {
        return GENERAL_BAD;
    }

    g_fluidsynth_xmf_overlay_id = sfid;
    
    // Check if any presets exist in bank 0 in the SF2 overlay
    fluid_sfont_t* sf = fluid_synth_get_sfont_by_id(g_fluidsynth_synth, g_fluidsynth_xmf_overlay_id);
    XBOOL hasBank0Presets = FALSE;
    if (sf) {
        fluid_preset_t* preset;
        fluid_sfont_iteration_start(sf);
        while ((preset = fluid_sfont_iteration_next(sf))) {
            if (fluid_preset_get_banknum(preset) == 0) {
                hasBank0Presets = TRUE;
                break;
            }
        }
    }
    
    // Apply bank offset if bank 0 presets exist (offset to bank 2 in HSB mode)
    g_fluidsynth_xmf_overlay_bank_offset = hasBank0Presets ? 2 : 0;
    if (g_fluidsynth_xmf_overlay_bank_offset > 0) {
        BAE_PRINTF("[XMF] XMF SF2 overlay has bank 0 presets, will apply bank offset +%d\n", 
                   g_fluidsynth_xmf_overlay_bank_offset);
    }
    
#if _DEBUG
    // Debug: Show all loaded soundfonts and their order
    int sfcount = fluid_synth_sfcount(g_fluidsynth_synth);
    BAE_PRINTF("[XMF] XMF SF2 overlay loaded successfully (id=%d), total soundfonts loaded: %d\n", 
               g_fluidsynth_xmf_overlay_id, sfcount);
    for (int i = 0; i < sfcount; i++) {
        fluid_sfont_t* sf = fluid_synth_get_sfont(g_fluidsynth_synth, i);
        if (sf) {
            const char* name = fluid_sfont_get_name(sf);
            int id = fluid_sfont_get_id(sf);
            BAE_PRINTF("[XMF]   Soundfont #%d: id=%d name='%s'\n", i, id, name ? name : "(null)");
        }
    }
#endif

    // Don't reset channel programs - let them keep their current settings
    // FluidSynth will automatically search the overlay first, then fall back to base soundfont
    return NO_ERR;
}
#endif
void GM_UnloadXMFOverlaySoundFont(void)
{
#if USE_XMF_SUPPORT == TRUE
    if (g_fluidsynth_synth && g_fluidsynth_xmf_overlay_id >= 0)
    {
        BAE_PRINTF("[XMF] Unloading XMF overlay soundfont (id=%d)\n", g_fluidsynth_xmf_overlay_id);
        
        // Kill all notes before unloading
        GM_ResetSF2();
        
        while (GM_SF2_GetActiveVoiceCount() > 0)
        {
            // Wait for voices to finish
            fluid_synth_process(g_fluidsynth_synth, SAMPLE_BLOCK_SIZE, 0, 0, 0, NULL);
        }
        
        // Unload the XMF overlay soundfont (TRUE = reset presets to fall back to base soundfont)
        fluid_synth_sfunload(g_fluidsynth_synth, g_fluidsynth_xmf_overlay_id, TRUE);
        g_fluidsynth_xmf_overlay_id = -1;
        g_fluidsynth_xmf_overlay_bank_offset = 0;
        g_hasBank121Presets = FALSE;
    }
    
    // Clean up temp file if it exists
    if (g_temp_xmf_overlay_is_tempfile) {
        unlink(g_temp_xmf_overlay_path);
        g_temp_xmf_overlay_path[0] = '\0';
        g_temp_xmf_overlay_is_tempfile = FALSE;
    }
#endif
}

void GM_UnloadSF2Soundfont(void)
{
    // First unload any XMF overlay
    GM_UnloadXMFOverlaySoundFont();
    
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
        g_fluidsynth_base_soundfont_id = -1;
        
        // Clear the unloading flag
        g_fluidsynth_unloading = FALSE;
    }
    g_fluidsynth_sf2_path[0] = '\0';
    if (g_temp_sf_is_tempfile) {
        unlink(g_temp_sf_path);
        g_temp_sf_path[0] = '\0';
        g_temp_sf_is_tempfile = FALSE;
    }
    GM_ResetSF2();
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
            sf2Info->channelReverb[i] = 40;  // Default reverb level
            sf2Info->channelChorus[i] = 0;   // Default chorus level (off)
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
    if ((!GM_IsSF2Song(pSong) && !GM_SF2_HasXmfEmbeddedBank()) || !g_fluidsynth_synth)
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
    fluid_preset_t* preset = fluid_synth_get_channel_preset(g_fluidsynth_synth, channel);
    if (!preset) {
        BAE_PRINTF("[SF2 NoteOn] Channel %d has NO PRESET selected!\n", channel);
    }

    fluid_synth_noteon(g_fluidsynth_synth, channel, note, scaledVelocity);
    
    // Update channel activity tracking with original velocity
    PV_SF2_UpdateChannelActivity(channel, scaledVelocity, TRUE);
}

void GM_SF2_ProcessNoteOff(GM_Song* pSong, int16_t channel, int16_t note, int16_t velocity)
{
    if ((!GM_IsSF2Song(pSong) && !GM_SF2_HasXmfEmbeddedBank()) || !g_fluidsynth_synth)
    {
        return;
    }
    
    fluid_synth_noteoff(g_fluidsynth_synth, channel, note);
    
    // Update channel activity tracking
    PV_SF2_UpdateChannelActivity(channel, 0, FALSE);
}

XBOOL GM_SF2_HasXmfEmbeddedBank()
{
    return (g_fluidsynth_xmf_overlay_id >= 0) ? TRUE : FALSE;
}

XBOOL GM_SF2_XmfOverlayHasPreset(int bank, int program)
{
    if (g_fluidsynth_xmf_overlay_id < 0 || !g_fluidsynth_synth)
        return FALSE;
    
    // Apply bank offset: if overlay has bank 0 presets, they're accessed as bank 2 in HSB mode
    int adjustedBank = bank - g_fluidsynth_xmf_overlay_bank_offset;
    if (adjustedBank < 0) return FALSE;  // Invalid after offset
    
    // Alias bank 0 → bank 121 if overlay has bank 121 presets
    if (g_hasBank121Presets && bank == 0) {
        if (PV_SF2_PresetExistsInSoundFont(g_fluidsynth_xmf_overlay_id, 121, program))
            return TRUE;
    }
    
    return PV_SF2_PresetExistsInSoundFont(g_fluidsynth_xmf_overlay_id, adjustedBank, program);
}

// Direct bank/program change without conversion logic (for HSB overlay routing)
void GM_SF2_SetChannelBankAndProgram(int16_t channel, int16_t bank, int16_t program)
{
    if (!g_fluidsynth_synth)
        return;
    
    // Apply bank offset: if overlay has bank 0 presets, they're accessed via offset bank in HSB mode
    int adjustedBank = bank - g_fluidsynth_xmf_overlay_bank_offset;
    
    // Alias bank 0 → bank 121 if overlay has bank 121 presets
    if (g_fluidsynth_xmf_overlay_id >= 0 && g_hasBank121Presets && bank == 0) {
        if (PV_SF2_PresetExistsInSoundFont(g_fluidsynth_xmf_overlay_id, 121, program)) {
            adjustedBank = 121;
            BAE_PRINTF("[SF2 Direct] Aliasing bank 0 → bank 121 for channel %d program %d\n", channel, program);
        }
    }
    
    BAE_PRINTF("[SF2 Direct] Setting channel %d to bank %d (adjusted: %d) program %d\n", 
               channel, bank, adjustedBank, program);
    
    // If we have an XMF overlay, explicitly select from it to avoid conflicts with base soundfont
    if (g_fluidsynth_xmf_overlay_id >= 0) {
        fluid_synth_program_select(g_fluidsynth_synth, channel, g_fluidsynth_xmf_overlay_id, adjustedBank, program);
        BAE_PRINTF("[SF2 Direct] Using program_select with XMF overlay (sfid=%d)\n", g_fluidsynth_xmf_overlay_id);
    } else {
        // No overlay, use standard bank select + program change
        fluid_synth_bank_select(g_fluidsynth_synth, channel, adjustedBank);
        fluid_synth_program_change(g_fluidsynth_synth, channel, program);
    }
}

void GM_SF2_ProcessProgramChange(GM_Song* pSong, int16_t channel, int32_t program)
{
    if ((!GM_IsSF2Song(pSong) && !GM_SF2_HasXmfEmbeddedBank()) || !g_fluidsynth_synth)
    {
        return;
    }
    BAE_PRINTF("[SF2 ProcessProgramChange] Raw Request: program: %i, channel %i\n", program, channel);    
    // Convert program ID to MIDI bank/program
    // NeoBAE uses: instrument = (bank * 128) + program + note
    // For percussion: bank = (bank * 2) + 1, note is included
    // For melodic: bank = bank * 2, note = 0
    int32_t midiBank = (int32_t)(program / 128);    // Bank number (internal mapping)
    int16_t midiProgram = (uint16_t)(program % 128); // Program number or note depending on mapping

    // Determine percussion intent from two signals:
    // 1) Internal odd-bank mapping (legacy NeoBAE percussion mapping)
    // 2) Direct MIDI bank MSB 128 (SF2 percussion bank convention)
    XBOOL isOddBankPerc = ((midiBank % 2) == 1);
    XBOOL isMSB128Perc = FALSE;

    if (!isOddBankPerc)
    {
        // If not odd mapping, treat direct bank 128 as percussion
        // Convert back to MIDI bank first to test the external value
        uint16_t extBank = midiBank / 2; // internal even bank encodes extBank*2
        if (extBank == 128 || (g_fluidsynth_soundfont_is_dls && extBank == 120))
            isMSB128Perc = TRUE;
    }

    if (isOddBankPerc)
    {
        if (pSong->songFlags == SONG_FLAG_IS_RMF) {
            // Odd banks are percussion in NeoBAE mapping
            midiBank = (midiBank - 1) / 2;     // Convert back to external MIDI bank
            // Route to SF2 percussion bank
            midiProgram = 0; // Standard drum kit preset
        }
        if (g_fluidsynth_soundfont_is_dls) {
            midiBank = 120;  // DLS percussion bank
        } else {
            midiBank = 128;  // SF2 percussion bank
        }
    }
    else if (isMSB128Perc)
    {
        // Treat explicit MIDI bank 128 as percussion
        // Keep requested kit program if provided; use note from low 7 bits if present
        uint16_t extProgram = midiProgram; // may indicate kit variant
        if (g_fluidsynth_soundfont_is_dls) {
            midiBank = 120;  // DLS percussion bank
        } else {
            midiBank = 128;  // SF2 percussion bank
        }
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
        if (g_fluidsynth_soundfont_is_dls) {
            midiBank = 120;  // DLS percussion bank
        } else {
            midiBank = 128;  // SF2 percussion bank
        }
    }

    if (pSong->channelBankMode[channel] == USE_GM_PERC_BANK) {
        if (midiProgram == 0 && midiBank == 0) {
            if (g_fluidsynth_soundfont_is_dls) {
                midiBank = 120;  // DLS percussion bank
            } else {
                midiBank = 128;  // SF2 percussion bank
            }
        } else {
            // change back to normal channel if the program is not a percussion program
            pSong->channelBankMode[channel] = USE_GM_DEFAULT;
            midiBank = midiBank / 2;
        }

    }

    BAE_PRINTF("[SF2 ProcessProgramChange] Final Interpretation: midiBank: %i, midiProgram: %i, channel: %i\n", midiBank, midiProgram, channel);

    // mobileBAE MIDI quirk: bank 121 program 124:125 are used for motor vibration.
    // Best behavior is to give the channel no preset at all.
    if (midiBank == 121 && (midiProgram == 124 || midiProgram == 125))
    {
        BAE_PRINTF("[SF2 ProcessProgramChange] Denying preset request %i:%i on channel %d (unsetting program)\n", midiBank, midiProgram, channel);
        fluid_synth_all_sounds_off(g_fluidsynth_synth, channel);
        fluid_synth_all_notes_off(g_fluidsynth_synth, channel);
        fluid_synth_unset_program(g_fluidsynth_synth, channel);
        return;
    }

    // Validate bank/program exist in current font; apply fallback if not
    int useBank = midiBank;
    int useProg = midiProgram;
    
    // First priority: Check if preset exists in XMF overlay (if loaded)
    // Apply bank offset: if overlay has bank 0 presets, HSB requests them as bank 2
    if (g_fluidsynth_xmf_overlay_id >= 0) {
        int overlayBank = useBank - g_fluidsynth_xmf_overlay_bank_offset;
        
        // Alias bank 0 → bank 121 if overlay has bank 121 presets
        if (g_hasBank121Presets && useBank == 0 && PV_SF2_PresetExistsInSoundFont(g_fluidsynth_xmf_overlay_id, 121, useProg)) {
            overlayBank = 121;
            BAE_PRINTF("[SF2 ProcessProgramChange] Aliasing bank 0 → bank 121 for overlay preset\n");
        }
        
        if (overlayBank >= 0 && PV_SF2_PresetExistsInSoundFont(g_fluidsynth_xmf_overlay_id, overlayBank, useProg)) {
            BAE_PRINTF("[SF2 ProcessProgramChange] Using XMF overlay preset: requested bank %d -> overlay bank %d prog %d on channel %d\n", 
                       useBank, overlayBank, useProg, channel);
            // Use program_select to explicitly select from the XMF overlay soundfont
            // This avoids ambiguity if the base soundfont also has the same bank/program
            fluid_synth_program_select(g_fluidsynth_synth, channel, g_fluidsynth_xmf_overlay_id, overlayBank, useProg);
            BAE_PRINTF("[SF2 ProcessProgramChange] Called fluid_synth_program_select(sfid=%d, bank=%d, prog=%d)\n", 
                       g_fluidsynth_xmf_overlay_id, overlayBank, useProg);            
            return;
        } else {
            BAE_PRINTF("[SF2 ProcessProgramChange] XMF overlay check: requested bank %d -> overlay bank %d (offset=%d) prog %d - not found or invalid\n",
                       useBank, overlayBank, g_fluidsynth_xmf_overlay_bank_offset, useProg);
        }
    }

    // Alias bank 121 -> bank 0 if bank 121 preset doesn't exist but bank 0 does
    // This handles MIDI files that request bank 121 but the soundfont only has bank 0
    if (useBank == 121 && !PV_SF2_PresetExists(121, useProg) && PV_SF2_PresetExists(0, useProg)) {
        BAE_PRINTF("[SF2 ProcessProgramChange] Aliasing bank 121 prog %d -> bank 0 prog %d (121:%d not found)\n", useProg, useProg, useProg);
        useBank = 0;
    }

    if (!PV_SF2_PresetExists(useBank, useProg)) {
        XBOOL percIntent = (channel == BAE_PERCUSSION_CHANNEL) || (useBank == 128 || (g_fluidsynth_soundfont_is_dls && useBank == 120));
        XBOOL found = FALSE;

        // 1. Try fallback to Bank 0 (Capital Tone) with same program
        // This is the standard GM fallback: if variation is missing, use standard instrument
        if (!percIntent && useBank != 0) {
            if (PV_SF2_PresetExists(0, useProg)) {
                BAE_PRINTF("[SF2 ProcessProgramChange] Fallback: bank %d prog %d not found; using bank 0 prog %d\n", useBank, useProg, useProg);
                useBank = 0;
                found = TRUE;
            }
        }


        // 2. If still not found, try bank 0 (or 128) default
        if (!found) {
            int fbBank = -1, fbProg = 0;
            if (g_fluidsynth_soundfont_is_dls) {
                // DLS special case: try bank 121/120 first
                fbProg = useProg;
                if (!percIntent && PV_SF2_FindFirstPresetInBank(121, &fbProg)) {
                    fbBank = 121;                    
                    if (PV_SF2_PresetExists(fbBank, useProg)) {
                        fbProg = useProg;
                    }
                } else if (percIntent && PV_SF2_FindFirstPresetInBank(120, &fbProg)) {
                    fbBank = 120;
                    if (PV_SF2_PresetExists(fbBank, useProg)) {
                        fbProg = useProg;
                    }
                }
            } else if (percIntent && PV_SF2_FindFirstPresetInBank(128, &fbProg)) {
                fbBank = 128;
            } else if (!percIntent && PV_SF2_FindFirstPresetInBank(0, &fbProg)) {
                fbBank = 0;
            } else if (PV_SF2_FindAnyPreset(&fbBank, &fbProg)) {
                // leave as found
            }
            if (fbBank >= 0) {
                BAE_PRINTF("[SF2 ProcessProgramChange] Fallback: no preset for bank %d:%d; selecting %d:%d\n", useBank, useProg, fbBank, fbProg);
                useBank = fbBank; useProg = fbProg;
            }
        }
    }

    pSong->channelRawBank[channel] = useBank;

    // If this soundfont has no canonical drum kit preset, don't load any bank for channel 10.
    // (Avoid incorrectly falling back to melodic bank 0 on the percussion channel.)
    if (channel == BAE_PERCUSSION_CHANNEL)
    {
        int drumBank = 128;
        if (!PV_SF2_PresetExists(drumBank, 0))
        {
            // try DLS percussion bank
            drumBank = 120;
            if (!PV_SF2_PresetExists(drumBank, 0)) {        
                BAE_PRINTF("[SF2 ProcessProgramChange] No drum kit preset 128:0 or 120:0 found; unsetting program on percussion channel %d\n", channel);
                fluid_synth_all_sounds_off(g_fluidsynth_synth, channel);
                fluid_synth_all_notes_off(g_fluidsynth_synth, channel);
                fluid_synth_unset_program(g_fluidsynth_synth, channel);
                return;
            }
        }
    }

    // Send MIDI program change event to FluidSynth
    fluid_synth_bank_select(g_fluidsynth_synth, channel, useBank);
    fluid_synth_program_change(g_fluidsynth_synth, channel, useProg);
    
}

void GM_SF2_ProcessController(GM_Song* pSong, int16_t channel, int16_t controller, int16_t value)
{
    if ((!GM_IsSF2Song(pSong) && !GM_SF2_HasXmfEmbeddedBank()) || !g_fluidsynth_synth)
    {
        return;
    }
    
    if (pSong->AnalyzeMode != SCAN_NORMAL) {
        return;
    }
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
    if ((!GM_IsSF2Song(pSong) && !GM_SF2_HasXmfEmbeddedBank()) || !g_fluidsynth_synth)
    {
        return;
    }

    // Only apply pitch bend during normal playback to avoid scan/preroll phases
    // leaving channels in a bent state.
    if (pSong->AnalyzeMode != SCAN_NORMAL)
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

void GM_SF2_ProcessSysEx(GM_Song* pSong, const unsigned char* message, int32_t length)
{
    if ((!GM_IsSF2Song(pSong) && !GM_SF2_HasXmfEmbeddedBank()) || !g_fluidsynth_synth)
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

    fluid_synth_sysex(g_fluidsynth_synth,
                      (const char*)message,
                      (int)length,
                      response,
                      &response_len,
                      &handled,
                      0);
}

// FluidSynth audio rendering - this gets called during mixer slice processing
void GM_SF2_RenderAudioSlice(GM_Song* pSong, int32_t* mixBuffer, int32_t* reverbBuffer, int32_t* chorusBuffer, int32_t frameCount)
{
    // Render if either SF2 mode is active OR there's an XMF overlay (for HSB mode with overlay channels)
    if ((!GM_IsSF2Song(pSong) && !GM_SF2_HasXmfEmbeddedBank()) || !g_fluidsynth_synth || !mixBuffer || frameCount <= 0)
    {
        return;
    }
    
    // Additional safety check during synth recreation - but allow XMF overlay without base font
    if (!g_fluidsynth_initialized || (g_fluidsynth_soundfont_id < 0 && g_fluidsynth_xmf_overlay_id < 0))
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
    
    // Get reverb and chorus levels (default to 0 if no info)
    uint8_t reverbLevels[BAE_MAX_MIDI_CHANNELS];
    uint8_t chorusLevels[BAE_MAX_MIDI_CHANNELS];
    for (int c = 0; c < BAE_MAX_MIDI_CHANNELS; c++)
    {
        reverbLevels[c] = info ? info->channelReverb[c] : 0;
        chorusLevels[c] = info ? info->channelChorus[c] : 0;
    }
    
    // Convert float to int32 and mix with existing buffer (including reverb/chorus sends)
    PV_SF2_ConvertFloatToInt32(g_fluidsynth_mix_buffer, mixBuffer, reverbBuffer, chorusBuffer,
                               frameCount, songScale, channelScales, reverbLevels, chorusLevels);
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
    // Allow checking for XMF overlay even if base soundfont isn't loaded (HSB mode)
    if (!g_fluidsynth_synth || (g_fluidsynth_soundfont_id < 0 && g_fluidsynth_xmf_overlay_id < 0)) {
        if (outPresetCount) *outPresetCount = 0;
        return FALSE;
    }
    int count = 0;
    
    // Search through ALL loaded soundfonts (overlay + base)
    int sfcount = fluid_synth_sfcount(g_fluidsynth_synth);
    for (int i = 0; i < sfcount; i++) {
        fluid_sfont_t* sf = fluid_synth_get_sfont(g_fluidsynth_synth, i);
        if (sf) {
            fluid_preset_t* p = NULL;
            fluid_sfont_iteration_start(sf);
            while ((p = fluid_sfont_iteration_next(sf)) != NULL) {
                count++;
                if (count > 0) break; // early out once we know it's non-empty
            }
            if (count > 0) break; // found presets, no need to check other soundfonts
        }
    }
    
    if (outPresetCount) *outPresetCount = count;
    return (count > 0) ? TRUE : FALSE;
}

void PV_SF2_SetBankPreset(GM_Song* pSong, int16_t channel, int16_t bank, int16_t preset) 
{
    if ((!GM_IsSF2Song(pSong) && !GM_SF2_HasXmfEmbeddedBank()) || !g_fluidsynth_synth)
    {
        return;
    }
    
    fluid_synth_bank_select(g_fluidsynth_synth, channel, bank);
    fluid_synth_program_change(g_fluidsynth_synth, channel, preset);
}

void GM_SF2_AllNotesOffChannel(GM_Song* pSong, int16_t channel)
{
    if ((!GM_IsSF2Song(pSong) && !GM_SF2_HasXmfEmbeddedBank()) || !g_fluidsynth_synth)
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
    if ((!GM_IsSF2Song(pSong) && !GM_SF2_HasXmfEmbeddedBank()) || !g_fluidsynth_synth)
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

static void PV_SF2_ConvertFloatToInt32(float* input, int32_t* output, int32_t* reverbOutput, int32_t* chorusOutput, 
                                        int32_t frameCount, float songVolumeScale, const float *channelScales,
                                        const uint8_t *reverbLevels, const uint8_t *chorusLevels)
{
    const float kScale = 2147483647.0f;
    
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
        reverbScale = (weightedReverb / totalWeight) / 128.0f * 1.0f;
        chorusScale = (weightedChorus / totalWeight) / 128.0f * 1.0f;
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
            
            // Clamp to prevent overflow
            if (mono > 1.0f) mono = 1.0f;
            else if (mono < -1.0f) mono = -1.0f;
            
            // Convert to 32-bit fixed point
            int32_t intSample = (int32_t)(mono * kScale);
            
            // Write single-channel PCM (one sample per frame, true mono layout)
            output[frame] += intSample;
            
            // Mix into reverb and chorus buffers if they exist
            // Use the scaled sample directly (reverb is a percentage of the dry signal)
            if (reverbOutput && reverbScale > 0.0f)
            {
                int32_t reverbSample = (int32_t)(intSample * reverbScale);
                reverbOutput[frame] += reverbSample;
            }
            if (chorusOutput && chorusScale > 0.0f)
            {
                int32_t chorusSample = (int32_t)(intSample * chorusScale);
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
            
            // Clamp to prevent overflow
            if (leftSample > 1.0f) leftSample = 1.0f;
            else if (leftSample < -1.0f) leftSample = -1.0f;
            if (rightSample > 1.0f) rightSample = 1.0f;
            else if (rightSample < -1.0f) rightSample = -1.0f;
            
            // Convert to 32-bit fixed point and add to existing buffer
            int32_t leftInt = (int32_t)(leftSample * kScale);
            int32_t rightInt = (int32_t)(rightSample * kScale);
            output[frame * 2] += leftInt;     // Left
            output[frame * 2 + 1] += rightInt; // Right
            
            // Mix into reverb and chorus buffers if they exist (stereo interleaved)
            // Use the scaled samples directly (reverb/chorus are percentages of the dry signal)
            if (reverbOutput && reverbScale > 0.0f)
            {
                int32_t monoSend = (leftInt / 2) + (rightInt / 2);
                reverbOutput[frame] += (int32_t)(monoSend * reverbScale);
            }
            if (chorusOutput && chorusScale > 0.0f)
            {
                int32_t monoSend = (leftInt / 2) + (rightInt / 2);
                chorusOutput[frame] += (int32_t)(monoSend * chorusScale);
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

    for (int i = 0; i < MAX_CHANNELS; i++) {
        GM_SF2_SetDefaultControllers(i);
    }

    // If no font loaded, nothing else to do
    if (g_fluidsynth_soundfont_id < 0)
        return;

    // Try to find a default melodic preset and a drum kit preset
    // We prefer: melodic -> bank 0, drums -> bank 128:0 (SF2) or 120:0 (DLS).
    // If the canonical drum kit preset doesn't exist, do NOT fall back to any other bank on the percussion channel.
    int foundMelodicBank = -1, foundMelodicProg = 0;
    int foundDrumBank = -1, foundDrumProg = 0; // look for bank 128 if available
    int firstBank = -1, firstProg = 0;         // fallback to the very first preset seen

    const int preferredDrumBank = g_fluidsynth_soundfont_is_dls ? 120 : 128;
    
    // Search through ALL loaded soundfonts (overlay + base)
    int sfcount = fluid_synth_sfcount(g_fluidsynth_synth);
    for (int i = 0; i < sfcount; i++) {
        fluid_sfont_t* sf = fluid_synth_get_sfont(g_fluidsynth_synth, i);
        if (sf) {
            fluid_preset_t* p = NULL;
            fluid_sfont_iteration_start(sf);
            while ((p = fluid_sfont_iteration_next(sf)) != NULL) {
                int bank = fluid_preset_get_banknum(p);
                int prog = fluid_preset_get_num(p);
                if (firstBank < 0) { firstBank = bank; firstProg = prog; }
                if (bank == 0 && foundMelodicBank < 0) { // capture first bank 0 as a generic melodic default
                    foundMelodicBank = bank; foundMelodicProg = prog;
                }
            }
        }
    }

    // Only accept the canonical drum kit preset.
    if (PV_SF2_PresetExists(preferredDrumBank, 0))
    {
        foundDrumBank = preferredDrumBank;
        foundDrumProg = 0;
    }

    // Fallbacks if preferred banks not found
    if (foundMelodicBank < 0 && firstBank >= 0) { foundMelodicBank = firstBank; foundMelodicProg = firstProg; }

    BAE_PRINTF("[FluidMem] Default presets: melodic bank=%d prog=%d, drums bank=%d prog=%d (first=%d:%d)\n",
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

XBOOL GM_SF2_isDLS(void)
{
    return g_fluidsynth_soundfont_is_dls;
}   

#endif // USE_SF2_SUPPORT && defined(_USING_FLUIDSYNTH)
