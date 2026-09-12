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

// gui_settings.c - Settings management and persistence

#include "BAE_API.h"
#include "NeoBAE.h"
#include "NeoBAEConfigPath.h"
#include "gui_settings.h"
#include "gui_bae.h"
#include "gui_export.h"
#include "gui_widgets.h"
#include "gui_text.h"
#include "gui_theme.h"
#include "gui_common.h"
#include "gui_midi.h"
#include "gui_playlist.h"
#include "gui_panels.h"
#include "GenPriv.h"
#if USE_XMF_SUPPORT == TRUE && (_USING_FLUIDLITE == TRUE || USE_NATIVE_DLS == TRUE)
#include "GenXMF.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if USE_NATIVE_DLS == TRUE
    #include "GenDLS_MobileBAE.h"
    bool g_use_dls_compatiblity_mode = true; /* default on for new installs */
#endif

extern bool g_dls_compat_tooltip_visible;
extern Rect g_dls_compat_tooltip_rect;
extern char g_dls_compat_tooltip_text[520];

// Settings dialog state (kept in sync with the popup window)
bool g_show_settings_dialog = false;

static SDL_Window *g_settings_window = NULL;
static SDL_Renderer *g_settings_renderer = NULL;
static int g_settings_mx = -10000;
static int g_settings_my = -10000;
static bool g_settings_mdown = false;
static bool g_settings_mclick = false;
static int g_settings_wheel_delta = 0;

typedef struct {
    int w, h;
    int lfoOff, terpOff, bitOff, stereoOff;
#if SUPPORT_MIDI_HW == TRUE
    int midiOff;
#endif
} SettingsDlgMetrics;

static SettingsDlgMetrics settings_metrics(void)
{
    SettingsDlgMetrics m;
    m.w = 560;
    /* Right column: checkboxes through chorus, then LFO, interpolation,
     * 16-bit, and stereo. MIDI device rows sit below that stack. */
    m.lfoOff = 216;
#if BAE_CLASSIC_CHORUS != TRUE
    m.lfoOff = 180;
#endif
    m.terpOff = m.lfoOff + 32;
    m.bitOff = m.terpOff + 28;
    m.stereoOff = m.bitOff + 24;
    m.h = 352;
#if SUPPORT_MIDI_HW == TRUE
    m.midiOff = m.stereoOff + 32;
    m.h = m.midiOff + 100;
    if (m.h < 352)
        m.h = 352;
#else
    if (m.stereoOff + 48 > m.h)
        m.h = m.stereoOff + 48;
#endif
    return m;
}

extern bool g_volumeCurveDropdownOpen;
extern bool g_sampleRateDropdownOpen;
extern bool g_midi_input_device_dd_open;
extern bool g_midi_output_device_dd_open;

static void settings_close_dropdowns(void)
{
    g_volumeCurveDropdownOpen = false;
    g_sampleRateDropdownOpen = false;
    g_exportDropdownOpen = false;
#if SUPPORT_MIDI_HW == TRUE
    g_midi_input_device_dd_open = false;
    g_midi_output_device_dd_open = false;
    g_midiRecordFormatDropdownOpen = false;
#endif
}

void settings_window_hide(void)
{
    if (!g_show_settings_dialog)
        return;
    settings_close_dropdowns();
    if (g_settings_window)
        SDL_HideWindow(g_settings_window);
    g_show_settings_dialog = false;
    g_settings_mdown = false;
    g_settings_mclick = false;
    g_settings_mx = g_settings_my = -10000;
}

void settings_window_show(void)
{
    if (g_show_settings_dialog)
        return;

    SettingsDlgMetrics met = settings_metrics();
    if (!g_settings_window)
    {
        int main_x = 100, main_y = 100;
        if (g_main_window)
        {
            SDL_GetWindowPosition(g_main_window, &main_x, &main_y);
            int main_w = 0, main_h = 0;
            SDL_GetWindowSize(g_main_window, &main_w, &main_h);
            main_x += main_w + 8;
        }

        g_settings_window = SDL_CreateWindow(
            "Settings",
#if USE_SDL2 == TRUE
            main_x, main_y,
#endif
            met.w,
            met.h,
            0);
        if (!g_settings_window)
            return;
#if USE_SDL2 != TRUE
        SDL_SetWindowPosition(g_settings_window, main_x, main_y);
#endif
#if USE_SDL2 == TRUE
        g_settings_renderer = SDL_CreateRenderer(g_settings_window, -1, 0);
#else
        g_settings_renderer = SDL_CreateRenderer(g_settings_window, NULL);
#endif
        if (!g_settings_renderer)
        {
            SDL_DestroyWindow(g_settings_window);
            g_settings_window = NULL;
            return;
        }
    }

    SDL_ShowWindow(g_settings_window);
    SDL_RaiseWindow(g_settings_window);
    g_show_settings_dialog = true;
}

void settings_window_toggle(void)
{
    if (g_show_settings_dialog)
        settings_window_hide();
    else
        settings_window_show();
}

bool settings_window_is_visible(void)
{
    return g_show_settings_dialog;
}

bool settings_handle_event(SDL_Event *event)
{
    if (!g_show_settings_dialog || !g_settings_window || !event)
        return false;

#if USE_SDL2 == TRUE
    Uint32 set_win_id = SDL_GetWindowID(g_settings_window);
#else
    SDL_WindowID set_win_id = SDL_GetWindowID(g_settings_window);
#endif
    bool is_ours = false;

    switch (event->type)
    {
#if USE_SDL2 == TRUE
    case SDL_WINDOWEVENT:
#else
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
    case SDL_EVENT_WINDOW_SHOWN:
    case SDL_EVENT_WINDOW_HIDDEN:
    case SDL_EVENT_WINDOW_EXPOSED:
    case SDL_EVENT_WINDOW_MOVED:
    case SDL_EVENT_WINDOW_RESIZED:
    case SDL_EVENT_WINDOW_MINIMIZED:
    case SDL_EVENT_WINDOW_MAXIMIZED:
    case SDL_EVENT_WINDOW_RESTORED:
    case SDL_EVENT_WINDOW_MOUSE_ENTER:
    case SDL_EVENT_WINDOW_MOUSE_LEAVE:
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
    case SDL_EVENT_WINDOW_FOCUS_LOST:
#endif
        is_ours = (event->window.windowID == set_win_id);
        break;
#if USE_SDL2 == TRUE
    case SDL_KEYDOWN:
    case SDL_KEYUP:
#else
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
#endif
        is_ours = (event->key.windowID == set_win_id);
        break;
#if USE_SDL2 == TRUE
    case SDL_MOUSEMOTION:
#else
    case SDL_EVENT_MOUSE_MOTION:
#endif
        is_ours = (event->motion.windowID == set_win_id);
        break;
#if USE_SDL2 == TRUE
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP:
#else
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
#endif
        is_ours = (event->button.windowID == set_win_id);
        break;
#if USE_SDL2 == TRUE
    case SDL_MOUSEWHEEL:
#else
    case SDL_EVENT_MOUSE_WHEEL:
#endif
        is_ours = (event->wheel.windowID == set_win_id);
        break;
    }

    if (!is_ours)
        return false;

#if USE_SDL2 == TRUE
    if (event->type == SDL_WINDOWEVENT && event->window.event == SDL_WINDOWEVENT_CLOSE)
#else
    if (event->type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
#endif
    {
        settings_window_hide();
        return true;
    }

#if USE_SDL2 == TRUE
    if (event->type == SDL_WINDOWEVENT && event->window.event == SDL_WINDOWEVENT_LEAVE)
#else
    if (event->type == SDL_EVENT_WINDOW_MOUSE_LEAVE)
#endif
    {
        g_settings_mx = g_settings_my = -10000;
        return true;
    }

#if USE_SDL2 == TRUE
    if (event->type == SDL_MOUSEMOTION)
    {
        g_settings_mx = event->motion.x;
        g_settings_my = event->motion.y;
        return true;
    }
    if (event->type == SDL_MOUSEBUTTONDOWN && event->button.button == SDL_BUTTON_LEFT)
    {
        g_settings_mdown = true;
        g_settings_mx = event->button.x;
        g_settings_my = event->button.y;
        return true;
    }
    if (event->type == SDL_MOUSEBUTTONUP && event->button.button == SDL_BUTTON_LEFT)
    {
        g_settings_mdown = false;
        g_settings_mclick = true;
        g_settings_mx = event->button.x;
        g_settings_my = event->button.y;
        return true;
    }
    if (event->type == SDL_KEYDOWN && event->key.keysym.sym == SDLK_ESCAPE)
    {
        settings_window_hide();
        return true;
    }
    if (event->type == SDL_MOUSEWHEEL)
    {
        int wy = event->wheel.y;
        g_settings_wheel_delta += (wy > 0) ? -1 : 1;
        return true;
    }
#else
    if (event->type == SDL_EVENT_MOUSE_MOTION)
    {
        g_settings_mx = (int)event->motion.x;
        g_settings_my = (int)event->motion.y;
        return true;
    }
    if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN && event->button.button == SDL_BUTTON_LEFT)
    {
        g_settings_mdown = true;
        g_settings_mx = (int)event->button.x;
        g_settings_my = (int)event->button.y;
        return true;
    }
    if (event->type == SDL_EVENT_MOUSE_BUTTON_UP && event->button.button == SDL_BUTTON_LEFT)
    {
        g_settings_mdown = false;
        g_settings_mclick = true;
        g_settings_mx = (int)event->button.x;
        g_settings_my = (int)event->button.y;
        return true;
    }
    if (event->type == SDL_EVENT_KEY_DOWN && event->key.key == SDLK_ESCAPE)
    {
        settings_window_hide();
        return true;
    }
    if (event->type == SDL_EVENT_MOUSE_WHEEL)
    {
        int wy = (int)event->wheel.y;
        g_settings_wheel_delta += (wy > 0) ? -1 : 1;
        return true;
    }
#endif
    return true;
}

