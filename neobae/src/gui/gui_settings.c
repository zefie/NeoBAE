// gui_settings.c - Settings management and persistence

#include "BAE_API.h"
#include "gui_settings.h"
#include "gui_bae.h"
#include "gui_export.h"
#include "gui_widgets.h"
#include "gui_text.h"
#include "gui_theme.h"
#include "gui_common.h"
#include "gui_midi.h"
#include "gui_playlist.h"
#include "GenPriv.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Settings dialog state
bool g_show_settings_dialog = false;

// Volume curve settings
int g_volume_curve = 0;
bool g_volumeCurveDropdownOpen = false;

// Sample rate settings
bool g_stereo_output = true;
int g_sample_rate_hz = 44100;
bool g_sampleRateDropdownOpen = false;

// External globals we need access to
extern bool g_show_virtual_keyboard;
extern int g_exportCodecIndex;
extern int g_window_h;

Settings load_settings(void)
{
    Settings settings = {0};

    char exe_dir[512];
    get_executable_directory(exe_dir, sizeof(exe_dir));

    char settings_path[768];

#ifdef _WIN32
    snprintf(settings_path, sizeof(settings_path), "%s\\zefidi.ini", exe_dir);
#else
    snprintf(settings_path, sizeof(settings_path), "%s/zefidi.ini", exe_dir);
#endif

    FILE *f = fopen(settings_path, "r");
    if (!f)
    {
        return settings; // Return defaults
    }

    char line[512];
    while (fgets(line, sizeof(line), f))
    {
        // Strip newline
        char *nl = strchr(line, '\n');
        if (nl)
            *nl = '\0';

        if (strncmp(line, "bank_path=", 10) == 0)
        {
            safe_strncpy(settings.bank_path, line + 10, sizeof(settings.bank_path) - 1);
            settings.bank_path[sizeof(settings.bank_path) - 1] = '\0';
            settings.has_bank = true;
        }
        else if (strncmp(line, "reverb_type=", 12) == 0)
        {
            settings.reverb_type = atoi(line + 12);
            settings.has_reverb = true;
        }
        else if (strncmp(line, "loop_enabled=", 13) == 0)
        {
            settings.loop_enabled = (atoi(line + 13) != 0);
            settings.has_loop = true;
        }
        else if (strncmp(line, "volume_curve=", 13) == 0)
        {
            settings.volume_curve = atoi(line + 13);
            settings.has_volume_curve = true;
        }
        else if (strncmp(line, "stereo_output=", 14) == 0)
        {
            settings.stereo_output = (atoi(line + 14) != 0);
            settings.has_stereo = true;
        }
        else if (strncmp(line, "sample_rate=", 12) == 0)
        {
            settings.sample_rate_hz = atoi(line + 12);
            if (settings.sample_rate_hz < 7000 || settings.sample_rate_hz > 50000)
            {
                settings.sample_rate_hz = 44100;
            }
            settings.has_sample_rate = true;
        }
        else if (strncmp(line, "show_keyboard=", 14) == 0)
        {
            settings.show_keyboard = (atoi(line + 14) != 0);
            settings.has_show_keyboard = true;
        }
        else if (strncmp(line, "disable_webtv_progress_bar=", 27) == 0)
        {
            settings.disable_webtv_progress_bar = (atoi(line + 27) != 0);
            settings.has_webtv = true;
        }
        else if (strncmp(line, "export_codec_index=", 19) == 0)
        {
            settings.export_codec_index = atoi(line + 19);
            settings.has_export_codec = true;
        }
        else if (strncmp(line, "shuffle_enabled=", 16) == 0)
        {
            settings.shuffle_enabled = (atoi(line + 16) != 0);
            settings.has_shuffle = true;
        }
        else if (strncmp(line, "repeat_mode=", 12) == 0)
        {
            settings.repeat_mode = atoi(line + 12);
            if (settings.repeat_mode < 0 || settings.repeat_mode > 2)
            {
                settings.repeat_mode = 0; // Default to no repeat
            }
            settings.has_repeat = true;
        }
        else if (strncmp(line, "playlist_enabled=", 17) == 0)
        {
            settings.playlist_enabled = (atoi(line + 17) != 0);
            settings.has_playlist_enabled = true;
        }
        else if (strncmp(line, "window_x=", 9) == 0)
        {
            settings.window_x = atoi(line + 9);
            settings.has_window_pos = true;
        }
        else if (strncmp(line, "window_y=", 9) == 0)
        {
            settings.window_y = atoi(line + 9);
            settings.has_window_pos = true;
        }
        // Load active custom reverb preset name
        else if (strncmp(line, "custom_reverb_preset=", 21) == 0)
        {
            safe_strncpy(settings.custom_reverb_preset_name, line + 21, sizeof(settings.custom_reverb_preset_name) - 1);
            settings.custom_reverb_preset_name[sizeof(settings.custom_reverb_preset_name) - 1] = '\0';
            settings.has_custom_reverb_preset = true;
        }
    }
    fclose(f);
    return settings;
}

void save_settings(const char *last_bank_path, int reverb_type, bool loop_enabled)
{
    char exe_dir[512];
    get_executable_directory(exe_dir, sizeof(exe_dir));

    char settings_path[768];
#ifdef _WIN32
    snprintf(settings_path, sizeof(settings_path), "%s\\zefidi.ini", exe_dir);
#else
    snprintf(settings_path, sizeof(settings_path), "%s/zefidi.ini", exe_dir);
#endif

    // Read existing content to preserve custom reverb presets
    char *content = NULL;
    size_t content_size = 0;
    FILE *f_read = fopen(settings_path, "r");
    if (f_read)
    {
        fseek(f_read, 0, SEEK_END);
        content_size = ftell(f_read);
        fseek(f_read, 0, SEEK_SET);
        content = (char *)malloc(content_size + 1);
        if (content)
        {
            fread(content, 1, content_size, f_read);
            content[content_size] = '\0';
        }
        fclose(f_read);
    }

    FILE *f = fopen(settings_path, "w");
    if (f)
    {
        if (last_bank_path && last_bank_path[0])
        {
            fprintf(f, "bank_path=%s\n", last_bank_path);
        }
        fprintf(f, "reverb_type=%d\n", reverb_type);
        fprintf(f, "loop_enabled=%d\n", loop_enabled ? 1 : 0);
        fprintf(f, "volume_curve=%d\n", g_volume_curve);
        fprintf(f, "stereo_output=%d\n", g_stereo_output ? 1 : 0);
        fprintf(f, "sample_rate=%d\n", g_sample_rate_hz);
        fprintf(f, "show_keyboard=%d\n", g_show_virtual_keyboard ? 1 : 0);
        fprintf(f, "disable_webtv_progress_bar=%d\n", g_disable_webtv_progress_bar ? 1 : 0);
        fprintf(f, "export_codec_index=%d\n", g_exportCodecIndex);
#if SUPPORT_PLAYLIST == TRUE
        fprintf(f, "shuffle_enabled=%d\n", g_playlist.shuffle_enabled ? 1 : 0);
        fprintf(f, "repeat_mode=%d\n", g_playlist.repeat_mode);
        fprintf(f, "playlist_enabled=%d\n", g_playlist.enabled ? 1 : 0);
#endif
        // Save window position if available
        extern SDL_Window *g_main_window;
        if (g_main_window)
        {
            int x, y;
            SDL_GetWindowPosition(g_main_window, &x, &y);
            fprintf(f, "window_x=%d\n", x);
            fprintf(f, "window_y=%d\n", y);
        }

#if USE_NEO_EFFECTS
        // Save current custom reverb preset name if one is loaded
        extern char g_current_custom_reverb_preset[64];
        if (g_current_custom_reverb_preset[0])
        {
            fprintf(f, "custom_reverb_preset=%s\n", g_current_custom_reverb_preset);
        }
        
        // Preserve existing custom reverb preset data
        if (content)
        {
            char *read_ptr = content;
            char line_buf[512];
            
            while (*read_ptr)
            {
                // Read line into buffer
                int i = 0;
                while (*read_ptr && *read_ptr != '\n' && i < sizeof(line_buf) - 1)
                {
                    if (*read_ptr != '\r')  // Skip \r characters
                        line_buf[i++] = *read_ptr;
                    read_ptr++;
                }
                line_buf[i] = '\0';
                if (*read_ptr == '\n') read_ptr++;
                
                // Only write lines that start with custom_reverb_%d_
                if (line_buf[0] && strncmp(line_buf, "custom_reverb_", 14) == 0)
                {
                    // Make sure it's the indexed format, not the preset name
                    char *underscore = strchr(line_buf + 14, '_');
                    if (underscore && line_buf[14] >= '0' && line_buf[14] <= '9')
                    {
                        fprintf(f, "%s\n", line_buf);
                    }
                }
            }
        }
#endif

        if (content) free(content);
        fclose(f);
    }
}

void save_full_settings(const Settings *settings)
{
    if (!settings)
        return;

    char exe_dir[512];
    get_executable_directory(exe_dir, sizeof(exe_dir));

    char settings_path[768];
#ifdef _WIN32
    snprintf(settings_path, sizeof(settings_path), "%s\\zefidi.ini", exe_dir);
#else
    snprintf(settings_path, sizeof(settings_path), "%s/zefidi.ini", exe_dir);
#endif

    // Read existing content to preserve custom reverb presets
    char *content = NULL;
    size_t content_size = 0;
    FILE *f_read = fopen(settings_path, "r");
    if (f_read)
    {
        fseek(f_read, 0, SEEK_END);
        content_size = ftell(f_read);
        fseek(f_read, 0, SEEK_SET);
        content = (char *)malloc(content_size + 1);
        if (content)
        {
            fread(content, 1, content_size, f_read);
            content[content_size] = '\0';
        }
        fclose(f_read);
    }

    FILE *f = fopen(settings_path, "w");
    if (f)
    {
        if (settings->has_bank && settings->bank_path[0])
        {
            fprintf(f, "bank_path=%s\n", settings->bank_path);
        }
        if (settings->has_reverb)
        {
            fprintf(f, "reverb_type=%d\n", settings->reverb_type);
        }
        if (settings->has_loop)
        {
            fprintf(f, "loop_enabled=%d\n", settings->loop_enabled ? 1 : 0);
        }
        if (settings->has_volume_curve)
        {
            fprintf(f, "volume_curve=%d\n", settings->volume_curve);
        }
        if (settings->has_stereo)
        {
            fprintf(f, "stereo_output=%d\n", settings->stereo_output ? 1 : 0);
        }
        if (settings->has_sample_rate)
        {
            fprintf(f, "sample_rate=%d\n", settings->sample_rate_hz);
        }
        if (settings->has_show_keyboard)
        {
            fprintf(f, "show_keyboard=%d\n", settings->show_keyboard ? 1 : 0);
        }
        if (settings->has_webtv)
        {
            fprintf(f, "disable_webtv_progress_bar=%d\n", settings->disable_webtv_progress_bar ? 1 : 0);
        }
        if (settings->has_export_codec)
        {
            fprintf(f, "export_codec_index=%d\n", settings->export_codec_index);
        }
        if (settings->has_shuffle)
        {
            fprintf(f, "shuffle_enabled=%d\n", settings->shuffle_enabled ? 1 : 0);
        }
        if (settings->has_repeat)
        {
            fprintf(f, "repeat_mode=%d\n", settings->repeat_mode);
        }
        if (settings->has_playlist_enabled)
        {
            fprintf(f, "playlist_enabled=%d\n", settings->playlist_enabled ? 1 : 0);
        }
        if (settings->has_window_pos)
        {
            fprintf(f, "window_x=%d\n", settings->window_x);
            fprintf(f, "window_y=%d\n", settings->window_y);
        }
        if (settings->has_custom_reverb_preset && settings->custom_reverb_preset_name[0])
        {
            fprintf(f, "custom_reverb_preset=%s\n", settings->custom_reverb_preset_name);
        }
        
#if USE_NEO_EFFECTS
        // Preserve existing custom reverb preset data
        if (content)
        {
            char *read_ptr = content;
            char line_buf[512];
            
            while (*read_ptr)
            {
                // Read line into buffer
                int i = 0;
                while (*read_ptr && *read_ptr != '\n' && i < sizeof(line_buf) - 1)
                {
                    if (*read_ptr != '\r')  // Skip \r characters
                        line_buf[i++] = *read_ptr;
                    read_ptr++;
                }
                line_buf[i] = '\0';
                if (*read_ptr == '\n') read_ptr++;
                
                // Only write lines that start with custom_reverb_%d_
                if (line_buf[0] && strncmp(line_buf, "custom_reverb_", 14) == 0)
                {
                    // Make sure it's the indexed format, not the preset name
                    char *underscore = strchr(line_buf + 14, '_');
                    if (underscore && line_buf[14] >= '0' && line_buf[14] <= '9')
                    {
                        fprintf(f, "%s\n", line_buf);
                    }
                }
            }
        }
#endif
        
        if (content) free(content);
        fclose(f);
    }
}

