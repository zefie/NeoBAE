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

 // gui_bae.c - BAE (Audio Engine) subsystem management

#include "gui_bae.h"
#include "bankinfo.h"
#include "gui_widgets.h"
#include "gui_settings.h"
#include "gui_midi.h"    // For gui_midi_event_callback and midi output functions
#include "gui_karaoke.h" // For karaoke functions
#include "X_API.h"
#include "BAE_API.h"
#include "NeoBAE.h"
#include "NeoBAEConfigPath.h"
#include "GenRingtone.h"
#include "GenSnd.h"
#include "GenPriv.h"
#if USE_RMI_SUPPORT == TRUE
    #include "GenRMI.h"
#endif
#if USE_SF2_SUPPORT == TRUE && _USING_FLUIDLITE == TRUE
    #include "GenSF2_FluidLite.h"
#endif
#if USE_NATIVE_DLS == TRUE
    #include "GenDLS_MobileBAE.h"
    #if USE_XMF_SUPPORT == TRUE
        #include "GenXMF.h"
    #endif
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

/* Per-path normalize gain cache so repeat plays skip MIDI estimate. */
#define NORM_CACHE_MAX 64
typedef struct {
    char path[1024];
    int32_t gain_pct;
} NormCacheEntry;
static NormCacheEntry g_norm_cache[NORM_CACHE_MAX];
static int g_norm_cache_count = 0;

static int norm_cache_lookup(const char *path)
{
    if (!path || !path[0])
        return 0;
    for (int i = 0; i < g_norm_cache_count; ++i)
    {
        if (strcmp(g_norm_cache[i].path, path) == 0)
            return (int)g_norm_cache[i].gain_pct;
    }
    return 0;
}

static void norm_cache_store(const char *path, int32_t gain_pct)
{
    if (!path || !path[0] || gain_pct <= 0)
        return;
    for (int i = 0; i < g_norm_cache_count; ++i)
    {
        if (strcmp(g_norm_cache[i].path, path) == 0)
        {
            g_norm_cache[i].gain_pct = gain_pct;
            return;
        }
    }
    int slot = (g_norm_cache_count < NORM_CACHE_MAX) ? g_norm_cache_count++ : (NORM_CACHE_MAX - 1);
    if (g_norm_cache_count >= NORM_CACHE_MAX)
    {
        /* simple ring: drop oldest */
        memmove(&g_norm_cache[0], &g_norm_cache[1], sizeof(NormCacheEntry) * (NORM_CACHE_MAX - 1));
        slot = NORM_CACHE_MAX - 1;
        g_norm_cache_count = NORM_CACHE_MAX;
    }
    safe_strncpy(g_norm_cache[slot].path, path, sizeof(g_norm_cache[slot].path) - 1);
    g_norm_cache[slot].path[sizeof(g_norm_cache[slot].path) - 1] = '\0';
    g_norm_cache[slot].gain_pct = gain_pct;
}

static void norm_cache_invalidate(const char *path)
{
    if (!path || !path[0])
        return;
    for (int i = 0; i < g_norm_cache_count; ++i)
    {
        if (strcmp(g_norm_cache[i].path, path) == 0)
        {
            if (i < g_norm_cache_count - 1)
                memmove(&g_norm_cache[i], &g_norm_cache[i + 1],
                        sizeof(NormCacheEntry) * (size_t)(g_norm_cache_count - i - 1));
            g_norm_cache_count--;
            return;
        }
    }
}

/* Bank swap invalidates all song→gain entries; next play re-estimates. */
static void norm_cache_clear(void)
{
    g_norm_cache_count = 0;
    if (g_bae.mixer)
        BAEMixer_SetSongNormalizeGain(g_bae.mixer, 100);
}

void bae_cancel_normalize(void)
{
    if (g_bae.mixer)
        BAEMixer_SetSongNormalizeGain(g_bae.mixer, 100);
}

int bae_apply_normalize_for_current_song(void)
{
    if (!g_bae.mixer || !g_bae.song || g_bae.is_audio_file)
        return 100;

    if (g_normalize_enabled && g_bae.loaded_path[0])
    {
        int cached = norm_cache_lookup(g_bae.loaded_path);
        if (cached > 0)
        {
            BAEMixer_SetSongNormalizeGain(g_bae.mixer, cached);
            BAE_PRINTF("Normalize cache hit: %d%%\n", cached);
            return cached;
        }
    }

    {
        Settings settings = load_settings();
        int32_t gainPct = 100;
        BAEResult nerr = BAESong_ApplyNormalizeFromMidiEstimate(
            g_bae.song, g_bae.mixer,
            g_normalize_enabled ? TRUE : FALSE,
            BAE_NORMALIZE_DEFAULT_TARGET_PEAK_PCT, &gainPct);
        if (!g_normalize_enabled)
            return 100;
        if (nerr != BAE_NO_ERROR)
        {
            BAE_PRINTF("Normalize estimate failed (%d); continuing without\n", (int)nerr);
            return 100;
        }

        BAE_PRINTF("Normalize estimate gain: %d%%\n", (int)gainPct);
        if (g_bae.loaded_path[0])
            norm_cache_store(g_bae.loaded_path, gainPct);

        /* Estimate walk rewinds the song; restore preroll state. */
#if SUPPORT_MIDI_HW == TRUE
        g_midi_output_suppressed_during_seek = true;
#endif
        BAESong_SetMicrosecondPosition(g_bae.song, 0);
#if SUPPORT_MIDI_HW == TRUE
        g_midi_output_suppressed_during_seek = false;
#endif
        BAESong_Preroll(g_bae.song);
        BAESong_SetVelocityCurve(g_bae.song, settings.volume_curve);
        return (int)gainPct;
    }
}

void bae_reload_song_for_normalize(int *transpose, int *tempo, int *volume, bool *loopPlay,
                                   int *reverbType, bool ch_enable[16], bool *playing)
{
    char path[1024];
    int pos_ms;

    if (!transpose || !tempo || !volume || !loopPlay || !reverbType || !ch_enable || !playing)
        return;
    if (!g_bae.song_loaded || g_bae.is_audio_file || !g_bae.loaded_path[0])
        return;

    safe_strncpy(path, g_bae.loaded_path, sizeof(path) - 1);
    path[sizeof(path) - 1] = '\0';

    bae_cancel_normalize();
    norm_cache_invalidate(path);
    if (g_bae.mixer)
        BAEMixer_SetSongNormalizeGain(g_bae.mixer, 100);

    if (*playing || g_bae.is_playing)
    {
        bool stop_playing = true;
        bae_stop(&stop_playing, &pos_ms);
        *playing = false;
    }

    set_status_message("Reloading for normalize...");
    if (!bae_load_song_with_settings(path, *transpose, *tempo, *volume, *loopPlay, *reverbType, ch_enable, false))
    {
        set_status_message("Failed to reload song for normalize");
        return;
    }

    g_bae.preserved_start_position_us = 0;
    g_bae.preserve_position_on_next_start = false;

    {
        bool play_flag = false;
        if (!bae_play(&play_flag))
            set_status_message("Failed to start after normalize reload");
        *playing = play_flag;
    }
}

static bool bae_finish_song_start(bool *playing, uint32_t startPosUs)
{
    BAE_PRINTF("Preroll complete. Start position now %u us for '%s'\n", startPosUs == 0 ? 0 : startPosUs, g_bae.loaded_path);
    BAE_PRINTF("Attempting BAESong_Start on '%s'\n", g_bae.loaded_path);
    BAEResult sr = BAESong_Start(g_bae.song, 0);
    if (sr != BAE_NO_ERROR)
    {
        BAE_PRINTF("BAESong_Start failed (%d) for '%s' (will try preroll+restart)\n", sr, g_bae.loaded_path);
#if SUPPORT_MIDI_HW == TRUE
        g_midi_output_suppressed_during_seek = true;
#endif
        BAESong_SetMicrosecondPosition(g_bae.song, 0);
        BAESong_Preroll(g_bae.song);
        if (startPosUs)
            BAESong_SetMicrosecondPosition(g_bae.song, startPosUs);
#if SUPPORT_MIDI_HW == TRUE
        g_midi_output_suppressed_during_seek = false;
#endif
        sr = BAESong_Start(g_bae.song, 0);
        if (sr != BAE_NO_ERROR)
        {
            BAE_PRINTF("Second BAESong_Start attempt failed (%d) for '%s'\n", sr, g_bae.loaded_path);
            return false;
        }
        BAE_PRINTF("Second BAESong_Start attempt succeeded for '%s'\n", g_bae.loaded_path);
    }
    else
    {
        BAE_PRINTF("BAESong_Start ok for '%s'\n", g_bae.loaded_path);
    }

    if (startPosUs)
    {
        unsigned int verifyPos = 0;
        BAESong_GetMicrosecondPosition(g_bae.song, &verifyPos);
        BAE_PRINTF("Post-start verify position %u us (requested %u us)\n", verifyPos, startPosUs);
    }

    if (g_bae.song_finished)
    {
        g_bae.song_finished = false;
        g_bae.preserve_position_on_next_start = false;
    }

    bae_set_reverb(g_bae.current_reverb_type);
    *playing = true;
    g_bae.preserve_position_on_next_start = false;
    g_bae.is_playing = true;
    return true;
}

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
float g_channel_vu[BAE_MAX_MIDI_CHANNELS] = {0.0f};
float g_channel_peak_level[BAE_MAX_MIDI_CHANNELS] = {0.0f};
uint32_t g_channel_peak_hold_until[BAE_MAX_MIDI_CHANNELS] = {0};
uint32_t g_channel_peak_hold_ms = 600; // how long to hold peak in ms