bool g_midi_input_device_dd_open = false;
bool g_midi_output_device_dd_open = false;
#if SUPPORT_MIDI_HW == TRUE
static int g_midi_input_dd_scroll = 0;
static int g_midi_output_dd_scroll = 0;
#endif

// MIDI device enumeration dirty flag - set true to force re-enumeration
static bool g_midi_device_list_dirty = true;

// Volume curve settings
int g_volume_curve = 1;
bool g_volumeCurveDropdownOpen = false;

// Sample rate settings
bool g_stereo_output = true;
int g_sample_rate_hz = 44100;
bool g_sampleRateDropdownOpen = false;

#if BAE_FIX_SPAN_DC
bool g_panfix_enabled = true;
#endif
#if BAE_CLASSIC_CHORUS
bool g_classic_chorus_enabled = false;
#endif
bool g_classic_lfo_enabled = false;
int g_terp_mode = BAE_LINEAR_INTERPOLATION;
bool g_output_16bit = true;
bool g_normalize_enabled = false;

// External globals we need access to
extern bool g_show_virtual_keyboard;
extern int g_exportCodecIndex;
extern int g_window_h;

/* Resolve zefidi.ini under the shared NeoBAE config root (migrate from exeDir once). */
static void zefidi_prepare_config_files(void)
{
    static int s_prepared = 0;

    if (s_prepared)
    {
        return;
    }
    (void)BAE_PrepareConfigFile("zefidi.ini", NULL);
    (void)BAE_PrepareConfigFile("zefidi_script.txt", NULL);
    (void)BAE_PrepareConfigFile("playlist.m3u", NULL);
    s_prepared = 1;
}

static void zefidi_get_settings_path(char *out, size_t outSize)
{
    if (!out || outSize == 0)
    {
        return;
    }
    out[0] = '\0';
    zefidi_prepare_config_files();
    (void)BAE_EnsureConfigDirectory();
    if (BAE_GetConfigFilePath(out, outSize, "zefidi.ini") != 0)
    {
        (void)BAE_GetRuntimeFilePath(out, outSize, "zefidi.ini");
    }
}

static void zefidi_get_script_path(char *out, size_t outSize)
{
    if (!out || outSize == 0)
    {
        return;
    }
    out[0] = '\0';
    zefidi_prepare_config_files();
    (void)BAE_EnsureConfigDirectory();
    if (BAE_GetConfigFilePath(out, outSize, "zefidi_script.txt") != 0)
    {
        (void)BAE_GetRuntimeFilePath(out, outSize, "zefidi_script.txt");
    }
}

void zefidi_get_playlist_path(char *out, size_t outSize)
{
    if (!out || outSize == 0)
    {
        return;
    }
    out[0] = '\0';
    zefidi_prepare_config_files();
    (void)BAE_EnsureConfigDirectory();
    if (BAE_GetConfigFilePath(out, outSize, "playlist.m3u") != 0)
    {
        (void)BAE_GetRuntimeFilePath(out, outSize, "playlist.m3u");
    }
}

Settings load_settings(void)
{
    Settings settings = {0};

    char settings_path[768];
    zefidi_get_settings_path(settings_path, sizeof(settings_path));

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
#if USE_NATIVE_DLS == TRUE
        else if (strncmp(line, "dls_compatibility_mode=", 23) == 0)
        {
            settings.dls_compatibility_mode = (atoi(line + 23) != 0);
            settings.has_dls_compatibility_mode = true;
        }
#endif
        else if (strncmp(line, "normalize_enabled=", 18) == 0)
        {
            settings.normalize_enabled = (atoi(line + 18) != 0);
            settings.has_normalize = true;
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
        else if (strncmp(line, "script_enabled=", 15) == 0)
        {
            settings.script_enabled = (atoi(line + 15) != 0);
            settings.has_script_enabled = true;
        }
        else if (strncmp(line, "script_path=", 12) == 0)
        {
            safe_strncpy(settings.script_path, line + 12, sizeof(settings.script_path) - 1);
            settings.script_path[sizeof(settings.script_path) - 1] = '\0';
            settings.has_script_path = true;
        }
#if BAE_FIX_SPAN_DC
        else if (strncmp(line, "panfix_enabled=", 15) == 0)
        {
            settings.panfix_enabled = (atoi(line + 15) != 0);
            settings.has_panfix = true;
        }
#endif
#if BAE_CLASSIC_CHORUS
        else if (strncmp(line, "classic_chorus_enabled=", 22) == 0)
        {
            settings.classic_chorus_enabled = (atoi(line + 22) != 0);
            settings.has_classic_chorus = true;
        }
#endif
        else if (strncmp(line, "classic_lfo_enabled=", 20) == 0)
        {
            settings.classic_lfo_enabled = (atoi(line + 20) != 0);
            settings.has_classic_lfo = true;
        }
        else if (strncmp(line, "terp_mode=", 10) == 0)
        {
            settings.terp_mode = atoi(line + 10);
            if (settings.terp_mode < BAE_DROP_SAMPLE || settings.terp_mode > BAE_SINC_INTERPOLATION)
            {
                settings.terp_mode = BAE_LINEAR_INTERPOLATION;
            }
            settings.has_terp_mode = true;
        }
        else if (strncmp(line, "output_16bit=", 13) == 0)
        {
            settings.output_16bit = (atoi(line + 13) != 0);
            settings.has_output_16bit = true;
        }
        else if (strncmp(line, "eq_enabled=", 11) == 0)
        {
            settings.eq_enabled = (atoi(line + 11) != 0);
            settings.has_eq = true;
        }
        else if (strncmp(line, "eq_gain_0=", 10) == 0)
        {
            settings.eq_gains[0] = (float)atof(line + 10);
            settings.has_eq = true;
        }
        else if (strncmp(line, "eq_gain_1=", 10) == 0)
        {
            settings.eq_gains[1] = (float)atof(line + 10);
            settings.has_eq = true;
        }
        else if (strncmp(line, "eq_gain_2=", 10) == 0)
        {
            settings.eq_gains[2] = (float)atof(line + 10);
            settings.has_eq = true;
        }
        else if (strncmp(line, "eq_gain_3=", 10) == 0)
        {
            settings.eq_gains[3] = (float)atof(line + 10);
            settings.has_eq = true;
        }
        else if (strncmp(line, "eq_gain_4=", 10) == 0)
        {
            settings.eq_gains[4] = (float)atof(line + 10);
            settings.has_eq = true;
        }
        else if (strncmp(line, "eq_preset=", 10) == 0)
        {
            safe_strncpy(settings.eq_preset_name, line + 10, sizeof(settings.eq_preset_name) - 1);
            settings.eq_preset_name[sizeof(settings.eq_preset_name) - 1] = '\0';
            settings.has_eq_preset = true;
        }
    }
    fclose(f);

    /* Load script_text from separate file (may contain newlines) */
    {
        char script_text_path[768];
        zefidi_get_script_path(script_text_path, sizeof(script_text_path));
        FILE *sf = fopen(script_text_path, "rb");
        if (sf) {
            fseek(sf, 0, SEEK_END);
            long sz = ftell(sf);
            if (sz > 0) {
                if (sz > (long)sizeof(settings.script_text) - 1)
                    sz = (long)sizeof(settings.script_text) - 1;
                fseek(sf, 0, SEEK_SET);
                int n = (int)fread(settings.script_text, 1, (size_t)sz, sf);
                settings.script_text[n] = '\0';
                settings.has_script_text = true;
            }
            fclose(sf);
        }
    }

    return settings;
}