void apply_settings_to_ui(const Settings *settings, int *transpose, int *tempo, int *volume,
                          bool *loopPlay, int *reverbType)
{
    if (!settings)
        return;

    if (settings->has_reverb)
    {
        *reverbType = settings->reverb_type;
        if (*reverbType == 0)
            *reverbType = 1;
    }
    if (settings->has_loop)
    {
        *loopPlay = settings->loop_enabled;
    }
    if (settings->has_volume_curve)
    {
        g_volume_curve = (settings->volume_curve >= 0 && settings->volume_curve <= 4) ? settings->volume_curve : 0;
    }
    if (settings->has_stereo)
    {
        g_stereo_output = settings->stereo_output;
    }
    if (settings->has_sample_rate)
    {
        g_sample_rate_hz = settings->sample_rate_hz;
    }
    if (settings->has_show_keyboard)
    {
        g_show_virtual_keyboard = settings->show_keyboard;
    }
    if (settings->has_export_codec)
    {
        g_exportCodecIndex = settings->export_codec_index;
        if (g_exportCodecIndex < 0)
            g_exportCodecIndex = 0;
    }
    if (settings->has_webtv)
    {
        g_disable_webtv_progress_bar = settings->disable_webtv_progress_bar;
    }
#if USE_NEO_EFFECTS
    if (settings->has_custom_reverb_preset && settings->custom_reverb_preset_name[0])
    {
        load_custom_reverb_preset(settings->custom_reverb_preset_name);
    }
#endif
#if SUPPORT_PLAYLIST == TRUE
    if (settings->has_shuffle)
    {
        g_playlist.shuffle_enabled = settings->shuffle_enabled;
    }
    if (settings->has_repeat)
    {
        g_playlist.repeat_mode = settings->repeat_mode;
    }
    if (settings->has_playlist_enabled)
    {
        g_playlist.enabled = settings->playlist_enabled;
    } else {
        g_playlist.enabled = true; // Default to enabled if not specified
    }
#endif
}

#if SUPPORT_PLAYLIST == TRUE
void save_playlist_settings(void)
{
    // Load current settings and update just the playlist ones
    Settings settings = load_settings();
    settings.has_shuffle = true;
    settings.shuffle_enabled = g_playlist.shuffle_enabled;
    settings.has_repeat = true;
    settings.repeat_mode = g_playlist.repeat_mode;
    save_full_settings(&settings);
}
#endif

