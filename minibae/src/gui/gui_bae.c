// gui_bae.c - BAE (Audio Engine) subsystem management

#include "gui_bae.h"
#include "bankinfo.h"
#include "gui_widgets.h"
#include "gui_settings.h"
#include "gui_midi.h"    // For gui_midi_event_callback and midi output functions
#include "gui_karaoke.h" // For karaoke functions
#include "X_API.h"
#if USE_SF2_SUPPORT
#include "GenTSF.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

/* Single, unconditional definition of the remembered user master volume
    intent. Declared extern in headers and other modules so all translation
    units reference this one symbol regardless of build flags. */
double g_last_requested_master_volume = 1.0; /* 0.0..1.0 per UI */

/* Remember last applied per-sound engine gain (in 0..1 engine space)
    so BAESound_Start can use the correct initial volume instead of
    hardcoding 1.0 which would override BAESound_SetVolume done earlier. */
double g_last_applied_sound_volume = 1.0;

// Volume mapping configuration moved to gui_bae.h

// Global state variables (from main)
BAESong g_live_song = NULL;
float g_channel_vu[16] = {0.0f};
float g_channel_peak_level[16] = {0.0f};
uint32_t g_channel_peak_hold_until[16] = {0};
uint32_t g_channel_peak_hold_ms = 600; // how long to hold peak in ms

// Bank info
BankEntry banks[32]; // Static array for simplicity
int bank_count = 0;

// External keyboard-related globals (defined in gui_midi_vkbd.c)
extern bool g_show_virtual_keyboard;
extern int g_keyboard_channel;
extern int g_keyboard_mouse_note;
extern uint32_t g_keyboard_suppress_until;
extern bool g_show_rmf_info_dialog;

// Audio position tracking for audio files
uint32_t audio_current_position = 0;
uint32_t audio_total_frames = 0;

// Export state
extern bool g_exporting;
extern bool g_export_realtime_mode;
extern BAEFileType g_export_file_type;
extern int g_export_progress;
extern uint32_t g_export_last_pos;
extern int g_export_stall_iters;
extern char g_export_path[512];
extern bool g_pcm_wav_recording;

// External functions from other modules
// Status message implementation
static char g_status_message[512] = {0};
static uint32_t g_status_message_time = 0;

void set_status_message(const char *msg)
{
    strncpy(g_status_message, msg, sizeof(g_status_message) - 1);
    g_status_message[sizeof(g_status_message) - 1] = '\0';
    g_status_message_time = SDL_GetTicks();

    // Also update the global g_bae status message for GUI display
    strncpy(g_bae.status_message, msg, sizeof(g_bae.status_message) - 1);
    g_bae.status_message[sizeof(g_bae.status_message) - 1] = '\0';
    g_bae.status_message_time = SDL_GetTicks();
}

// Forward declarations
bool bae_load_bank(const char *bank_path);

// Stub implementations for missing functions
void load_bankinfo()
{
    // Replaced XML parsing with embedded metadata from bankinfo.h
    bank_count = 0;
    for (int i = 0; i < kBankCount && i < 32; ++i)
    {
        const BankInfo *eb = &kBanks[i];
        BankEntry *be = &banks[bank_count];
        memset(be, 0, sizeof(*be));
        // src now unknown until user loads; retain legacy src field only for UI display when known
        strncpy(be->name, eb->name, sizeof(be->name) - 1);
        strncpy(be->sha1, eb->sha1, sizeof(be->sha1) - 1);
        bank_count++;
    }
    BAE_PRINTF("Loaded info about %d banks\n", bank_count);
}