// Bank info
BankEntry banks[32]; // Static array for simplicity
int bank_count = 0;

// User bank tracking for RMI/XMF embedded soundbanks
static bool g_user_bank_stored = false;
static char g_user_bank_path[1024] = {0};
static char g_user_bank_name[256] = {0};

/* Drop embedded-bank display labels from a name (in place). */
static void strip_embedded_bank_suffix(char *name)
{
    static const char kSuffix[] = " + Embedded Bank";
    size_t n, s;
    if (!name || !name[0])
        return;
    if (strcmp(name, "Embedded Bank") == 0)
    {
        name[0] = '\0';
        return;
    }
    n = strlen(name);
    s = sizeof(kSuffix) - 1;
    if (n >= s && strcmp(name + (n - s), kSuffix) == 0)
        name[n - s] = '\0';
}

// External keyboard-related globals (defined in gui_midi_vkbd.c)
extern bool g_show_virtual_keyboard;
extern int g_keyboard_channel;
extern int g_keyboard_mouse_note;
extern uint32_t g_keyboard_suppress_until;
extern bool g_show_rmf_info_dialog;
extern int g_keyboard_program;
extern int g_keyboard_bank;

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
    safe_strncpy(g_status_message, msg, sizeof(g_status_message) - 1);
    g_status_message[sizeof(g_status_message) - 1] = '\0';
    g_status_message_time = SDL_GetTicks();

    // Also update the global g_bae status message for GUI display
    safe_strncpy(g_bae.status_message, msg, sizeof(g_bae.status_message) - 1);
    g_bae.status_message[sizeof(g_bae.status_message) - 1] = '\0';
    g_bae.status_message_time = SDL_GetTicks();
}

// Engine callback: fired on MIDI-file (or queued) CC0 / Program Change so the GUI can
// update the MSB/Program widgets in realtime without polling.
static void gui_program_bank_callback(void *threadContext, struct GM_Song *pSong,
                                      uint8_t channel, uint8_t eventType, uint8_t value,
                                      uint32_t timeMicroseconds, void *reference)
{
    (void)threadContext;
    (void)pSong;
    (void)timeMicroseconds;
    (void)reference;

    if (channel >= 16)
        return;

    if (eventType == GM_PROGRAM_BANK_EVENT_BANK_MSB)
    {
#if SUPPORT_MIDI_HW == TRUE        
        g_midi_bank[channel] = value;
#endif        
        if ((int)channel == g_keyboard_channel)
        {
            g_keyboard_bank = (int)value;
        }
    }
    else if (eventType == GM_PROGRAM_BANK_EVENT_PROGRAM)
    {
#if SUPPORT_MIDI_HW == TRUE        
        g_midi_bank_program[channel] = value;
#endif
        if ((int)channel == g_keyboard_channel)
        {
            g_keyboard_program = (int)value;
        }
    }
}

// Forward declarations
bool bae_load_bank(const char *bank_path);

static bool restore_bank_for_song_load(const char *bank_path, const char *bank_name_hint)
{
    if (!g_bae.mixer || !bank_path || !bank_path[0])
        return false;

#if _BUILT_IN_PATCHES == TRUE
    if (strcmp(bank_path, "__builtin__") == 0)
    {
        BAEBankLoadInfo info;
        BAEResult br = BAEMixer_LoadBankBuiltinOnly(g_bae.mixer, &info);
        if (br != BAE_NO_ERROR || !info.token)
            return false;

        g_bae.bank_token = info.token;
        g_bae.bank_loaded = true;
        safe_strncpy(g_current_bank_path, "__builtin__", sizeof(g_current_bank_path) - 1);
        g_current_bank_path[sizeof(g_current_bank_path) - 1] = '\0';

        if (bank_name_hint && bank_name_hint[0])
        {
            safe_strncpy(g_bae.bank_name, bank_name_hint, sizeof(g_bae.bank_name) - 1);
            g_bae.bank_name[sizeof(g_bae.bank_name) - 1] = '\0';
        }
        else
        {
            const char *friendly_name = get_bank_friendly_name();
            const char *display_name = (friendly_name && friendly_name[0]) ? friendly_name : "(built-in)";
            safe_strncpy(g_bae.bank_name, display_name, sizeof(g_bae.bank_name) - 1);
            g_bae.bank_name[sizeof(g_bae.bank_name) - 1] = '\0';
        }
        return true;
    }
#endif

    if (!bae_load_bank(bank_path))
        return false;

    g_bae.bank_loaded = true;
    safe_strncpy(g_current_bank_path, bank_path, sizeof(g_current_bank_path) - 1);
    g_current_bank_path[sizeof(g_current_bank_path) - 1] = '\0';

    if (bank_name_hint && bank_name_hint[0])
    {
        safe_strncpy(g_bae.bank_name, bank_name_hint, sizeof(g_bae.bank_name) - 1);
        g_bae.bank_name[sizeof(g_bae.bank_name) - 1] = '\0';
    }

    return true;
}

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
        safe_strncpy(be->name, eb->name, sizeof(be->name) - 1);
        safe_strncpy(be->sha1, eb->sha1, sizeof(be->sha1) - 1);
        bank_count++;
    }
    BAE_PRINTF("Loaded info about %d banks\n", bank_count);
}