// Settings dialog rendering
void render_settings_dialog(SDL_Renderer *R, int mx, int my, bool mclick, bool mdown,
                            int *transpose, int *tempo, int *volume, bool *loopPlay,
                            int *reverbType, bool ch_enable[16], int *progress, int *duration, bool *playing)
{
    if (!g_show_settings_dialog)
        return;

    // Dim background
    SDL_Color dim = g_is_dark_mode ? (SDL_Color){0, 0, 0, 120} : (SDL_Color){0, 0, 0, 90};
    draw_rect(R, (Rect){0, 0, WINDOW_W, g_window_h}, dim);
    int dlgW = 560;
    int dlgH = 280;
    int pad = 10; // dialog size (wider two-column)
    Rect dlg = {(WINDOW_W - dlgW) / 2, (g_window_h - dlgH) / 2, dlgW, dlgH};
    SDL_Color dlgBg = g_panel_bg;
    dlgBg.a = 240;
    SDL_Color dlgFrame = g_panel_border;
    draw_rect(R, dlg, dlgBg);
    draw_frame(R, dlg, dlgFrame);
    // Title
    draw_text(R, dlg.x + pad, dlg.y + 8, "Settings", g_header_color);
    // Close X (slightly larger for better hit/visibility)
    Rect closeBtn = {dlg.x + dlg.w - 22, dlg.y + 6, 16, 16};
    bool overClose = point_in(mx, my, closeBtn);
    draw_rect(R, closeBtn, overClose ? g_button_hover : g_button_base);
    draw_frame(R, closeBtn, g_button_border);
    // Nudge X up ~3px for better visual alignment
    draw_text(R, closeBtn.x + 4, closeBtn.y - 1, "X", g_button_text);
    if (mclick && overClose)
    {
        g_show_settings_dialog = false;
        g_volumeCurveDropdownOpen = false;
#if SUPPORT_MIDI_HW == TRUE
        g_midiRecordFormatDropdownOpen = false;
#endif
    }

    // Two-column geometry
    int colW = (dlg.w - pad * 3) / 2; // two columns with padding between
    int leftX = dlg.x + pad;
    int rightX = dlg.x + pad * 2 + colW;
    int controlW = 150;
    int controlRightX = leftX + colW - controlW; // dropdowns right-aligned in left column

    // Declare rects that will be used in dropdown rendering
    Rect vcRect = {controlRightX, dlg.y + 32, controlW, 24};
    Rect srRect = {controlRightX, dlg.y + 68, controlW, 24};
#if USE_MPEG_ENCODER == TRUE
    Rect expRect = {controlRightX, dlg.y + 104, controlW, 24};
#endif
#if SUPPORT_MIDI_HW == TRUE
    Rect midiDevRect = {controlRightX, dlg.y + 172, controlW + 200, 24};
    Rect midiOutDevRect = {controlRightX, dlg.y + 200, controlW + 200, 24};
    Rect recordCodecRect = {controlRightX, dlg.y + 228, controlW + 200, 24};
#endif

    // Left column controls (stacked)
    // Volume Curve selector
    draw_text(R, leftX, dlg.y + 36, "Vol. Curve (HSB):", g_text_color);
    const char *volumeCurveNames[] = {"miniBAE S Curve", "Peaky S Curve", "WebTV Curve", "2x Exponential", "2x Linear"};
    int vcCount = 5;
    bool volumeCurveEnabled = !g_midiRecordFormatDropdownOpen;
    SDL_Color dd_bg = g_button_base;
    SDL_Color dd_txt = g_button_text;
    SDL_Color dd_frame = g_button_border;
    if (!volumeCurveEnabled)
    {
        dd_bg.a = 180;
        dd_txt.a = 180;
    }
    else if (point_in(mx, my, vcRect))
        dd_bg = g_button_hover;
    draw_rect(R, vcRect, dd_bg);
    draw_frame(R, vcRect, dd_frame);
    const char *vcCur = (g_volume_curve >= 0 && g_volume_curve < vcCount) ? volumeCurveNames[g_volume_curve] : "?";
    draw_text(R, vcRect.x + 6, vcRect.y + 3, vcCur, dd_txt);
    draw_text(R, vcRect.x + vcRect.w - 16, vcRect.y + 3, g_volumeCurveDropdownOpen ? "^" : "v", dd_txt);
    if (volumeCurveEnabled && point_in(mx, my, vcRect) && mclick)
    {
        g_volumeCurveDropdownOpen = !g_volumeCurveDropdownOpen;
        if (g_volumeCurveDropdownOpen)
        {
            g_sampleRateDropdownOpen = false;
            g_exportDropdownOpen = false;
#if SUPPORT_MIDI_HW == TRUE
            g_midi_input_device_dd_open = false;
            g_midi_output_device_dd_open = false;
            g_midiRecordFormatDropdownOpen = false;
#endif
        }
    }

    // Sample Rate selector
    draw_text(R, leftX, dlg.y + 72, "Sample Rate:", g_text_color);
    const int sampleRates[] = {BAE_RATE_8K, BAE_RATE_11K, BAE_RATE_16K, BAE_RATE_22K, BAE_RATE_32K, BAE_RATE_44K, BAE_RATE_48K};
    const int sampleRateCount = (int)(sizeof(sampleRates) / sizeof(sampleRates[0]));
    int curR = g_sample_rate_hz;
    int best = sampleRates[0];
    int bestDiff = abs(curR - best);
    bool exact = false;
    for (int i = 0; i < sampleRateCount; i++)
    {
        if (sampleRates[i] == curR)
        {
            exact = true;
            break;
        }
        int d = abs(curR - sampleRates[i]);
        if (d < bestDiff)
        {
            bestDiff = d;
            best = sampleRates[i];
        }
    }
    if (!exact)
    {
        g_sample_rate_hz = best;
    }
    char srLabel[32];
    snprintf(srLabel, sizeof(srLabel), "%d Hz", g_sample_rate_hz);
    bool sampleRateEnabled = !g_volumeCurveDropdownOpen && !g_midiRecordFormatDropdownOpen;
    SDL_Color sr_bg = g_button_base;
    if (!sampleRateEnabled)
    {
        sr_bg.a = 180;
    }
    else if (point_in(mx, my, srRect))
        sr_bg = g_button_hover;
    draw_rect(R, srRect, sr_bg);
    draw_frame(R, srRect, g_button_border);
    SDL_Color sr_text_col = g_button_text;
    if (!sampleRateEnabled)
    {
        sr_text_col.a = 180;
    }
    draw_text(R, srRect.x + 6, srRect.y + 3, srLabel, sr_text_col);
    draw_text(R, srRect.x + srRect.w - 16, srRect.y + 3, g_sampleRateDropdownOpen ? "^" : "v", sr_text_col);
    if (sampleRateEnabled && point_in(mx, my, srRect) && mclick)
    {
        g_sampleRateDropdownOpen = !g_sampleRateDropdownOpen;
        if (g_sampleRateDropdownOpen)
        {
            g_exportDropdownOpen = false;
#if SUPPORT_MIDI_HW == TRUE
            g_midi_input_device_dd_open = false;
            g_midi_output_device_dd_open = false;
            g_midiRecordFormatDropdownOpen = false;
#endif
        }
    }

    // Export codec selector (left column, below sample rate)
#if USE_MPEG_ENCODER == TRUE
    draw_text(R, leftX, dlg.y + 108, "Export Codec:", g_text_color);
    bool exportEnabled = !g_volumeCurveDropdownOpen && !g_sampleRateDropdownOpen && !g_midiRecordFormatDropdownOpen;
    SDL_Color exp_bg = g_button_base;
    SDL_Color exp_txt = g_button_text;
    if (!exportEnabled)
    {
        exp_bg.a = 180;
        exp_txt.a = 180;
    }
    else
    {
        if (point_in(mx, my, expRect))
            exp_bg = g_button_hover;
        if (g_exportDropdownOpen)
            exp_bg = g_button_press;
    }
    draw_rect(R, expRect, exp_bg);
    draw_frame(R, expRect, g_button_border);
    const char *expName = g_exportCodecNames[g_exportCodecIndex];
    draw_text(R, expRect.x + 6, expRect.y + 3, expName, exp_txt);
    draw_text(R, expRect.x + expRect.w - 16, expRect.y + 3, g_exportDropdownOpen ? "^" : "v", exp_txt);
    if (exportEnabled && point_in(mx, my, expRect) && mclick)
    {
        g_exportDropdownOpen = !g_exportDropdownOpen;
        if (g_exportDropdownOpen)
        {
            g_volumeCurveDropdownOpen = false;
            g_sampleRateDropdownOpen = false;
#if SUPPORT_MIDI_HW == TRUE
            g_midi_input_device_dd_open = false;
            g_midi_output_device_dd_open = false;
            g_midiRecordFormatDropdownOpen = false;
#endif
        }
    }
#endif
#if SUPPORT_MIDI_HW == TRUE
    // MIDI input enable checkbox and device selector (left column, below Export)
    Rect midiEnRect = {leftX, dlg.y + 176, 18, 18};
    if (ui_toggle(R, midiEnRect, &g_midi_input_enabled, "MIDI Input", mx, my, mclick))
    {
        // initialize or shutdown midi input as requested
        if (g_midi_input_enabled)
        {
            // Start background service first (so events queue safely as soon as RtMidi is opened)
            midi_service_start();
            if (g_bae.mixer)
            {
                BAEMixer_Idle(g_bae.mixer);
                BAEMixer_ServiceStreams(g_bae.mixer);
            }
            // When enabling MIDI In, stop and unload any current media so the live synth takes over
            // Stop and delete loaded song or sound if present
            if (g_exporting)
            {
                bae_stop_wav_export();
            }
            if (g_bae.is_audio_file && g_bae.sound)
            {
                BAESound_Stop(g_bae.sound, FALSE);
                BAESound_Delete(g_bae.sound);
                g_bae.sound = NULL;
            }
            if (g_bae.song)
            {
                BAESong_Stop(g_bae.song, FALSE);
                BAESong_Delete(g_bae.song);
                g_bae.song = NULL;
            }
            g_bae.song_loaded = false;
            g_bae.is_audio_file = false;
            g_bae.is_rmf_file = false;
            g_bae.song_length_us = 0;
            // Reinitialize: ensure a clean start by shutting down any existing MIDI input first, then init
            midi_input_shutdown();
            // Ensure we have a live song available for incoming MIDI
            if (g_live_song == NULL && g_bae.mixer)
            {
                g_live_song = BAESong_New(g_bae.mixer);
                if (g_live_song)
                {
                    BAESong_Preroll(g_live_song);
                }
            }
            /* Apply remembered master volume intent via bae_set_volume so the
               same normalization used for song loads is applied to the live
               synth and mixer after cleanup and live song creation. */
            if (volume)
            {
                bae_set_volume(*volume);
            }
            // If the user has chosen a specific input device, open that one
            if (g_midi_input_device_index >= 0 && g_midi_input_device_index < g_midi_input_device_count)
            {
                int api = g_midi_device_api[g_midi_input_device_index];
                int port = g_midi_device_port[g_midi_input_device_index];
                midi_input_init("miniBAE", api, port);
            }
            else
            {
                midi_input_init("miniBAE", -1, -1);
            }
            if (g_bae.mixer)
            {
                for (int _i = 0; _i < 8; ++_i)
                {
                    BAEMixer_Idle(g_bae.mixer);
                    BAEMixer_ServiceStreams(g_bae.mixer);
                }
            }
            /* Re-apply stored master volume intent after MIDI input is opened.
               Call bae_set_volume to ensure the same normalization/boost
               behavior used for loaded songs is also used for MIDI-in. */
            BAE_WaitMicroseconds(150000);
            if (volume)
            {
                bae_set_volume(*volume);
            }
#if USE_SF2_SUPPORT == TRUE
            if (!BAESong_IsSF2Song(g_live_song))
#endif            
            {            
                BAESong_SetVelocityCurve(g_live_song, g_volume_curve);
            }
        }
        else
        {
            // Stop service thread first to avoid racing engine teardown
            midi_service_stop();
            // Capture current engine targets (both) before shutdown
            BAESong saved_song = g_bae.song;
            BAESong saved_live = g_live_song;
            // Shutdown MIDI input
            midi_input_shutdown();
            // Also send All-Notes-Off to any MIDI output device to silence external hardware
            midi_output_send_all_notes_off();
            // Tell engine to silence any notes using the saved pointers (panic both)
            if (saved_song)
            {
                gui_panic_all_notes(saved_song);
            }
            if (saved_live)
            {
                gui_panic_all_notes(saved_live);
            }
            // Extra pass with a tiny idle helps some synth paths flush tails
            if (g_bae.mixer)
            {
                BAEMixer_Idle(g_bae.mixer);
            }
            if (saved_song)
            {
                gui_panic_all_notes(saved_song);
            }
            if (saved_live)
            {
                gui_panic_all_notes(saved_live);
            }
            // Clear virtual keyboard UI state so no keys remain highlighted (do this regardless of visibility)
            g_keyboard_mouse_note = -1;
            memset(g_keyboard_active_notes_by_channel, 0, sizeof(g_keyboard_active_notes_by_channel));
            memset(g_keyboard_active_notes, 0, sizeof(g_keyboard_active_notes));
            g_keyboard_suppress_until = SDL_GetTicks() + 250;
            // Process mixer idle a few times to flush note-off events promptly
            if (g_bae.mixer)
            {
                for (int i = 0; i < 4; i++)
                {
                    BAEMixer_Idle(g_bae.mixer);
                }
            }
            // To be absolutely sure the lightweight live synth is quiet, delete and recreate it
            if (g_live_song)
            {
                BAESong_Stop(g_live_song, FALSE);
                BAESong_Delete(g_live_song);
                g_live_song = NULL;
            }
            if (g_bae.mixer)
            {
                g_live_song = BAESong_New(g_bae.mixer);
                if (g_live_song)
                {
                    BAESong_Preroll(g_live_song);
                }
            }
            // Also clear any visible per-channel VU/peak state immediately so the UI goes quiet
            for (int i = 0; i < 16; ++i)
            {
                g_channel_vu[i] = 0.0f;
                g_channel_peak_level[i] = 0.0f;
                g_channel_peak_hold_until[i] = 0;
            }
        }
        // persist
        save_settings(g_current_bank_path[0] ? g_current_bank_path : NULL, *reverbType, *loopPlay);
    }
    // MIDI device dropdown (right-aligned in left column)
    // populate device list lazily when dropdown opened
    if (g_midi_input_device_dd_open || g_midi_output_device_dd_open)
    {
        // Enumerate compiled RtMidi APIs and collect input ports first, then output ports.
        g_midi_device_count = 0;
        g_midi_input_device_count = 0;
        g_midi_output_device_count = 0;
        enum RtMidiApi apis[16];
        int apiCount = rtmidi_get_compiled_api(apis, (unsigned int)(sizeof(apis) / sizeof(apis[0])));
        if (apiCount <= 0)
            apiCount = 0;
        const char *dbg = getenv("MINIBAE_DEBUG_MIDI");
        // First: inputs
        for (int ai = 0; ai < apiCount && g_midi_device_count < 64; ++ai)
        {
            RtMidiInPtr r = rtmidi_in_create(apis[ai], "miniBAE_enum", 1000);
            if (!r)
                continue;
            unsigned int cnt = rtmidi_get_port_count(r);
            if (dbg)
            {
                const char *an = rtmidi_api_name(apis[ai]);
                fprintf(stderr, "[MIDI ENUM IN] API %d (%s): ok=%d msg='%s' ports=%u\n", ai, an ? an : "?", r->ok, r->msg ? r->msg : "", cnt);
            }
            for (unsigned int di = 0; di < cnt && g_midi_device_count < 64; ++di)
            {
                int needed = 0;
                rtmidi_get_port_name(r, di, NULL, &needed);
                if (needed > 0)
                {
                    int bufLen = needed < 128 ? needed : 128;
                    char buf[128];
                    buf[0] = '\0';
                    if (rtmidi_get_port_name(r, di, buf, &bufLen) >= 0)
                    {
                        const char *apiName = rtmidi_api_name(apis[ai]);
                        if (apiName && apiName[0])
                        {
                            char full[192];
                            snprintf(full, sizeof(full), "%s: %s", apiName, buf);
                            safe_strncpy(g_midi_device_name_cache[g_midi_device_count], full, sizeof(g_midi_device_name_cache[g_midi_device_count]) - 1);
                        }
                        else
                        {
                            safe_strncpy(g_midi_device_name_cache[g_midi_device_count], buf, sizeof(g_midi_device_name_cache[g_midi_device_count]) - 1);
                        }
                        g_midi_device_name_cache[g_midi_device_count][sizeof(g_midi_device_name_cache[g_midi_device_count]) - 1] = '\0';
                        g_midi_device_api[g_midi_device_count] = ai;
                        g_midi_device_port[g_midi_device_count] = (int)di;
                        ++g_midi_device_count;
                        ++g_midi_input_device_count;
                    }
                }
            }
            rtmidi_in_free(r);
        }
        // Then: outputs (append after inputs)
        for (int ai = 0; ai < apiCount && g_midi_device_count < 64; ++ai)
        {
            RtMidiOutPtr r = rtmidi_out_create(apis[ai], "miniBAE_enum");
            if (!r)
                continue;
            unsigned int cnt = rtmidi_get_port_count(r);
            if (dbg)
            {
                const char *an = rtmidi_api_name(apis[ai]);
                fprintf(stderr, "[MIDI ENUM OUT] API %d (%s): ok=%d msg='%s' ports=%u\n", ai, an ? an : "?", r->ok, r->msg ? r->msg : "", cnt);
            }
            for (unsigned int di = 0; di < cnt && g_midi_device_count < 64; ++di)
            {
                int needed = 0;
                rtmidi_get_port_name(r, di, NULL, &needed);
                if (needed > 0)
                {
                    int bufLen = needed < 128 ? needed : 128;
                    char buf[128];
                    buf[0] = '\0';
                    if (rtmidi_get_port_name(r, di, buf, &bufLen) >= 0)
                    {
                        const char *apiName = rtmidi_api_name(apis[ai]);
                        if (apiName && apiName[0])
                        {
                            char full[192];
                            snprintf(full, sizeof(full), "%s: %s", apiName, buf);
                            safe_strncpy(g_midi_device_name_cache[g_midi_device_count], full, sizeof(g_midi_device_name_cache[g_midi_device_count]) - 1);
                        }
                        else
                        {
                            safe_strncpy(g_midi_device_name_cache[g_midi_device_count], buf, sizeof(g_midi_device_name_cache[g_midi_device_count]) - 1);
                        }
                        g_midi_device_name_cache[g_midi_device_count][sizeof(g_midi_device_name_cache[g_midi_device_count]) - 1] = '\0';
                        g_midi_device_api[g_midi_device_count] = ai;
                        g_midi_device_port[g_midi_device_count] = (int)di;
                        ++g_midi_device_count;
                        ++g_midi_output_device_count;
                    }
                }
            }
            rtmidi_out_free(r);
        }
    }
    // draw current input device name
    const char *curDev = (g_midi_input_device_index >= 0 && g_midi_input_device_index < g_midi_input_device_count) ? g_midi_device_name_cache[g_midi_input_device_index] : "(Default)";
    // Allow input UI to be active unless other dropdowns (sample rate, volume curve, export, record codec) are open.
    bool midiInputEnabled = !(g_volumeCurveDropdownOpen || g_sampleRateDropdownOpen || g_exportDropdownOpen || g_midiRecordFormatDropdownOpen);
    // MIDI output should be disabled whenever the MIDI input dropdown is open (so only one of them can be active at once)
    bool midiOutputEnabled = !(g_volumeCurveDropdownOpen || g_sampleRateDropdownOpen || g_exportDropdownOpen || g_midi_input_device_dd_open || g_midiRecordFormatDropdownOpen);
    SDL_Color md_bg = g_button_base;
    SDL_Color md_txt = g_button_text;
    if (!midiInputEnabled)
    {
        md_bg.a = 180;
        md_txt.a = 180;
    }
    else if (point_in(mx, my, midiDevRect))
        md_bg = g_button_hover;
    draw_rect(R, midiDevRect, md_bg);
    draw_frame(R, midiDevRect, g_button_border);
    draw_text(R, midiDevRect.x + 6, midiDevRect.y + 3, curDev, md_txt);
    draw_text(R, midiDevRect.x + midiDevRect.w - 16, midiDevRect.y + 3, g_midi_input_device_dd_open ? "^" : "v", md_txt);
    if (midiInputEnabled && point_in(mx, my, midiDevRect) && mclick)
    {
        g_midi_input_device_dd_open = !g_midi_input_device_dd_open;
        if (g_midi_input_device_dd_open)
        {
            g_volumeCurveDropdownOpen = false;
            g_sampleRateDropdownOpen = false;
            g_exportDropdownOpen = false;
            g_midi_output_device_dd_open = false;
            g_midiRecordFormatDropdownOpen = false;
        }
    }

    // MIDI output checkbox and device selector (placed next to input)
    Rect midiOutEnRect = {leftX, dlg.y + 204, 18, 18};
    // Disable MIDI Output toggle while exporting or when export dropdown is open
    bool midiOut_toggle_allowed = !g_exporting && !g_exportDropdownOpen && !g_midiRecordFormatDropdownOpen;
    if (!midiOut_toggle_allowed)
    {
        // Draw disabled (dimmed) checkbox and label but do not allow toggling
        bool over = point_in(mx, my, midiOutEnRect);
        draw_custom_checkbox(R, midiOutEnRect, g_midi_output_enabled, over);
        // Keep text normal color even when disabled - only the checkbox is dimmed
        draw_text(R, midiOutEnRect.x + midiOutEnRect.w + 6, midiOutEnRect.y + 2, "MIDI Output", g_text_color);
    }
    else
    {
        // Normal interactive toggle (existing behavior)
        if (ui_toggle(R, midiOutEnRect, &g_midi_output_enabled, "MIDI Output", mx, my, mclick))
        {
            if (g_midi_output_enabled)
            {
                // try to init default output (will open first port or virtual)
                // Ensure any previous output is cleanly silenced first
                midi_output_init("miniBAE", -1, -1);
                // After opening, send current instrument table so external device matches internal synth
                if (g_bae.song)
                {
                    for (unsigned char ch = 0; ch < 16; ++ch)
                    {
                        unsigned char program = 0, bank = 0;
                        if (BAESong_GetProgramBank(g_bae.song, ch, &program, &bank, TRUE) == BAE_NO_ERROR)
                        {
                            unsigned char buf[3];
                            // Send Bank Select MSB (controller 0) if bank fits into MSB
                            buf[0] = (unsigned char)(0xB0 | (ch & 0x0F));
                            buf[1] = 0;
                            buf[2] = (unsigned char)(bank & 0x7F);
                            midi_output_send(buf, 3);
                            // Program Change
                            buf[0] = (unsigned char)(0xC0 | (ch & 0x0F));
                            buf[1] = (unsigned char)(program & 0x7F);
                            midi_output_send(buf, 2);
                        }
                    }
                }
                // Register engine MIDI event callback to mirror events
                if (g_bae.song)
                {
                    BAESong_SetMidiEventCallback(g_bae.song, gui_midi_event_callback, NULL);
                }
                // Mute overall device (not just song) so internal synth is silent
                if (g_bae.mixer)
                {
                    BAEMixer_SetMasterVolume(g_bae.mixer, FLOAT_TO_UNSIGNED_FIXED(0.0));
                    g_master_muted_for_midi_out = true;
                }
            }
            else
            {
                // Before closing output, tell external device to silence and reset
                midi_output_send_all_notes_off();
                midi_output_shutdown();
                // Unregister engine MIDI event callback
                if (g_bae.song)
                {
                    BAESong_SetMidiEventCallback(g_bae.song, NULL, NULL);
                }
                // Restore master volume
                if (g_bae.mixer)
                {
                    BAEMixer_SetMasterVolume(g_bae.mixer, FLOAT_TO_UNSIGNED_FIXED(g_last_requested_master_volume));
                    g_master_muted_for_midi_out = false;
                }
            }
            save_settings(g_current_bank_path[0] ? g_current_bank_path : NULL, *reverbType, *loopPlay);
        }
    }
    const char *curOutDev = (g_midi_output_device_index >= 0 && g_midi_output_device_index < g_midi_output_device_count) ? g_midi_device_name_cache[g_midi_input_device_count + g_midi_output_device_index] : "(Default)";
    SDL_Color mo_bg = g_button_base;
    SDL_Color mo_txt = g_button_text;
    if (!midiOutputEnabled)
    {
        mo_bg.a = 180;
        mo_txt.a = 180;
    }
    else if (point_in(mx, my, midiOutDevRect))
        mo_bg = g_button_hover;
    draw_rect(R, midiOutDevRect, mo_bg);
    draw_frame(R, midiOutDevRect, g_button_border);
    draw_text(R, midiOutDevRect.x + 6, midiOutDevRect.y + 3, curOutDev, mo_txt);
    draw_text(R, midiOutDevRect.x + midiOutDevRect.w - 16, midiOutDevRect.y + 3, g_midi_output_device_dd_open ? "^" : "v", mo_txt);
    if (midiOutputEnabled && point_in(mx, my, midiOutDevRect) && mclick)
    {
        g_midi_output_device_dd_open = !g_midi_output_device_dd_open;
        if (g_midi_output_device_dd_open)
        {
            g_volumeCurveDropdownOpen = false;
            g_sampleRateDropdownOpen = false;
            g_exportDropdownOpen = false;
            g_midi_input_device_dd_open = false;
            g_midiRecordFormatDropdownOpen = false;
        }
    }

    // Record Codec selector (left column, below MIDI Output)
    draw_text(R, leftX, dlg.y + 232, "MIDI In Record:", g_text_color);
    bool recordCodecEnabled = !(g_volumeCurveDropdownOpen || g_sampleRateDropdownOpen || g_exportDropdownOpen || g_midi_input_device_dd_open || g_midi_output_device_dd_open);
    SDL_Color rc_bg = g_button_base;
    SDL_Color rc_txt = g_button_text;

    if (!recordCodecEnabled)
    {
        rc_bg.a = 180;
        rc_txt.a = 180;
    }
    else if (point_in(mx, my, recordCodecRect))
        rc_bg = g_button_hover;
    if (g_midiRecordFormatDropdownOpen)
        rc_bg = g_button_press;    
    draw_rect(R, recordCodecRect, rc_bg);
    draw_frame(R, recordCodecRect, g_button_border);
    const char *rcName = g_midiRecordFormatNames[g_midiRecordFormatIndex];
    draw_text(R, recordCodecRect.x + 6, recordCodecRect.y + 3, rcName, rc_txt);
    draw_text(R, recordCodecRect.x + recordCodecRect.w - 16, recordCodecRect.y + 3, g_midiRecordFormatDropdownOpen ? "^" : "v", rc_txt);
    // Note: ui_dropdown_two_column handles the button click internally, so we don't handle clicks here
#endif
    // Right column controls (checkboxes)
    Rect cbRect = {rightX, dlg.y + 36, 18, 18};
    if (ui_toggle(R, cbRect, &g_stereo_output, "Stereo Output", mx, my, mclick))
    {
        bool wasPlayingBefore = g_bae.is_playing;     
        if (recreate_mixer_and_restore(g_sample_rate_hz, g_stereo_output, *reverbType, *transpose, *tempo, *volume, *loopPlay, ch_enable))
        {

            if (wasPlayingBefore && progress > 0)
            {
                bae_seek_ms(*progress);
            } 
            else {
                *playing = false;
            }

#if SUPPORT_MIDI_HW == TRUE
            // If MIDI input was active, reinitialize it so hardware stays in a consistent state
            if (g_midi_input_enabled)
            {
                // Stop service thread prior to changing devices
                midi_service_stop();
                midi_input_shutdown();
                if (g_midi_input_device_index >= 0 && g_midi_input_device_index < g_midi_input_device_count)
                {
                    int api = g_midi_device_api[g_midi_input_device_index];
                    int port = g_midi_device_port[g_midi_input_device_index];
                    midi_input_init("miniBAE", api, port);
                    midi_service_start();
                }
                else
                {
                    midi_input_init("miniBAE", -1, -1);
                    midi_service_start();
                }
            }
#endif
        }
        save_settings(g_current_bank_path[0] ? g_current_bank_path : NULL, *reverbType, *loopPlay);
    }

    Rect kbRect = {rightX, dlg.y + 72, 18, 18};
    if (ui_toggle(R, kbRect, &g_show_virtual_keyboard, "Show Virtual Keyboard", mx, my, mclick))
    {
        save_settings(g_current_bank_path[0] ? g_current_bank_path : NULL, *reverbType, *loopPlay);
        if (!g_show_virtual_keyboard)
            g_keyboard_channel_dd_open = false;
    }

    Rect playlistRect = {rightX, dlg.y + 108, 18, 18};
#if SUPPORT_PLAYLIST == TRUE
    if (ui_toggle(R, playlistRect, &g_playlist.enabled, "Enable Playlist", mx, my, mclick))
    {
        save_settings(g_current_bank_path[0] ? g_current_bank_path : NULL, *reverbType, *loopPlay);
    }
#else
    // Show disabled checkbox when playlist support is not compiled in
    draw_custom_checkbox(R, playlistRect, false, false);
    draw_text(R, playlistRect.x + playlistRect.w + 6, playlistRect.y + 2, "Enable Playlist (disabled)", g_text_color);
#endif

    Rect wtvRect = {rightX, dlg.y + 144, 18, 18};
    bool webtv_enabled = !g_disable_webtv_progress_bar;
    if (ui_toggle(R, wtvRect, &webtv_enabled, "WebTV Style Bar", mx, my, mclick))
    {
        g_disable_webtv_progress_bar = !webtv_enabled;
        save_settings(g_current_bank_path[0] ? g_current_bank_path : NULL, *reverbType, *loopPlay);
    }

    // MIDI channel selector removed from Settings dialog - channel is now controlled in the virtual keyboard dialog

    // Footer info removed (moved to About dialog)

    // Render dropdown lists LAST so they layer over footer text
    if (g_sampleRateDropdownOpen && !g_volumeCurveDropdownOpen)
    {
        int itemH = 24;
        Rect box = {srRect.x, srRect.y + srRect.h + 1, srRect.w, itemH * sampleRateCount};
        SDL_Color ddBg = g_panel_bg;
        ddBg.a = 255;
        SDL_Color shadow = {0, 0, 0, g_is_dark_mode ? 120 : 90};
        Rect shadowRect = {box.x + 2, box.y + 2, box.w, box.h};
        draw_rect(R, shadowRect, shadow);
        draw_rect(R, box, ddBg);
        draw_frame(R, box, g_panel_border);
        for (int i = 0; i < sampleRateCount; i++)
        {
            Rect ir = {box.x, box.y + i * itemH, box.w, itemH};
            bool over = point_in(mx, my, ir);
            int r = sampleRates[i];
            bool selected = (r == g_sample_rate_hz);
            SDL_Color ibg = selected ? g_highlight_color : g_panel_bg;
            if (over)
                ibg = g_button_hover;
            draw_rect(R, ir, ibg);
            if (i < sampleRateCount - 1)
            {
                SDL_Color sep = g_panel_border;
                SDL_SetRenderDrawColor(R, sep.r, sep.g, sep.b, 255);
                SDL_RenderLine(R, ir.x, ir.y + ir.h, ir.x + ir.w, ir.y + ir.h);
            }
            char txt[32];
            snprintf(txt, sizeof(txt), "%d Hz", r);
            SDL_Color itxt = (selected || over) ? g_button_text : g_text_color;
            draw_text(R, ir.x + 6, ir.y + 6, txt, itxt);
            if (over && mclick)
            {
                bool changed = (g_sample_rate_hz != r);
                g_sample_rate_hz = r;
                g_sampleRateDropdownOpen = false;
                if (changed)
                {
                    bool wasPlayingBefore = g_bae.is_playing;
                    if (recreate_mixer_and_restore(g_sample_rate_hz, g_stereo_output, *reverbType, *transpose, *tempo, *volume, *loopPlay, ch_enable))
                    {
                        if (wasPlayingBefore && progress > 0)
                        {
                            bae_seek_ms(*progress);
                        } 
                        else {
                            *playing = false;
                        }
#if SUPPORT_MIDI_HW == TRUE
                        // If MIDI input was active when we changed sample rate, reinit MIDI hardware so it stays connected
                        if (g_midi_input_enabled)
                        {
                            midi_service_stop();
                            midi_input_shutdown();
                            if (g_midi_input_device_index >= 0 && g_midi_input_device_index < g_midi_input_device_count)
                            {
                                int api = g_midi_device_api[g_midi_input_device_index];
                                int port = g_midi_device_port[g_midi_input_device_index];
                                midi_input_init("miniBAE", api, port);
                                midi_service_start();
                            }
                            else
                            {
                                midi_input_init("miniBAE", -1, -1);
                                midi_service_start();
                            }
                        }
#endif
                        save_settings(g_current_bank_path[0] ? g_current_bank_path : NULL, *reverbType, *loopPlay);
                    }
                }
            }
        }
        if (mclick && !point_in(mx, my, srRect) && !point_in(mx, my, box))
            g_sampleRateDropdownOpen = false;
    }
#if SUPPORT_MIDI_HW == TRUE
    // MIDI input device dropdown
    if (g_midi_input_device_dd_open)
    {
        int itemH = midiDevRect.h;
        int deviceCount = g_midi_input_device_count;
        if (deviceCount <= 0)
            deviceCount = 1; // show placeholder
        Rect box = {midiDevRect.x, midiDevRect.y + midiDevRect.h + 1, midiDevRect.w, itemH * deviceCount};
        SDL_Color ddBg = g_panel_bg;
        ddBg.a = 255;
        SDL_Color shadow = {0, 0, 0, g_is_dark_mode ? 120 : 90};
        Rect shadowRect = {box.x + 2, box.y + 2, box.w, box.h};
        draw_rect(R, shadowRect, shadow);
        draw_rect(R, box, ddBg);
        draw_frame(R, box, g_panel_border);
        if (g_midi_input_device_count == 0)
        { // placeholder
            Rect ir = {box.x, box.y, box.w, itemH};
            draw_rect(R, ir, g_panel_bg);
            draw_text(R, ir.x + 6, ir.y + 6, "No MIDI devices", g_text_color);
        }
        else
        {
            for (int i = 0; i < g_midi_input_device_count && i < 64; i++)
            {
                Rect ir = {box.x, box.y + i * itemH, box.w, itemH};
                bool over = point_in(mx, my, ir);
                SDL_Color ibg = (i == g_midi_input_device_index) ? g_highlight_color : g_panel_bg;
                if (over)
                    ibg = g_button_hover;
                draw_rect(R, ir, ibg);
                if (i < g_midi_input_device_count - 1)
                {
                    SDL_SetRenderDrawColor(R, g_panel_border.r, g_panel_border.g, g_panel_border.b, 255);
                    SDL_RenderLine(R, ir.x, ir.y + ir.h, ir.x + ir.w, ir.y + ir.h);
                }
                draw_text(R, ir.x + 6, ir.y + 6, g_midi_device_name_cache[i], g_button_text);
                if (over && mclick)
                {
                    g_midi_input_device_index = i;
                    g_midi_input_device_dd_open = false; // reopen midi input with chosen device
                    midi_service_stop();
                    midi_input_shutdown();
                    midi_input_init("miniBAE", g_midi_device_api[i], g_midi_device_port[i]);
                    midi_service_start();
                    save_settings(g_current_bank_path[0] ? g_current_bank_path : NULL, *reverbType, *loopPlay);
                }
            }
        }
        if (mclick && !point_in(mx, my, midiDevRect) && !point_in(mx, my, box))
            g_midi_input_device_dd_open = false;
    }

    // MIDI output device dropdown
    // Don't render the output dropdown while the input dropdown is open
    if (g_midi_output_device_dd_open && !g_midi_input_device_dd_open)
    {
        int itemH = midiOutDevRect.h;
        int deviceCount = g_midi_output_device_count;
        if (deviceCount <= 0)
            deviceCount = 1; // show placeholder
        Rect box = {midiOutDevRect.x, midiOutDevRect.y + midiOutDevRect.h + 1, midiOutDevRect.w, itemH * deviceCount};
        SDL_Color ddBg = g_panel_bg;
        ddBg.a = 255;
        SDL_Color shadow = {0, 0, 0, g_is_dark_mode ? 120 : 90};
        Rect shadowRect = {box.x + 2, box.y + 2, box.w, box.h};
        draw_rect(R, shadowRect, shadow);
        draw_rect(R, box, ddBg);
        draw_frame(R, box, g_panel_border);
        if (g_midi_output_device_count == 0)
        { // placeholder
            Rect ir = {box.x, box.y, box.w, itemH};
            draw_rect(R, ir, g_panel_bg);
            draw_text(R, ir.x + 6, ir.y + 6, "No MIDI devices", g_text_color);
        }
        else
        {
            for (int i = 0; i < g_midi_output_device_count && i < 64; i++)
            {
                Rect ir = {box.x, box.y + i * itemH, box.w, itemH};
                bool over = point_in(mx, my, ir);
                SDL_Color ibg = (i == g_midi_output_device_index) ? g_highlight_color : g_panel_bg;
                if (over)
                    ibg = g_button_hover;
                draw_rect(R, ir, ibg);
                if (i < g_midi_output_device_count - 1)
                {
                    SDL_SetRenderDrawColor(R, g_panel_border.r, g_panel_border.g, g_panel_border.b, 255);
                    SDL_RenderLine(R, ir.x, ir.y + ir.h, ir.x + ir.w, ir.y + ir.h);
                }
                draw_text(R, ir.x + 6, ir.y + 6, g_midi_device_name_cache[g_midi_input_device_count + i], g_button_text);
                if (over && mclick)
                {
                    g_midi_output_device_index = i;
                    g_midi_output_device_dd_open = false; // reopen midi output with chosen device
                    // Silence previous device before switching
                    midi_output_send_all_notes_off();
                    midi_output_shutdown();
                    midi_output_init("miniBAE", g_midi_device_api[g_midi_input_device_count + i], g_midi_device_port[g_midi_input_device_count + i]);
                    // After opening, send current instrument table
                    if (g_bae.song)
                    {
                        for (unsigned char ch = 0; ch < 16; ++ch)
                        {
                            unsigned char program = 0, bank = 0;
                            if (BAESong_GetProgramBank(g_bae.song, ch, &program, &bank, TRUE) == BAE_NO_ERROR)
                            {
                                unsigned char buf[3];
                                buf[0] = (unsigned char)(0xB0 | (ch & 0x0F));
                                buf[1] = 0;
                                buf[2] = (unsigned char)(bank & 0x7F);
                                midi_output_send(buf, 3);
                                buf[0] = (unsigned char)(0xC0 | (ch & 0x0F));
                                buf[1] = (unsigned char)(program & 0x7F);
                                midi_output_send(buf, 2);
                            }
                        }
                    }
                    save_settings(g_current_bank_path[0] ? g_current_bank_path : NULL, *reverbType, *loopPlay);
                }
            }
        }
        if (mclick && !point_in(mx, my, midiOutDevRect) && !point_in(mx, my, box))
            g_midi_output_device_dd_open = false;
    }
#endif

    // Record Codec dropdown
#if SUPPORT_MIDI_HW == TRUE
    if (g_midiRecordFormatDropdownOpen || (recordCodecEnabled && point_in(mx, my, recordCodecRect) && mclick))
    {
        // Close other dropdowns when this one is about to open
        if (!g_midiRecordFormatDropdownOpen)
        {
            g_volumeCurveDropdownOpen = false;
            g_sampleRateDropdownOpen = false;
            g_exportDropdownOpen = false;
            g_midi_input_device_dd_open = false;
            g_midi_output_device_dd_open = false;
        }
        bool changed = ui_dropdown_two_column_above(R, recordCodecRect, &g_midiRecordFormatIndex, g_midiRecordFormatNames, g_midiRecordFormatCount, &g_midiRecordFormatDropdownOpen, mx, my, mdown, mclick);
        if (changed)
        {
            save_settings(g_current_bank_path[0] ? g_current_bank_path : NULL, *reverbType, *loopPlay);
        }
    }
#endif

    if (g_volumeCurveDropdownOpen)
    {
        int itemH = vcRect.h;
        int totalH = itemH * vcCount;
        Rect box = {vcRect.x, vcRect.y + vcRect.h + 1, vcRect.w, totalH};
        SDL_Color ddBg = g_panel_bg;
        ddBg.a = 255;
        SDL_Color shadow = {0, 0, 0, g_is_dark_mode ? 120 : 90};
        Rect shadowRect = {box.x + 2, box.y + 2, box.w, box.h};
        draw_rect(R, shadowRect, shadow);
        draw_rect(R, box, ddBg);
        draw_frame(R, box, g_panel_border);
        for (int i = 0; i < vcCount; i++)
        {
            Rect ir = {box.x, box.y + i * itemH, box.w, itemH};
            bool over = point_in(mx, my, ir);
            SDL_Color ibg = (i == g_volume_curve) ? g_highlight_color : g_panel_bg;
            if (over)
                ibg = g_button_hover;
            draw_rect(R, ir, ibg);
            if (i < vcCount - 1)
            {
                SDL_Color sep = g_panel_border;
                SDL_SetRenderDrawColor(R, sep.r, sep.g, sep.b, 255);
                SDL_RenderLine(R, ir.x, ir.y + ir.h, ir.x + ir.w, ir.y + ir.h);
            }
            SDL_Color itxt = (i == g_volume_curve || over) ? g_button_text : g_text_color;
            draw_text(R, ir.x + 6, ir.y + 6, volumeCurveNames[i], itxt);
            if (over && mclick)
            {
                g_volume_curve = i;
                g_volumeCurveDropdownOpen = false;
#if USE_SF2_SUPPORT == TRUE
                if (!BAESong_IsSF2Song(g_bae.song))
#endif                
                {
                    BAE_SetDefaultVelocityCurve(g_volume_curve);
                    if (g_bae.song && !g_bae.is_audio_file)
                    {
                        BAESong_SetVelocityCurve(g_bae.song, g_volume_curve);
                    }
#if SUPPORT_MIDI_HW == TRUE
                    if (g_live_song && g_midi_input_enabled)
                    {
                        BAESong_SetVelocityCurve(g_live_song, g_volume_curve);
                    }
#endif
                }
                save_settings(g_current_bank_path[0] ? g_current_bank_path : NULL, *reverbType, *loopPlay);
            }
        }
        if (mclick && !point_in(mx, my, vcRect) && !point_in(mx, my, box))
            g_volumeCurveDropdownOpen = false;
    }

    // Discard clicks outside dialog (after dropdown so it doesn't immediately close on open)
    if (mclick && !point_in(mx, my, dlg))
    { /* swallow */
    }
}