bool load_bank(const char *path, bool current_playing_state, int transpose, int tempo, int volume, bool loop_enabled, int reverb_type, bool ch_enable[16], bool save_to_settings)
{
    if (!g_bae.mixer)
        return false;
    if (!path)
        return false;

    // Store current song info before bank change
    bool had_song = g_bae.song_loaded;
    char current_song_path[1024] = {0};
    bool was_playing = false;
    int current_position_ms = 0;
    uint32_t current_position_us = 0;

    if (had_song && g_bae.song)
    {
        strncpy(current_song_path, g_bae.loaded_path, sizeof(current_song_path) - 1);
        // Use the passed playing state
        was_playing = current_playing_state;
        current_position_ms = bae_get_pos_ms();
        uint32_t tmpUs = 0;
        BAESong_GetMicrosecondPosition(g_bae.song, &tmpUs);
        current_position_us = tmpUs; // capture precise position
        BAESong_Stop(g_bae.song, FALSE);
        BAESong_Delete(g_bae.song);
        g_bae.song = NULL;
        g_bae.song_loaded = false;
        g_bae.is_playing = false;
    }

    // Unload existing banks (single active bank paradigm like original patch switcher)
    if (g_bae.bank_loaded)
    {
        BAEMixer_UnloadBanks(g_bae.mixer);
        g_bae.bank_loaded = false;
    }
#ifdef _BUILT_IN_PATCHES
    if (strcmp(path, "__builtin__") == 0)
    {
        BAEBankToken t;
        BAEResult br = BAEMixer_LoadBuiltinBank(g_bae.mixer, &t);
        if (br == BAE_NO_ERROR)
        {
            g_bae.bank_token = t;
            const char *friendly_name = get_bank_friendly_name();
            const char *display_name;

            if (friendly_name && friendly_name[0])
            {
                display_name = friendly_name;
            }
            else
            {
                display_name = "(built-in)";
            }
            strncpy(g_bae.bank_name, display_name, sizeof(g_bae.bank_name) - 1);
            g_bae.bank_loaded = true;
            strncpy(g_current_bank_path, "__builtin__", sizeof(g_current_bank_path) - 1);
            g_current_bank_path[sizeof(g_current_bank_path) - 1] = '\0';
            BAE_PRINTF("Loaded built-in bank\n");
            set_status_message("Loaded built-in bank");

            // Update MSB/LSB values for the current channel after loading a new bank
            update_msb_lsb_for_channel();

#ifdef SUPPORT_MIDI_HW
            // If external MIDI input is enabled, recreate mixer so live MIDI
            // continues to route into the new mixer with the new bank.
            if (g_midi_input_enabled && !g_in_bank_load_recreate)
            {
                g_in_bank_load_recreate = true;
                recreate_mixer_and_restore(g_sample_rate_hz, g_stereo_output, reverb_type,
                                           transpose, tempo, volume, loop_enabled, ch_enable);
                g_in_bank_load_recreate = false;
            }
#endif
            // Save this as the last used bank only if requested
            if (save_to_settings)
            {
                save_settings("__builtin__", reverb_type, loop_enabled);
            }
        }
        else
        {
            BAE_PRINTF("Failed loading built-in bank (%d)\n", br);
            return false;
        }
    }
    else
    {
#endif
        // Use the bae_load_bank function which handles both HSB and SF2 files
        if (!bae_load_bank(path))
        {
            BAE_PRINTF("Failed to load bank: %s\n", path);
            return false;
        }

        // Use friendly name if available, otherwise use filename
        const char *friendly_name = get_bank_friendly_name();
        const char *display_name;

        if (friendly_name && friendly_name[0])
        {
            display_name = friendly_name;
        }
        else
        {
            const char *base = path;
            for (const char *p = path; *p; ++p)
            {
                if (*p == '/' || *p == '\\')
                    base = p + 1;
            }
            display_name = base;
        }

        // Store the display name (friendly name or filename) instead of full path

        strncpy(g_bae.bank_name, display_name, sizeof(g_bae.bank_name) - 1);
        g_bae.bank_name[sizeof(g_bae.bank_name) - 1] = '\0';
        g_bae.bank_loaded = true;
        strncpy(g_current_bank_path, path, sizeof(g_current_bank_path) - 1);
        g_current_bank_path[sizeof(g_current_bank_path) - 1] = '\0';
        BAE_PRINTF("Loaded bank %s\n", path);

        // Update MSB/LSB values for the current channel after loading a new bank
        update_msb_lsb_for_channel();

        // Save this as the last used bank only if requested
        if (save_to_settings)
        {
            BAE_PRINTF("About to save settings with path: %s\n", path);
            save_settings(path, reverb_type, loop_enabled);
        }

        char msg[128];
        snprintf(msg, sizeof(msg), "Loaded bank: %s", display_name);
        set_status_message(msg); // If external MIDI input is enabled, recreate the mixer so the live
                                 // MIDI routing is attached to a fresh mixer instance with the new
                                 // bank loaded. Protect with a guard to avoid infinite recursion
                                 // because recreate_mixer_and_restore itself calls load_bank.
#ifdef SUPPORT_MIDI_HW
        if (g_midi_input_enabled && !g_in_bank_load_recreate)
#else
    if (!g_in_bank_load_recreate)
#endif
        {
            g_in_bank_load_recreate = true;
            // reuse current GUI settings (sample rate & stereo output)
            recreate_mixer_and_restore(g_sample_rate_hz, g_stereo_output, reverb_type,
                                       transpose, tempo, volume, loop_enabled, ch_enable);
            g_in_bank_load_recreate = false;
        }
#ifdef _BUILT_IN_PATCHES
    }
#endif

    // Auto-reload current song if one was loaded
    if (had_song && current_song_path[0] != '\0')
    {
        BAE_PRINTF("Auto-reloading song with new bank: %s\n", current_song_path);
        set_status_message("Reloading song with new bank...");

        // Ensure we fully stop and clean up before reloading to avoid engine state conflicts
        if (g_bae.song)
        {
            g_bae.song = NULL;
            g_bae.song_loaded = false;
            g_bae.is_playing = false;
        }

        if (bae_load_song_with_settings(current_song_path, transpose, tempo, volume, loop_enabled, reverb_type, ch_enable))
        {
            // Restore playback state
            if (was_playing)
            {
                // Preserve position for next start (microseconds preferred)
                if (current_position_us == 0 && current_position_ms > 0)
                {
                    current_position_us = (uint32_t)current_position_ms * 1000UL;
                }
                g_bae.preserved_start_position_us = current_position_us;
                g_bae.preserve_position_on_next_start = (current_position_us > 0);
                BAE_PRINTF("Preserving playback position across bank reload: %u us (%d ms)\n", current_position_us, current_position_ms);
                bool playing_state = false;
                bae_play(&playing_state); // Will honor preserved position
            }
            else if (current_position_ms > 0)
            {
                bae_seek_ms(current_position_ms);
            }
            BAE_PRINTF("Song reloaded successfully with new bank\n");
            set_status_message("Song reloaded with new bank");
        }
        else
        {
            BAE_PRINTF("Failed to reload song with new bank\n");
            set_status_message("Failed to reload song with new bank");
        }
    }

    return true;
}

bool load_bank_simple(const char *path, bool save_to_settings, int reverb_type, bool loop_enabled)
{
    bool dummy_ch[16];
    for (int i = 0; i < 16; i++)
        dummy_ch[i] = true;

    // If no specific path provided, do fallback discovery
    if (!path)
    {
        BAE_PRINTF("No bank specified, trying fallback discovery\n");

        // Try traditional auto bank discovery
        const char *autoBanks[] = {
#ifdef _BUILT_IN_PATCHES
            "__builtin__",
#endif
            "patches.hsb", "npatches.hsb", NULL};
        for (int i = 0; autoBanks[i] && !g_bae.bank_loaded; ++i)
        {
            if (load_bank(autoBanks[i], false, 0, 100, 75, loop_enabled, reverb_type, dummy_ch, false))
            {
                return true;
            }
        }
        return false;
    }

    return load_bank(path, false, 0, 100, 75, loop_enabled, reverb_type, dummy_ch, save_to_settings);
}

// Platform file open dialog abstraction. Returns malloc'd string (caller frees) or NULL.
static char *open_file_dialog()
{
#ifdef _WIN32
    char fileBuf[1024] = {0};
    OPENFILENAMEA ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFilter = "Audio/MIDI/RMF\0*.mid;*.midi;*.kar;*.rmf;*.wav;*.aif;*.aiff;*.au;*.mp2;*.mp3\0MIDI Files\0*.mid;*.midi;*.kar\0RMF Files\0*.rmf\0Audio Files\0*.wav;*.aif;*.aiff;*.au;*.mp3\0All Files\0*.*\0";
    ofn.lpstrFile = fileBuf;
    ofn.nMaxFile = sizeof(fileBuf);
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    ofn.lpstrDefExt = "mid";
    if (GetOpenFileNameA(&ofn))
    {
        size_t len = strlen(fileBuf);
        char *ret = (char *)malloc(len + 1);
        if (ret)
        {
            memcpy(ret, fileBuf, len + 1);
        }
        return ret;
    }
    return NULL;
#else
    const char *cmds[] = {
        "zenity --file-selection --title='Open Audio/MIDI/RMF' --file-filter='Audio/MIDI/RMF | *.mid *.midi *.kar *.rmf *.wav *.aif *.aiff *.au *.mp2 *.mp3' 2>/dev/null",
        "kdialog --getopenfilename . '*.mid *.midi *.kar *.rmf *.wav *.aif *.aiff *.au *.mp2 *.mp3' 2>/dev/null",
        "yad --file-selection --title='Open Audio/MIDI/RMF' --file-filter='Audio/MIDI/RMF | *.mid *.midi *.kar *.rmf *.wav *.aif *.aiff *.au *.mp2 *.mp3' 2>/dev/null",
        NULL};
    for (int i = 0; cmds[i]; ++i)
    {
        FILE *p = popen(cmds[i], "r");
        if (!p)
            continue;
        char buf[1024];
        if (fgets(buf, sizeof(buf), p))
        {
            pclose(p);
            // strip newline
            size_t l = strlen(buf);
            while (l > 0 && (buf[l - 1] == '\n' || buf[l - 1] == '\r'))
                buf[--l] = '\0';
            if (l > 0)
            {
                char *ret = (char *)malloc(l + 1);
                if (ret)
                {
                    memcpy(ret, buf, l + 1);
                }
                return ret;
            }
        }
        else
        {
            pclose(p);
        }
    }
    BAE_PRINTF("No GUI file chooser available (zenity/kdialog/yad). Drag & drop still works for media and bank files.\n");
    return NULL;
#endif
}