bool load_bank(const char *path, bool current_playing_state, int transpose, int tempo, int volume, bool loop_enabled, int reverb_type, bool ch_enable[BAE_MAX_MIDI_CHANNELS], bool save_to_settings)
{
    if (!g_bae.mixer)
        return false;
    if (!path)
        return false;

    /* Patch loudness changes with the bank — drop cached normalize gains. */
    if (!g_in_bank_load_recreate)
        norm_cache_clear();

#if SUPPORT_MIDI_HW == TRUE
    bool midi_was_enabled_for_swap = false;
    int midi_restore_api = -1;
    int midi_restore_port = -1;
    if (g_midi_input_enabled && !g_in_bank_load_recreate)
    {
        midi_was_enabled_for_swap = true;
        if (g_midi_input_device_index >= 0 && g_midi_input_device_index < g_midi_input_device_count)
        {
            midi_restore_api = g_midi_device_api[g_midi_input_device_index];
            midi_restore_port = g_midi_device_port[g_midi_input_device_index];
        }
        midi_service_stop();
        midi_input_shutdown();
    }
#endif

    // Host bank is changing. Drop embedded-bank GUI state so a later song
    // load can re-detect (and re-title) against the new host bank. The song
    // auto-reload below re-enables embeds when the file still has one.
    // RMI: audio stays on the reloaded embed by design; UI will show
    // "Embedded Bank" again after reload (not host + embed).
    if (g_bae.has_embedded_soundbank)
    {
        BAE_PRINTF("Clearing embedded-bank UI state for host bank change\n");
#if USE_RMI_SUPPORT == TRUE
        GM_ClearRMISoundbankFlag();
#endif
        g_bae.has_embedded_soundbank = false;
        g_bae.has_rmi_embedded_soundbank = false;
        strip_embedded_bank_suffix(g_bae.bank_name);
        g_user_bank_stored = false;
        g_user_bank_path[0] = '\0';
        g_user_bank_name[0] = '\0';
    }

    // Store current song info before bank change
    bool had_song = g_bae.song_loaded;
    char current_song_path[1024] = {0};
    bool was_playing = false;
    int current_position_ms = 0;
    uint32_t current_position_us = 0;

    if (had_song && g_bae.song)
    {
        safe_strncpy(current_song_path, g_bae.loaded_path, sizeof(current_song_path) - 1);
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

    /* Shared LoadBank* helpers unload HSB/SF2/DLS first (single-active-bank). */
    g_bae.bank_loaded = false;

#if _BUILT_IN_PATCHES == TRUE
    if (strcmp(path, "__builtin__") == 0)
    {
        BAEBankLoadInfo info;
        BAEResult br = BAEMixer_LoadBankBuiltinOnly(g_bae.mixer, &info);
        if (br == BAE_NO_ERROR)
        {
            g_bae.bank_token = info.token;
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
            safe_strncpy(g_bae.bank_name, display_name, sizeof(g_bae.bank_name) - 1);
            g_bae.bank_loaded = true;
            safe_strncpy(g_current_bank_path, "__builtin__", sizeof(g_current_bank_path) - 1);
            g_current_bank_path[sizeof(g_current_bank_path) - 1] = '\0';
            BAE_PRINTF("Loaded built-in bank\n");
            set_status_message("Loaded built-in bank");

            // Update MSB/Program values for the current channel after loading a new bank
            update_bank_program_for_channel();

#if SUPPORT_MIDI_HW == TRUE
            // If external MIDI input is enabled, recreate mixer so live MIDI
            // continues to route into the new mixer with the new bank.
            if (g_midi_input_enabled && !g_in_bank_load_recreate)
            {
                g_in_bank_load_recreate = true;
                recreate_mixer_and_restore(g_sample_rate_hz, reverb_type,
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
            goto load_bank_fail;
        }
    }
    else
    {
#endif
        // Use the bae_load_bank function which handles both HSB and SF2 files
        if (!bae_load_bank(path))
        {
            BAE_PRINTF("Failed to load bank: %s\n", path);
            goto load_bank_fail;
        }

        // SF2/SF3/SFO may use a built-in fallback bank internally, so the
        // generic bank token friendly-name can point at the wrong bank.
        // DLS has its own SHA1→bankinfo lookup (see BAEMixer_GetDLSBankFriendlyName).
        const char *ext = strrchr(path, '.');
        bool is_dls = (ext && strcasecmp(ext, ".dls") == 0);
        bool prefer_filename_label = false;
        if (ext)
        {
            if (strcasecmp(ext, ".sf2") == 0
                || strcasecmp(ext, ".sf3") == 0
                || strcasecmp(ext, ".sfo") == 0
            )
            {
                prefer_filename_label = true;
            }
        }

        // Use friendly name when reliable, otherwise use filename.
        const char *friendly_name = NULL;
        static char dls_friendly_buf[256];
        if (is_dls)
        {
#if USE_NATIVE_DLS == TRUE
            if (g_bae.mixer &&
                BAEMixer_GetDLSBankFriendlyName(g_bae.mixer, dls_friendly_buf,
                                                sizeof(dls_friendly_buf)) == BAE_NO_ERROR &&
                dls_friendly_buf[0])
            {
                friendly_name = dls_friendly_buf;
            }
#endif
        }
        else if (!prefer_filename_label)
        {
            friendly_name = get_bank_friendly_name();
        }
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

        safe_strncpy(g_bae.bank_name, display_name, sizeof(g_bae.bank_name) - 1);
        g_bae.bank_name[sizeof(g_bae.bank_name) - 1] = '\0';
        g_bae.bank_loaded = true;
        safe_strncpy(g_current_bank_path, path, sizeof(g_current_bank_path) - 1);
        g_current_bank_path[sizeof(g_current_bank_path) - 1] = '\0';
        BAE_PRINTF("Loaded bank %s\n", path);

        // Update MSB/LSB values for the current channel after loading a new bank
        update_bank_program_for_channel();

        // Save this as the last used bank only if requested
        if (save_to_settings)
        {
            BAE_PRINTF("About to save settings with path: %s\n", path);
            save_settings(path, reverb_type, loop_enabled);
        }

        char msg[160];
#if USE_NATIVE_DLS == TRUE
        if (BAEMixer_HasEggsDLSBank(g_bae.mixer))
        {
            snprintf(msg, sizeof(msg), "Loaded bank: %s  (scrambled eggs)", display_name);
        }
        else
#endif
        {
            snprintf(msg, sizeof(msg), "Loaded bank: %s", display_name);
        }
        set_status_message(msg); // If external MIDI input is enabled, recreate the mixer so the live
                                 // MIDI routing is attached to a fresh mixer instance with the new
                                 // bank loaded. Protect with a guard to avoid infinite recursion
                                 // because recreate_mixer_and_restore itself calls load_bank.
#if SUPPORT_MIDI_HW == TRUE
        if (g_midi_input_enabled && !g_in_bank_load_recreate)
#else
    if (!g_in_bank_load_recreate)
#endif
        {
            g_in_bank_load_recreate = true;
            // reuse current GUI settings (sample rate & stereo output)
            recreate_mixer_and_restore(g_sample_rate_hz, reverb_type,
                                       transpose, tempo, volume, loop_enabled, ch_enable);
            g_in_bank_load_recreate = false;
        }
#if _BUILT_IN_PATCHES == TRUE
    }
#endif

    // Auto-reload current song if one was loaded.
    // Editor sessions (.bsn/.zsn) own their instruments; do not reload a prior
    // session path via LoadRmfFromFile (closes a temp XFILE under live tokens),
    // and do not auto-reload an unrelated song when the new bank is a session.
    if (had_song && current_song_path[0] != '\0' &&
        !path_is_editor_session(path) &&
        !path_is_editor_session(current_song_path))
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

        /* Keep embedded banks (XMF/RMI) so chips/title stay correct. */
        if (bae_load_song_with_settings(current_song_path, transpose, tempo, volume, loop_enabled, reverb_type, ch_enable, true))
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

#if SUPPORT_MIDI_HW == TRUE
    if (midi_was_enabled_for_swap && g_midi_input_enabled && !g_midi_service_thread)
    {
        if (midi_restore_api >= 0 && midi_restore_port >= 0)
        {
            midi_input_init("zefidi", midi_restore_api, midi_restore_port);
        }
        else
        {
            midi_input_init("zefidi", -1, -1);
        }
        midi_service_start();
    }
#endif

    return true;

load_bank_fail:
#if SUPPORT_MIDI_HW == TRUE
    if (midi_was_enabled_for_swap && g_midi_input_enabled && !g_midi_service_thread)
    {
        if (midi_restore_api >= 0 && midi_restore_port >= 0)
        {
            midi_input_init("zefidi", midi_restore_api, midi_restore_port);
        }
        else
        {
            midi_input_init("zefidi", -1, -1);
        }
        midi_service_start();
    }
#endif
    return false;
}

bool zefidi_next_default_bank(int *index, char *out, size_t outSize)
{
    static const char *const names[] = {
        "patches.hsb",
        "patches.zsb",
#if USE_NATIVE_DLS == TRUE
        "patches.dls",
#endif
#if USE_SF2_SUPPORT == TRUE
        "patches.sf2",
#endif
        NULL};
    int name_count = 0;

    if (!index || !out || outSize == 0)
    {
        return false;
    }
    out[0] = '\0';

    while (names[name_count])
    {
        ++name_count;
    }
    if (name_count == 0)
    {
        return false;
    }

    /* Slot layout: [0 .. name_count) = app/runtime dir, then config dir. */
    while (*index < name_count * 2)
    {
        int slot = *index;
        int dir = slot / name_count; /* 0 = app, 1 = config */
        int ni = slot % name_count;
        (*index)++;

        if (dir == 0)
        {
            if (BAE_GetRuntimeFilePath(out, outSize, names[ni]) != 0)
            {
                continue;
            }
        }
        else
        {
            if (BAE_GetConfigFilePath(out, outSize, names[ni]) != 0)
            {
                continue;
            }
        }

        FILE *tf = fopen(out, "r");
        if (tf)
        {
            fclose(tf);
            return true;
        }
    }
    return false;
}

bool zefidi_find_default_bank(char *out, size_t outSize)
{
    int index = 0;
    return zefidi_next_default_bank(&index, out, outSize);
}

bool load_bank_simple(const char *path, bool save_to_settings, int reverb_type, bool loop_enabled)
{
    bool dummy_ch[BAE_MAX_MIDI_CHANNELS];
    for (int i = 0; i < BAE_MAX_MIDI_CHANNELS; i++)
        dummy_ch[i] = true;

    // If no specific path provided, do fallback discovery
    if (!path)
    {
        BAE_PRINTF("No bank specified, trying fallback discovery\n");

        char candidate[1100];
        int di = 0;
        while (zefidi_next_default_bank(&di, candidate, sizeof(candidate)))
        {
            if (load_bank(candidate, false, 0, 100, 75, loop_enabled, reverb_type, dummy_ch, false))
            {
                return true;
            }
            BAE_PRINTF("Found default bank but failed to load: %s\n", candidate);
        }

#if _BUILT_IN_PATCHES == TRUE
        if (load_bank("__builtin__", false, 0, 100, 75, loop_enabled, reverb_type, dummy_ch, false))
        {
            return true;
        }
#endif
        /* Last resort: cwd-relative npatches.hsb (legacy playbae-style name). */
        if (load_bank("npatches.hsb", false, 0, 100, 75, loop_enabled, reverb_type, dummy_ch, false))
        {
            return true;
        }
        return false;
    }

    return load_bank(path, false, 0, 100, 75, loop_enabled, reverb_type, dummy_ch, save_to_settings);
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
extern SDL_Mutex *g_lyric_mutex;

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

bool bae_init(int sampleRateHz)
{
    // Initialize BAE mixer
    g_bae.mixer = BAEMixer_New();
    if (!g_bae.mixer)
    {
        BAE_PRINTF("BAEMixer_New failed\n");
        return false;
    }

    BAEAudioModifiers modifiers = 0;
    if (g_stereo_output)
        modifiers |= BAE_USE_STEREO;
    if (g_output_16bit)
        modifiers |= BAE_USE_16;
    BAEResult result = BAEMixer_Open(g_bae.mixer,
                                     sampleRateHz,
                                     g_terp_mode,
                                     modifiers,
                                     64,    // maxMidiVoices
                                     8,     // maxSoundVoices
                                     64,    // mixLevel (must be > 0)
                                     TRUE); // engageAudio

    if (result != BAE_NO_ERROR)
    {
        BAE_PRINTF("BAEMixer_Open failed (%d)\n", result);
        BAEMixer_Delete(g_bae.mixer);
        g_bae.mixer = NULL;
        return false;
    }

    gui_apply_eq_from_settings();
    BAE_SetClassicLFO(g_classic_lfo_enabled ? TRUE : FALSE);

    BAE_PRINTF("BAE initialized: %d Hz\n", sampleRateHz);

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

    // Clean up FluidSynth after the mixer is closed (audio callback stopped).
    // GM_CleanupSF2 shuts down FluidSynth's internal threads; without this call
    // those threads continue running past process teardown and cause a crash on exit.
#if USE_SF2_SUPPORT == TRUE
#if _USING_FLUIDLITE == TRUE
    GM_CleanupSF2();
#endif
#endif

    memset(&g_bae, 0, sizeof(g_bae));
    audio_current_position = 0;
    audio_total_frames = 0;
}

bool path_is_editor_session(const char *path)
{
    const char *ext;

    if (!path || !path[0])
        return false;
    ext = strrchr(path, '.');
    if (!ext)
        return false;
    return strcasecmp(ext, ".bsn") == 0 || strcasecmp(ext, ".zsn") == 0;
}

bool bae_load_bank(const char *bank_path)
{
    BAEBankLoadInfo info;

    if (!g_bae.mixer || !bank_path)
        return false;

    {
        BAEResult result = BAEMixer_LoadBankFromPath(g_bae.mixer, (BAEPathName)bank_path, &info);
        if (result != BAE_NO_ERROR)
        {
            BAE_PRINTF("Bank load failed: %d %s\n", result, bank_path);
            return false;
        }
    }

    g_bae.bank_token = info.token;
    g_bae.bank_loaded = true;
    BAE_PRINTF("Bank loaded: %s (kind=%d token=%p)\n", bank_path, (int)info.kind, g_bae.bank_token);
    return true;
}

// Load a bank from memory
bool bae_load_bank_from_memory(const char *bankdata, int banksize)
{
    BAEBankLoadInfo info;

    if (!g_bae.mixer || !bankdata || banksize <= 0)
        return false;

    {
        BAEResult result = BAEMixer_LoadBankFromMemory(
            g_bae.mixer, bankdata, (uint32_t)banksize, NULL, &info);
        if (result != BAE_NO_ERROR)
        {
            BAE_PRINTF("Bank load failed: %d\n", result);
            return false;
        }
    }

    g_bae.bank_token = info.token;
    g_bae.bank_loaded = true;
    BAE_PRINTF("Bank loaded from memory (kind=%d token=%p)\n", (int)info.kind, g_bae.bank_token);
    return true;
}

bool bae_load_song(const char *path, bool use_embedded_banks)
{
    if (!g_bae.mixer || !path)
        return false;

    // Restore user's bank BEFORE loading new song if we had an embedded bank previously.
    // This must run for both FluidSynth and native-DLS embedded RMI banks,
    // and for XMF embedded DLS overlay banks loaded via nativedls.
#if USE_RMI_SUPPORT == TRUE || USE_XMF_SUPPORT == TRUE || USE_NATIVE_DLS == TRUE
    if (g_bae.has_embedded_soundbank)
    {
#if USE_RMI_SUPPORT == TRUE
        GM_ClearRMISoundbankFlag();
#endif

#if USE_SF2_SUPPORT == TRUE && _USING_FLUIDLITE == TRUE
        GM_UnloadSF2Soundfont();
        GM_SetMixerSF2Mode(FALSE);
#endif
#if USE_NATIVE_DLS == TRUE
        BAEMixer_UnloadXMFDLSOverlayBank(g_bae.mixer);
        BAEMixer_UnloadDLSBank(g_bae.mixer);
        GM_SetMixerDLSMode(FALSE);
#endif

        g_bae.has_embedded_soundbank = false;
        g_bae.has_rmi_embedded_soundbank = false;

        // Reload the user's previous bank if we captured one.
        if (g_user_bank_stored)
        {
            strip_embedded_bank_suffix(g_user_bank_name);
            BAE_PRINTF("Restoring user bank before loading new song: %s (%s)\n", g_user_bank_name, g_user_bank_path);
            if (g_user_bank_path[0] != '\0' && restore_bank_for_song_load(g_user_bank_path, g_user_bank_name))
            {
                BAE_PRINTF("Restored user bank successfully\n");
            }
            else
            {
                BAE_PRINTF("Warning: Failed to restore user bank\n");
            }

            /* Always restore the clean host display name, even if the bank
             * reload failed — otherwise "+ Embedded Bank" sticks in the UI. */
            if (g_user_bank_name[0] != '\0')
            {
                safe_strncpy(g_bae.bank_name, g_user_bank_name, sizeof(g_bae.bank_name) - 1);
                g_bae.bank_name[sizeof(g_bae.bank_name) - 1] = '\0';
            }
            else if (g_user_bank_path[0] != '\0')
            {
                const char *base = g_user_bank_path;
                for (const char *p = g_user_bank_path; *p; ++p)
                {
                    if (*p == '/' || *p == '\\')
                        base = p + 1;
                }
                if (strcmp(g_user_bank_path, "__builtin__") == 0)
                    safe_strncpy(g_bae.bank_name, "(built-in)", sizeof(g_bae.bank_name) - 1);
                else
                    safe_strncpy(g_bae.bank_name, base, sizeof(g_bae.bank_name) - 1);
                g_bae.bank_name[sizeof(g_bae.bank_name) - 1] = '\0';
            }
            else
            {
                strip_embedded_bank_suffix(g_bae.bank_name);
            }

            // Clear stored bank after restoration attempt
            g_user_bank_stored = false;
            g_user_bank_path[0] = '\0';
            g_user_bank_name[0] = '\0';
        }
        else
        {
            strip_embedded_bank_suffix(g_bae.bank_name);
        }
    }
#endif

    // Clean previous
    if (g_bae.song)
    {
        BAESong_Stop(g_bae.song, FALSE);
#if SUPPORT_KARAOKE == TRUE
        // Clear any existing callbacks before deleting to prevent stale events
        BAESong_SetMetaEventCallback(g_bae.song, NULL, NULL);
        // Clear lyric callback if it exists
        extern BAEResult BAESong_SetLyricCallback(BAESong song, GM_SongLyricCallbackProcPtr pCallback, void *callbackReference);
        BAESong_SetLyricCallback(g_bae.song, NULL, NULL);
#endif
        BAESong_Delete(g_bae.song);
        g_bae.song = NULL;
    }
    else if (g_bae.sound)
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
#if SUPPORT_KARAOKE == TRUE    
    karaoke_cleanup();
#endif    
    rmf_info_reset();
    
#if USE_SF2_SUPPORT == TRUE
    GM_ResetSF2();
#endif    

    BAEFileType ftype = X_DetermineFileType(path);
    if (ftype == BAE_INVALID_TYPE) {
        return false;
    }

    bool isAudio = false;
    if (ftype == BAE_WAVE_TYPE || ftype == BAE_AIFF_TYPE || ftype == BAE_AU_TYPE
#if USE_MPEG_DECODER == TRUE
        || ftype == BAE_MPEG_TYPE
#endif
#if USE_FLAC_DECODER == TRUE
        || ftype == BAE_FLAC_TYPE
#endif
#if USE_VORBIS_DECODER == TRUE
        || ftype == BAE_VORBIS_TYPE
#endif
#if USE_OPUS_DECODER == TRUE
        || ftype == BAE_OPUS_TYPE
#endif
#if USE_ADP_SUPPORT == TRUE
        || ftype == BAE_ADP_TYPE
#endif
#if USE_ADX_SUPPORT == TRUE
        || ftype == BAE_ADX_TYPE
#endif
#if USE_QOA_SUPPORT == TRUE
        || ftype == BAE_QOA_TYPE
#endif
#if USE_WMA_SUPPORT == TRUE
        || ftype == BAE_WMA_TYPE
#endif
    )
    {
        isAudio = true;
    }

    if (isAudio)
    {
        g_bae.sound = BAESound_New(g_bae.mixer);
        if (!g_bae.sound)
            return false;


        BAEResult sr = BAESound_LoadFileSample(g_bae.sound, (BAEPathName)path, ftype);
        if (sr != BAE_NO_ERROR)
        {
            BAESound_Delete(g_bae.sound);
            g_bae.sound = NULL;
            BAE_PRINTF("Audio load failed %d %s\n", sr, path);
            return false;
        }

        safe_strncpy(g_bae.loaded_path, path, sizeof(g_bae.loaded_path) - 1);
        g_bae.loaded_path[sizeof(g_bae.loaded_path) - 1] = '\0';
        g_bae.song_loaded = true;
        g_bae.is_audio_file = true;
        /* Mixer-level song normalize must not boost PCM sample playback. */
        bae_cancel_normalize();
        if (g_normalize_enabled)
        {
            int32_t gainPct = 100;
            BAEResult nerr = BAESound_NormalizeFromPeak(g_bae.sound, 89, &gainPct);
            if (nerr != BAE_NO_ERROR)
            {
                BAE_PRINTF("Sound normalize failed (%d); continuing without\n", (int)nerr);
            }
            else
            {
                char nmsg[64];
                BAE_PRINTF("Sound normalize gain: %d%%\n", (int)gainPct);
                snprintf(nmsg, sizeof(nmsg), "Normalized (%d%%)", (int)gainPct);
                set_status_message(nmsg);
            }
        }
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
            /* Reverse the quadratic curve: engineGain = (pct/100)^2 * baseline
               so pct = sqrt(engineGain / baseline) * 100 */
            double ratio = stored / baseline;
            if (ratio < 0.0) ratio = 0.0;
            if (ratio > 1.0) ratio = 1.0;
            /* Approximate sqrt via Newton's method to avoid math.h dependency:
               two iterations is accurate to ~6 decimal places for 0..1 */
            double sqrtRatio;
            if (ratio <= 0.0) {
                sqrtRatio = 0.0;
            } else {
                sqrtRatio = (ratio + 1.0) * 0.5; /* initial guess */
                sqrtRatio = (sqrtRatio + ratio / sqrtRatio) * 0.5;
                sqrtRatio = (sqrtRatio + ratio / sqrtRatio) * 0.5;
            }
            int volPct = (int)(sqrtRatio * 100.0 + 0.5);
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
    BAEResult sr = BAE_NO_ERROR;
    g_bae.song = BAESong_New(g_bae.mixer);
    if (!g_bae.song)
        return false;

    if (path_is_editor_session(path))
    {
        /* Sessions are banks: content probe would call them BAE_RMF and
         * LoadRmfFromFile closes the XFILE while instruments still reference it. */
        if (!g_bae.bank_loaded || strcasecmp(g_current_bank_path, path) != 0)
        {
            BAE_PRINTF("Editor session song requires matching open bank: %s\n", path);
            BAESong_Delete(g_bae.song);
            g_bae.song = NULL;
            return false;
        }
        sr = BAESong_LoadSongFromMixerBank(g_bae.song, 0, TRUE);
        g_bae.is_rmf_file = true;
    }
    else if (ftype == BAE_RMF)
    {
        sr = BAESong_LoadRmfFromFile(g_bae.song, (BAEPathName)path, 0, TRUE);
        g_bae.is_rmf_file = true;
    }
#if USE_XMF_SUPPORT == TRUE
    else if (ftype == BAE_XMF)
    {
        sr = BAESong_LoadXmfFromFile(g_bae.song, (BAEPathName)path, TRUE);
        g_bae.is_rmf_file = false;
    }
#endif
#if USE_RMI_SUPPORT == TRUE
    else if (ftype == BAE_RMI)
    {
        // Store current bank info before attempting RMI load in case embedded DLS fails
        char temp_bank_path[1024] = {0};
        char temp_bank_name[256] = {0};
        bool had_bank_before = g_bae.bank_loaded;
        
        if (use_embedded_banks && had_bank_before)
        {
            safe_strncpy(temp_bank_path, g_current_bank_path, sizeof(temp_bank_path) - 1);
            safe_strncpy(temp_bank_name, g_bae.bank_name, sizeof(temp_bank_name) - 1);
        }
        sr = BAESong_LoadRmiFromFile(g_bae.song, (BAEPathName)path, TRUE, (bool)use_embedded_banks);
        
        // If RMI load failed and we requested embedded banks, restore user bank and retry without embedded bank.
        // Do not retry when embedded bank is explicitly unsupported (e.g. embedded SF2 without FluidSynth),
        // because that should fail the whole file load instead of silently playing with a different bank.
        if (sr != BAE_NO_ERROR && sr != BAE_UNSUPPORTED_HARDWARE && use_embedded_banks && had_bank_before && temp_bank_path[0] != '\0')
        {
            BAE_PRINTF("RMI load with embedded bank failed (error %d), restoring previous bank and retrying: %s\n", sr, temp_bank_name);
            if (restore_bank_for_song_load(temp_bank_path, temp_bank_name))
            {
                BAE_PRINTF("Successfully restored previous bank, now loading RMI without embedded bank\n");
                
                // Retry loading RMI without embedded bank option
                sr = BAESong_LoadRmiFromFile(g_bae.song, (BAEPathName)path, TRUE, FALSE);
                if (sr == BAE_NO_ERROR)
                {
                    BAE_PRINTF("RMI MIDI data loaded successfully with user bank\n");
                }
            }
            else
            {
                BAE_PRINTF("Warning: Failed to restore previous bank\n");
            }
        }
        
        g_bae.is_rmf_file = false;
    }
#endif    
    else if (ftype == BAE_MIDI_TYPE) 
    {
        sr = BAESong_LoadMidiFromFile(g_bae.song, (BAEPathName)path, TRUE);
        g_bae.is_rmf_file = false;
    }
#if USE_RETRO_RINGTONE_SUPPORT == TRUE
    else if (ftype == BAE_RINGTONE_IMY || ftype == BAE_RINGTONE_RNG || ftype == BAE_RINGTONE_RTX)
    {
        unsigned char *midiOut = NULL;
        uint32_t midiOutSize = 0;

        sr = BAERingtone_ConvertToMidiFromFile((BAEPathName)path, ftype, &midiOut, &midiOutSize);
        if (sr == BAE_NO_ERROR)
        {
            sr = BAESong_LoadMidiFromMemory(g_bae.song, midiOut, midiOutSize, TRUE);
        }
        BAERingtone_FreeMidiBuffer(midiOut);
        midiOut = NULL;
        g_bae.is_rmf_file = false;
    }
#endif
    else
    {
        // Default to standard MIDI for any remaining cases (including detected MIDI_TYPE)
        sr = BAESong_LoadMidiFromFile(g_bae.song, (BAEPathName)path, TRUE);
        g_bae.is_rmf_file = false;
    }

    if (sr != BAE_NO_ERROR)
    {
        BAE_PRINTF("Song load failed %d %s\n", sr, path);
        BAESong_Delete(g_bae.song);
        g_bae.song = NULL;
        return false;
    }

    // Check if this MIDI file has an embedded soundbank (RMI or XMF)
#if USE_RMI_SUPPORT == TRUE || USE_XMF_SUPPORT == TRUE || USE_NATIVE_DLS == TRUE
    if (use_embedded_banks)
    {
        bool is_rmi_embed = false;
        bool is_xmf_embed = false;
#if USE_RMI_SUPPORT == TRUE
        is_rmi_embed = GM_LastRMIHadEmbeddedSoundbank();
#endif
#if USE_NATIVE_DLS == TRUE && USE_XMF_SUPPORT == TRUE
        if (!is_rmi_embed && g_bae.mixer)
        {
            is_xmf_embed = (BAEMixer_HasXMFDLSOverlayBank(g_bae.mixer) != 0);
        }
#endif
        if (is_rmi_embed || is_xmf_embed)
        {
            // Store host bank so we can restore it after leaving the song.
            if (!g_user_bank_stored && g_bae.bank_loaded)
            {
                safe_strncpy(g_user_bank_path, g_current_bank_path, sizeof(g_user_bank_path) - 1);
                g_user_bank_path[sizeof(g_user_bank_path) - 1] = '\0';
                safe_strncpy(g_user_bank_name, g_bae.bank_name, sizeof(g_user_bank_name) - 1);
                g_user_bank_name[sizeof(g_user_bank_name) - 1] = '\0';
                strip_embedded_bank_suffix(g_user_bank_name);
                /* Don't persist a previous "Embedded Bank" label as the host. */
                if (strcmp(g_user_bank_name, "Embedded Bank") == 0)
                    g_user_bank_name[0] = '\0';
                g_user_bank_stored = (g_user_bank_path[0] != '\0');
                if (g_user_bank_stored)
                    BAE_PRINTF("Stored user bank: %s (%s)\n", g_user_bank_name, g_user_bank_path);
            }

            g_bae.has_embedded_soundbank = true;
            g_bae.has_rmi_embedded_soundbank = is_rmi_embed;
            g_bae.bank_loaded = true;

            if (is_rmi_embed)
            {
                /* RMI replaces the host bank entirely — title is just the embed. */
                const char *embed_name = "Embedded Bank";
#if USE_NATIVE_DLS == TRUE
                static char rmi_dls_friendly[256];
                if (g_bae.mixer && GM_GetMixerDLSMode() &&
                    BAEMixer_GetDLSBankFriendlyName(g_bae.mixer, rmi_dls_friendly,
                                                   sizeof(rmi_dls_friendly)) == BAE_NO_ERROR &&
                    rmi_dls_friendly[0])
                {
                    embed_name = rmi_dls_friendly;
                }
#endif
                safe_strncpy(g_bae.bank_name, embed_name, sizeof(g_bae.bank_name) - 1);
                g_bae.bank_name[sizeof(g_bae.bank_name) - 1] = '\0';
                BAE_PRINTF("Using RMI embedded soundbank (replaces host bank in UI)\n");
            }
            else
            {
                /* XMF overlays the host — show both in the title. */
                if (g_user_bank_name[0] != '\0')
                {
                    snprintf(g_bae.bank_name, sizeof(g_bae.bank_name), "%s + Embedded Bank", g_user_bank_name);
                }
                else
                {
                    char clean_name[256];
                    safe_strncpy(clean_name, g_bae.bank_name, sizeof(clean_name) - 1);
                    clean_name[sizeof(clean_name) - 1] = '\0';
                    strip_embedded_bank_suffix(clean_name);
                    if (clean_name[0] != '\0' && strcmp(clean_name, "Embedded Bank") != 0)
                        snprintf(g_bae.bank_name, sizeof(g_bae.bank_name), "%s + Embedded Bank", clean_name);
                    else
                        safe_strncpy(g_bae.bank_name, "Embedded Bank", sizeof(g_bae.bank_name) - 1);
                    g_bae.bank_name[sizeof(g_bae.bank_name) - 1] = '\0';
                }
                BAE_PRINTF("Using XMF embedded soundbank overlay\n");
            }
        }
    }
    else
    {
        BAE_PRINTF("Skipping embedded bank detection (use_embedded_banks=false)\n");
    }
#endif

    // Restore Reverb after load
    Settings settings = load_settings();
    bae_set_reverb(settings.reverb_type);
    // Defer preroll until just before first Start so that any user settings
    // (transpose, tempo, channel mutes, reverb, loops) are applied first.
    BAESong_GetMicrosecondLength(g_bae.song, &g_bae.song_length_us);
    safe_strncpy(g_bae.loaded_path, path, sizeof(g_bae.loaded_path) - 1);
    g_bae.loaded_path[sizeof(g_bae.loaded_path) - 1] = '\0';
    g_bae.song_loaded = true;
    g_bae.is_audio_file = false; // is_rmf_file already set

    // Update MSB/LSB values for the current channel after loading a new song
    update_bank_program_for_channel();

    /* Apply current UI master volume via OutputGain (full mix bus, before
       the limiter). Do NOT route UI volume through BAESong_SetVolume /
       SetMasterVolume — those only scale HSB and skew DLS-RMF balance.
       Do NOT use GlobalVolume after the limiter — that fades a clipped mix. */
    {
        double stored = g_last_requested_master_volume; /* 0..1 engine space */
        double baseline = (NEW_BASELINE_PCT / 100.0);
        double ratio = (baseline > 0.0) ? (stored / baseline) : 0.0;
        double sqrtRatio;
        int volPct;

        if (ratio < 0.0) ratio = 0.0;
        if (ratio > 1.0) ratio = 1.0;
        if (ratio <= 0.0) {
            sqrtRatio = 0.0;
        } else {
            sqrtRatio = (ratio + 1.0) * 0.5;
            sqrtRatio = (sqrtRatio + ratio / sqrtRatio) * 0.5;
            sqrtRatio = (sqrtRatio + ratio / sqrtRatio) * 0.5;
        }
        volPct = (int)(sqrtRatio * 100.0 + 0.5);
        if (volPct < 0)
            volPct = 0;
        if (volPct > NEW_MAX_VOLUME_PCT)
            volPct = NEW_MAX_VOLUME_PCT;
        if (g_bae.song)
            BAESong_SetVolume(g_bae.song, FLOAT_TO_UNSIGNED_FIXED(1.0));
        bae_set_volume(volPct);
    }

#if SUPPORT_KARAOKE == TRUE
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

#if SUPPORT_MIDI_HW == TRUE
    // If MIDI Output already enabled, register engine MIDI event callback so events are forwarded
    if (g_midi_output_enabled && g_bae.song)
    {
        BAESong_SetMidiEventCallback(g_bae.song, gui_midi_event_callback, NULL);
    }
#endif

    // Always register Program/Bank change callback so MSB/Program UI stays in sync
    // with MIDI-file playback (independent of MIDI output settings).
    if (g_bae.song)
    {
        BAESong_SetProgramBankCallback(g_bae.song, gui_program_bank_callback, NULL);
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

bool bae_load_song_with_settings(const char *path, int transpose, int tempo, int volume, bool loop_enabled, int reverb_type, bool ch_enable[BAE_MAX_MIDI_CHANNELS], bool use_embedded_banks)
{
    if (!bae_load_song(path, use_embedded_banks))
        return false;
    bae_apply_current_settings(transpose, tempo, volume, loop_enabled, reverb_type, ch_enable);
    return true;
}

void bae_set_volume(int volPct)
{
    int gainPct;

    // Accept expanded UI range: 0 .. NEW_MAX_VOLUME_PCT
    if (volPct < 0)
        volPct = 0;
    if (volPct > NEW_MAX_VOLUME_PCT)
        volPct = NEW_MAX_VOLUME_PCT;

    // Map UI percent to engine gain with a perceptual (quadratic) curve.
    // Human hearing is logarithmic, so a linear amplitude mapping makes
    // the upper slider range feel compressed.  Squaring the normalized
    // slider position spreads out the perceived loudness more evenly.
    double normalized = (double)volPct / 100.0;
    double engineGain = (normalized * normalized) * (NEW_BASELINE_PCT / 100.0);

    // Keep a remembered requested master volume in the old 0..1 space
    // so other modules (and sound load) can reconstruct user intent.
    double storedVol = engineGain;
    g_last_requested_master_volume = storedVol; // remember user intent

    gainPct = (int)(engineGain * 100.0 + 0.5);
    if (gainPct < 0)
        gainPct = 0;

    if (g_bae.is_audio_file && g_bae.sound)
    {
        /* For raw audio files we apply an extra per-sound multiplier so the
            UI's "100%" feels louder. Use a smooth, monotonic mapping so
            increasing the UI percent never reduces the resulting gain. */
        double soundMultiplierBase = 3.0;
        double soundMultiplier = soundMultiplierBase * (1.0 + (double)volPct / 100.0);
        double soundGain = engineGain * soundMultiplier;
        soundGain *= 0.25;
        if (soundGain < 0.0)
            soundGain = 0.0;
        BAESound_SetVolume(g_bae.sound, FLOAT_TO_UNSIGNED_FIXED(soundGain));
        /* remember actual per-sound engine gain applied so BAESound_Start
            can use the same value when it starts playback */
        g_last_applied_sound_volume = soundGain;
        /* PCM is already scaled on the voice (input). Keep the mix-bus gain
           at unity so the slider is not applied twice. */
        if (g_bae.mixer)
        {
            BAEMixer_SetOutputGain(g_bae.mixer, 100);
            BAEMixer_SetGlobalVolume(g_bae.mixer, FLOAT_TO_UNSIGNED_FIXED(1.0));
        }
    }
    else if (g_bae.mixer)
    {
        /* MIDI/RMF/live: full mix bus BEFORE the peak limiter (same path as
           playbae -v / nbeditor). Turning the slider down reduces limiter
           engagement instead of fading an already-clipped mix. */
        if (g_bae.song)
            BAESong_SetVolume(g_bae.song, FLOAT_TO_UNSIGNED_FIXED(1.0));
        BAEMixer_SetOutputGain(g_bae.mixer, gainPct);
        BAEMixer_SetGlobalVolume(g_bae.mixer, FLOAT_TO_UNSIGNED_FIXED(1.0));
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

#if SUPPORT_MIDI_HW == TRUE
    // Suppress MIDI output during seeking to avoid sending events prematurely
    g_midi_output_suppressed_during_seek = true;
#endif

    uint32_t totalTicks = 0;
    if (BAESong_GetTickLength(g_bae.song, &totalTicks) == BAE_NO_ERROR &&
        totalTicks > 0 && g_bae.song_length_us > 0)
    {
        double percent = (double)us / (double)g_bae.song_length_us;
        uint32_t targetTick = (uint32_t)(percent * (double)totalTicks);
        BAESong_SetTickPosition(g_bae.song, targetTick);
    }
    else
    {
        BAESong_SetMicrosecondPosition(g_bae.song, us);
    }

#if SUPPORT_MIDI_HW == TRUE
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

#if SUPPORT_MIDI_HW == TRUE
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

    uint32_t currentTicks = 0;
    uint32_t totalTicks = 0;
    if (BAESong_GetTickPosition(g_bae.song, &currentTicks) == BAE_NO_ERROR &&
        BAESong_GetTickLength(g_bae.song, &totalTicks) == BAE_NO_ERROR &&
        totalTicks > 0 && g_bae.song_length_us > 0)
    {
        double percent = (double)currentTicks / (double)totalTicks;
        return (int)((double)g_bae.song_length_us * percent / 1000.0);
    }

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
    g_bae.current_reverb_type = idx;
    if (g_bae.mixer)
    {
        if (idx < 0)
            idx = 0;
        if (idx >= BAE_REVERB_TYPE_COUNT)
            idx = BAE_REVERB_TYPE_COUNT - 1;
        BAEMixer_SetDefaultReverb(g_bae.mixer, (BAEReverbType)idx);
    }
}

void bae_update_channel_mutes(bool ch_enable[BAE_MAX_MIDI_CHANNELS])
{
    if (g_bae.is_audio_file || !g_bae.song)
        return; // Only works with MIDI/RMF

    for (int i = 0; i < BAE_MAX_MIDI_CHANNELS; i++)
    {
        if (ch_enable[i])
            BAESong_UnmuteChannel(g_bae.song, (uint16_t)i);
        else
            BAESong_MuteChannel(g_bae.song, (uint16_t)i);
    }
}

void bae_apply_current_settings(int transpose, int tempo, int volume, bool loop_enabled, int reverb_type, bool ch_enable[BAE_MAX_MIDI_CHANNELS])
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
            /* Ensure leftover MIDI normalize gain is cleared for sample playback. */
            bae_cancel_normalize();

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
                /* Pause must not clear normalize; re-apply cached gain just in case. */
                if (g_normalize_enabled && g_bae.mixer && g_bae.loaded_path[0])
                {
                    int cached = norm_cache_lookup(g_bae.loaded_path);
                    if (cached > 0)
                        BAEMixer_SetSongNormalizeGain(g_bae.mixer, cached);
                }
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
#if SUPPORT_MIDI_HW == TRUE
                    g_midi_output_suppressed_during_seek = true;
#endif
                    BAESong_SetMicrosecondPosition(g_bae.song, 0);
#if SUPPORT_MIDI_HW == TRUE
                    g_midi_output_suppressed_during_seek = false;
#endif
                    BAESong_Preroll(g_bae.song);
                    Settings settings = load_settings();

                    BAESong_SetVelocityCurve(g_bae.song, settings.volume_curve);
                    /* Match estimate FX padding to what playback will use. */
                    bae_set_reverb(g_bae.current_reverb_type);

                    {
                        int gainPct = bae_apply_normalize_for_current_song();
                        if (g_normalize_enabled && gainPct > 0)
                        {
                            char nmsg[64];
                            snprintf(nmsg, sizeof(nmsg), "Normalized (%d%%)", gainPct);
                            set_status_message(nmsg);
                        }
                    }
                }
                else
                {
                    // For resume, preroll from start (engine needs initial setup) then seek to desired position AFTER preroll
                    Settings settings = load_settings();
#if SUPPORT_MIDI_HW == TRUE
                    g_midi_output_suppressed_during_seek = true;
#endif
                    BAESong_SetMicrosecondPosition(g_bae.song, 0);
                    BAESong_Preroll(g_bae.song);
                    BAESong_SetVelocityCurve(g_bae.song, settings.volume_curve);
                    bae_set_reverb(g_bae.current_reverb_type);

                    {
                        int gainPct = bae_apply_normalize_for_current_song();
                        if (g_normalize_enabled && gainPct > 0)
                        {
                            char nmsg[64];
                            snprintf(nmsg, sizeof(nmsg), "Normalized (%d%%)", gainPct);
                            set_status_message(nmsg);
                        }
                    }

                    BAESong_SetMicrosecondPosition(g_bae.song, startPosUs);
#if SUPPORT_MIDI_HW == TRUE
                    g_midi_output_suppressed_during_seek = false;
#endif
                }

                return bae_finish_song_start(playing, startPosUs);
            }

            bae_set_reverb(g_bae.current_reverb_type);

            *playing = true;
            // Clear preservation now that we've successfully (re)started
            g_bae.preserve_position_on_next_start = false;
            g_bae.is_playing = true;
            return true;
        }
        else
        {
            /* Keep song normalize gain across pause/resume; stop clears it. */
            BAESong_Pause(g_bae.song);
            // Ensure external MIDI devices are silenced on pause
#if SUPPORT_MIDI_HW == TRUE
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
                    for (int n = 0; n < BAE_MAX_NOTES; n++)
                    {
                        if (g_keyboard_active_notes[n]) {
                            BAESong_NoteOff(target, (unsigned char)g_keyboard_channel, (unsigned char)n, 0, 0);
                        }
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
            for (int i = 0; i < BAE_MAX_MIDI_CHANNELS; ++i)
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
    bae_cancel_normalize();
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
#if SUPPORT_MIDI_HW == TRUE
        if (g_midi_output_enabled)
        {
            midi_output_send_all_notes_off();
        }
#endif
#if SUPPORT_MIDI_HW == TRUE
        g_midi_output_suppressed_during_seek = true;
#endif
        BAESong_SetMicrosecondPosition(g_bae.song, 0);
#if SUPPORT_MIDI_HW == TRUE
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
            for (int n = 0; n < BAE_MAX_NOTES; n++)
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
    for (int i = 0; i < BAE_MAX_MIDI_CHANNELS; ++i)
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

    safe_strncpy(name, "Unknown Bank", name_size - 1);
    name[name_size - 1] = '\0';
    return false;
}

void bae_enable_midi_callback(void)
{
#if SUPPORT_MIDI_HW == TRUE
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
#if SUPPORT_MIDI_HW == TRUE
    if (g_bae.song && g_midi_output_enabled)
#else
    if (g_bae.song)
#endif
    {
        BAESong_SetMidiEventCallback(g_bae.song, NULL, NULL);
    }
}

#if SUPPORT_MIDI_HW == TRUE
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
            /* MasterVolume is HSB voice scale / MIDI-out mute, not the UI
               slider. Restore unity; player level lives on OutputGain. */
            BAEMixer_SetMasterVolume(g_bae.mixer, FLOAT_TO_UNSIGNED_FIXED(1.0));
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

void gui_apply_eq_from_settings(void)
{
    if (g_bae.mixer)
    {
        Settings settings = load_settings();
        if (settings.has_eq)
        {
            BAEMixer_SetEQEnabled(g_bae.mixer, settings.eq_enabled ? TRUE : FALSE);
            for (int i = 0; i < 5; i++)
            {
                BAEMixer_SetEQGain(g_bae.mixer, i, settings.eq_gains[i]);
            }
        }
    }
}

bool g_in_bank_load_recreate = false;

BAERate map_rate_from_hz(int hz)
{
    switch (hz)
    {
    case 8000:
        return BAE_RATE_8K;
    case 11025:
        return BAE_RATE_11K;
    case 16000:
        return BAE_RATE_16K;
    case 22050:
        return BAE_RATE_22K;
    case 32000:
        return BAE_RATE_32K;
    case 44100:
        return BAE_RATE_44K;
    case 48000:
        return BAE_RATE_48K;
    default: // choose closest
        if (hz < 9600)
            return BAE_RATE_8K;
        if (hz < 13500)
            return BAE_RATE_11K;
        if (hz < 19000)
            return BAE_RATE_16K;
        if (hz < 27000)
            return BAE_RATE_22K;
        if (hz < 38000)
            return BAE_RATE_32K;
        if (hz < 46000)
            return BAE_RATE_44K;
        return BAE_RATE_48K;
    }
}

// Recreate mixer with new sample rate setting preserving current playback state where possible.
bool recreate_mixer_and_restore(int sampleRateHz, int reverbType,
                                int transpose, int tempo, int volume, bool loopPlay,
                                bool ch_enable[BAE_MAX_MIDI_CHANNELS])
{
#if SUPPORT_MIDI_HW == TRUE
    // If MIDI service is active, stop it before tearing down mixer/songs to avoid races.
    bool resume_midi_service = (g_midi_service_thread != NULL);
    if (resume_midi_service)
    {
        midi_service_stop();
    }
#endif
    if (g_exporting)
    {
        set_status_message("Can't change audio format during export");
        return false;
    }
    // Capture current song/audio state
    char last_song_path[1024];
    last_song_path[0] = '\0';
    bool had_song = g_bae.song_loaded;
    bool was_playing = g_bae.is_playing;
    int pos_ms = 0;
    if (had_song)
    {
        safe_strncpy(last_song_path, g_bae.loaded_path, sizeof(last_song_path) - 1);
        last_song_path[sizeof(last_song_path) - 1] = '\0';
        pos_ms = bae_get_pos_ms();
    }

    // Tear down existing mixer & objects (without clearing captured path)
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
    if (g_live_song)
    {
        BAESong_Stop(g_live_song, FALSE);
        BAESong_Delete(g_live_song);
        g_live_song = NULL;
    }
    if (g_bae.mixer)
    {
        // Clear reverb buffers before teardown to avoid stale delay lines
        // causing artifacts when the new mixer initializes at a different rate.
        GM_CleanupReverb();
        ShutdownNewReverb();
#if USE_NEO_EFFECTS == TRUE
        ShutdownNeoReverb();
#endif   
        BAEMixer_Close(g_bae.mixer);
        BAEMixer_Delete(g_bae.mixer);
        g_bae.mixer = NULL;
    }
    g_bae.song_loaded = false;
    g_bae.is_playing = false;
    g_bae.bank_loaded = false;
    g_bae.bank_token = 0;
#if USE_SF2_SUPPORT == TRUE
    if (GM_SF2_IsActive())
    {
        GM_SF2_SetSampleRate(sampleRateHz);
    }
#endif    
    // Create new mixer
#if USE_SF2_SUPPORT == TRUE
    bool wasSF2 = GM_GetMixerSF2Mode();
#endif
    g_bae.mixer = BAEMixer_New();
    if (!g_bae.mixer)
    {
        set_status_message("Mixer recreate failed");
        return false;
    }
#if USE_SF2_SUPPORT == TRUE
    GM_SetMixerSF2Mode(wasSF2);
#endif
    BAERate rate = map_rate_from_hz(sampleRateHz);
    BAEAudioModifiers mods = 0;
    if (g_stereo_output)
        mods |= BAE_USE_STEREO;
    if (g_output_16bit)
        mods |= BAE_USE_16;
    BAEResult mr = BAEMixer_Open(g_bae.mixer, rate, g_terp_mode, mods, 32, 8, 32, TRUE);
    if (mr != BAE_NO_ERROR)
    {
        char msg[96];
        snprintf(msg, sizeof(msg), "Mixer open failed (%d)", mr);
        set_status_message(msg);
        BAEMixer_Delete(g_bae.mixer);
        g_bae.mixer = NULL;
        return false;
    }
    BAEMixer_SetAudioTask(g_bae.mixer, gui_audio_task, g_bae.mixer);
    BAEMixer_ReengageAudio(g_bae.mixer);
    gui_apply_eq_from_settings();
    BAE_SetClassicLFO(g_classic_lfo_enabled ? TRUE : FALSE);
    // reverbType may be a UI index beyond BAE_REVERB_TYPE_COUNT when using custom presets
    bae_set_reverb(reverbType);
    BAEMixer_SetMasterVolume(g_bae.mixer, FLOAT_TO_UNSIGNED_FIXED(1.0));
    bae_set_volume(volume);

    // Create a new live song bound to the new mixer for external MIDI input.
    g_live_song = BAESong_New(g_bae.mixer);
    if (g_live_song)
    {
        BAESong_Preroll(g_live_song);
    }

    // Reload bank if we had one recorded
    if (g_current_bank_path[0])
    {
        bool dummy_play = false;
        // Use load_bank to restore bank (don't instantly save again – pass save_to_settings=false)
        load_bank(g_current_bank_path, dummy_play, transpose, tempo, volume, loopPlay, reverbType, ch_enable, false);
    }
    else
    {
        // Attempt fallback default bank
        load_bank_simple(NULL, false, reverbType, loopPlay);
    }

    // Reload prior song
    if (had_song && last_song_path[0])
    {
        if (bae_load_song_with_settings(last_song_path, transpose, tempo, volume, loopPlay, reverbType, ch_enable, true))
        {
            if (pos_ms > 0)
            {
                bae_seek_ms(pos_ms);
            }
            if (was_playing)
            {
                bool playFlag = false;
                bae_play(&playFlag);
            }
        }
    }
    set_status_message("Audio device reconfigured");
#if SUPPORT_MIDI_HW == TRUE
    // If MIDI input is enabled, ensure MIDI input device is (re)initialized and service resumed
    if (g_midi_input_enabled)
    {
        midi_input_shutdown();
        if (g_midi_input_device_index >= 0 && g_midi_input_device_index < g_midi_input_device_count)
        {
            int api = g_midi_device_api[g_midi_input_device_index];
            int port = g_midi_device_port[g_midi_input_device_index];
            midi_input_init("zefidi", api, port);
        }
        else
        {
            midi_input_init("zefidi", -1, -1);
        }
        midi_service_start();
    }
    else if (resume_midi_service)
    {
        // Service was running before but MIDI input no longer enabled; keep it stopped.
    }
#endif
    return true;
}

