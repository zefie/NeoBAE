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

#ifndef GUI_BAE_H
#define GUI_BAE_H

#include "gui_common.h"
#include "NeoBAE.h" // For BAE types

// Volume mapping configuration shared between GUI and BAE code.
// NEW_BASELINE_PCT: UI 100% corresponds to this engine gain percent (e.g. 35 means UI 100 -> engine 0.35)
// NEW_MAX_VOLUME_PCT: maximum allowed UI percent (e.g. 300 allows up to 3.0x baseline)
#define NEW_BASELINE_PCT 100
#define NEW_MAX_VOLUME_PCT 100

// BAE GUI state structure (shared between gui_main.c and gui_bae.c)
typedef struct
{
    BAEMixer mixer;
    BAESong song;
    BAESound sound;          // For audio files (WAV, MP2/MP3, etc.)
    uint32_t song_length_us; // cached length
    bool song_loaded;
    bool is_audio_file;                  // true if loaded file is audio (not MIDI/RMF)
    bool is_rmf_file;                    // true if loaded song is RMF (not MIDI)
    bool paused;                         // track pause state
    bool is_playing;                     // track playing state
    bool was_playing_before_export;      // for export state restoration
    bool loop_enabled_gui;               // current GUI loop toggle state
    bool loop_was_enabled_before_export; // store loop state for export restore
    uint32_t position_us_before_export;  // to restore playback position
    bool audio_engaged_before_export;    // track hardware engagement
    int current_reverb_type;             // current reverb type index
    char loaded_path[1024];
    // Preserve position across bank reloads
    bool preserve_position_on_next_start;
    uint32_t preserved_start_position_us;
    bool song_finished; // true if engine reported song finished
    // Patch bank info
    BAEBankToken bank_token;
    char bank_name[256];
    bool bank_loaded;
    // Embedded soundbank tracking (RMI replace and/or XMF overlay)
    bool has_embedded_soundbank;
    /* RMI embeds replace the host bank (full GM bank in-file). XMF overlays
     * stack on the host — only RMI sets this so UI can treat them differently. */
    bool has_rmi_embedded_soundbank;
    char previous_bank_name[256];
    char previous_bank_path[1024];
    // Status message system
    char status_message[256];
    Uint32 status_message_time;
} BAEGUI;

extern BAEGUI g_bae;

// Bank management
extern char g_current_bank_path[512];

// Live song for virtual keyboard
extern BAESong g_live_song;

// Globals from other modules used by gui_bae
extern bool g_in_bank_load_recreate;
extern int g_sample_rate_hz;

// Sound volume tracking
extern double g_last_applied_sound_volume;

// Per-channel VU meter state
extern float g_channel_vu[16];
extern float g_channel_peak_level[16];
extern uint32_t g_channel_peak_hold_until[16];
extern uint32_t g_channel_peak_hold_ms;

// Audio position tracking
extern uint32_t audio_total_frames;
extern uint32_t audio_current_position;

// Bank info
typedef struct
{
    char src[128];
    char name[128];
    char sha1[48];
} BankEntry;
extern BankEntry banks[32]; // Static array defined in gui_bae.c
extern int bank_count;      // defined in gui_bae.c

// Function declarations
void gui_audio_task(void *reference);
void set_status_message(const char *msg);

// BAE initialization and management
bool bae_init(int sampleRateHz);
void bae_shutdown(void);

// Song loading and playback
/* Editor session (.bsn/.zsn): instruments + optional SONG; must load as bank. */
bool path_is_editor_session(const char *path);
bool bae_load_song(const char *path, bool use_embedded_banks);
bool bae_load_song_with_settings(const char *path, int transpose, int tempo, int volume,
                                 bool loop_enabled, int reverb_type, bool ch_enable[16], bool use_embedded_banks);