extern void set_status_message(const char *msg);
extern void rmf_info_reset(void);
extern void karaoke_reset(void);
extern void karaoke_newline(uint32_t pos_us);
extern void karaoke_add_text(const char *text, uint32_t pos_us);
extern void midi_output_send_all_notes_off(void);
extern void gui_panic_all_notes(BAESong song);
extern void gui_panic_channel_notes(BAESong song, int channel);
extern void pcm_wav_finalize(void);
extern SDL_mutex *g_lyric_mutex;

// Audio position tracking helpers
void update_audio_position(void)
{
    if (g_bae.is_audio_file && g_bae.sound)
    {
        BAEResult result = BAESound_GetSamplePlaybackPosition(g_bae.sound, &audio_current_position);
        if (result != BAE_NO_ERROR)
        {
            audio_current_position = 0;
        }
    }
}

void get_audio_total_frames(void)
{
    if (g_bae.is_audio_file && g_bae.sound)
    {
        BAESampleInfo info;
        BAEResult result = BAESound_GetInfo(g_bae.sound, &info);
        if (result == BAE_NO_ERROR)
        {
            audio_total_frames = info.waveFrames;
        }
        else
        {
            audio_total_frames = 0;
        }
    }
}

// Karaoke callbacks - now just call the external functions
// (These are defined in gui_karaoke.c but implemented here for BAE integration)

// API Implementation

bool bae_init(int sampleRateHz, bool stereo)
{
    // Initialize BAE mixer
    g_bae.mixer = BAEMixer_New();
    if (!g_bae.mixer)
    {
        BAE_PRINTF("BAEMixer_New failed\n");
        return false;
    }

    BAEAudioModifiers modifiers = BAE_USE_16 | (stereo ? BAE_USE_STEREO : 0);
    BAEResult result = BAEMixer_Open(g_bae.mixer,
                                     sampleRateHz,
                                     E_LINEAR_INTERPOLATION,
                                     modifiers,
                                     32,    // maxMidiVoices
                                     8,     // maxSoundVoices
                                     32,    // mixLevel (must be > 0)
                                     TRUE); // engageAudio

    if (result != BAE_NO_ERROR)
    {
        BAE_PRINTF("BAEMixer_Open failed (%d)\n", result);
        BAEMixer_Delete(g_bae.mixer);
        g_bae.mixer = NULL;
        return false;
    }

    BAE_PRINTF("BAE initialized: %d Hz, %s\n",
               sampleRateHz, stereo ? "stereo" : "mono");

    return true;
}

void bae_shutdown(void)
{
    // Clean up any loaded content
    if (g_bae.song)
    {
        BAESong_Stop(g_bae.song, FALSE);
        BAESong_Delete(g_bae.song);
        g_bae.song = NULL;
    }
    if (g_bae.sound)
    {
        BAESound_Stop(g_bae.sound, FALSE);
        BAESound_Delete(g_bae.sound);
        g_bae.sound = NULL;
    }

    // Clean up live song if it exists
    if (g_live_song)
    {
        BAESong_Stop(g_live_song, FALSE);
        BAESong_Delete(g_live_song);
        g_live_song = NULL;
    }

    // Close and delete mixer
    if (g_bae.mixer)
    {
        BAEMixer_Close(g_bae.mixer);
        BAEMixer_Delete(g_bae.mixer);
        g_bae.mixer = NULL;
    }

    memset(&g_bae, 0, sizeof(g_bae));
    audio_current_position = 0;
    audio_total_frames = 0;
}

bool bae_load_bank(const char *bank_path)
{
    if (!g_bae.mixer || !bank_path)
        return false;

    const char *ext = strrchr(bank_path, '.');
#if USE_SF2_SUPPORT == TRUE
    GM_UnloadTSFSoundfont();
    // Check if this is an SF2 file
    if (ext && (strcasecmp(ext, ".sf2") == 0
#if USE_VORBIS_DECODER == TRUE
    || (strcasecmp(ext, ".sf3") == 0 || strcasecmp(ext, ".sfo") == 0)
#endif
    ))
    {
        XBOOL stoppedPlayback = FALSE;
        // Load SF2 bank
        SF2_Bank *sf2Bank = NULL;
        

        OPErr err = GM_LoadTSFSoundfont(bank_path);
        if (err != NO_ERR)
        {
            BAE_PRINTF("SF2 bank load failed: %d %s\n", err, bank_path);
            return false;
        }

        // Mark as loaded
        g_bae.bank_loaded = true;
        return true;
    }
#endif

    // Load the bank (HSB format)
    BAEResult result = BAEMixer_AddBankFromFile(g_bae.mixer, (BAEPathName)bank_path, &g_bae.bank_token);
    if (result != BAE_NO_ERROR)
    {
        BAE_PRINTF("Bank load failed: %d %s\n", result, bank_path);
        return false;
    }

    BAE_PRINTF("Bank loaded: %s (token=%p)\n", bank_path, g_bae.bank_token);
    return true;
}

// Load a bank from memory. The BAEMixer API expects a void*, but callers
// commonly provide const data blobs; cast here to satisfy the API while
// keeping the caller's const contract.
bool bae_load_bank_from_memory(const char *bankdata, int banksize)
{
    if (!g_bae.mixer || !bankdata || banksize <= 0)
        return false;

    // Load the bank (API expects void*)
    BAEResult result = BAEMixer_AddBankFromMemory(g_bae.mixer, (void *)bankdata, (uint32_t)banksize, &g_bae.bank_token);
    if (result != BAE_NO_ERROR)
    {
        BAE_PRINTF("Bank load failed: %d\n", result);
        return false;
    }

    BAE_PRINTF("Bank loaded from memory (token=%p)\n", g_bae.bank_token);
    return true;
}