void settings_init(void)
{
    g_show_settings_dialog = false;
    g_volumeCurveDropdownOpen = false;
    g_sampleRateDropdownOpen = false;
    g_volume_curve = 0;
    g_stereo_output = true;
    g_sample_rate_hz = 44100;
}

void settings_cleanup(void)
{
    g_show_settings_dialog = false;
    g_volumeCurveDropdownOpen = false;
    g_sampleRateDropdownOpen = false;
    
    // Free custom reverb preset list
    if (g_custom_reverb_presets)
    {
        free(g_custom_reverb_presets);
        g_custom_reverb_presets = NULL;
    }
    g_custom_reverb_preset_count = 0;
}

// Custom reverb preset management
CustomReverbPreset *g_custom_reverb_presets = NULL;
int g_custom_reverb_preset_count = 0;
char g_current_custom_reverb_preset[64] = {0}; // Track which preset is currently loaded
int g_current_custom_reverb_lowpass = 64; // Track current lowpass (engine has no getter)
// Track the *intended* custom reverb values (so import/export/persistence round-trips exactly).
// Engine getters can quantize due to internal fixed-point conversions.
int g_current_custom_reverb_comb_count = 0; // 0 means "not yet synced"
int g_current_custom_reverb_delays[MAX_NEO_COMBS] = {0};
int g_current_custom_reverb_feedback[MAX_NEO_COMBS] = {0};
int g_current_custom_reverb_gain[MAX_NEO_COMBS] = {127, 127, 127, 127};