bool bae_play(bool *playing);
void bae_stop(bool *playing, int *progress);
/* Clear song-normalize gain (e.g. when disabling the setting). */
void bae_cancel_normalize(void);
/* Apply mixer normalize gain for the current MIDI/RMF (cache or estimate).
 * Caller should set reverb first so estimate FX padding matches render.
 * Returns applied gain percent (100 = unity). */
int bae_apply_normalize_for_current_song(void);
/* Reload current song and (re)start so normalize estimate applies. Used when enabling the setting. */
void bae_reload_song_for_normalize(int *transpose, int *tempo, int *volume, bool *loopPlay,
                                   int *reverbType, bool ch_enable[16], bool *playing);
void bae_pause(bool *playing, bool *paused);
void bae_seek_ms(int ms);
int bae_get_pos_ms(void);
int bae_get_len_ms(void);

// Settings and controls
void bae_set_transpose(int transpose);
void bae_set_tempo(int tempo);
// Set master volume using UI percent. Accepted range: 0..NEW_MAX_VOLUME_PCT.
// UI value 100 corresponds to NEW_BASELINE_PCT engine gain (e.g. 35 -> 0.35).
// MIDI/RMF uses BAEMixer_SetOutputGain (full mix bus, before the peak limiter).
void bae_set_volume(int volume);
void bae_set_loop(bool enabled);
void bae_set_reverb(int type);
void bae_update_channel_mutes(bool ch_enable[16]);
void bae_apply_current_settings(int transpose, int tempo, int volume, bool loop_enabled,
                                int reverb_type, bool ch_enable[16]);

// Bank management
bool load_bank(const char *path, bool current_playing_state, int transpose, int tempo, int volume,
               bool loop_enabled, int reverb_type, bool ch_enable[16], bool save_to_settings);
bool load_bank_simple(const char *path, bool save_to_settings, int reverb_type, bool loop_enabled);
/* Default Bank discovery: app dir first, then config dir.
 * Names: patches.hsb, patches.zsb, patches.dls (if DLS), patches.sf2 (if SF2).
 * zefidi_next_default_bank: start *index at 0; each call advances and fills next existing path.
 * zefidi_find_default_bank: first existing candidate only. */
bool zefidi_next_default_bank(int *index, char *out, size_t outSize);
bool zefidi_find_default_bank(char *out, size_t outSize);
// Memory-based bank loader
bool bae_load_bank_from_memory(const char *bankdata, int banksize);
const char *get_bank_friendly_name();
void load_bankinfo(void);

// Mixer recreation
bool recreate_mixer_and_restore(int sampleRateHz, int reverbType,
                                int transpose, int tempo, int volume, bool loopPlay,
                                bool ch_enable[16]);

// Audio position helpers
void update_audio_position(void);
void get_audio_total_frames(void);

// Getters for external access to BAE state
BAEMixer bae_get_mixer(void);
BAESong bae_get_song(void);
BAESound bae_get_sound(void);
BAEBankToken bae_get_bank_token(void);
const char *bae_get_loaded_path(void);
bool bae_is_song_loaded(void);
bool bae_is_audio_file(void);
bool bae_is_rmf_file(void);
bool bae_is_song_finished(void);
void bae_set_song_finished(bool finished);
bool bae_is_playing(void);
void bae_set_is_playing(bool playing);
uint32_t bae_get_song_length_us(void);
void bae_create_live_song(void);
void bae_delete_live_song(void);
bool bae_get_bank_name(char *name, size_t name_size);
void bae_enable_midi_callback(void);
void bae_disable_midi_callback(void);
void bae_set_master_muted_for_midi_out(bool muted);

// Settings persistence
void save_settings(const char *last_bank_path, int reverb_type, bool loop_enabled);
// Settings load_settings(void);  // TODO: Define Settings type

// Bank/Program update function (from gui_main.c)
void update_bank_program_for_channel(void);

// Rate conversion (snaps Hz to nearest supported BAERate; values equal Hz)
BAERate map_rate_from_hz(int hz);

void gui_apply_eq_from_settings(void);

#endif // GUI_BAE_H