bool bae_load_song(const char *path)
{
    if (!g_bae.mixer || !path)
        return false;

    // Clean previous
    if (g_bae.song)
    {
        BAESong_Stop(g_bae.song, FALSE);
        BAESong_Delete(g_bae.song);
        g_bae.song = NULL;
    }
    if (g_bae.sound)
    {
        BAESound_Stop(g_bae.sound, FALSE);
        BAESound_Delete(g_bae.sound);
        g_bae.sound = NULL;
    }
    g_bae.song_loaded = false;
    g_bae.is_audio_file = false;
    g_bae.song_finished = false;
    g_bae.is_rmf_file = false;
    g_bae.song_length_us = 0;
    g_show_rmf_info_dialog = false;
    rmf_info_reset();

    // Detect extension
    const char *le = strrchr(path, '.');
    char ext[8] = {0};
    if (le)
    {
        strncpy(ext, le, sizeof(ext) - 1);
        for (char *p = ext; *p; ++p)
            *p = (char)tolower(*p);
    }

    bool isAudio = false;
    if (le)
    {
        if (strcmp(ext, ".wav") == 0 || strcmp(ext, ".aif") == 0 || strcmp(ext, ".aiff") == 0 ||
            strcmp(ext, ".au") == 0 || strcmp(ext, ".mp2") == 0 || strcmp(ext, ".mp3") == 0 ||
            strcmp(ext, ".flac") == 0 || strcmp(ext, ".ogg") == 0 || strcmp(ext, ".oga") == 0)
        {
            isAudio = true;
        }
    }

    if (isAudio)
    {
        g_bae.sound = BAESound_New(g_bae.mixer);
        if (!g_bae.sound)
            return false;

        BAEFileType ftype = BAE_INVALID_TYPE;
        if (strcmp(ext, ".wav") == 0)
            ftype = BAE_WAVE_TYPE;
        else if (strcmp(ext, ".aif") == 0 || strcmp(ext, ".aiff") == 0)
            ftype = BAE_AIFF_TYPE;
        else if (strcmp(ext, ".au") == 0)
            ftype = BAE_AU_TYPE;
        else if (strcmp(ext, ".mp2") == 0)
            ftype = BAE_MPEG_TYPE;
        else if (strcmp(ext, ".mp3") == 0)
            ftype = BAE_MPEG_TYPE;
        else if (strcmp(ext, ".flac") == 0)
            ftype = BAE_FLAC_TYPE;
        else if (strcmp(ext, ".ogg") == 0 || strcmp(ext, ".oga") == 0)
            ftype = BAE_VORBIS_TYPE;

        BAEResult sr = (ftype != BAE_INVALID_TYPE) ? BAESound_LoadFileSample(g_bae.sound, (BAEPathName)path, ftype) : BAE_BAD_FILE_TYPE;
        if (sr != BAE_NO_ERROR)
        {
            BAESound_Delete(g_bae.sound);
            g_bae.sound = NULL;
            BAE_PRINTF("Audio load failed %d %s\n", sr, path);
            return false;
        }

        strncpy(g_bae.loaded_path, path, sizeof(g_bae.loaded_path) - 1);
        g_bae.loaded_path[sizeof(g_bae.loaded_path) - 1] = '\0';
        g_bae.song_loaded = true;
        g_bae.is_audio_file = true;
        get_audio_total_frames();
        audio_current_position = 0;
        /* Apply the user's last requested master volume consistently by
           delegating to bae_set_volume. This ensures BAESound and mixer
           master volumes use the same mapping and any per-sound boost logic
           is centralized in one place. Reconstruct a UI percent from the
           stored engine-space value so we can call the same setter.
        */
        {
            double stored = g_last_requested_master_volume; /* 0..1 engine space */
            double baseline = (NEW_BASELINE_PCT / 100.0);
            int volPct = (int)(stored / baseline * 100.0 + 0.5);
            if (volPct < 0)
                volPct = 0;
            if (volPct > NEW_MAX_VOLUME_PCT)
                volPct = NEW_MAX_VOLUME_PCT;
            /* Call the central volume setter which will adjust both the
               per-sound volume and the master mixer volume appropriately. */
            bae_set_volume(volPct);
        }

        const char *base = path;
        for (const char *p = path; *p; ++p)
        {
            if (*p == '/' || *p == '\\')
                base = p + 1;
        }
        char msg[128];
        snprintf(msg, sizeof(msg), "Loaded: %s", base);
        set_status_message(msg);
        return true;
    }

    // MIDI / RMF
    g_bae.song = BAESong_New(g_bae.mixer);
    if (!g_bae.song)
        return false;

    BAEResult r;
    if (le && (strcmp(ext, ".mid") == 0 || strcmp(ext, ".midi") == 0 || strcmp(ext, ".kar") == 0))
    {
        r = BAESong_LoadMidiFromFile(g_bae.song, (BAEPathName)path, TRUE);
        g_bae.is_rmf_file = false;
    }
    else
    {
        r = BAESong_LoadRmfFromFile(g_bae.song, (BAEPathName)path, 0, TRUE);
        g_bae.is_rmf_file = true;
    }

    if (r != BAE_NO_ERROR)
    {
        BAE_PRINTF("Song load failed %d %s\n", r, path);
        BAESong_Delete(g_bae.song);
        g_bae.song = NULL;
        return false;
    }

    // Restore Reverb after load
    Settings settings = load_settings();
    BAEMixer_SetDefaultReverb(g_bae.mixer, (BAEReverbType)settings.reverb_type);
    // Defer preroll until just before first Start so that any user settings
    // (transpose, tempo, channel mutes, reverb, loops) are applied first.
    BAESong_GetMicrosecondLength(g_bae.song, &g_bae.song_length_us);
    strncpy(g_bae.loaded_path, path, sizeof(g_bae.loaded_path) - 1);
    g_bae.loaded_path[sizeof(g_bae.loaded_path) - 1] = '\0';
    g_bae.song_loaded = true;
    g_bae.is_audio_file = false; // is_rmf_file already set

    // Update MSB/LSB values for the current channel after loading a new song
    update_msb_lsb_for_channel();

    /* Apply current user-requested master volume to the newly loaded song
       so UI volume state is respected immediately on load. Songs do not get
       the per-sound doubling applied to raw audio files. */
    {
        double stored = g_last_requested_master_volume; /* 0..1 engine space */
        if (g_bae.song)
            BAESong_SetVolume(g_bae.song, FLOAT_TO_UNSIGNED_FIXED(stored));

#ifdef SUPPORT_MIDI_HW
        if (g_bae.mixer && !g_master_muted_for_midi_out)
#else
        if (g_bae.mixer)
#endif
        {
            BAEMixer_SetMasterVolume(g_bae.mixer, FLOAT_TO_UNSIGNED_FIXED(stored));
        }
    }

#ifdef SUPPORT_KARAOKE
    // Prepare karaoke capture
    karaoke_reset();
    if (g_karaoke_enabled && g_bae.song)
    {
        // Prefer dedicated lyric callback if engine supports it
        extern BAEResult BAESong_SetLyricCallback(BAESong song, GM_SongLyricCallbackProcPtr pCallback, void *callbackReference);
        if (BAESong_SetLyricCallback(g_bae.song, gui_lyric_callback, NULL) != BAE_NO_ERROR)
        {
            // Fallback to meta event callback if lyric callback unsupported
            BAESong_SetMetaEventCallback(g_bae.song, gui_meta_event_callback, NULL);
        }
    }
#endif

#ifdef SUPPORT_MIDI_HW
    // If MIDI Output already enabled, register engine MIDI event callback so events are forwarded
    if (g_midi_output_enabled && g_bae.song)
    {
        BAESong_SetMidiEventCallback(g_bae.song, gui_midi_event_callback, NULL);
    }
#endif

    const char *base = path;
    for (const char *p = path; *p; ++p)
    {
        if (*p == '/' || *p == '\\')
            base = p + 1;
    }
    char msg[128];
    snprintf(msg, sizeof(msg), "Loaded: %s", base);
    set_status_message(msg);
    return true;
}