#if USE_NEO_EFFECTS
static void ensure_current_custom_reverb_state_synced_from_engine(void)
{
    if (g_current_custom_reverb_comb_count < 1)
    {
        g_current_custom_reverb_comb_count = GetNeoCustomReverbCombCount();
        if (g_current_custom_reverb_comb_count < 1)
            g_current_custom_reverb_comb_count = 1;
        if (g_current_custom_reverb_comb_count > MAX_NEO_COMBS)
            g_current_custom_reverb_comb_count = MAX_NEO_COMBS;

        for (int i = 0; i < MAX_NEO_COMBS; i++)
        {
            g_current_custom_reverb_delays[i] = GetNeoCustomReverbCombDelay(i);
            g_current_custom_reverb_feedback[i] = GetNeoCustomReverbCombFeedback(i);
            g_current_custom_reverb_gain[i] = GetNeoCustomReverbCombGain(i);
        }
    }
}
#endif

// Text input dialog state
bool g_show_preset_name_dialog = false;
char g_preset_name_input[64] = {0};
int g_preset_name_cursor = 0;

// Hard cap to keep the reverb dropdown manageable.
#define MAX_CUSTOM_REVERB_PRESETS 65

// Delete confirmation dialog state
bool g_show_preset_delete_confirm_dialog = false;
bool g_preset_delete_confirmed = false;
char g_preset_delete_name[64] = {0};

