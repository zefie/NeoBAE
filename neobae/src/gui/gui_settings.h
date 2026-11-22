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

#ifndef GUI_SETTINGS_H
#define GUI_SETTINGS_H

#include "gui_common.h"
#include "GenPriv.h" // For NEO_CUSTOM_MAX_COMBS

// Settings structure for persistence
typedef struct
{
    bool has_bank;
    char bank_path[512];
    bool has_reverb;
    int reverb_type;
    bool has_loop;
    bool loop_enabled;
    bool has_volume_curve;
    int volume_curve;
    bool has_stereo;
    bool stereo_output;
    bool has_sample_rate;
    int sample_rate_hz;
    bool has_show_keyboard;
    bool show_keyboard;
    bool has_webtv;
    bool disable_webtv_progress_bar;
    bool has_export_codec;
    int export_codec_index;
    bool has_shuffle;
    bool shuffle_enabled;
    bool has_repeat;
    int repeat_mode;
    bool has_playlist_enabled;
    bool playlist_enabled;
    bool has_window_pos;
    int window_x;
    int window_y;
    bool has_custom_reverb_preset;
    char custom_reverb_preset_name[64];
    bool has_script_enabled;
    bool script_enabled;
    bool has_script_path;
    char script_path[1024];
    bool has_script_text;
    char script_text[65536];
#if BAE_FIX_SPAN_DC
    bool has_panfix;
    bool panfix_enabled;
#endif
#if BAE_CLASSIC_CHORUS
    bool has_classic_chorus;
    bool classic_chorus_enabled;
#endif
#if USE_WEBTV_MODE
    bool has_webtv_mode;
    bool webtv_mode_enabled;
#endif
    bool has_classic_lfo;
    bool classic_lfo_enabled;
    bool has_terp_mode;
    int terp_mode;
    bool has_output_16bit;
    bool output_16bit;
    bool has_eq;
    bool eq_enabled;
    float eq_gains[5];
    bool has_eq_preset;
    char eq_preset_name[64];
#if USE_NATIVE_DLS == TRUE
    bool has_dls_compatibility_mode;
    bool dls_compatibility_mode;
#endif
    bool has_normalize;
    bool normalize_enabled;
} Settings;

// Custom reverb preset structure
typedef struct
{
    char name[64];
    int comb_count;
    int delays[NEO_CUSTOM_MAX_COMBS];
    int feedback[NEO_CUSTOM_MAX_COMBS];
    int gain[NEO_CUSTOM_MAX_COMBS];
    int lowpass; // 0-127 (MIDI-style)
    int mix;     // 0-255 (wet/dry mix)
} CustomReverbPreset;

// Custom EQ preset structure
typedef struct
{
    char name[64];
    float gains[5];
} CustomEQPreset;

// Custom reverb preset list
extern CustomReverbPreset *g_custom_reverb_presets;
extern int g_custom_reverb_preset_count;
extern char g_current_custom_reverb_preset[64];
extern int g_current_custom_reverb_lowpass;
extern int g_current_custom_reverb_comb_count;
extern int g_current_custom_reverb_delays[NEO_CUSTOM_MAX_COMBS];
extern int g_current_custom_reverb_feedback[NEO_CUSTOM_MAX_COMBS];
extern int g_current_custom_reverb_gain[NEO_CUSTOM_MAX_COMBS];

// Custom EQ preset list
extern CustomEQPreset *g_custom_eq_presets;
extern int g_custom_eq_preset_count;
extern char g_current_custom_eq_preset[64];
extern int g_selected_eq_preset;
extern bool g_preset_dialog_is_eq;
extern bool g_eq_enabled;
extern float g_eq_gains[5];

// Settings dialog state
extern bool g_show_settings_dialog;

// Text input dialog for preset names
extern bool g_show_preset_name_dialog;
extern char g_preset_name_input[64];
extern int g_preset_name_cursor;

// Confirmation dialog for deleting a custom reverb preset
extern bool g_show_preset_delete_confirm_dialog;
extern bool g_preset_delete_confirmed;
extern char g_preset_delete_name[64];

// Volume curve settings
extern int g_volume_curve;
extern bool g_volumeCurveDropdownOpen;

// Sample rate settings
extern bool g_stereo_output;
extern int g_sample_rate_hz;
extern bool g_sampleRateDropdownOpen;
#if BAE_FIX_SPAN_DC
extern bool g_panfix_enabled;
#endif
#if BAE_CLASSIC_CHORUS
extern bool g_classic_chorus_enabled;
#endif
#if USE_WEBTV_MODE
extern bool g_webtv_mode_enabled;
#endif
extern bool g_classic_lfo_enabled;
extern int g_terp_mode;
extern bool g_output_16bit;
extern bool g_normalize_enabled;

// Settings persistence functions
Settings load_settings(void);
void save_settings(const char *last_bank_path, int reverb_type, bool loop_enabled);
void save_full_settings(const Settings *settings);
/* Auto playlist path under NeoBAE config root (migrates exeDir/playlist.m3u once). */
void zefidi_get_playlist_path(char *out, size_t outSize);
void save_playlist_settings(void); // Save just shuffle and repeat settings
void save_custom_reverb_preset(const char *name);
void load_custom_reverb_preset(const char *name);
void delete_custom_reverb_preset(const char *name);
void load_custom_reverb_preset_list(void);
int get_custom_reverb_preset_index(const char *name);

void save_custom_eq_preset(const char *name);
void load_custom_eq_preset(const char *name);
void delete_custom_eq_preset(const char *name);
void load_custom_eq_preset_list(void);
int get_custom_eq_preset_index(const char *name);

// .neoreverb XML import/export helpers
bool export_custom_reverb_neoreverb(const char *preset_name, const char *path);
bool import_custom_reverb_neoreverb(const char *path, char *out_preset_name, size_t out_preset_name_size);
void render_preset_name_dialog(SDL_Renderer *R, int mx, int my, bool mclick, bool mdown, int window_h, int *reverbType);
void render_preset_delete_confirm_dialog(SDL_Renderer *R, int mx, int my, bool mclick, bool mdown, int window_h);

// Settings application functions
void apply_settings_to_ui(const Settings *settings, int *transpose, int *tempo, int *volume,
                          bool *loopPlay, int *reverbType);

// Settings popup window (separate SDL window, like BAEScript)
void settings_window_show(void);
void settings_window_hide(void);
void settings_window_toggle(void);
bool settings_window_is_visible(void);
bool settings_handle_event(SDL_Event *event);
void settings_window_render(int *transpose, int *tempo, int *volume, bool *loopPlay,
                            int *reverbType, bool ch_enable[16], int *progress, int *duration, bool *playing);

// Settings initialization/cleanup
void settings_init(void);
void settings_cleanup(void);

#endif // GUI_SETTINGS_H