bool bae_load_song_with_settings(const char *path, int transpose, int tempo, int volume, bool loop_enabled, int reverb_type, bool ch_enable[16])
{
    if (!bae_load_song(path))
        return false;
    bae_apply_current_settings(transpose, tempo, volume, loop_enabled, reverb_type, ch_enable);
    return true;
}

void bae_set_volume(int volPct)
{
    // Accept expanded UI range: 0 .. NEW_MAX_VOLUME_PCT
    if (volPct < 0)
        volPct = 0;
    if (volPct > NEW_MAX_VOLUME_PCT)
        volPct = NEW_MAX_VOLUME_PCT;

    // Map UI percent to engine linear gain. Users see "100%" at volPct == 100,
    // but we treat that as NEW_BASELINE_PCT of engine unity. Therefore:
    // engineGain = (volPct / 100.0) * (NEW_BASELINE_PCT / 100.0)
    double engineGain = (double)volPct / 100.0 * (NEW_BASELINE_PCT / 100.0);

    // Keep a remembered requested master volume in the old 0..1 space
    // so other modules (and sound load) can reconstruct user intent.
    double storedVol = engineGain;
    g_last_requested_master_volume = storedVol; // remember user intent

    if (g_bae.is_audio_file && g_bae.sound)
    {
        /* For raw audio files we apply an extra per-sound multiplier so the
            UI's "100%" feels louder. Use a smooth, monotonic mapping so
            increasing the UI percent never reduces the resulting gain. */
        double soundMultiplierBase = 3.0;
        double soundMultiplier = soundMultiplierBase * (1.0 + (double)volPct / 100.0);
        double soundGain = engineGain * soundMultiplier;
        if (soundGain < 0.0)
            soundGain = 0.0;
        BAESound_SetVolume(g_bae.sound, FLOAT_TO_UNSIGNED_FIXED(soundGain));
        /* remember actual per-sound engine gain applied so BAESound_Start
            can use the same value when it starts playback */
        g_last_applied_sound_volume = soundGain;
    }
    else if (!g_bae.is_audio_file && g_bae.song)
    {
        BAESong_SetVolume(g_bae.song, FLOAT_TO_UNSIGNED_FIXED(engineGain));
    }

    /* Also apply to the lightweight live synth used for incoming MIDI so changes
       to the master volume UI affect live input immediately. */
    if (g_live_song)
    {
        BAESong_SetVolume(g_live_song, FLOAT_TO_UNSIGNED_FIXED(engineGain));
    }

    // Also adjust master volume unless globally muted for MIDI Out
#ifdef SUPPORT_MIDI_HW
    if (g_bae.mixer && !g_master_muted_for_midi_out)
#else
    if (g_bae.mixer)
#endif
    {
        BAEMixer_SetMasterVolume(g_bae.mixer, FLOAT_TO_UNSIGNED_FIXED(engineGain));
    }
}

void bae_set_tempo(int percent)
{
    if (g_bae.is_audio_file || !g_bae.song)
        return; // Only works with MIDI/RMF
    if (percent < 25)
        percent = 25;
    if (percent > 200)
        percent = 200; // clamp
    double ratio = percent / 100.0;
    BAESong_SetMasterTempo(g_bae.song, FLOAT_TO_UNSIGNED_FIXED(ratio));

    // After changing tempo, refresh cached song length and (optionally) current
    // position so callers that use bae_get_len_ms()/bae_get_pos_ms() see the
    // tempo-adjusted values immediately.
    if (g_bae.song)
    {
        uint32_t uslen = 0;
        if (BAESong_GetMicrosecondLength(g_bae.song, &uslen) == BAE_NO_ERROR)
        {
            g_bae.song_length_us = uslen;
        }

        uint32_t uspos = 0;
        if (BAESong_GetMicrosecondPosition(g_bae.song, &uspos) == BAE_NO_ERROR)
        {
            // No additional global to update here; GUI will call bae_get_pos_ms()
            // which reads from the engine. We update nothing else in this module.
            (void)uspos;
        }
    }
}

void bae_set_transpose(int semitones)
{
    if (g_bae.is_audio_file || !g_bae.song)
        return; // Only works with MIDI/RMF
    BAESong_SetTranspose(g_bae.song, semitones);
}