void load_custom_reverb_preset_list(void)
{
    // Free existing presets
    if (g_custom_reverb_presets)
    {
        free(g_custom_reverb_presets);
        g_custom_reverb_presets = NULL;
    }
    g_custom_reverb_preset_count = 0;

    char exe_dir[512];
    get_executable_directory(exe_dir, sizeof(exe_dir));

    char settings_path[768];
#ifdef _WIN32
    snprintf(settings_path, sizeof(settings_path), "%s\\zefidi.ini", exe_dir);
#else
    snprintf(settings_path, sizeof(settings_path), "%s/zefidi.ini", exe_dir);
#endif

    FILE *f = fopen(settings_path, "r");
    if (!f) return;

    // First pass: find maximum numeric preset index present in custom_reverb_%d_* keys
    int max_idx = -1;
    char line[512];
    while (fgets(line, sizeof(line), f))
    {
        // Strip newline and carriage return
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        char *cr = strchr(line, '\r');
        if (cr) *cr = '\0';

        if (strncmp(line, "custom_reverb_", 14) != 0)
            continue;

        const char *p = line + 14;
        if (!(*p >= '0' && *p <= '9'))
            continue;

        int idx = 0;
        while (*p >= '0' && *p <= '9')
        {
            idx = idx * 10 + (*p - '0');
            p++;
        }
        if (*p != '_')
            continue;

        if (idx > max_idx)
            max_idx = idx;
    }

    if (max_idx < 0)
    {
        fclose(f);
        return;
    }

    // Temporary dense array by numeric index; later compact to g_custom_reverb_presets
    CustomReverbPreset *tmp = (CustomReverbPreset *)calloc((size_t)max_idx + 1, sizeof(CustomReverbPreset));
    if (!tmp)
    {
        fclose(f);
        return;
    }

    for (int i = 0; i <= max_idx; i++)
    {
        tmp[i].comb_count = 4;
        for (int j = 0; j < MAX_NEO_COMBS; j++)
        {
            tmp[i].delays[j] = 0;
            tmp[i].feedback[j] = 0;
            tmp[i].gain[j] = 127;
        }
        tmp[i].lowpass = 64;
    }

    // Second pass: parse values
    rewind(f);
    while (fgets(line, sizeof(line), f))
    {
        // Strip newline and carriage return
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        char *cr = strchr(line, '\r');
        if (cr) *cr = '\0';

        if (strncmp(line, "custom_reverb_", 14) != 0)
            continue;

        const char *p = line + 14;
        if (!(*p >= '0' && *p <= '9'))
            continue;

        int idx = 0;
        while (*p >= '0' && *p <= '9')
        {
            idx = idx * 10 + (*p - '0');
            p++;
        }
        if (*p != '_' || idx < 0 || idx > max_idx)
            continue;

        p++; // skip '_'
        const char *eq = strchr(p, '=');
        if (!eq)
            continue;

        char key[64];
        size_t key_len = (size_t)(eq - p);
        if (key_len >= sizeof(key))
            key_len = sizeof(key) - 1;
        memcpy(key, p, key_len);
        key[key_len] = '\0';

        const char *value = eq + 1;

        if (strcmp(key, "name") == 0)
        {
            safe_strncpy(tmp[idx].name, value, sizeof(tmp[idx].name) - 1);
            tmp[idx].name[sizeof(tmp[idx].name) - 1] = '\0';
        }
        else if (strcmp(key, "comb_count") == 0)
        {
            tmp[idx].comb_count = atoi(value);
        }
        else if (strncmp(key, "delay_", 6) == 0)
        {
            int comb = atoi(key + 6);
            if (comb >= 0 && comb < MAX_NEO_COMBS)
                tmp[idx].delays[comb] = atoi(value);
        }
        else if (strncmp(key, "feedback_", 9) == 0)
        {
            int comb = atoi(key + 9);
            if (comb >= 0 && comb < MAX_NEO_COMBS)
                tmp[idx].feedback[comb] = atoi(value);
        }
        else if (strncmp(key, "gain_", 5) == 0)
        {
            int comb = atoi(key + 5);
            if (comb >= 0 && comb < MAX_NEO_COMBS)
                tmp[idx].gain[comb] = atoi(value);
        }
        else if (strcmp(key, "lowpass") == 0)
        {
            tmp[idx].lowpass = atoi(value);
        }
    }

    fclose(f);

    // Compact: only presets with a name count as valid presets
    int count = 0;
    for (int i = 0; i <= max_idx; i++)
    {
        if (tmp[i].name[0])
            count++;
    }

    if (count == 0)
    {
        free(tmp);
        return;
    }

    int capped_count = (count > MAX_CUSTOM_REVERB_PRESETS) ? MAX_CUSTOM_REVERB_PRESETS : count;

    g_custom_reverb_presets = (CustomReverbPreset *)malloc(sizeof(CustomReverbPreset) * (size_t)capped_count);
    if (!g_custom_reverb_presets)
    {
        free(tmp);
        return;
    }

    int out = 0;
    for (int i = 0; i <= max_idx; i++)
    {
        if (!tmp[i].name[0])
            continue;
        if (out >= capped_count)
            break;
        g_custom_reverb_presets[out++] = tmp[i];
    }
    g_custom_reverb_preset_count = out;
    free(tmp);
}

int get_custom_reverb_preset_index(const char *name)
{
    if (!name || !g_custom_reverb_presets) return -1;
    
    for (int i = 0; i < g_custom_reverb_preset_count; i++)
    {
        if (strcmp(g_custom_reverb_presets[i].name, name) == 0)
        {
            return i;
        }
    }
    return -1;
}

