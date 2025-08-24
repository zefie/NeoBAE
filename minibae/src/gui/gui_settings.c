// gui_settings.c - Settings management and persistence

#include "gui_settings.h"
#include "gui_bae.h"
#include "gui_export.h"
#include "gui_widgets.h"
#include "gui_text.h"
#include "gui_theme.h"
#include "gui_common.h"
#include "gui_midi.h"
#include "gui_playlist.h"
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
bool g_disable_webtv_progress_bar = false;
extern int g_window_h;

// External function we need
extern void get_executable_directory(char *dir, size_t size);

Settings load_settings(void)
{
    Settings settings = {0};

    char exe_dir[512];
    get_executable_directory(exe_dir, sizeof(exe_dir));

    char settings_path[768];
#ifdef _WIN32
    snprintf(settings_path, sizeof(settings_path), "%s\\minibae.ini", exe_dir);
#else
    snprintf(settings_path, sizeof(settings_path), "%s/minibae.ini", exe_dir);
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
            strncpy(settings.bank_path, line + 10, sizeof(settings.bank_path) - 1);
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
    snprintf(settings_path, sizeof(settings_path), "%s\\minibae.ini", exe_dir);
#else
    snprintf(settings_path, sizeof(settings_path), "%s/minibae.ini", exe_dir);
#endif

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
        fprintf(f, "shuffle_enabled=%d\n", g_playlist.shuffle_enabled ? 1 : 0);
        fprintf(f, "repeat_mode=%d\n", g_playlist.repeat_mode);
        
        // Save window position if available
        extern SDL_Window *g_main_window;
        if (g_main_window)
        {
            int x, y;
            SDL_GetWindowPosition(g_main_window, &x, &y);
            fprintf(f, "window_x=%d\n", x);
            fprintf(f, "window_y=%d\n", y);
        }
        
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
    snprintf(settings_path, sizeof(settings_path), "%s\\minibae.ini", exe_dir);
#else
    snprintf(settings_path, sizeof(settings_path), "%s/minibae.ini", exe_dir);
#endif

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
        if (settings->has_window_pos)
        {
            fprintf(f, "window_x=%d\n", settings->window_x);
            fprintf(f, "window_y=%d\n", settings->window_y);
        }
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
    if (settings->has_shuffle)
    {
        g_playlist.shuffle_enabled = settings->shuffle_enabled;
    }
    if (settings->has_repeat)
    {
        g_playlist.repeat_mode = settings->repeat_mode;
    }
}

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
    }

    // Two-column geometry
    int colW = (dlg.w - pad * 3) / 2; // two columns with padding between
    int leftX = dlg.x + pad;
    int rightX = dlg.x + pad * 2 + colW;
    int controlW = 150;
    int controlRightX = leftX + colW - controlW; // dropdowns right-aligned in left column

    // Left column controls (stacked)
    // Volume Curve selector
    draw_text(R, leftX, dlg.y + 36, "Volume Curve:", g_text_color);
    const char *volumeCurveNames[] = {"Default S Curve", "Peaky S Curve", "WebTV Curve", "2x Exponential", "2x Linear"};
    int vcCount = 5;
    Rect vcRect = {controlRightX, dlg.y + 32, controlW, 24};
    SDL_Color dd_bg = g_button_base;
    SDL_Color dd_txt = g_button_text;
    SDL_Color dd_frame = g_button_border;
    if (point_in(mx, my, vcRect))
        dd_bg = g_button_hover;
    draw_rect(R, vcRect, dd_bg);
    draw_frame(R, vcRect, dd_frame);
    const char *vcCur = (g_volume_curve >= 0 && g_volume_curve < vcCount) ? volumeCurveNames[g_volume_curve] : "?";
    draw_text(R, vcRect.x + 6, vcRect.y + 3, vcCur, dd_txt);
    draw_text(R, vcRect.x + vcRect.w - 16, vcRect.y + 3, g_volumeCurveDropdownOpen ? "^" : "v", dd_txt);
    if (point_in(mx, my, vcRect) && mclick)
    {
        g_volumeCurveDropdownOpen = !g_volumeCurveDropdownOpen;
        if (g_volumeCurveDropdownOpen)
        {
            g_sampleRateDropdownOpen = false;
            g_exportDropdownOpen = false;
#ifdef SUPPORT_MIDI_HW
            g_midi_input_device_dd_open = false;
            g_midi_output_device_dd_open = false;
#endif
        }
    }

    // Sample Rate selector
    draw_text(R, leftX, dlg.y + 72, "Sample Rate:", g_text_color);
    const int sampleRates[] = {8000, 11025, 16000, 22050, 32000, 44100, 48000};
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
    Rect srRect = {controlRightX, dlg.y + 68, controlW, 24};
    bool sampleRateEnabled = !g_volumeCurveDropdownOpen;
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
#ifdef SUPPORT_MIDI_HW
            g_midi_input_device_dd_open = false;
            g_midi_output_device_dd_open = false;