void bae_seek_ms(int ms)
{
    if (g_bae.is_audio_file)
    {
        if (g_bae.sound)
        {
            // Convert milliseconds to frames
            BAESampleInfo info;
            if (BAESound_GetInfo(g_bae.sound, &info) == BAE_NO_ERROR)
            {
                double sampleRate = (double)(info.sampledRate >> 16) + (double)(info.sampledRate & 0xFFFF) / 65536.0;
                if (sampleRate > 0)
                {
                    uint32_t frame_position = (uint32_t)((double)ms * sampleRate / 1000.0);
                    if (frame_position < audio_total_frames)
                    {
                        BAESound_SetSamplePlaybackPosition(g_bae.sound, frame_position);
                        audio_current_position = frame_position;
                    }
                }
            }
        }
        return;
    }

    if (!g_bae.song)
        return;

    uint32_t us = (uint32_t)ms * 1000UL;

#ifdef SUPPORT_MIDI_HW
    // Suppress MIDI output during seeking to avoid sending events prematurely
    g_midi_output_suppressed_during_seek = true;
#endif

    BAESong_SetMicrosecondPosition(g_bae.song, us);

#ifdef SUPPORT_MIDI_HW
    g_midi_output_suppressed_during_seek = false;
#endif

    // If the song had previously finished and the user is allowed to seek
    // past the end via the UI, preserve this position so Play will resume
    // from the user-selected spot. This makes seeking after a finished
    // playback behave like repositioning for the next start.
    if (g_bae.song_finished && g_bae.song_loaded && !g_bae.is_audio_file)
    {
        g_bae.preserved_start_position_us = us;
        g_bae.preserve_position_on_next_start = true;
        BAE_PRINTF("User seek while finished: preserving start position %u us\n", us);
    }

#ifdef SUPPORT_MIDI_HW
    // When seeking, ensure external MIDI devices are silenced to avoid hanging notes
    if (g_midi_output_enabled)
    {
        midi_output_send_all_notes_off();
    }
#endif

    // Reset virtual keyboard UI and release any held virtual note when seeking
    if (g_show_virtual_keyboard)
    {
        if (g_keyboard_mouse_note != -1)
        {
            BAESong target = g_bae.song ? g_bae.song : g_live_song;
            if (target)
                BAESong_NoteOff(target, (unsigned char)g_keyboard_channel, (unsigned char)g_keyboard_mouse_note, 0, 0);
            g_keyboard_mouse_note = -1;
        }
        // Clear per-channel incoming flags (keep UI array cleared too)
        memset(g_keyboard_active_notes_by_channel, 0, sizeof(g_keyboard_active_notes_by_channel));
        memset(g_keyboard_active_notes, 0, sizeof(g_keyboard_active_notes));
    }
}

int bae_get_pos_ms(void)
{
    if (g_bae.is_audio_file)
    {
        if (g_bae.sound)
        {
            update_audio_position();
            // Convert frames to milliseconds - need sample rate from BAESampleInfo
            BAESampleInfo info;
            if (BAESound_GetInfo(g_bae.sound, &info) == BAE_NO_ERROR)
            {
                double sampleRate = (double)(info.sampledRate >> 16) + (double)(info.sampledRate & 0xFFFF) / 65536.0;
                if (sampleRate > 0)
                {
                    return (int)((double)audio_current_position * 1000.0 / sampleRate);
                }
            }
        }
        return 0;
    }

    if (!g_bae.song)
        return 0;

    uint32_t us = 0;
    BAESong_GetMicrosecondPosition(g_bae.song, &us);
    return (int)(us / 1000UL);
}

int bae_get_len_ms(void)
{
    if (g_bae.is_audio_file)
    {
        if (g_bae.sound && audio_total_frames > 0)
        {
            // Convert frames to milliseconds
            BAESampleInfo info;
            if (BAESound_GetInfo(g_bae.sound, &info) == BAE_NO_ERROR)
            {
                double sampleRate = (double)(info.sampledRate >> 16) + (double)(info.sampledRate & 0xFFFF) / 65536.0;
                if (sampleRate > 0)
                {
                    return (int)((double)audio_total_frames * 1000.0 / sampleRate);
                }
            }
        }
        return 0;
    }

    if (!g_bae.song)
        return 0;

    return (int)(g_bae.song_length_us / 1000UL);
}

void bae_set_loop(bool loop)
{
    if (g_bae.is_audio_file || !g_bae.song)
        return; // Only works with MIDI/RMF

    // Set repeat counter
    BAESong_SetLoops(g_bae.song, loop ? 32767 : 0);
    g_bae.loop_enabled_gui = loop; // Cache for later use
}

void bae_set_reverb(int idx)
{
    if (g_bae.mixer)
    {
        if (idx < 0)
            idx = 0;
        if (idx >= BAE_REVERB_TYPE_COUNT)
            idx = BAE_REVERB_TYPE_COUNT - 1;
        BAEMixer_SetDefaultReverb(g_bae.mixer, (BAEReverbType)idx);
    }
}

void bae_update_channel_mutes(bool ch_enable[16])
{
    if (g_bae.is_audio_file || !g_bae.song)
        return; // Only works with MIDI/RMF

    for (int i = 0; i < 16; i++)
    {
        if (ch_enable[i])
            BAESong_UnmuteChannel(g_bae.song, (uint16_t)i);
        else
            BAESong_MuteChannel(g_bae.song, (uint16_t)i);
    }
}

void bae_apply_current_settings(int transpose, int tempo, int volume, bool loop_enabled, int reverb_type, bool ch_enable[16])
{
    if (!g_bae.song)
        return;

    bae_set_transpose(transpose);
    bae_set_tempo(tempo);
    bae_set_volume(volume);
    bae_set_loop(loop_enabled);
    bae_set_reverb(reverb_type);
    bae_update_channel_mutes(ch_enable);
}