void save_custom_reverb_preset(const char *name)
{
#if USE_NEO_EFFECTS
    if (!name || !name[0]) return;

    // Allow overwriting by name, but prevent creating more than the hard cap.
    // (This keeps the dropdown list size bounded.)
    extern void set_status_message(const char *msg);
    
    // Read all existing content
    char exe_dir[512];
    get_executable_directory(exe_dir, sizeof(exe_dir));
    
    char settings_path[768];
#ifdef _WIN32
    snprintf(settings_path, sizeof(settings_path), "%s\\zefidi.ini", exe_dir);
#else
    snprintf(settings_path, sizeof(settings_path), "%s/zefidi.ini", exe_dir);
#endif
    
    // Read existing content to preserve settings and find max index
    char *content = NULL;
    size_t content_size = 0;
    FILE *f = fopen(settings_path, "r");
    if (f)
    {
        fseek(f, 0, SEEK_END);
        content_size = ftell(f);
        fseek(f, 0, SEEK_SET);
        content = (char *)malloc(content_size + 1);
        if (content)
        {
            fread(content, 1, content_size, f);
            content[content_size] = '\0';
        }
        fclose(f);
    }
    
    // Find existing preset index by name, or allocate next available
    int preset_idx = -1;
    int max_idx = -1;
    
    // First pass: scan file for existing preset with this name and find max index
    if (content)
    {
        char *scan_ptr = content;
        char line_buf[512];

        while (*scan_ptr)
        {
            // Read line into buffer
            int i = 0;
            while (*scan_ptr && *scan_ptr != '\n' && i < (int)sizeof(line_buf) - 1)
            {
                if (*scan_ptr != '\r')  // Skip \r characters
                    line_buf[i++] = *scan_ptr;
                scan_ptr++;
            }
            line_buf[i] = '\0';
            if (*scan_ptr == '\n') scan_ptr++;

            if (strncmp(line_buf, "custom_reverb_", 14) != 0)
                continue;

            const char *p = line_buf + 14;
            if (!(*p >= '0' && *p <= '9'))
                continue;

            int idx = 0;
            while (*p >= '0' && *p <= '9')
            {
                idx = idx * 10 + (*p - '0');
                p++;
            }
            if (*p != '_')
                continue;

            if (idx > max_idx)
                max_idx = idx;

            p++; // skip '_'
            if (strncmp(p, "name=", 5) == 0)
            {
                const char *val = p + 5;
                if (val[0] && strcmp(val, name) == 0)
                {
                    preset_idx = idx;
                }
            }
        }
    }
    
    // If preset not found by name, use next available index
    if (preset_idx < 0)
    {
        if (g_custom_reverb_preset_count >= MAX_CUSTOM_REVERB_PRESETS)
        {
            set_status_message("Too many custom reverb presets (max 65)");
            if (content) free(content);
            return;
        }
        preset_idx = max_idx + 1;
    }
    
    // Rewrite file, excluding old preset data for this index
    f = fopen(settings_path, "w");
    if (!f)
    {
        if (content) free(content);
        return;
    }
    
    // Write existing content, excluding lines for this preset index
    if (content)
    {
        char prefix[64];
        snprintf(prefix, sizeof(prefix), "custom_reverb_%d_", preset_idx);
        int prefix_len = strlen(prefix);
        
        char *write_ptr = content;
        char line_buf[512];
        
        while (*write_ptr)
        {
            // Read line into buffer
            int i = 0;
            while (*write_ptr && *write_ptr != '\n' && i < sizeof(line_buf) - 1)
            {
                if (*write_ptr != '\r')  // Skip \r characters
                    line_buf[i++] = *write_ptr;
                write_ptr++;
            }
            line_buf[i] = '\0';
            if (*write_ptr == '\n') write_ptr++;
            
            // Skip lines that match this preset index
            if (line_buf[0] && strncmp(line_buf, prefix, prefix_len) != 0)
            {
                fprintf(f, "%s\n", line_buf);
            }
        }
        free(content);
    }
    
    // Append new preset data
    fprintf(f, "custom_reverb_%d_name=%s\n", preset_idx, name);
    ensure_current_custom_reverb_state_synced_from_engine();
    fprintf(f, "custom_reverb_%d_comb_count=%d\n", preset_idx, g_current_custom_reverb_comb_count);
    for (int i = 0; i < MAX_NEO_COMBS; i++)
    {
        fprintf(f, "custom_reverb_%d_delay_%d=%d\n", preset_idx, i, g_current_custom_reverb_delays[i]);
        fprintf(f, "custom_reverb_%d_feedback_%d=%d\n", preset_idx, i, g_current_custom_reverb_feedback[i]);
        fprintf(f, "custom_reverb_%d_gain_%d=%d\n", preset_idx, i, g_current_custom_reverb_gain[i]);
    }
    fprintf(f, "custom_reverb_%d_lowpass=%d\n", preset_idx, g_current_custom_reverb_lowpass);
    
    fclose(f);
    
    // Update current preset name
    safe_strncpy(g_current_custom_reverb_preset, name, sizeof(g_current_custom_reverb_preset) - 1);
    g_current_custom_reverb_preset[sizeof(g_current_custom_reverb_preset) - 1] = '\0';
    
    // Reload preset list
    load_custom_reverb_preset_list();
#endif
}

void load_custom_reverb_preset(const char *name)
{
#if USE_NEO_EFFECTS
    if (!name || !name[0]) return;
    
    int idx = get_custom_reverb_preset_index(name);
    if (idx < 0) return;
    
    CustomReverbPreset *preset = &g_custom_reverb_presets[idx];

    // Update tracked state first (so UI/export/persistence round-trips the original ints).
    g_current_custom_reverb_comb_count = preset->comb_count;
    if (g_current_custom_reverb_comb_count < 1)
        g_current_custom_reverb_comb_count = 1;
    if (g_current_custom_reverb_comb_count > MAX_NEO_COMBS)
        g_current_custom_reverb_comb_count = MAX_NEO_COMBS;
    for (int i = 0; i < MAX_NEO_COMBS; i++)
    {
        g_current_custom_reverb_delays[i] = preset->delays[i];
        g_current_custom_reverb_feedback[i] = preset->feedback[i];
        g_current_custom_reverb_gain[i] = preset->gain[i];
    }

    SetNeoCustomReverbCombCount(preset->comb_count);
    
    for (int i = 0; i < MAX_NEO_COMBS; i++)
    {
        SetNeoCustomReverbCombDelay(i, preset->delays[i]);
        SetNeoCustomReverbCombFeedback(i, preset->feedback[i]);
        SetNeoCustomReverbCombGain(i, preset->gain[i]);
    }

    g_current_custom_reverb_lowpass = preset->lowpass;
    SetNeoCustomReverbLowpass(g_current_custom_reverb_lowpass);
    
    // Update current preset name
    safe_strncpy(g_current_custom_reverb_preset, name, sizeof(g_current_custom_reverb_preset) - 1);
    g_current_custom_reverb_preset[sizeof(g_current_custom_reverb_preset) - 1] = '\0';

    // Force the custom reverb dialog to refresh its cached slider values
    extern int g_custom_reverb_dialog_sync_serial;
    g_custom_reverb_dialog_sync_serial++;
#endif
}

static void xml_escape(const char *in, char *out, size_t out_size)
{
    if (!out || out_size == 0) return;
    out[0] = '\0';
    if (!in) return;

    size_t o = 0;
    for (size_t i = 0; in[i] && o + 1 < out_size; i++)
    {
        const char *rep = NULL;
        switch (in[i])
        {
            case '&': rep = "&amp;"; break;
            case '<': rep = "&lt;"; break;
            case '>': rep = "&gt;"; break;
            case '"': rep = "&quot;"; break;
            case '\'': rep = "&apos;"; break;
            default: rep = NULL; break;
        }
        if (rep)
        {
            size_t rl = strlen(rep);
            if (o + rl >= out_size) break;
            memcpy(out + o, rep, rl);
            o += rl;
            out[o] = '\0';
        }
        else
        {
            out[o++] = in[i];
            out[o] = '\0';
        }
    }
}

static void xml_unescape_inplace(char *s)
{
    if (!s) return;

    char *w = s;
    for (char *r = s; *r; )
    {
        if (*r == '&')
        {
            if (strncmp(r, "&amp;", 5) == 0) { *w++ = '&'; r += 5; continue; }
            if (strncmp(r, "&lt;", 4) == 0) { *w++ = '<'; r += 4; continue; }
            if (strncmp(r, "&gt;", 4) == 0) { *w++ = '>'; r += 4; continue; }
            if (strncmp(r, "&quot;", 6) == 0) { *w++ = '"'; r += 6; continue; }
            if (strncmp(r, "&apos;", 6) == 0) { *w++ = '\''; r += 6; continue; }
        }
        *w++ = *r++;
    }
    *w = '\0';
}

static bool xml_get_tag_text(const char *xml, const char *tag, char *out, size_t out_size)
{
    if (!xml || !tag || !out || out_size == 0) return false;
    out[0] = '\0';

    char open[64];
    char close[64];
    snprintf(open, sizeof(open), "<%s>", tag);
    snprintf(close, sizeof(close), "</%s>", tag);

    const char *s = strstr(xml, open);
    if (!s) return false;
    s += strlen(open);
    const char *e = strstr(s, close);
    if (!e || e <= s) return false;
    size_t len = (size_t)(e - s);
    if (len >= out_size) len = out_size - 1;
    memcpy(out, s, len);
    out[len] = '\0';
    xml_unescape_inplace(out);
    return true;
}

bool export_custom_reverb_neoreverb(const char *preset_name, const char *path)
{
#if USE_NEO_EFFECTS
    if (!preset_name || !preset_name[0] || !path || !path[0]) return false;

    FILE *f = fopen(path, "wb");
    if (!f) return false;

    char esc_name[256];
    xml_escape(preset_name, esc_name, sizeof(esc_name));

    fprintf(f, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    fprintf(f, "<neoreverb version=\"1\">\n");
    fprintf(f, "  <name>%s</name>\n", esc_name);
    ensure_current_custom_reverb_state_synced_from_engine();
    fprintf(f, "  <combCount>%d</combCount>\n", g_current_custom_reverb_comb_count);
    fprintf(f, "  <lowpass>%d</lowpass>\n", g_current_custom_reverb_lowpass);
    for (int i = 0; i < MAX_NEO_COMBS; i++)
    {
        fprintf(f,
                "  <comb index=\"%d\" delayMs=\"%d\" feedback=\"%d\" gain=\"%d\"/>\n",
                i,
                g_current_custom_reverb_delays[i],
                g_current_custom_reverb_feedback[i],
                g_current_custom_reverb_gain[i]);
    }
    fprintf(f, "</neoreverb>\n");
    fclose(f);
    return true;
#else
    (void)preset_name;
    (void)path;
    return false;
#endif
}

bool import_custom_reverb_neoreverb(const char *path, char *out_preset_name, size_t out_preset_name_size)
{
#if USE_NEO_EFFECTS
    if (out_preset_name && out_preset_name_size)
        out_preset_name[0] = '\0';
    if (!path || !path[0]) return false;

    FILE *f = fopen(path, "rb");
    if (!f) return false;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > (1024 * 1024))
    {
        fclose(f);
        return false;
    }

    char *xml = (char *)malloc((size_t)sz + 1);
    if (!xml)
    {
        fclose(f);
        return false;
    }
    size_t rd = fread(xml, 1, (size_t)sz, f);
    fclose(f);
    xml[rd] = '\0';

    CustomReverbPreset preset;
    memset(&preset, 0, sizeof(preset));
    preset.comb_count = 4;
    for (int i = 0; i < MAX_NEO_COMBS; i++)
    {
        preset.delays[i] = 0;
        preset.feedback[i] = 0;
        preset.gain[i] = 127;
    }
    preset.lowpass = 64;

    if (!xml_get_tag_text(xml, "name", preset.name, sizeof(preset.name)))
    {
        free(xml);
        return false;
    }

    char buf[64];
    if (xml_get_tag_text(xml, "combCount", buf, sizeof(buf)))
        preset.comb_count = atoi(buf);
    if (xml_get_tag_text(xml, "lowpass", buf, sizeof(buf)))
        preset.lowpass = atoi(buf);

    // Parse <comb .../> entries
    const char *p = xml;
    while ((p = strstr(p, "<comb")) != NULL)
    {
        int index = -1, delayMs = -1, feedback = -1, gain = -1;
        if (sscanf(p, "<comb index=\"%d\" delayMs=\"%d\" feedback=\"%d\" gain=\"%d\"",
                   &index, &delayMs, &feedback, &gain) == 4)
        {
            if (index >= 0 && index < MAX_NEO_COMBS)
            {
                preset.delays[index] = delayMs;
                preset.feedback[index] = feedback;
                preset.gain[index] = gain;
            }
        }
        p += 5;
    }

    free(xml);

    // Enforce max presets unless overwriting by name
    int existing = get_custom_reverb_preset_index(preset.name);
    if (existing < 0 && g_custom_reverb_preset_count >= MAX_CUSTOM_REVERB_PRESETS)
    {
        set_status_message("Too many custom reverb presets (max 65)");
        return false;
    }

    // Apply to engine then save using existing persistence format
    g_current_custom_reverb_comb_count = preset.comb_count;
    if (g_current_custom_reverb_comb_count < 1)
        g_current_custom_reverb_comb_count = 1;
    if (g_current_custom_reverb_comb_count > MAX_NEO_COMBS)
        g_current_custom_reverb_comb_count = MAX_NEO_COMBS;
    SetNeoCustomReverbCombCount(g_current_custom_reverb_comb_count);
    for (int i = 0; i < MAX_NEO_COMBS; i++)
    {
        g_current_custom_reverb_delays[i] = preset.delays[i];
        g_current_custom_reverb_feedback[i] = preset.feedback[i];
        g_current_custom_reverb_gain[i] = preset.gain[i];
        SetNeoCustomReverbCombDelay(i, g_current_custom_reverb_delays[i]);
        SetNeoCustomReverbCombFeedback(i, g_current_custom_reverb_feedback[i]);
        SetNeoCustomReverbCombGain(i, g_current_custom_reverb_gain[i]);
    }
    g_current_custom_reverb_lowpass = preset.lowpass;
    SetNeoCustomReverbLowpass(g_current_custom_reverb_lowpass);
    save_custom_reverb_preset(preset.name);

    if (out_preset_name && out_preset_name_size)
        safe_strncpy(out_preset_name, preset.name, out_preset_name_size);
    return true;
#else
    (void)path;
    if (out_preset_name && out_preset_name_size)
        out_preset_name[0] = '\0';
    return false;
#endif
}