void save_settings(const char *last_bank_path, int reverb_type, bool loop_enabled)
{
    char settings_path[768];
    zefidi_get_settings_path(settings_path, sizeof(settings_path));

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
            size_t bytes_read = fread(content, 1, content_size, f_read);
            content[bytes_read] = '\0';
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
        fprintf(f, "terp_mode=%d\n", g_terp_mode);
        fprintf(f, "output_16bit=%d\n", g_output_16bit ? 1 : 0);
        fprintf(f, "show_keyboard=%d\n", g_show_virtual_keyboard ? 1 : 0);
#if USE_NATIVE_DLS == TRUE
        fprintf(f, "dls_compatibility_mode=%d\n", g_use_dls_compatiblity_mode ? 1 : 0);
#endif
        fprintf(f, "normalize_enabled=%d\n", g_normalize_enabled ? 1 : 0);
        fprintf(f, "disable_webtv_progress_bar=%d\n", g_disable_webtv_progress_bar ? 1 : 0);
        fprintf(f, "export_codec_index=%d\n", g_exportCodecIndex);
#if BAE_FIX_SPAN_DC
        fprintf(f, "panfix_enabled=%d\n", g_panfix_enabled ? 1 : 0);
#endif
#if BAE_CLASSIC_CHORUS
        fprintf(f, "classic_chorus_enabled=%d\n", g_classic_chorus_enabled ? 1 : 0);
#endif
        fprintf(f, "classic_lfo_enabled=%d\n", g_classic_lfo_enabled ? 1 : 0);
        if (g_bae.mixer)
        {
            BAE_BOOL eq_on = FALSE;
            BAEMixer_GetEQEnabled(g_bae.mixer, &eq_on);
            fprintf(f, "eq_enabled=%d\n", eq_on ? 1 : 0);
            for (int i = 0; i < 5; i++)
            {
                float gain = 0.0f;
                BAEMixer_GetEQGain(g_bae.mixer, i, &gain);
                fprintf(f, "eq_gain_%d=%f\n", i, gain);
            }
            if (g_selected_eq_preset < 7)
            {
                const char *std_names[] = {"Flat", "Bass Boost", "Acoustic", "Rock", "Pop", "Classical", "Vocal"};
                fprintf(f, "eq_preset=%s\n", std_names[g_selected_eq_preset]);
            }
            else if (g_selected_eq_preset == 7)
            {
                fprintf(f, "eq_preset=Custom\n");
            }
            else
            {
                fprintf(f, "eq_preset=%s\n", g_current_custom_eq_preset);
            }
        }
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
#endif
        
        // Preserve existing custom reverb and custom EQ preset data
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
                
#if USE_NEO_EFFECTS
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
                else
#endif
                if (line_buf[0] && strncmp(line_buf, "custom_eq_", 10) == 0)
                {
                    char *underscore = strchr(line_buf + 10, '_');
                    if (underscore && line_buf[10] >= '0' && line_buf[10] <= '9')
                    {
                        fprintf(f, "%s\n", line_buf);
                    }
                }
            }
        }

        if (content) free(content);
        fclose(f);
    }
}