bool bae_play(bool *playing)
{
    if (!g_bae.song_loaded)
        return false;

    if (g_bae.is_audio_file && g_bae.sound)
    {
        // Handle audio files (WAV, MP2/MP3, etc.)
        if (!*playing)
        {
            // Set loop count: 0 for no looping, 0xFFFFFFFF for infinite looping
            uint32_t loopCount = g_bae.loop_enabled_gui ? 0xFFFFFFFF : 0;
            BAESound_SetLoopCount(g_bae.sound, loopCount);

            BAE_PRINTF("Attempting BAESound_Start on '%s' (loop count: %u)\n", g_bae.loaded_path, loopCount);
            BAEResult sr = BAESound_Start(g_bae.sound, 0, FLOAT_TO_UNSIGNED_FIXED(g_last_applied_sound_volume), 0);
            if (sr != BAE_NO_ERROR)
            {
                BAE_PRINTF("BAESound_Start failed (%d) for '%s'\n", sr, g_bae.loaded_path);
                return false;
            }
            BAE_PRINTF("BAESound_Start ok for '%s'\n", g_bae.loaded_path);
            *playing = true;
            g_bae.is_playing = true; // ensure main loop sees playing state for progress updates
            return true;
        }
        else
        {
            BAESound_Stop(g_bae.sound, FALSE);
            *playing = false;
            g_bae.is_playing = false;
            return true;
        }
    }
    else if (!g_bae.is_audio_file && g_bae.song)
    {
        // Handle MIDI/RMF files
        if (!*playing)
        {
            // if paused resume else start
            BAE_BOOL isPaused = FALSE;
            BAESong_IsPaused(g_bae.song, &isPaused);
            if (isPaused)
            {
                BAE_PRINTF("Resuming paused song '%s'\n", g_bae.loaded_path);
                BAEResult rr = BAESong_Resume(g_bae.song);
                if (rr != BAE_NO_ERROR)
                {
                    BAE_PRINTF("BAESong_Resume returned %d\n", rr);
                }
            }
            else
            {
                BAE_PRINTF("Preparing to start song '%s' (pos=%d ms)\n", g_bae.loaded_path, bae_get_pos_ms());
                // Reapply loop state right before start in case it was cleared by prior stop/export/load
                if (!g_bae.is_audio_file)
                {
                    BAESong_SetLoops(g_bae.song, g_bae.loop_enabled_gui ? 32767 : 0);
                    BAE_PRINTF("Loop state applied: %d (loops=%s)\n", g_bae.loop_enabled_gui ? 1 : 0, g_bae.loop_enabled_gui ? "32767" : "0");
                }

                uint32_t startPosUs = 0;
                if (g_bae.preserve_position_on_next_start)
                {
                    startPosUs = g_bae.preserved_start_position_us;
                    BAE_PRINTF("Resume with preserved position %u us for '%s'\n", startPosUs, g_bae.loaded_path);
                }

                if (startPosUs == 0)
                {
                    // Standard start from beginning: position then preroll
#ifdef SUPPORT_MIDI_HW
                    g_midi_output_suppressed_during_seek = true;
#endif
                    BAESong_SetMicrosecondPosition(g_bae.song, 0);
#ifdef SUPPORT_MIDI_HW
                    g_midi_output_suppressed_during_seek = false;
#endif
                    BAESong_Preroll(g_bae.song);
                }
                else
                {
                    // For resume, preroll from start (engine needs initial setup) then seek to desired position AFTER preroll
#ifdef SUPPORT_MIDI_HW
                    g_midi_output_suppressed_during_seek = true;
#endif
                    BAESong_SetMicrosecondPosition(g_bae.song, 0);
                    BAESong_Preroll(g_bae.song);
                    BAESong_SetMicrosecondPosition(g_bae.song, startPosUs);
#ifdef SUPPORT_MIDI_HW
                    g_midi_output_suppressed_during_seek = false;
#endif
                }

                BAE_PRINTF("Preroll complete. Start position now %u us for '%s'\n", startPosUs == 0 ? 0 : startPosUs, g_bae.loaded_path);
                BAE_PRINTF("Attempting BAESong_Start on '%s'\n", g_bae.loaded_path);
                BAEResult sr = BAESong_Start(g_bae.song, 0);
                if (sr != BAE_NO_ERROR)
                {
                    BAE_PRINTF("BAESong_Start failed (%d) for '%s' (will try preroll+restart)\n", sr, g_bae.loaded_path);
                    // Try a safety preroll + rewind then attempt once more
#ifdef SUPPORT_MIDI_HW
                    g_midi_output_suppressed_during_seek = true;
#endif
                    BAESong_SetMicrosecondPosition(g_bae.song, 0);
                    BAESong_Preroll(g_bae.song);
                    if (startPosUs)
                    {
                        BAESong_SetMicrosecondPosition(g_bae.song, startPosUs);
                    }
#ifdef SUPPORT_MIDI_HW
                    g_midi_output_suppressed_during_seek = false;
#endif
                    sr = BAESong_Start(g_bae.song, 0);
                    if (sr != BAE_NO_ERROR)
                    {
                        BAE_PRINTF("Second BAESong_Start attempt failed (%d) for '%s'\n", sr, g_bae.loaded_path);
                        return false;
                    }
                    else
                    {
                        BAE_PRINTF("Second BAESong_Start attempt succeeded for '%s'\n", g_bae.loaded_path);
                    }
                }
                else
                {
                    BAE_PRINTF("BAESong_Start ok for '%s'\n", g_bae.loaded_path);
                }

                // Verify resume position if applicable
                if (startPosUs)
                {
                    unsigned int verifyPos = 0;
                    BAESong_GetMicrosecondPosition(g_bae.song, &verifyPos);
                    BAE_PRINTF("Post-start verify position %u us (requested %u us)\n", verifyPos, startPosUs);
                    if (verifyPos < startPosUs - 10000 || verifyPos > startPosUs + 10000)
                    {
                        BAE_PRINTF("WARNING: resume position mismatch (delta=%d us)\n", (int)verifyPos - (int)startPosUs);
                    }
                }

                // Clear finished flag now that we're starting playback from a preserved or normal position
                if (g_bae.song_finished)
                {
                    g_bae.song_finished = false;
                    g_bae.preserve_position_on_next_start = false; // consumed
                }
            }

            // Give mixer a few idle cycles to prime buffers (helps avoid initial stall)
            if (g_bae.mixer)
            {
                for (int i = 0; i < 3; i++)
                {
                    BAEMixer_Idle(g_bae.mixer);
                    BAEMixer_ServiceStreams(g_bae.mixer);
                }
            }

            *playing = true;
            // Clear preservation now that we've successfully (re)started
            g_bae.preserve_position_on_next_start = false;
            g_bae.is_playing = true;
            return true;
        }
        else
        {
            BAESong_Pause(g_bae.song);
            // Ensure external MIDI devices are silenced on pause
#ifdef SUPPORT_MIDI_HW
            if (g_midi_output_enabled)
            {
                midi_output_send_all_notes_off();
            }
#endif
            // Release any held virtual keyboard notes and clear keyboard UI state on pause
            if (g_show_virtual_keyboard)
            {
                BAESong target = g_bae.song ? g_bae.song : g_live_song;
                if (target)
                {
                    for (int n = 0; n < 128; n++)
                    {
                        BAESong_NoteOff(target, (unsigned char)g_keyboard_channel, (unsigned char)n, 0, 0);
                    }
                }
                g_keyboard_mouse_note = -1;
                memset(g_keyboard_active_notes_by_channel, 0, sizeof(g_keyboard_active_notes_by_channel));
                memset(g_keyboard_active_notes, 0, sizeof(g_keyboard_active_notes));
                g_keyboard_suppress_until = SDL_GetTicks() + 250;
            }

            *playing = false;
            g_bae.is_playing = false;

            // Clear per-channel VU meters and peaks so UI shows empty levels immediately when stopped
            for (int i = 0; i < 16; ++i)
            {
                g_channel_vu[i] = 0.0f;
                g_channel_peak_level[i] = 0.0f;
                g_channel_peak_hold_until[i] = 0;
            }
            return true;
        }
    }
    return false;
}

