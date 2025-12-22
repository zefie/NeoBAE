#ifndef GUI_SETTINGS_H
#define GUI_SETTINGS_H

#include "gui_common.h"

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

    // Reverb dropdown UI state
    bool has_reverb_custom_mode;
    bool reverb_custom_mode; // true when "Custom" or a user preset was selected last
    bool has_reverb_custom_preset_index;
    int reverb_custom_preset_index; // -1 for plain "Custom", >=0 for user preset
} Settings;

// Custom Reverb/Chorus presets (stored in zefidi.ini)
#define MAX_REVERB_PRESETS 32

typedef struct
{
    char name[64];
    int reverb_level; // 0-127
    int chorus_level; // 0-127
} ReverbPreset;

extern ReverbPreset g_reverb_presets[MAX_REVERB_PRESETS];
extern int g_reverb_preset_count;

// Last selected reverb dropdown UI state (written to zefidi.ini)
extern int g_last_reverb_custom_mode;
extern int g_last_reverb_custom_preset_index;

// Reverb preset modal dialogs (owned by gui_main.c; referenced by ui_modal_blocking())
extern bool g_show_reverb_preset_name_dialog;
extern bool g_show_reverb_preset_delete_confirm;

// Settings dialog state
extern bool g_show_settings_dialog;

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
