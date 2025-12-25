#ifndef GUI_SETTINGS_H
#define GUI_SETTINGS_H

#include "gui_common.h"
#include "GenPriv.h" // For MAX_NEO_COMBS

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
} Settings;

// Custom reverb preset structure
typedef struct
{
    char name[64];
    int comb_count;
    int delays[MAX_NEO_COMBS];
    int feedback[MAX_NEO_COMBS];
    int gain[MAX_NEO_COMBS];
    int lowpass; // 0-127 (MIDI-style)
} CustomReverbPreset;

// Custom reverb preset list
extern CustomReverbPreset *g_custom_reverb_presets;
extern int g_custom_reverb_preset_count;
extern char g_current_custom_reverb_preset[64];
extern int g_current_custom_reverb_lowpass;

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

// Settings persistence functions
Settings load_settings(void);
void save_settings(const char *last_bank_path, int reverb_type, bool loop_enabled);
void save_full_settings(const Settings *settings);
void save_playlist_settings(void); // Save just shuffle and repeat settings
void save_custom_reverb_preset(const char *name);
void load_custom_reverb_preset(const char *name);
void delete_custom_reverb_preset(const char *name);
void load_custom_reverb_preset_list(void);
int get_custom_reverb_preset_index(const char *name);

// .neoreverb XML import/export helpers
bool export_custom_reverb_neoreverb(const char *preset_name, const char *path);
bool import_custom_reverb_neoreverb(const char *path, char *out_preset_name, size_t out_preset_name_size);
void render_preset_name_dialog(SDL_Renderer *R, int mx, int my, bool mclick, bool mdown, int window_h);
void render_preset_delete_confirm_dialog(SDL_Renderer *R, int mx, int my, bool mclick, bool mdown, int window_h);

// Settings application functions
void apply_settings_to_ui(const Settings *settings, int *transpose, int *tempo, int *volume,
                          bool *loopPlay, int *reverbType);

// Settings dialog rendering
void render_settings_dialog(SDL_Renderer *R, int mx, int my, bool mclick, bool mdown,
                            int *transpose, int *tempo, int *volume, bool *loopPlay,
                            int *reverbType, bool ch_enable[16], int *progress, int *duration, bool *playing);

// Settings initialization/cleanup
void settings_init(void);
void settings_cleanup(void);

#endif // GUI_SETTINGS_H