void save_full_settings(const Settings *settings)
{
    if (!settings)
        return;

    char settings_path[768];
    zefidi_get_settings_path(settings_path, sizeof(settings_path));

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
            size_t bytes_read = fread(content, 1, content_size, f_read);
            content[bytes_read] = '\0';
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
        if (settings->has_terp_mode)
        {
            fprintf(f, "terp_mode=%d\n", settings->terp_mode);
        }
        if (settings->has_output_16bit)
        {
            fprintf(f, "output_16bit=%d\n", settings->output_16bit ? 1 : 0);
        }
        if (settings->has_show_keyboard)
        {
            fprintf(f, "show_keyboard=%d\n", settings->show_keyboard ? 1 : 0);
        }
#if USE_NATIVE_DLS == TRUE
        if (settings->has_dls_compatibility_mode)
        {
            fprintf(f, "dls_compatibility_mode=%d\n", settings->dls_compatibility_mode ? 1 : 0);
        }
#endif
        if (settings->has_normalize)
        {
            fprintf(f, "normalize_enabled=%d\n", settings->normalize_enabled ? 1 : 0);
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
        if (settings->has_script_enabled)
        {
            fprintf(f, "script_enabled=%d\n", settings->script_enabled ? 1 : 0);
        }
        if (settings->has_script_path && settings->script_path[0])
        {
            fprintf(f, "script_path=%s\n", settings->script_path);
        }
#if BAE_FIX_SPAN_DC
        if (settings->has_panfix)
        {
            fprintf(f, "panfix_enabled=%d\n", settings->panfix_enabled ? 1 : 0);
        }
#endif
#if BAE_CLASSIC_CHORUS
        if (settings->has_classic_chorus)
        {
            fprintf(f, "classic_chorus_enabled=%d\n", settings->classic_chorus_enabled ? 1 : 0);
        }
#endif
        if (settings->has_classic_lfo)
        {
            fprintf(f, "classic_lfo_enabled=%d\n", settings->classic_lfo_enabled ? 1 : 0);
        }
        if (settings->has_eq)
        {
            fprintf(f, "eq_enabled=%d\n", settings->eq_enabled ? 1 : 0);
            for (int i = 0; i < 5; i++)
            {
                fprintf(f, "eq_gain_%d=%f\n", i, settings->eq_gains[i]);
            }
        }
        if (settings->has_eq_preset && settings->eq_preset_name[0])
        {
            fprintf(f, "eq_preset=%s\n", settings->eq_preset_name);
        }
        
#if USE_NEO_EFFECTS
        // Save current custom reverb preset name if one is loaded
        if (settings->has_custom_reverb_preset && settings->custom_reverb_preset_name[0])
        {
            fprintf(f, "custom_reverb_preset=%s\n", settings->custom_reverb_preset_name);
        }
#endif
        
        // Preserve existing custom reverb and custom EQ preset data
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
                
#if USE_NEO_EFFECTS
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
                else
#endif
                if (line_buf[0] && strncmp(line_buf, "custom_eq_", 10) == 0)
                {
                    char *underscore = strchr(line_buf + 10, '_');
                    if (underscore && line_buf[10] >= '0' && line_buf[10] <= '9')
                    {
                        fprintf(f, "%s\n", line_buf);
                    }
                }
            }
        }
        
        if (content) free(content);
        fclose(f);
    }

    /* Save script text to a separate file (it may contain newlines) */
    {
        char script_text_path[768];
        zefidi_get_script_path(script_text_path, sizeof(script_text_path));
        if (settings->has_script_text && settings->script_text[0])
        {
            FILE *sf = fopen(script_text_path, "wb");
            if (sf) {
                fwrite(settings->script_text, 1, strlen(settings->script_text), sf);
                fclose(sf);
            }
        }
        else
        {
            remove(script_text_path);
        }
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
        g_volume_curve = (settings->volume_curve >= 0 && settings->volume_curve <= 4) ? settings->volume_curve : 1;
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
#if USE_NATIVE_DLS == TRUE
    if (settings->has_dls_compatibility_mode)
    {
        g_use_dls_compatiblity_mode = settings->dls_compatibility_mode;
        GM_DLS_SetMobileBAEQuirks(g_use_dls_compatiblity_mode ? false : true); // inverted
    }
#endif
    if (settings->has_normalize)
    {
        g_normalize_enabled = settings->normalize_enabled;
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
#if BAE_FIX_SPAN_DC
    if (settings->has_panfix)
    {
        g_panfix_enabled = settings->panfix_enabled;
    }
#endif
#if BAE_CLASSIC_CHORUS
    if (settings->has_classic_chorus)
    {
        g_classic_chorus_enabled = settings->classic_chorus_enabled;
    }
#endif
    if (settings->has_classic_lfo)
    {
        g_classic_lfo_enabled = settings->classic_lfo_enabled;
    }
    if (settings->has_terp_mode)
    {
        g_terp_mode = settings->terp_mode;
        if (g_terp_mode < BAE_DROP_SAMPLE || g_terp_mode > BAE_SINC_INTERPOLATION)
        {
            g_terp_mode = BAE_LINEAR_INTERPOLATION;
        }
    }
    if (settings->has_output_16bit)
    {
        g_output_16bit = settings->output_16bit;
    }
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

static void render_settings_dialog(SDL_Renderer *R, int mx, int my, bool mclick, bool mdown,
                            int *transpose, int *tempo, int *volume, bool *loopPlay,
                            int *reverbType, bool ch_enable[16], int *progress, int *duration, bool *playing);

void settings_window_render(int *transpose, int *tempo, int *volume, bool *loopPlay,
                            int *reverbType, bool ch_enable[16], int *progress, int *duration, bool *playing)
{
    if (!g_show_settings_dialog || !g_settings_window || !g_settings_renderer)
        return;

    bool mclick = g_settings_mclick;
    g_settings_mclick = false;
    render_settings_dialog(g_settings_renderer, g_settings_mx, g_settings_my, mclick, g_settings_mdown,
                           transpose, tempo, volume, loopPlay, reverbType, ch_enable, progress, duration, playing);
    SDL_RenderPresent(g_settings_renderer);
}

#if SUPPORT_MIDI_HW == TRUE
/* MIDI device rows sit at the bottom of the settings window. A list sized
 * itemH * deviceCount is clipped by the window, so only ~3 ports showed.
 * Open toward the larger gap, clamp height, and scroll the rest. */
static int render_clamped_device_menu(SDL_Renderer *R, Rect trigger, int dlgH,
                                      char names[][128], int count, int selected,
                                      int *scroll, bool *open,
                                      int mx, int my, bool mclick)
{
    int itemH = trigger.h;
    if (itemH < 18)
        itemH = 18;
    int listCount = count > 0 ? count : 1;
    const int margin = 4;
    int spaceBelow = dlgH - (trigger.y + trigger.h + 1) - margin;
    int spaceAbove = trigger.y - margin;
    if (spaceBelow < 0)
        spaceBelow = 0;
    if (spaceAbove < 0)
        spaceAbove = 0;
    bool openUp = spaceAbove > spaceBelow;
    int avail = openUp ? spaceAbove : spaceBelow;
    if (avail < itemH)
        avail = itemH;
    int visible = avail / itemH;
    if (visible < 1)
        visible = 1;
    if (visible > listCount)
        visible = listCount;

    if (g_settings_wheel_delta != 0)
    {
        *scroll += g_settings_wheel_delta;
        g_settings_wheel_delta = 0;
    }

    int maxScroll = listCount - visible;
    if (maxScroll < 0)
        maxScroll = 0;
    if (*scroll < 0)
        *scroll = 0;
    if (*scroll > maxScroll)
        *scroll = maxScroll;

    int boxH = visible * itemH;
    Rect box = {trigger.x, openUp ? (trigger.y - boxH) : (trigger.y + trigger.h + 1), trigger.w, boxH};

    SDL_Color ddBg = g_panel_bg;
    ddBg.a = 255;
    SDL_Color shadow = {0, 0, 0, g_is_dark_mode ? 120 : 90};
    Rect shadowRect = {box.x + 2, box.y + 2, box.w, box.h};
    draw_rect(R, shadowRect, shadow);
    draw_rect(R, box, ddBg);
    draw_frame(R, box, g_panel_border);

    int sbW = (maxScroll > 0) ? 8 : 0;
    Rect track = {box.x + box.w - 7, box.y + 2, 5, box.h - 4};
    bool onScroll = (sbW > 0) && point_in(mx, my, track);
    if (onScroll && mclick && maxScroll > 0 && box.h > 0)
    {
        int rel = my - box.y;
        if (rel < 0)
            rel = 0;
        if (rel > box.h)
            rel = box.h;
        *scroll = (rel * maxScroll) / box.h;
        if (*scroll < 0)
            *scroll = 0;
        if (*scroll > maxScroll)
            *scroll = maxScroll;
        mclick = false;
    }

#if USE_SDL2 == TRUE
    SDL_Rect sdl_clip = {box.x, box.y, box.w, box.h};
    SDL_RenderSetClipRect(R, &sdl_clip);
#else
    SDL_Rect sdl_clip = {box.x, box.y, box.w, box.h};
    SDL_SetRenderClipRect(R, &sdl_clip);
#endif

    int chosen = -1;
    if (count <= 0)
    {
        Rect ir = {box.x, box.y, box.w, itemH};
        draw_rect(R, ir, g_panel_bg);
        draw_text(R, ir.x + 6, ir.y + 6, "No MIDI devices", g_text_color);
    }
    else
    {
        int first = *scroll;
        for (int vis = 0; vis < visible && (first + vis) < count; vis++)
        {
            int i = first + vis;
            Rect ir = {box.x, box.y + vis * itemH, box.w - sbW, itemH};
            bool over = !onScroll && point_in(mx, my, ir);
            SDL_Color ibg = (i == selected) ? g_highlight_color : g_panel_bg;
            if (over)
                ibg = g_button_hover;
            draw_rect(R, ir, ibg);
            if (i < count - 1)
            {
                SDL_SetRenderDrawColor(R, g_panel_border.r, g_panel_border.g, g_panel_border.b, 255);
#if USE_SDL2 == TRUE
                SDL_RenderDrawLine(R, ir.x, ir.y + ir.h, ir.x + ir.w, ir.y + ir.h);
#else
                SDL_RenderLine(R, ir.x, ir.y + ir.h, ir.x + ir.w, ir.y + ir.h);
#endif
            }
            draw_text(R, ir.x + 6, ir.y + 6, names[i], g_button_text);
            if (over && mclick)
            {
                chosen = i;
                *open = false;
            }
        }
    }

#if USE_SDL2 == TRUE
    SDL_RenderSetClipRect(R, NULL);
#else
    SDL_SetRenderClipRect(R, NULL);
#endif

    if (maxScroll > 0)
    {
        SDL_Color trackCol = g_is_dark_mode ? (SDL_Color){40, 40, 48, 255} : (SDL_Color){200, 200, 210, 255};
        draw_rect(R, track, trackCol);
        int thumbH = (track.h * visible) / listCount;
        if (thumbH < 12)
            thumbH = 12;
        int thumbY = track.y;
        if (maxScroll > 0 && track.h > thumbH)
            thumbY = track.y + ((track.h - thumbH) * (*scroll)) / maxScroll;
        Rect thumb = {track.x, thumbY, track.w, thumbH};
        draw_rect(R, thumb, g_accent_color);
    }

    if (mclick && chosen < 0 && !point_in(mx, my, trigger) && !point_in(mx, my, box))
        *open = false;

    return chosen;
}
#endif

// Settings dialog rendering
static void render_settings_dialog(SDL_Renderer *R, int mx, int my, bool mclick, bool mdown,
                            int *transpose, int *tempo, int *volume, bool *loopPlay,
                            int *reverbType, bool ch_enable[16], int *progress, int *duration, bool *playing)
{
    if (!g_show_settings_dialog)
        return;

    SettingsDlgMetrics met = settings_metrics();
    int dlgW = met.w;
    int lfoOff = met.lfoOff;
    int terpOff = met.terpOff;
    int bitOff = met.bitOff;
    int stereoOff = met.stereoOff;
    int dlgH = met.h;
#if SUPPORT_MIDI_HW == TRUE
    int midiOff = met.midiOff;
#endif
    int pad = 10;
    Rect dlg = {0, 0, dlgW, dlgH};
    SDL_Color dlgBg = g_panel_bg;
    dlgBg.a = 255;
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
        settings_window_hide();
        return;
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
#if USE_MPEG_ENCODER == TRUE || USE_VORBIS_ENCODER == TRUE || USE_OPUS_ENCODER == TRUE || USE_FLAC_ENCODER == TRUE
    Rect expRect = {controlRightX, dlg.y + 104, controlW, 24};
#endif
#if SUPPORT_MIDI_HW == TRUE
    Rect midiDevRect = {controlRightX, dlg.y + midiOff, controlW + 200, 24};
    Rect midiOutDevRect = {controlRightX, dlg.y + midiOff + 28, controlW + 200, 24};
    Rect recordCodecRect = {controlRightX, dlg.y + midiOff + 56, controlW + 200, 24};
#endif

    // Left column controls (stacked)
    // Volume Curve selector
    draw_text(R, leftX, dlg.y + 36, "Vol. Curve (HSB):", g_text_color);
    const char *volumeCurveNames[] = {"NeoBAE S Curve", "Peaky S Curve", "WebTV Curve", "2x Exponential", "2x Linear", "No Curve"};
    int vcCount = 6;
    bool volumeCurveEnabled = !g_midiRecordFormatDropdownOpen && !g_midi_input_device_dd_open && !g_midi_output_device_dd_open;
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
    bool sampleRateEnabled = !g_volumeCurveDropdownOpen && !g_midiRecordFormatDropdownOpen && !g_midi_input_device_dd_open && !g_midi_output_device_dd_open;
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
    bool exportEnabled = !g_volumeCurveDropdownOpen && !g_sampleRateDropdownOpen && !g_midiRecordFormatDropdownOpen && !g_midi_input_device_dd_open && !g_midi_output_device_dd_open;
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
    Rect midiEnRect = {leftX, dlg.y + midiOff + 4, 18, 18};
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
            bool midi_ok = false;
            if (g_midi_input_device_index >= 0 && g_midi_input_device_index < g_midi_input_device_count)
            {
                int api = g_midi_device_api[g_midi_input_device_index];
                int port = g_midi_device_port[g_midi_input_device_index];
                midi_ok = midi_input_init("NeoBAE", api, port);
            }
            else
            {
                midi_ok = midi_input_init("NeoBAE", -1, -1);
            }
            if (!midi_ok)
            {
                midi_service_stop();
                g_midi_input_enabled = false;
                set_status_message("MIDI input failed to open");
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
            if (g_live_song)
            {
                bool skip_curve = false;
#if USE_SF2_SUPPORT == TRUE
                if (BAESong_IsSF2Song(g_live_song))
                    skip_curve = true;
#endif
#if USE_NATIVE_DLS == TRUE
                if (BAESong_IsDLSSong(g_live_song))
                    skip_curve = true;
#endif
                if (!skip_curve)
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
    // populate device list lazily when dropdown opened, but only re-enumerate when dirty
    if ((g_midi_input_device_dd_open || g_midi_output_device_dd_open) && g_midi_device_list_dirty)
    {
        g_midi_device_list_dirty = false;
        // Enumerate compiled RtMidi APIs and collect input ports first, then output ports.
        g_midi_device_count = 0;
        g_midi_input_device_count = 0;
        g_midi_output_device_count = 0;
        enum RtMidiApi apis[16];
        int apiCount = rtmidi_get_compiled_api(apis, (unsigned int)(sizeof(apis) / sizeof(apis[0])));
        if (apiCount <= 0)
            apiCount = 0;
        const char *dbg = getenv("NEOBAE_DEBUG_MIDI");
        // First: inputs
        for (int ai = 0; ai < apiCount && g_midi_device_count < 64; ++ai)
        {
            RtMidiInPtr r = rtmidi_in_create(apis[ai], "NeoBAE_enum", 1000);
            if (!r)
                continue;
            unsigned int cnt = rtmidi_get_port_count(r);
            if (dbg)
            {
                const char *an = rtmidi_api_name(apis[ai]);
                BAE_STDERR("[MIDI ENUM IN] API %d (%s): ok=%d msg='%s' ports=%u\n", ai, an ? an : "?", r->ok, r->msg ? r->msg : "", cnt);
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
            RtMidiOutPtr r = rtmidi_out_create(apis[ai], "NeoBAE_enum");
            if (!r)
                continue;
            unsigned int cnt = rtmidi_get_port_count(r);
            if (dbg)
            {
                const char *an = rtmidi_api_name(apis[ai]);
                BAE_STDERR("[MIDI ENUM OUT] API %d (%s): ok=%d msg='%s' ports=%u\n", ai, an ? an : "?", r->ok, r->msg ? r->msg : "", cnt);
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
            g_midi_device_list_dirty = true;
            g_midi_input_dd_scroll = g_midi_input_device_index;
            if (g_midi_input_dd_scroll < 0)
                g_midi_input_dd_scroll = 0;
            g_volumeCurveDropdownOpen = false;
            g_sampleRateDropdownOpen = false;
            g_exportDropdownOpen = false;
            g_midi_output_device_dd_open = false;
            g_midiRecordFormatDropdownOpen = false;
        }
    }

    // MIDI output checkbox and device selector (placed next to input)
    Rect midiOutEnRect = {leftX, dlg.y + midiOff + 32, 18, 18};
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
                midi_output_init("NeoBAE", -1, -1);
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
                // Restore HSB mute; player slider is OutputGain (re-applied below).
                if (g_bae.mixer)
                {
                    BAEMixer_SetMasterVolume(g_bae.mixer, FLOAT_TO_UNSIGNED_FIXED(1.0));
                    g_master_muted_for_midi_out = false;
                    if (volume)
                        bae_set_volume(*volume);
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
            g_midi_device_list_dirty = true;
            g_midi_output_dd_scroll = g_midi_output_device_index;
            if (g_midi_output_dd_scroll < 0)
                g_midi_output_dd_scroll = 0;
            g_volumeCurveDropdownOpen = false;
            g_sampleRateDropdownOpen = false;
            g_exportDropdownOpen = false;
            g_midi_input_device_dd_open = false;
            g_midiRecordFormatDropdownOpen = false;
        }
    }

    // Record Codec selector (left column, below MIDI Output)
    draw_text(R, leftX, dlg.y + midiOff + 60, "MIDI In Record:", g_text_color);
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
    bool rightColumnCheckboxesEnabled = !(g_volumeCurveDropdownOpen || g_sampleRateDropdownOpen || g_exportDropdownOpen || g_midi_input_device_dd_open || g_midi_output_device_dd_open || g_midiRecordFormatDropdownOpen);

    Rect kbRect = {rightX, dlg.y + 36, 18, 18};
    if (!rightColumnCheckboxesEnabled)
    {
        bool over = point_in(mx, my, kbRect);
        draw_custom_checkbox(R, kbRect, g_show_virtual_keyboard, over);
        draw_text(R, kbRect.x + kbRect.w + 6, kbRect.y + 2, "Show Virtual Keyboard", g_text_color);
    }
    else if (ui_toggle(R, kbRect, &g_show_virtual_keyboard, "Show Virtual Keyboard", mx, my, mclick))
    {
        save_settings(g_current_bank_path[0] ? g_current_bank_path : NULL, *reverbType, *loopPlay);
        if (!g_show_virtual_keyboard)
            g_keyboard_channel_dd_open = false;
    }

    Rect playlistRect = {rightX, dlg.y + 72, 18, 18};
#if SUPPORT_PLAYLIST == TRUE
    if (!rightColumnCheckboxesEnabled)
    {
        bool over = point_in(mx, my, playlistRect);
        draw_custom_checkbox(R, playlistRect, g_playlist.enabled, over);
        draw_text(R, playlistRect.x + playlistRect.w + 6, playlistRect.y + 2, "Enable Playlist", g_text_color);
    }
    else if (ui_toggle(R, playlistRect, &g_playlist.enabled, "Enable Playlist", mx, my, mclick))
    {
        save_settings(g_current_bank_path[0] ? g_current_bank_path : NULL, *reverbType, *loopPlay);
    }
#else
    // Show disabled checkbox when playlist support is not compiled in
    draw_custom_checkbox(R, playlistRect, false, false);
    draw_text(R, playlistRect.x + playlistRect.w + 6, playlistRect.y + 2, "Enable Playlist (disabled)", g_text_color);
#endif

    Rect wtvRect = {rightX, dlg.y + 108, 18, 18};
    bool webtv_enabled = !g_disable_webtv_progress_bar;
    if (!rightColumnCheckboxesEnabled)
    {
        bool over = point_in(mx, my, wtvRect);
        draw_custom_checkbox(R, wtvRect, webtv_enabled, over);
        draw_text(R, wtvRect.x + wtvRect.w + 6, wtvRect.y + 2, "WebTV Style Bar", g_text_color);
    }
    else if (ui_toggle(R, wtvRect, &webtv_enabled, "WebTV Style Bar", mx, my, mclick))
    {
        g_disable_webtv_progress_bar = !webtv_enabled;
        save_settings(g_current_bank_path[0] ? g_current_bank_path : NULL, *reverbType, *loopPlay);
    }

#if BAE_FIX_SPAN_DC == TRUE
    Rect panfixRect = {rightX, dlg.y + 144, 18, 18};
    if (!rightColumnCheckboxesEnabled)
    {
        bool over = point_in(mx, my, panfixRect);
        draw_custom_checkbox(R, panfixRect, g_panfix_enabled, over);
        draw_text(R, panfixRect.x + panfixRect.w + 6, panfixRect.y + 2, "Fix Pan LFO Bias", g_text_color);
    }
    else if (ui_toggle(R, panfixRect, &g_panfix_enabled, "Fix Pan LFO Bias", mx, my, mclick))
    {
        BAE_SetSpanDCFix(g_panfix_enabled ? TRUE : FALSE);
        save_settings(g_current_bank_path[0] ? g_current_bank_path : NULL, *reverbType, *loopPlay);
    }
#endif

#if BAE_CLASSIC_CHORUS == TRUE
    Rect chorusRect = {rightX, dlg.y + 180, 18, 18};
    if (!rightColumnCheckboxesEnabled)
    {
        bool over = point_in(mx, my, chorusRect);
        draw_custom_checkbox(R, chorusRect, g_classic_chorus_enabled, over);
        draw_text(R, chorusRect.x + chorusRect.w + 6, chorusRect.y + 2, "Classic Chorus Order", g_text_color);
    }
    else if (ui_toggle(R, chorusRect, &g_classic_chorus_enabled, "Classic Chorus Order", mx, my, mclick))
    {
        BAE_SetClassicChorus(g_classic_chorus_enabled ? TRUE : FALSE);
        save_settings(g_current_bank_path[0] ? g_current_bank_path : NULL, *reverbType, *loopPlay);
    }
#endif

    {
        int lfoY = dlg.y + lfoOff;
        Rect lfoRect = {rightX, lfoY, 18, 18};
        if (!rightColumnCheckboxesEnabled)
        {
            bool over = point_in(mx, my, lfoRect);
            draw_custom_checkbox(R, lfoRect, g_classic_lfo_enabled, over);
            draw_text(R, lfoRect.x + lfoRect.w + 6, lfoRect.y + 2, "Classic LFO timing", g_text_color);
        }
        else if (ui_toggle(R, lfoRect, &g_classic_lfo_enabled, "Classic LFO timing", mx, my, mclick))
        {
            BAE_SetClassicLFO(g_classic_lfo_enabled ? TRUE : FALSE);
            save_settings(g_current_bank_path[0] ? g_current_bank_path : NULL, *reverbType, *loopPlay);
        }

        static const char *terpNames[] = {"Drop", "2-point", "Linear", "Sinc"};
        int terpIndex = g_terp_mode;
        int terpY = dlg.y + terpOff;
        if (terpIndex < 0 || terpIndex > 3)
            terpIndex = 2;
        draw_text(R, rightX, terpY + 4, "Interpolation:", g_text_color);
        Rect terpRect = {rightX + 100, terpY, 130, 24};
        SDL_Color tbg = point_in(mx, my, terpRect) ? g_button_hover : g_button_base;
        draw_rect(R, terpRect, tbg);
        draw_frame(R, terpRect, g_button_border);
        draw_text(R, terpRect.x + 6, terpRect.y + 3, terpNames[terpIndex], g_button_text);
        if (point_in(mx, my, terpRect) && mclick)
        {
            int next = (terpIndex + 1) % 4;
            g_terp_mode = next;
            save_settings(g_current_bank_path[0] ? g_current_bank_path : NULL, *reverbType, *loopPlay);
            (void)recreate_mixer_and_restore(g_sample_rate_hz, *reverbType, *transpose, *tempo, *volume, *loopPlay, ch_enable);
        }

        Rect bitRect = {rightX, dlg.y + bitOff, 18, 18};
        if (!rightColumnCheckboxesEnabled)
        {
            bool over = point_in(mx, my, bitRect);
            draw_custom_checkbox(R, bitRect, g_output_16bit, over);
            draw_text(R, bitRect.x + bitRect.w + 6, bitRect.y + 2, "16-bit output", g_text_color);
        }
        else if (ui_toggle(R, bitRect, &g_output_16bit, "16-bit output", mx, my, mclick))
        {
            save_settings(g_current_bank_path[0] ? g_current_bank_path : NULL, *reverbType, *loopPlay);
            (void)recreate_mixer_and_restore(g_sample_rate_hz, *reverbType, *transpose, *tempo, *volume, *loopPlay, ch_enable);
        }

        Rect stereoRect = {rightX, dlg.y + stereoOff, 18, 18};
        if (!rightColumnCheckboxesEnabled)
        {
            bool over = point_in(mx, my, stereoRect);
            draw_custom_checkbox(R, stereoRect, g_stereo_output, over);
            draw_text(R, stereoRect.x + stereoRect.w + 6, stereoRect.y + 2, "Stereo output", g_text_color);
        }
        else if (ui_toggle(R, stereoRect, &g_stereo_output, "Stereo output", mx, my, mclick))
        {
            save_settings(g_current_bank_path[0] ? g_current_bank_path : NULL, *reverbType, *loopPlay);
            (void)recreate_mixer_and_restore(g_sample_rate_hz, *reverbType, *transpose, *tempo, *volume, *loopPlay, ch_enable);
        }
    }

#if USE_NATIVE_DLS == TRUE
    Rect compatDlsRect = {leftX, dlg.y + 144, 18, 18};
    if (!rightColumnCheckboxesEnabled)
    {
        bool over = point_in(mx, my, compatDlsRect);
        draw_custom_checkbox(R, compatDlsRect, g_use_dls_compatiblity_mode, over);
        draw_text(R, compatDlsRect.x + compatDlsRect.w + 6, compatDlsRect.y + 2, "Smart DLS Handling", g_text_color);
    }
    else if (ui_toggle(R, compatDlsRect, &g_use_dls_compatiblity_mode, "Smart DLS Handling", mx, my, mclick))
    {
        save_settings(g_current_bank_path[0] ? g_current_bank_path : NULL, *reverbType, *loopPlay);
        GM_DLS_SetMobileBAEQuirks(g_use_dls_compatiblity_mode ? false : true); // inverted
    }
    {
        Rect compatDlsFullRect = {leftX, dlg.y + 144, 280, 24};
        if (point_in(mx, my, compatDlsFullRect))
        {
            const char *tooltip_text = "Enables Smart DLS Handling. Disable to force all DLS to use mobileBAE behavior.";
            int text_w = 0, text_h = 0;
            measure_text(tooltip_text, &text_w, &text_h);
            int tooltip_w = text_w + 8;
            int tooltip_h = text_h + 8;
            if (tooltip_w > 500)
                tooltip_w = 500;
            int tooltip_x = mx + 10;
            int tooltip_y = my - 30;
            if (tooltip_x + tooltip_w > dlg.w - 4)
                tooltip_x = dlg.w - tooltip_w - 4;
            if (tooltip_y < 4)
                tooltip_y = my + 25;
            ui_set_tooltip((Rect){tooltip_x, tooltip_y, tooltip_w, tooltip_h}, tooltip_text,
                           &g_dls_compat_tooltip_visible, &g_dls_compat_tooltip_rect, g_dls_compat_tooltip_text, sizeof(g_dls_compat_tooltip_text));
        }
    }
    Rect normalizeRect = {leftX, dlg.y + 174, 18, 18};
#else
    Rect normalizeRect = {leftX, dlg.y + 144, 18, 18};
#endif
    if (!rightColumnCheckboxesEnabled)
    {
        bool over = point_in(mx, my, normalizeRect);
        draw_custom_checkbox(R, normalizeRect, g_normalize_enabled, over);
        draw_text(R, normalizeRect.x + normalizeRect.w + 6, normalizeRect.y + 2, "Normalize Playback", g_text_color);
    }
    else if (ui_toggle(R, normalizeRect, &g_normalize_enabled, "Normalize Playback", mx, my, mclick))
    {
        save_settings(g_current_bank_path[0] ? g_current_bank_path : NULL, *reverbType, *loopPlay);
        if (!g_normalize_enabled)
        {
            bae_cancel_normalize();
            if (g_bae.mixer)
                BAEMixer_SetSongNormalizeGain(g_bae.mixer, 100);
        }
        else
        {
            /* Enabling: reload current song so the estimate applies on play. */
            bae_reload_song_for_normalize(transpose, tempo, volume, loopPlay, reverbType, ch_enable, playing);
            if (progress > 0)
                bae_seek_ms(*progress);
        }
    }
    {
        Rect normalizeFullRect = {normalizeRect.x, normalizeRect.y, 280, 24};
        if (point_in(mx, my, normalizeFullRect))
        {
            const char *tooltip_text = "Fast peak estimate using MIDI+patch data.";
            int text_w = 0, text_h = 0;
            measure_text(tooltip_text, &text_w, &text_h);
            int tooltip_w = text_w + 8;
            int tooltip_h = text_h + 8;
            if (tooltip_w > 500)
                tooltip_w = 500;
            int tooltip_x = mx + 10;
            int tooltip_y = my - 30;
            if (tooltip_x + tooltip_w > dlg.w - 4)
                tooltip_x = dlg.w - tooltip_w - 4;
            if (tooltip_y < 4)
                tooltip_y = my + 25;
            ui_set_tooltip((Rect){tooltip_x, tooltip_y, tooltip_w, tooltip_h}, tooltip_text,
                           &g_dls_compat_tooltip_visible, &g_dls_compat_tooltip_rect, g_dls_compat_tooltip_text, sizeof(g_dls_compat_tooltip_text));
        }
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
#if USE_SDL2 == TRUE
                SDL_RenderDrawLine(R, ir.x, ir.y + ir.h, ir.x + ir.w, ir.y + ir.h);
#else
                SDL_RenderLine(R, ir.x, ir.y + ir.h, ir.x + ir.w, ir.y + ir.h);
#endif
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
                    if (recreate_mixer_and_restore(g_sample_rate_hz, *reverbType, *transpose, *tempo, *volume, *loopPlay, ch_enable))
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
                                midi_input_init("NeoBAE", api, port);
                                midi_service_start();
                            }
                            else
                            {
                                midi_input_init("NeoBAE", -1, -1);
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
        int chosen = render_clamped_device_menu(R, midiDevRect, dlgH,
                                                g_midi_device_name_cache, g_midi_input_device_count,
                                                g_midi_input_device_index, &g_midi_input_dd_scroll,
                                                &g_midi_input_device_dd_open, mx, my, mclick);
        if (chosen >= 0)
        {
            g_midi_input_device_index = chosen;
            g_midi_input_device_dd_open = false;
            midi_service_stop();
            midi_input_shutdown();
            midi_input_init("NeoBAE", g_midi_device_api[chosen], g_midi_device_port[chosen]);
            midi_service_start();
            save_settings(g_current_bank_path[0] ? g_current_bank_path : NULL, *reverbType, *loopPlay);
        }
    }

    // MIDI output device dropdown
    // Don't render the output dropdown while the input dropdown is open
    if (g_midi_output_device_dd_open && !g_midi_input_device_dd_open)
    {
        int outOffset = g_midi_input_device_count;
        if (outOffset < 0)
            outOffset = 0;
        if (outOffset > 63)
            outOffset = 63;
        int chosen = render_clamped_device_menu(R, midiOutDevRect, dlgH,
                                                &g_midi_device_name_cache[outOffset], g_midi_output_device_count,
                                                g_midi_output_device_index, &g_midi_output_dd_scroll,
                                                &g_midi_output_device_dd_open, mx, my, mclick);
        if (chosen >= 0)
        {
            g_midi_output_device_index = chosen;
            g_midi_output_device_dd_open = false;
            midi_output_send_all_notes_off();
            midi_output_shutdown();
            midi_output_init("NeoBAE", g_midi_device_api[outOffset + chosen], g_midi_device_port[outOffset + chosen]);
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
#if USE_SDL2 == TRUE
                SDL_RenderDrawLine(R, ir.x, ir.y + ir.h, ir.x + ir.w, ir.y + ir.h);
#else
                SDL_RenderLine(R, ir.x, ir.y + ir.h, ir.x + ir.w, ir.y + ir.h);
#endif
            }
            SDL_Color itxt = (i == g_volume_curve || over) ? g_button_text : g_text_color;
            draw_text(R, ir.x + 6, ir.y + 6, volumeCurveNames[i], itxt);
            if (over && mclick)
            {
                g_volume_curve = i;
                g_volumeCurveDropdownOpen = false;
                BAESong theSong = NULL;
                if (g_live_song) {
                    theSong = g_live_song;
                } else if (g_bae.song) {
                    theSong = g_bae.song;
                }
#if USE_SF2_SUPPORT == TRUE
                if (theSong && !BAESong_IsSF2Song(theSong))
#elif USE_NATIVE_DLS == TRUE
                if (theSong && !BAESong_IsDLSSong(theSong))
#elif USE_SF2_SUPPORT == TRUE && USE_NATIVE_DLS == TRUE
                if (theSong && !BAESong_IsSF2Song(theSong) && !BAESong_IsDLSSong(theSong))
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

#if USE_MPEG_ENCODER == TRUE
    if (g_exportDropdownOpen)
    {
        int codecCount = g_exportCodecCount;
        int cols = 2;
        int rows = (codecCount + cols - 1) / cols;
        int gapX = 6;
        int itemH = expRect.h;
        int itemW = expRect.w;
        int boxW = itemW * cols + gapX * (cols - 1);
        int boxH = itemH * rows;
        int boxY = expRect.y + expRect.h + 1;
        if (boxY + boxH > dlg.h && expRect.y - boxH - 1 >= 0)
            boxY = expRect.y - boxH - 1;
        Rect box = {expRect.x, boxY, boxW, boxH};
        SDL_Color ddBg = g_panel_bg;
        ddBg.a = 255;
        Rect shadowRect = {box.x + 2, box.y + 2, box.w, box.h};
        SDL_Color shadow = {0, 0, 0, g_is_dark_mode ? 160 : 120};
        draw_rect(R, shadowRect, shadow);
        draw_rect(R, box, ddBg);
        draw_frame(R, box, g_panel_border);
        for (int i = 0; i < codecCount; ++i)
        {
            int col = i / rows;
            int row = i % rows;
            Rect ir = {box.x + col * (itemW + gapX), box.y + row * itemH, itemW, itemH};
            bool over = point_in(mx, my, ir);
            SDL_Color ibg = (i == g_exportCodecIndex) ? g_highlight_color : g_panel_bg;
            if (over)
                ibg = g_button_hover;
            draw_rect(R, ir, ibg);
            if (row < rows - 1)
            {
                SDL_SetRenderDrawColor(R, g_panel_border.r, g_panel_border.g, g_panel_border.b, 255);
#if USE_SDL2 == TRUE
                SDL_RenderDrawLine(R, ir.x, ir.y + ir.h, ir.x + ir.w, ir.y + ir.h);
#else
                SDL_RenderLine(R, ir.x, ir.y + ir.h, ir.x + ir.w, ir.y + ir.h);
#endif
            }
            draw_text(R, ir.x + 6, ir.y + 6, g_exportCodecNames[i], g_button_text);
            if (over && mclick)
            {
                int oldExportIdx = g_exportCodecIndex;
                g_exportCodecIndex = i;
                g_exportDropdownOpen = false;
                if (oldExportIdx != g_exportCodecIndex)
                    save_settings(g_current_bank_path[0] ? g_current_bank_path : NULL, *reverbType, *loopPlay);
            }
        }
        if (mclick && !point_in(mx, my, box) && !point_in(mx, my, expRect))
            g_exportDropdownOpen = false;
    }
#endif

    if (g_dls_compat_tooltip_visible)
    {
        ui_draw_tooltip(R, g_dls_compat_tooltip_rect, g_dls_compat_tooltip_text, true, true);
    }

    g_settings_wheel_delta = 0;
}

void settings_init(void)
{
    g_show_settings_dialog = false;
    g_volumeCurveDropdownOpen = false;
    g_sampleRateDropdownOpen = false;
    g_volume_curve = 1;
    g_stereo_output = true;
    g_sample_rate_hz = 44100;
#if BAE_FIX_SPAN_DC == TRUE
    g_panfix_enabled = true;
#endif
#if BAE_CLASSIC_CHORUS == TRUE
    g_classic_chorus_enabled = false;
#endif
    g_classic_lfo_enabled = false;
    g_terp_mode = BAE_LINEAR_INTERPOLATION;
    g_output_16bit = true;
    g_selected_eq_preset = 0;
    g_current_custom_eq_preset[0] = '\0';
    g_preset_dialog_is_eq = false;
    g_eq_enabled = false;
    for (int i = 0; i < 5; i++) g_eq_gains[i] = 0.0f;
}

void settings_cleanup(void)
{
    if (g_settings_renderer)
    {
        SDL_DestroyRenderer(g_settings_renderer);
        g_settings_renderer = NULL;
    }
    if (g_settings_window)
    {
        SDL_DestroyWindow(g_settings_window);
        g_settings_window = NULL;
    }
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
    
    // Free custom EQ preset list
    if (g_custom_eq_presets)
    {
        free(g_custom_eq_presets);
        g_custom_eq_presets = NULL;
    }
    g_custom_eq_preset_count = 0;
}

// Custom reverb preset management
CustomReverbPreset *g_custom_reverb_presets = NULL;
int g_custom_reverb_preset_count = 0;
char g_current_custom_reverb_preset[64] = {0}; // Track which preset is currently loaded
int g_current_custom_reverb_lowpass = 64; // Track current lowpass (engine has no getter)
// Track the *intended* custom reverb values (so import/export/persistence round-trips exactly).
// Engine getters can quantize due to internal fixed-point conversions.
int g_current_custom_reverb_comb_count = 0; // 0 means "not yet synced"
int g_current_custom_reverb_delays[NEO_CUSTOM_MAX_COMBS] = {0};
int g_current_custom_reverb_feedback[NEO_CUSTOM_MAX_COMBS] = {0};
int g_current_custom_reverb_gain[NEO_CUSTOM_MAX_COMBS] = {127, 127, 127, 127};

// Custom EQ preset management
CustomEQPreset *g_custom_eq_presets = NULL;
int g_custom_eq_preset_count = 0;
char g_current_custom_eq_preset[64] = {0};
int g_selected_eq_preset = 0;
bool g_preset_dialog_is_eq = false;
bool g_eq_enabled = false;
float g_eq_gains[5] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f};

#if USE_NEO_EFFECTS
static void ensure_current_custom_reverb_state_synced_from_engine(void)
{
    if (g_current_custom_reverb_comb_count < 1)
    {
        g_current_custom_reverb_comb_count = GetNeoCustomReverbCombCount();
        if (g_current_custom_reverb_comb_count < 1)
            g_current_custom_reverb_comb_count = 1;
        if (g_current_custom_reverb_comb_count > NEO_CUSTOM_MAX_COMBS)
            g_current_custom_reverb_comb_count = NEO_CUSTOM_MAX_COMBS;

        for (int i = 0; i < NEO_CUSTOM_MAX_COMBS; i++)
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

    char settings_path[768];
    zefidi_get_settings_path(settings_path, sizeof(settings_path));

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
        for (int j = 0; j < NEO_CUSTOM_MAX_COMBS; j++)
        {
            tmp[i].delays[j] = 0;
            tmp[i].feedback[j] = 0;
            tmp[i].gain[j] = 127;
        }
        tmp[i].lowpass = 64;
        tmp[i].mix = 255;  // Default to max wet
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
            if (comb >= 0 && comb < NEO_CUSTOM_MAX_COMBS)
                tmp[idx].delays[comb] = atoi(value);
        }
        else if (strncmp(key, "feedback_", 9) == 0)
        {
            int comb = atoi(key + 9);
            if (comb >= 0 && comb < NEO_CUSTOM_MAX_COMBS)
                tmp[idx].feedback[comb] = atoi(value);
        }
        else if (strncmp(key, "gain_", 5) == 0)
        {
            int comb = atoi(key + 5);
            if (comb >= 0 && comb < NEO_CUSTOM_MAX_COMBS)
                tmp[idx].gain[comb] = atoi(value);
        }
        else if (strcmp(key, "lowpass") == 0)
        {
            tmp[idx].lowpass = atoi(value);
        }
        else if (strcmp(key, "mix") == 0)
        {
            tmp[idx].mix = atoi(value);
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
    char settings_path[768];
    zefidi_get_settings_path(settings_path, sizeof(settings_path));

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
            size_t bytes_read = fread(content, 1, content_size, f);
            content[bytes_read] = '\0';
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
    for (int i = 0; i < NEO_CUSTOM_MAX_COMBS; i++)
    {
        fprintf(f, "custom_reverb_%d_delay_%d=%d\n", preset_idx, i, g_current_custom_reverb_delays[i]);
        fprintf(f, "custom_reverb_%d_feedback_%d=%d\n", preset_idx, i, g_current_custom_reverb_feedback[i]);
        fprintf(f, "custom_reverb_%d_gain_%d=%d\n", preset_idx, i, g_current_custom_reverb_gain[i]);
    }
    fprintf(f, "custom_reverb_%d_lowpass=%d\n", preset_idx, g_current_custom_reverb_lowpass);
    int current_mix = GetNeoReverbMix();
    fprintf(f, "custom_reverb_%d_mix=%d\n", preset_idx, current_mix);
    
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
    if (g_current_custom_reverb_comb_count > NEO_CUSTOM_MAX_COMBS)
        g_current_custom_reverb_comb_count = NEO_CUSTOM_MAX_COMBS;
    for (int i = 0; i < NEO_CUSTOM_MAX_COMBS; i++)
    {
        g_current_custom_reverb_delays[i] = preset->delays[i];
        g_current_custom_reverb_feedback[i] = preset->feedback[i];
        g_current_custom_reverb_gain[i] = preset->gain[i];
    }

    SetNeoCustomReverbCombCount(preset->comb_count);
    
    for (int i = 0; i < NEO_CUSTOM_MAX_COMBS; i++)
    {
        SetNeoCustomReverbCombDelay(i, preset->delays[i]);
        SetNeoCustomReverbCombFeedback(i, preset->feedback[i]);
        SetNeoCustomReverbCombGain(i, preset->gain[i]);
    }

    g_current_custom_reverb_lowpass = preset->lowpass;
    SetNeoCustomReverbLowpass(g_current_custom_reverb_lowpass);
    
    SetNeoReverbMix(preset->mix);
    
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
    int current_mix = GetNeoReverbMix();
    fprintf(f, "  <mix>%d</mix>\n", current_mix);
    for (int i = 0; i < NEO_CUSTOM_MAX_COMBS; i++)
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
    for (int i = 0; i < NEO_CUSTOM_MAX_COMBS; i++)
    {
        preset.delays[i] = 0;
        preset.feedback[i] = 0;
        preset.gain[i] = 127;
    }
    preset.lowpass = 64;
    preset.mix = 110;  // Default to 110

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
    if (xml_get_tag_text(xml, "mix", buf, sizeof(buf)))
        preset.mix = atoi(buf);

    // Parse <comb .../> entries
    const char *p = xml;
    while ((p = strstr(p, "<comb")) != NULL)
    {
        int index = -1, delayMs = -1, feedback = -1, gain = -1;
        if (sscanf(p, "<comb index=\"%d\" delayMs=\"%d\" feedback=\"%d\" gain=\"%d\"",
                   &index, &delayMs, &feedback, &gain) == 4)
        {
            if (index >= 0 && index < NEO_CUSTOM_MAX_COMBS)
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
    if (g_current_custom_reverb_comb_count > NEO_CUSTOM_MAX_COMBS)
        g_current_custom_reverb_comb_count = NEO_CUSTOM_MAX_COMBS;
    SetNeoCustomReverbCombCount(g_current_custom_reverb_comb_count);
    for (int i = 0; i < NEO_CUSTOM_MAX_COMBS; i++)
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
    SetNeoReverbMix(preset.mix);
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
    char settings_path[768];
    zefidi_get_settings_path(settings_path, sizeof(settings_path));

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
            size_t bytes_read = fread(content, 1, content_size, f);
            content[bytes_read] = '\0';
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

void render_preset_name_dialog(SDL_Renderer *R, int mx, int my, bool mclick, bool mdown, int window_h, int *reverbType)
{
    extern bool g_show_eq_dialog;
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
#if USE_SDL2 == TRUE
    SDL_RenderDrawLine(R, cursor_x, inputBox.y + 4, cursor_x, inputBox.y + inputBox.h - 4);
#else
    SDL_RenderLine(R, cursor_x, inputBox.y + 4, cursor_x, inputBox.y + inputBox.h - 4);
#endif
    
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
            if (g_show_eq_dialog)
            {
                save_custom_eq_preset(g_preset_name_input);
            }
            else
            {
                save_custom_reverb_preset(g_preset_name_input);
                int preset_list_idx = get_custom_reverb_preset_index(g_preset_name_input);
                if (preset_list_idx >= 0 && reverbType)
                {
                    load_custom_reverb_preset(g_preset_name_input);
                    extern int g_custom_reverb_preset_count;
                    int base_count = get_reverb_count() - g_custom_reverb_preset_count;
                    *reverbType = base_count + preset_list_idx + 1;
                    bae_set_reverb(*reverbType);
                    save_settings(g_current_bank_path[0] ? g_current_bank_path : NULL, *reverbType, g_bae.loop_enabled_gui);
                }
            }
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

void load_custom_eq_preset_list(void)
{
    // Free existing presets
    if (g_custom_eq_presets)
    {
        free(g_custom_eq_presets);
        g_custom_eq_presets = NULL;
    }
    g_custom_eq_preset_count = 0;

    char settings_path[768];
    zefidi_get_settings_path(settings_path, sizeof(settings_path));

FILE *f = fopen(settings_path, "r");
    if (!f) return;

    // First pass: find maximum numeric preset index present in custom_eq_%d_* keys
    int max_idx = -1;
    char line[512];
    while (fgets(line, sizeof(line), f))
    {
        // Strip newline and carriage return
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        char *cr = strchr(line, '\r');
        if (cr) *cr = '\0';

        if (strncmp(line, "custom_eq_", 10) != 0)
            continue;

        const char *p = line + 10;
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

    // Temporary dense array by numeric index; later compact to g_custom_eq_presets
    CustomEQPreset *tmp = (CustomEQPreset *)calloc((size_t)max_idx + 1, sizeof(CustomEQPreset));
    if (!tmp)
    {
        fclose(f);
        return;
    }

    for (int i = 0; i <= max_idx; i++)
    {
        for (int j = 0; j < 5; j++)
        {
            tmp[i].gains[j] = 0.0f;
        }
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

        if (strncmp(line, "custom_eq_", 10) != 0)
            continue;

        const char *p = line + 10;
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
        else if (strncmp(key, "gain_", 5) == 0)
        {
            int band = atoi(key + 5);
            if (band >= 0 && band < 5)
                tmp[idx].gains[band] = (float)atof(value);
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

    int capped_count = (count > 65) ? 65 : count;

    g_custom_eq_presets = (CustomEQPreset *)malloc(sizeof(CustomEQPreset) * (size_t)capped_count);
    if (!g_custom_eq_presets)
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
        g_custom_eq_presets[out++] = tmp[i];
    }
    g_custom_eq_preset_count = out;
    free(tmp);
}

int get_custom_eq_preset_index(const char *name)
{
    if (!name || !g_custom_eq_presets) return -1;
    
    for (int i = 0; i < g_custom_eq_preset_count; i++)
    {
        if (strcmp(g_custom_eq_presets[i].name, name) == 0)
        {
            return i;
        }
    }
    return -1;
}

void save_custom_eq_preset(const char *name)
{
    if (!name || !name[0]) return;

    extern void set_status_message(const char *msg);
    
    // Read all existing content
    char settings_path[768];
    zefidi_get_settings_path(settings_path, sizeof(settings_path));

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
            size_t bytes_read = fread(content, 1, content_size, f);
            content[bytes_read] = '\0';
        }
        fclose(f);
    }
    
    // Find existing preset index by name, or allocate next available
    int preset_idx = -1;
    int max_idx = -1;
    
    if (content)
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

            if (strncmp(line_buf, "custom_eq_", 10) != 0)
                continue;

            const char *p = line_buf + 10;
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
    
    if (preset_idx < 0)
    {
        if (g_custom_eq_preset_count >= 65)
        {
            set_status_message("Too many custom EQ presets (max 65)");
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
    
    if (content)
    {
        char prefix[64];
        snprintf(prefix, sizeof(prefix), "custom_eq_%d_", preset_idx);
        int prefix_len = strlen(prefix);
        
        char *write_ptr = content;
        char line_buf[512];
        
        while (*write_ptr)
        {
            int i = 0;
            while (*write_ptr && *write_ptr != '\n' && i < sizeof(line_buf) - 1)
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
    }
    
    // Append new preset data
    fprintf(f, "custom_eq_%d_name=%s\n", preset_idx, name);
    for (int i = 0; i < 5; i++)
    {
        float gain = 0.0f;
        if (g_bae.mixer)
        {
            BAEMixer_GetEQGain(g_bae.mixer, i, &gain);
        }
        fprintf(f, "custom_eq_%d_gain_%d=%f\n", preset_idx, i, gain);
    }
    
    fclose(f);
    
    // Update current preset name
    safe_strncpy(g_current_custom_eq_preset, name, sizeof(g_current_custom_eq_preset) - 1);
    g_current_custom_eq_preset[sizeof(g_current_custom_eq_preset) - 1] = '\0';
    
    // Reload preset list
    load_custom_eq_preset_list();

    // Set active EQ preset to the saved custom preset
    int custom_idx = get_custom_eq_preset_index(name);
    if (custom_idx >= 0)
    {
        g_selected_eq_preset = 8 + custom_idx;
    }
    save_settings(g_current_bank_path[0] ? g_current_bank_path : NULL, g_bae.current_reverb_type, g_bae.loop_enabled_gui);
}

void load_custom_eq_preset(const char *name)
{
    if (!name || !name[0]) return;
    
    int idx = get_custom_eq_preset_index(name);
    if (idx < 0) return;
    
    CustomEQPreset *preset = &g_custom_eq_presets[idx];

    for (int i = 0; i < 5; i++)
    {
        g_eq_gains[i] = preset->gains[i];
        if (g_bae.mixer)
        {
            BAEMixer_SetEQGain(g_bae.mixer, i, preset->gains[i]);
        }
    }
    
    // Update current preset name
    safe_strncpy(g_current_custom_eq_preset, name, sizeof(g_current_custom_eq_preset) - 1);
    g_current_custom_eq_preset[sizeof(g_current_custom_eq_preset) - 1] = '\0';
}

void delete_custom_eq_preset(const char *name)
{
    if (!name || !name[0]) return;

    int preset_file_idx = -1;
    
    char settings_path[768];
    zefidi_get_settings_path(settings_path, sizeof(settings_path));

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
            size_t bytes_read = fread(content, 1, content_size, f);
            content[bytes_read] = '\0';
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

            if (strncmp(line_buf, "custom_eq_", 10) == 0)
            {
                const char *p = line_buf + 10;
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
    snprintf(prefix, sizeof(prefix), "custom_eq_%d_", preset_file_idx);
    int prefix_len = strlen(prefix);

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
    if (strcmp(g_current_custom_eq_preset, name) == 0)
    {
        g_current_custom_eq_preset[0] = '\0';
    }
    
    // Reload preset list
    load_custom_eq_preset_list();
}