#endif
        }
    }

    // Export codec selector (left column, below sample rate)
#if USE_MPEG_ENCODER != FALSE
    Rect expRect = {controlRightX, dlg.y + 104, controlW, 24};
    draw_text(R, leftX, dlg.y + 108, "Export Codec:", g_text_color);
    bool exportEnabled = !g_volumeCurveDropdownOpen && !g_sampleRateDropdownOpen;
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
#ifdef SUPPORT_MIDI_HW
            g_midi_input_device_dd_open = false;
            g_midi_output_device_dd_open = false;
#endif
        }
    }
#endif
#ifdef SUPPORT_MIDI_HW
    // MIDI input enable checkbox and device selector (left column, below Export)
    Rect midiEnRect = {leftX, dlg.y + 140, 18, 18};
    if (ui_toggle(R, midiEnRect, &g_midi_input_enabled, "MIDI Input", mx, my, mclick))
    {
        // initialize or shutdown midi input as requested
        if (g_midi_input_enabled)
        {
            // Start background service first (so events queue safely as soon as RtMidi is opened)
            midi_service_start();
            /* Apply remembered master volume intent via bae_set_volume so the
               same normalization used for song loads is applied to the live
               synth and mixer before ports are opened or events arrive. */
            if (volume)
            {
                bae_set_volume(*volume);
            }
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
                    /* Use bae_set_volume so per-type normalization is applied to
                       the newly created live synth and mixer state. */
                    if (volume)
                    {
                        bae_set_volume(*volume);
                    }
                }
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
                for (int _i = 0; _i < 3; ++_i)
                {
                    BAEMixer_Idle(g_bae.mixer);
                    BAEMixer_ServiceStreams(g_bae.mixer);
                }
            }
            /* Re-apply stored master volume intent after MIDI input is opened.
               Call bae_set_volume to ensure the same normalization/boost
               behavior used for loaded songs is also used for MIDI-in. */
            if (volume)
            {
                bae_set_volume(*volume);
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
    Rect midiDevRect = {controlRightX, dlg.y + 136, controlW + 200, 24};
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
                            strncpy(g_midi_device_name_cache[g_midi_device_count], full, sizeof(g_midi_device_name_cache[g_midi_device_count]) - 1);
                        }
                        else
                        {
                            strncpy(g_midi_device_name_cache[g_midi_device_count], buf, sizeof(g_midi_device_name_cache[g_midi_device_count]) - 1);
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
                            strncpy(g_midi_device_name_cache[g_midi_device_count], full, sizeof(g_midi_device_name_cache[g_midi_device_count]) - 1);
                        }
                        else
                        {
                            strncpy(g_midi_device_name_cache[g_midi_device_count], buf, sizeof(g_midi_device_name_cache[g_midi_device_count]) - 1);
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
    // Allow input UI to be active unless other dropdowns (sample rate, volume curve, export) are open.
    bool midiInputEnabled = !(g_volumeCurveDropdownOpen || g_sampleRateDropdownOpen || g_exportDropdownOpen);
    // MIDI output should be disabled whenever the MIDI input dropdown is open (so only one of them can be active at once)
    bool midiOutputEnabled = !(g_volumeCurveDropdownOpen || g_sampleRateDropdownOpen || g_exportDropdownOpen || g_midi_input_device_dd_open);
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
        }
    }

    // MIDI output checkbox and device selector (placed next to input)
    Rect midiOutEnRect = {leftX, dlg.y + 168, 18, 18};
    // Disable MIDI Output toggle while exporting or when export dropdown is open
    bool midiOut_toggle_allowed = !g_exporting && !g_exportDropdownOpen;
    if (!midiOut_toggle_allowed)
    {
        // Draw disabled (dimmed) checkbox and label but do not allow toggling
        bool over = point_in(mx, my, midiOutEnRect);
        draw_custom_checkbox(R, midiOutEnRect, g_midi_output_enabled, over);
        SDL_Color txt = g_text_color;
        txt.a = 160;
        draw_text(R, midiOutEnRect.x + midiOutEnRect.w + 6, midiOutEnRect.y + 2, "MIDI Output", txt);
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
                        if (BAESong_GetProgramBank(g_bae.song, ch, &program, &bank) == BAE_NO_ERROR)
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
    Rect midiOutDevRect = {controlRightX, dlg.y + 164, controlW + 200, 24};
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
        }
    }
