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
} Settings;

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