void bae_stop(bool *playing, int *progress)
{
    if (g_bae.is_audio_file && g_bae.sound)
    {
        BAESound_Stop(g_bae.sound, FALSE);
        *playing = false;
        *progress = 0;
        g_bae.is_playing = false;
    }
    else if (!g_bae.is_audio_file && g_bae.song)
    {
        BAESong_Stop(g_bae.song, FALSE);
        // Proactively silence any lingering voices both on the file song and the live song
        if (g_bae.song)
        {
            gui_panic_all_notes(g_bae.song);
        }
        if (g_live_song)
        {
            gui_panic_all_notes(g_live_song);
        }

        if (g_bae.mixer)
        {
            for (int i = 0; i < 3; i++)
            {
                BAEMixer_Idle(g_bae.mixer);
            }
        }
#ifdef SUPPORT_MIDI_HW
        if (g_midi_output_enabled)
        {
            midi_output_send_all_notes_off();
        }
#endif
#ifdef SUPPORT_MIDI_HW
        g_midi_output_suppressed_during_seek = true;
#endif
        BAESong_SetMicrosecondPosition(g_bae.song, 0);
#ifdef SUPPORT_MIDI_HW
        g_midi_output_suppressed_during_seek = false;
#endif
        *playing = false;
        *progress = 0;
        g_bae.is_playing = false;
    }

    // Clear finished flag when user stops playback
    g_bae.song_finished = false;

    // Always reset virtual keyboard UI and release any held virtual notes when stopping
    if (g_show_virtual_keyboard)
    {
        BAESong target = g_bae.song ? g_bae.song : g_live_song;
        if (target)
        {
            for (int n = 0; n < 128; n++)
            {
                BAESong_NoteOff(target, (unsigned char)g_keyboard_channel, (unsigned char)n, 0, 0);
            }
        }
        g_keyboard_mouse_note = -1;
        memset(g_keyboard_active_notes_by_channel, 0, sizeof(g_keyboard_active_notes_by_channel));
        memset(g_keyboard_active_notes, 0, sizeof(g_keyboard_active_notes));
        g_keyboard_suppress_until = SDL_GetTicks() + 250;
    }

    // Clear per-channel VU meters and peaks so UI shows empty levels immediately when stopped
    for (int i = 0; i < 16; ++i)
    {
        g_channel_vu[i] = 0.0f;
        g_channel_peak_level[i] = 0.0f;
        g_channel_peak_hold_until[i] = 0;
    }
}

// Getters for external access to BAE state
BAEMixer bae_get_mixer(void)
{
    return g_bae.mixer;
}

BAESong bae_get_song(void)
{
    return g_bae.song;
}

BAESound bae_get_sound(void)
{
    return g_bae.sound;
}

BAEBankToken bae_get_bank_token(void)
{
    return g_bae.bank_token;
}

const char *bae_get_loaded_path(void)
{
    return g_bae.loaded_path;
}

bool bae_is_song_loaded(void)
{
    return g_bae.song_loaded;
}

bool bae_is_audio_file(void)
{
    return g_bae.is_audio_file;
}

bool bae_is_rmf_file(void)
{
    return g_bae.is_rmf_file;
}

bool bae_is_song_finished(void)
{
    return g_bae.song_finished;
}

void bae_set_song_finished(bool finished)
{
    g_bae.song_finished = finished;
}

bool bae_is_playing(void)
{
    // Check actual BAE song status instead of cached state
    if (!g_bae.song)
        return false;

    BAE_BOOL isDone = FALSE;
    BAEResult result = BAESong_IsDone(g_bae.song, &isDone);

    if (result == BAE_NO_ERROR)
    {
        bool actually_playing = !isDone;
        // Update cached state to match actual state
        g_bae.is_playing = actually_playing;
        return actually_playing;
    }

    // Fallback to cached state if query fails
    return g_bae.is_playing;
}

void bae_set_is_playing(bool playing)
{
    g_bae.is_playing = playing;
}

uint32_t bae_get_song_length_us(void)
{
    return g_bae.song_length_us;
}

void bae_create_live_song(void)
{
    if (!g_bae.mixer)
        return;

    if (g_live_song)
    {
        BAESong_Stop(g_live_song, FALSE);
        BAESong_Delete(g_live_song);
        g_live_song = NULL;
    }

    g_live_song = BAESong_New(g_bae.mixer);
    if (g_live_song)
    {
        BAE_PRINTF("Created live song for virtual keyboard\n");
    }
}

void bae_delete_live_song(void)
{
    if (g_live_song)
    {
        BAESong_Stop(g_live_song, FALSE);
        BAESong_Delete(g_live_song);
        g_live_song = NULL;
    }
}

bool bae_get_bank_name(char *name, size_t name_size)
{
    if (!g_bae.mixer || !name || name_size == 0)
        return false;

    if (BAE_GetBankFriendlyName(g_bae.mixer, g_bae.bank_token, name, (uint32_t)name_size) == BAE_NO_ERROR)
    {
        return true;
    }

    strncpy(name, "Unknown Bank", name_size - 1);
    name[name_size - 1] = '\0';
    return false;
}

void bae_enable_midi_callback(void)
{
#ifdef SUPPORT_MIDI_HW
    if (g_bae.song && g_midi_output_enabled)
#else
    if (g_bae.song)
#endif
    {
        // TODO: Fix MIDI callback signature mismatch
        // BAESong_SetMidiEventCallback(g_bae.song, gui_midi_event_callback, NULL);
    }
}

void bae_disable_midi_callback(void)
{
    if (g_bae.song)
    {
        BAESong_SetMidiEventCallback(g_bae.song, NULL, NULL);
    }
}

#ifdef SUPPORT_MIDI_HW
void bae_set_master_muted_for_midi_out(bool muted)
{
    g_master_muted_for_midi_out = muted;

    if (g_bae.mixer)
    {
        if (muted)
        {
            BAEMixer_SetMasterVolume(g_bae.mixer, 0);
        }
        else
        {
            BAEMixer_SetMasterVolume(g_bae.mixer, FLOAT_TO_UNSIGNED_FIXED(g_last_requested_master_volume));
        }
    }
}
#endif

const char *get_bank_friendly_name()
{
    // Use the BAE API to get the friendly name if we have a loaded bank
    if (g_bae.mixer && g_bae.bank_token)
    {
        static char friendly_name_buffer[256];
        BAEResult result = BAE_GetBankFriendlyName(g_bae.mixer, g_bae.bank_token,
                                                   friendly_name_buffer, sizeof(friendly_name_buffer));
        if (result == BAE_NO_ERROR && friendly_name_buffer[0])
        {
            BAE_PRINTF("Found friendly name via BAE API: %s\n", friendly_name_buffer);
            return friendly_name_buffer;
        }
        else
        {
            BAE_PRINTF("BAE API returned result %d for bank friendly name\n", result);
        }
    }
    return NULL; // Return NULL if no bank loaded or no friendly name found
}