#endif
    // Right column controls (checkboxes)
    Rect cbRect = {rightX, dlg.y + 36, 18, 18};
    if (ui_toggle(R, cbRect, &g_stereo_output, "Stereo Output", mx, my, mclick))
    {
        int prePosMs = bae_get_pos_ms();
        bool wasPlayingBefore = g_bae.is_playing;
        if (recreate_mixer_and_restore(g_sample_rate_hz, g_stereo_output, *reverbType, *transpose, *tempo, *volume, *loopPlay, ch_enable))
        {
            if (wasPlayingBefore)
            {
                *progress = bae_get_pos_ms();
                *duration = bae_get_len_ms();
            }
            else
            {
                if (prePosMs > 0)
                {
                    bae_seek_ms(prePosMs);
                    *progress = prePosMs;
                    *duration = bae_get_len_ms();
                }
                else
                {
                    *progress = 0;
                    *duration = bae_get_len_ms();
                }
                *playing = false;
            }
#ifdef SUPPORT_MIDI_HW
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

    Rect wtvRect = {rightX, dlg.y + 108, 18, 18};
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
                SDL_RenderDrawLine(R, ir.x, ir.y + ir.h, ir.x + ir.w, ir.y + ir.h);
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
                    int prePosMs = bae_get_pos_ms();
                    bool wasPlayingBefore = g_bae.is_playing;
                    if (recreate_mixer_and_restore(g_sample_rate_hz, g_stereo_output, *reverbType, *transpose, *tempo, *volume, *loopPlay, ch_enable))
                    {
                        if (wasPlayingBefore)
                        {
                            *progress = bae_get_pos_ms();
                            *duration = bae_get_len_ms();
                        }
                        else if (prePosMs > 0)
                        {
                            bae_seek_ms(prePosMs);
                            *progress = prePosMs;
                            *duration = bae_get_len_ms();
                            *playing = false;
                        }
                        else
                        {
                            *progress = 0;
                            *duration = bae_get_len_ms();
                            *playing = false;
                        }
#ifdef SUPPORT_MIDI_HW
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
#ifdef SUPPORT_MIDI_HW
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
                    SDL_RenderDrawLine(R, ir.x, ir.y + ir.h, ir.x + ir.w, ir.y + ir.h);
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
                    SDL_RenderDrawLine(R, ir.x, ir.y + ir.h, ir.x + ir.w, ir.y + ir.h);
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
                            if (BAESong_GetProgramBank(g_bae.song, ch, &program, &bank) == BAE_NO_ERROR)
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
                SDL_RenderDrawLine(R, ir.x, ir.y + ir.h, ir.x + ir.w, ir.y + ir.h);
            }
            SDL_Color itxt = (i == g_volume_curve || over) ? g_button_text : g_text_color;
            draw_text(R, ir.x + 6, ir.y + 6, volumeCurveNames[i], itxt);
            if (over && mclick)
            {
                g_volume_curve = i;
                g_volumeCurveDropdownOpen = false;
                BAE_SetDefaultVelocityCurve(g_volume_curve);
                if (g_bae.song && !g_bae.is_audio_file)
                {
                    BAESong_SetVelocityCurve(g_bae.song, g_volume_curve);
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
}