void delete_custom_reverb_preset(const char *name)
{
    if (!name || !name[0]) return;

    // Find the numeric preset index in the file by matching custom_reverb_%d_name
    int preset_file_idx = -1;
    
    // Read all existing content
    char exe_dir[512];
    get_executable_directory(exe_dir, sizeof(exe_dir));
    
    char settings_path[768];
#ifdef _WIN32
    snprintf(settings_path, sizeof(settings_path), "%s\\zefidi.ini", exe_dir);
#else
    snprintf(settings_path, sizeof(settings_path), "%s/zefidi.ini", exe_dir);
#endif
    
    // Read existing content
    char *content = NULL;
    size_t content_size = 0;
    FILE *f = fopen(settings_path, "r");
    if (f)
    {
        fseek(f, 0, SEEK_END);
        content_size = ftell(f);
        fseek(f, 0, SEEK_SET);
        content = (char *)malloc(content_size + 1);
        if (content)
        {
            fread(content, 1, content_size, f);
            content[content_size] = '\0';
        }
        fclose(f);
    }

    if (!content) return;

    // Scan for matching name to discover numeric index
    {
        char *scan_ptr = content;
        char line_buf[512];
        while (*scan_ptr)
        {
            int i = 0;
            while (*scan_ptr && *scan_ptr != '\n' && i < (int)sizeof(line_buf) - 1)
            {
                if (*scan_ptr != '\r')
                    line_buf[i++] = *scan_ptr;
                scan_ptr++;
            }
            line_buf[i] = '\0';
            if (*scan_ptr == '\n') scan_ptr++;

            if (strncmp(line_buf, "custom_reverb_", 14) == 0)
            {
                const char *p = line_buf + 14;
                if (*p >= '0' && *p <= '9')
                {
                    int idx = 0;
                    while (*p >= '0' && *p <= '9')
                    {
                        idx = idx * 10 + (*p - '0');
                        p++;
                    }
                    if (*p == '_' && strncmp(p + 1, "name=", 5) == 0)
                    {
                        const char *val = p + 1 + 5;
                        if (strcmp(val, name) == 0)
                        {
                            preset_file_idx = idx;
                            break;
                        }
                    }
                }
            }
        }
    }

    if (preset_file_idx < 0)
    {
        free(content);
        return;
    }
    
    // Rewrite file, skipping lines for this preset index
    f = fopen(settings_path, "w");
    if (!f)
    {
        free(content);
        return;
    }
    
    char prefix[64];
    snprintf(prefix, sizeof(prefix), "custom_reverb_%d_", preset_file_idx);
    int prefix_len = strlen(prefix);

    // Manual parsing (avoid strtok, strip \r)
    char *write_ptr = content;
    char line_buf[512];
    while (*write_ptr)
    {
        int i = 0;
        while (*write_ptr && *write_ptr != '\n' && i < (int)sizeof(line_buf) - 1)
        {
            if (*write_ptr != '\r')
                line_buf[i++] = *write_ptr;
            write_ptr++;
        }
        line_buf[i] = '\0';
        if (*write_ptr == '\n') write_ptr++;

        if (line_buf[0] && strncmp(line_buf, prefix, prefix_len) != 0)
        {
            fprintf(f, "%s\n", line_buf);
        }
    }

    free(content);
    fclose(f);
    
    // Clear current preset name if we just deleted it
    if (strcmp(g_current_custom_reverb_preset, name) == 0)
    {
        g_current_custom_reverb_preset[0] = '\0';
    }
    
    // Reload preset list
    load_custom_reverb_preset_list();
}

void render_preset_name_dialog(SDL_Renderer *R, int mx, int my, bool mclick, bool mdown, int window_h)
{
    if (!g_show_preset_name_dialog) return;
    
    int dlg_w = 300;
    int dlg_h = 120;
    
    Rect dlg = {(WINDOW_W - dlg_w) / 2, (window_h - dlg_h) / 2, dlg_w, dlg_h};
    
    // Shadow
    SDL_Color shadow = {0, 0, 0, 128};
    Rect shadowRect = {dlg.x + 3, dlg.y + 3, dlg.w, dlg.h};
    draw_rect(R, shadowRect, shadow);
    
    // Dialog background
    draw_rect(R, dlg, g_panel_bg);
    draw_frame(R, dlg, g_panel_border);
    
    // Title
    draw_text(R, dlg.x + 10, dlg.y + 10, "Enter Preset Name:", g_text_color);
    
    // Text input box
    Rect inputBox = {dlg.x + 10, dlg.y + 35, dlg.w - 20, 24};
    draw_rect(R, inputBox, g_bg_color);
    draw_frame(R, inputBox, g_panel_border);
    draw_text(R, inputBox.x + 4, inputBox.y + 4, g_preset_name_input, g_text_color);
    
    // Cursor
    int cursor_x = inputBox.x + 4 + (g_preset_name_cursor * 7); // Approximate char width
    SDL_SetRenderDrawColor(R, g_text_color.r, g_text_color.g, g_text_color.b, g_text_color.a);
    SDL_RenderLine(R, cursor_x, inputBox.y + 4, cursor_x, inputBox.y + inputBox.h - 4);
    
    // Buttons
    Rect okBtn = {dlg.x + dlg.w - 160, dlg.y + dlg.h - 35, 70, 25};
    Rect cancelBtn = {dlg.x + dlg.w - 80, dlg.y + dlg.h - 35, 70, 25};
    
    bool overOk = point_in(mx, my, okBtn);
    bool overCancel = point_in(mx, my, cancelBtn);
    
    SDL_Color ok_bg = overOk ? g_button_hover : g_button_base;
    SDL_Color cancel_bg = overCancel ? g_button_hover : g_button_base;
    
    draw_rect(R, okBtn, ok_bg);
    draw_frame(R, okBtn, g_button_border);
    draw_text(R, okBtn.x + 20, okBtn.y + 5, "OK", g_button_text);
    
    draw_rect(R, cancelBtn, cancel_bg);
    draw_frame(R, cancelBtn, g_button_border);
    draw_text(R, cancelBtn.x + 10, cancelBtn.y + 5, "Cancel", g_button_text);
    
    if (mclick)
    {
        if (overOk && g_preset_name_input[0])
        {
            save_custom_reverb_preset(g_preset_name_input);
            g_show_preset_name_dialog = false;
            memset(g_preset_name_input, 0, sizeof(g_preset_name_input));
            g_preset_name_cursor = 0;
        }
        else if (overCancel)
        {
            g_show_preset_name_dialog = false;
            memset(g_preset_name_input, 0, sizeof(g_preset_name_input));
            g_preset_name_cursor = 0;
        }
    }
}

void render_preset_delete_confirm_dialog(SDL_Renderer *R, int mx, int my, bool mclick, bool mdown, int window_h)
{
    (void)mdown;

    if (!g_show_preset_delete_confirm_dialog) return;

    int dlg_w = 360;
    int dlg_h = 130;

    Rect dlg = {(WINDOW_W - dlg_w) / 2, (window_h - dlg_h) / 2, dlg_w, dlg_h};

    // Shadow
    SDL_Color shadow = (SDL_Color){0, 0, 0, 128};
    Rect shadowRect = (Rect){dlg.x + 3, dlg.y + 3, dlg.w, dlg.h};
    draw_rect(R, shadowRect, shadow);

    // Dialog background
    draw_rect(R, dlg, g_panel_bg);
    draw_frame(R, dlg, g_panel_border);

    // Title + message
    draw_text(R, dlg.x + 10, dlg.y + 10, "Delete Preset?", g_text_color);

    char msg[256];
    if (g_preset_delete_name[0])
        snprintf(msg, sizeof(msg), "Delete preset \"%s\"?", g_preset_delete_name);
    else
        snprintf(msg, sizeof(msg), "Delete preset?");
    draw_text(R, dlg.x + 10, dlg.y + 38, msg, g_text_color);

    // Buttons
    Rect cancelBtn = (Rect){dlg.x + dlg.w - 160, dlg.y + dlg.h - 35, 70, 25};
    Rect deleteBtn = (Rect){dlg.x + dlg.w - 80, dlg.y + dlg.h - 35, 70, 25};

    bool overCancel = point_in(mx, my, cancelBtn);
    bool overDelete = point_in(mx, my, deleteBtn);

    SDL_Color cancel_bg = overCancel ? g_button_hover : g_button_base;
    SDL_Color delete_bg = overDelete ? g_button_hover : g_button_base;

    draw_rect(R, cancelBtn, cancel_bg);
    draw_frame(R, cancelBtn, g_button_border);
    draw_text(R, cancelBtn.x + 10, cancelBtn.y + 5, "Cancel", g_button_text);

    draw_rect(R, deleteBtn, delete_bg);
    draw_frame(R, deleteBtn, g_button_border);
    draw_text(R, deleteBtn.x + 10, deleteBtn.y + 5, "Delete", g_button_text);

    if (mclick)
    {
        if (overDelete)
        {
            g_preset_delete_confirmed = true;
            g_show_preset_delete_confirm_dialog = false;
        }
        else if (overCancel)
        {
            g_show_preset_delete_confirm_dialog = false;
            g_preset_delete_confirmed = false;
            memset(g_preset_delete_name, 0, sizeof(g_preset_delete_name));
        }
    }
}
