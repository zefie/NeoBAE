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

// SDL3 GUI for NeoBAE – inspired by the BXPlayer GUI.
// zefie
// 2025-08-22: This file is still a hot mess even after refactor,
//             but we shaved nearly 5000 lines from it
// 2025-12-02: Updated to SDL3

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <time.h>
#include <stdarg.h>
#include <math.h> // for cosf/sinf gear icon
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdint.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN 1
#include <windows.h>
#include <commdlg.h>
#include <stdlib.h> // for _fullpath

#include <winreg.h>   // for registry access
#include <shellapi.h> // for ShellExecuteA
#endif

#if !defined(_WIN32)
#include <stdio.h>
#include <errno.h>
#include <stdlib.h> // for realpath
#include <limits.h> // for PATH_MAX
#include <unistd.h> // for readlink
#endif

#include "NeoBAE.h"
#include "gui_bae.h"
#include "BAE_API.h" // for BAE_GetDeviceSamplesPlayedPosition diagnostics
#include "X_Assert.h"

#if USE_SDL2 == TRUE
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
#include <SDL2/SDL_syswm.h>
#else
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_opengl.h>
#endif

// GUI includes
#include "gui_common.h"
#include "gui_settings.h"
#include "gui_dialogs.h"
#include "gui_bae.h"
#include "gui_export.h"
#include "gui_theme.h"    // for theme globals and detection functions
#include "gui_widgets.h"  // for UI widget functions
#include "gui_text.h"     // for text rendering functions
#include "gui_midi.h"     // for virtual keyboard functions
#include "gui_playlist.h" // for playlist panel functions
#include "gui_panels.h"
#include "gui_frame.h"
#include "gui_channels.h"
#include "gui_playback_controls.h"
#include "gui_transport.h"
#include "gui_keyboard_panel.h"
#include "gui_status_bank.h"
#include "gui_file_open.h"
#if _DEBUG == TRUE
#include "gui_debug_console.h" // for debug console window
#endif

#if SUPPORT_BAESCRIPT == TRUE
#include "gui_script_editor.h"
#endif

#if USE_SF2_SUPPORT == TRUE
    #if _USING_FLUIDLITE == TRUE
        #include "GenSF2_FluidLite.h"
    #endif
#endif

#if USE_XMF_SUPPORT == TRUE && (_USING_FLUIDLITE == TRUE || USE_NATIVE_DLS == TRUE)
#include "GenXMF.h"
#endif

#if USE_NATIVE_DLS == TRUE
#include "GenDLS_MobileBAE.h"
#endif

int g_thread_ch_enabled[BAE_MAX_MIDI_CHANNELS] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
/* Forward-declare dialog renderer from gui_dialogs.c to avoid including the
    full header (which defines globals that conflict with this file's statics). */
void render_about_dialog(SDL_Renderer *R, int mx, int my, bool mclick);

#if SUPPORT_MIDI_HW == TRUE
#include "gui_midi_hw.h"
#endif

#if SUPPORT_KARAOKE == TRUE
#include "gui_karaoke.h"
#endif

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif


// Forward declarations for internal types used in meta callback to avoid including heavy internal headers
struct GM_Song;       // opaque
typedef short int16_t; // 16-bit signed used by engine for track index

void safe_strncpy(char *dst, const char *src, size_t size) {
    if (size == 0)
        return;

    size_t len = strlen(src);
    if (len >= size)
        len = size;

    memcpy(dst, src, len);
    dst[len] = '\0';
}


// Helper function to update Bank/Program display values based on current channel's bank settings
void update_bank_program_for_channel(void)
{
#if SUPPORT_MIDI_HW == TRUE
    // Check if we're actually in MIDI input mode
    if (g_midi_input_enabled)
    {
        // Use the tracked MIDI bank values for the current channel when MIDI input is active
        g_keyboard_bank = g_midi_bank[g_keyboard_channel];
        g_keyboard_program = g_midi_bank_program[g_keyboard_channel];
        return;
    }
#endif
    // When MIDI input is not active, try to query the engine
    // for the current program/bank settings for this channel
    BAESong target = g_bae.song ? g_bae.song : g_live_song;
    if (target)
    {
        unsigned char program = 0;
        unsigned char bank = 0;
        BAEResult result = BAESong_GetProgramBank(target, (unsigned char)g_keyboard_channel, &program, &bank, TRUE);
        if (result == BAE_NO_ERROR)
        {
            // For General MIDI compatibility, bank 0 typically maps to Bank=0, Program=0
            // Higher banks may use different Bank/Program combinations depending on the sound bank
            g_keyboard_bank = bank;
            g_keyboard_program = program;
        }
        else
        {
            // Fallback to default values if query fails
            g_keyboard_bank = 0;
            g_keyboard_program = 0;
        }
    }
    else
    {
        // No song available, use default values
        g_keyboard_bank = 0;
        g_keyboard_program = 0;
    }
}

// Helper function to send bank select messages when Bank/Program values change
void send_bank_select_for_current_channel(void)
{
#if SUPPORT_MIDI_HW == TRUE
    // Update the tracked bank values for the current channel
    g_midi_bank[g_keyboard_channel] = g_keyboard_bank;
    g_midi_bank_program[g_keyboard_channel] = g_keyboard_program;
#endif

    BAE_PRINTF("Bank Select - Channel %d: Bank=%d, Program=%d\n", g_keyboard_channel + 1, g_keyboard_bank, g_keyboard_program);

    // Send bank select messages to both the engine and external MIDI output
    BAESong target = g_bae.song ? g_bae.song : g_live_song;
    if (target)
    {
        BAESong_ProgramBankChange(target, (unsigned char)g_keyboard_channel, g_keyboard_program, g_keyboard_bank, 0);
        BAESong_LoadInstrument(target, g_keyboard_program);
    }

#if SUPPORT_MIDI_HW == TRUE
    // Also send to external MIDI output if enabled
    if (g_midi_output_enabled)
    {
        unsigned char bank_msg[3] = {(unsigned char)(0xB0 | (g_keyboard_channel & 0x0F)), 0, (unsigned char)g_keyboard_bank};
        unsigned char program_msg[3] = {(unsigned char)(0xB0 | (g_keyboard_channel & 0x0F)), 32, (unsigned char)g_keyboard_program};
        midi_output_send(bank_msg, 3);
        midi_output_send(program_msg, 3);
    }
#endif
}

// Calculate the required window height based on visible panels
int calculate_window_height(void)
{
    // Transport panel
    int transportPanelY = 160;
    int transportPanelH = 85;

    // Keyboard panel comes after transport
    int keyboardPanelY = transportPanelY + transportPanelH + 10;
    int keyboardPanelH = 110;
    bool showKeyboard = g_show_virtual_keyboard; // Simplified for init
    bool showWaveform = false;                   // No file loaded yet during init

#if SUPPORT_KARAOKE == TRUE
    int karaokePanelHeight = 40;
    bool showKaraoke = false; // No karaoke during init
#endif

    int statusY = ((showKeyboard || showWaveform) ? (keyboardPanelY + keyboardPanelH + 10) : (transportPanelY + transportPanelH + 10));
#if SUPPORT_KARAOKE == TRUE
    if (showKaraoke)
        statusY += karaokePanelHeight + 5;
#endif

    // Add playlist panel
    int playlistPanelHeight = 300;
    statusY += playlistPanelHeight + 10;

    return statusY + 115; // status panel + bottom padding
}

// Panel and slider helpers moved to gui_panels.{c,h}

// Embedded TTF font (generated header). Define EMBED_TTF_FONT and
// generate embedded_font.h via scripts/create_embedded_font_h.py to enable.
#ifdef EMBED_TTF_FONT
#include "embedded_font.h" // provides embedded_font_data[], embedded_font_size
#endif

void gui_audio_task(void *reference)
{
    if (reference)
    {
        if (!g_exporting) {
            BAEMixer_ServiceStreams(reference);
        }
    }
}

// Platform file open dialog abstraction. Returns malloc'd string (caller frees) or NULL.

// Global variable to track current bank path for settings saving
char g_current_bank_path[512] = "";

#define WINDOW_W 900
int g_window_h = WINDOW_BASE_H; // dynamic height (expands when karaoke visible)

// Global window reference for settings saving
SDL_Window *g_main_window = NULL;

// ------------- NeoBAE integration -------------

BAEGUI g_bae = {0};
bool g_reverbDropdownOpen = false;

// Utility function to get absolute path (moved from gui_main_new.c)
char *get_absolute_path(const char *path)
{
    if (!path || !path[0])
        return NULL;

    // Handle special case for built-in bank
    if (strcmp(path, "__builtin__") == 0)
    {
        char *result = malloc(strlen(path) + 1);
        if (result)
        {
            strcpy(result, path);
        }
        return result;
    }

#ifdef _WIN32
    char *abs_path = malloc(MAX_PATH);
    if (abs_path && _fullpath(abs_path, path, MAX_PATH))
    {
        return abs_path;
    }
    if (abs_path)
        free(abs_path);
    return NULL;
#else
    char *abs_path = realpath(path, NULL);
    return abs_path; // realpath allocates memory that caller must free
#endif
}

#if SUPPORT_KARAOKE == TRUE
#define KARAOKE_MAX_LINES 256

// Forward declaration (defined later) so helpers can call it
void karaoke_commit_line(uint32_t time_us, const char *line);
#endif

// Progress/VU state moved to gui_transport.c / gui_status_bank.c / gui_channels.c
// Toggle for WebTV-style progress bar (Settings -> "WebTV Style Bar")
bool g_disable_webtv_progress_bar = false; // default: WebTV enabled

// Dialog state (defined in gui_dialogs.c) - reference via externs so both
// translation units share the same state and rendering functions.
extern bool g_show_rmf_info_dialog;
extern bool g_rmf_info_loaded;
extern char g_rmf_info_values[INFO_TYPE_COUNT][512];

extern bool g_show_about_dialog;
extern int g_about_page;

extern bool g_bank_tooltip_visible;
extern Rect g_bank_tooltip_rect;
extern char g_bank_tooltip_text[520];

extern bool g_file_tooltip_visible;
extern Rect g_file_tooltip_rect;
extern char g_file_tooltip_text[520];

extern bool g_reverb_tooltip_visible;
extern Rect g_reverb_tooltip_rect;
extern char g_reverb_tooltip_text[520];

extern bool g_loop_tooltip_visible;
extern Rect g_loop_tooltip_rect;
extern char g_loop_tooltip_text[520];

extern bool g_voice_tooltip_visible;
extern Rect g_voice_tooltip_rect;
extern char g_voice_tooltip_text[520];

extern bool g_program_tooltip_visible;
extern Rect g_program_tooltip_rect;
extern char g_program_tooltip_text[520];

extern bool g_bank_tooltip_visible;
extern Rect g_bank_tooltip_rect;
extern char g_bank_tooltip_text[520];

extern bool g_dls_compat_tooltip_visible;
extern Rect g_dls_compat_tooltip_rect;
extern char g_dls_compat_tooltip_text[520];

void setWindowTitle(SDL_Window *window)
{
#ifndef _VERSION
#define _VERSION "unknown"
#endif
    const char *libNeoBAECPUArch = BAE_GetCurrentCPUArchitecture();
    char debug[64];
#if _LDEBUG == TRUE
    snprintf(debug, sizeof(debug), " (debug build with symbols)");
#elif _DEBUG == TRUE
    snprintf(debug, sizeof(debug), " (debug build)");
#else
    debug[0] = '\0';
#endif

    char windowTitle[128];
    snprintf(windowTitle, sizeof(windowTitle), "zefidi Media Player - %s - version %s%s", libNeoBAECPUArch, _VERSION, debug);
    SDL_SetWindowTitle(window, windowTitle);
}

void setWindowIcon(SDL_Window *window)
{
#ifdef _WIN32
    // On Windows, the icon will be automatically loaded from the resource file
    // when the executable is built with the resource compiled in.
    // The window icon is typically handled by the system for applications with embedded icons.

    // Try to get the window handle and set the icon manually as a fallback
#if USE_SDL2 == TRUE
    SDL_SysWMinfo wmInfo;
    SDL_VERSION(&wmInfo.version);
    if (SDL_GetWindowWMInfo(window, &wmInfo)) {
        HWND hwnd = wmInfo.info.win.window;
#else
    SDL_PropertiesID props = SDL_GetWindowProperties(window);
    if (props) {
        void *hw = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
        if (hw) {
            HWND hwnd = (HWND)hw;
#endif
            HINSTANCE hInstance = GetModuleHandle(NULL);
            HICON hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(101));

            if (hIcon)
            {
                SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
                SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
                BAE_PRINTF("Successfully set window icon from resource\n");
            }
            else
            {
                BAE_PRINTF("Failed to load icon resource\n");
            }
        }
#if USE_SDL2 != TRUE
    }
#endif
#else
    // On non-Windows platforms, try to load beatnik.ico if available
    char icon_path[512];
    char exe_dir[512];
    get_executable_directory(exe_dir, sizeof(exe_dir));
    snprintf(icon_path, sizeof(icon_path), "%s/beatnik.ico", exe_dir);

    BAE_PRINTF("Icon path (Linux/macOS): %s\n", icon_path);
    // Note: Full icon loading would require SDL2_image or custom ICO parser
#endif
}

#ifdef _WIN32
/* LTSC/LTSB (incl. 2019/17763 and 2021/19044) have historically flaky default
 * Direct3D Present with SDL — force OpenGL before SDL_Init.
 * Previously this only matched ReleaseId == "21H2", so LTSC 2019 never got it. */
static bool isWindowsLTSC(void) {
    HKEY hKey;
    char productName[256] = {0};
    DWORD size = sizeof(productName);

    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
        "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
        0, KEY_READ, &hKey) != ERROR_SUCCESS) {
        return false;
    }

    if (RegGetValueA(hKey, NULL, "ProductName", RRF_RT_REG_SZ, NULL, productName, &size) != ERROR_SUCCESS) {
        RegCloseKey(hKey);
        return false;
    }

    RegCloseKey(hKey);

    return (strstr(productName, "LTSC") != NULL || strstr(productName, "LTSB") != NULL);
}
#endif



// Playlist export functions
int main(int argc, char *argv[])
{
    // Single-instance check (Windows): if another instance exists, forward any file arg and exit.
    if (!gui_file_open_single_instance_or_forward(argc, argv))
        return 0;
#ifdef _WIN32
    if (isWindowsLTSC()) {
        SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengl");
        BAE_PRINTF("Windows LTSC/LTSB detected; forcing SDL render driver to OpenGL\n");
    }
#endif
#if USE_SDL2 == TRUE
    if (SDL_Init(SDL_INIT_VIDEO) != 0)
#else
    if (SDL_Init(SDL_INIT_VIDEO) != true)
#endif
    {
        BAE_PRINTF("SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    
#if _DEBUG == TRUE
    // Initialize debug console
    debug_console_init();
# if _FULL_DEBUG == TRUE
    BAE_PRINTF("Debug console initialized (press D to toggle)\n");
# else
    BAE_PRINTF("Debug console initialized (press F12 to toggle)\n");
# endif
#endif

#if SUPPORT_BAESCRIPT == TRUE
    script_editor_init();
#endif
    
#if USE_SDL2 == TRUE
    if (TTF_Init() != 0)
#else
    if (TTF_Init() != true)
#endif
    {
        BAE_PRINTF("SDL_ttf init failed: %s (continuing with bitmap font)\n", SDL_GetError());
    }
    else
    {
        static int ttf_font_size = 14;

#if EMBED_TTF_FONT == TRUE
#if USE_SDL2 == TRUE
        g_font = TTF_OpenFontRW(SDL_RWFromConstMem(embedded_font_data, embedded_font_size), 1, ttf_font_size);
#elif defined(SDL_IOFromConstMem)
        g_font = TTF_OpenFontIO(SDL_IOFromConstMem(embedded_font_data, embedded_font_size), false, ttf_font_size);
#else
        /* Fall back to SDL_RWFromMem: cast away const to match the API and silence warnings. */
        g_font = TTF_OpenFontIO(SDL_IOFromMem((void *)embedded_font_data, embedded_font_size), false, ttf_font_size);
#endif
#endif
        if (!g_font) {
            const char *tryFonts[] = {
                "C:/Windows/Fonts/arial.ttf",
                "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
                "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                NULL};
            for (int i = 0; tryFonts[i]; ++i)
            {
                if (!g_font)
                {
                    g_font = TTF_OpenFont(tryFonts[i], ttf_font_size);
                }
            }
        }
    }

    // Detect Windows theme
    detect_windows_theme();

    // Preload settings BEFORE creating mixer so we can open with desired format
    bool ch_enable[BAE_MAX_MIDI_CHANNELS];
    for (int i = 0; i < BAE_MAX_MIDI_CHANNELS; i++)
        ch_enable[i] = true; // need early for recreate helper fallback
    int transpose = 0;
    int tempo = 100;
    int volume = 100;
    bool loopPlay = true;
    int reverbLvl = 15, chorusLvl = 15;
    (void)reverbLvl;
    (void)chorusLvl;
    int progress = 0;
    int duration = 0;
    bool playing = false;
    int reverbType = 7;

    Settings settings = load_settings();
    if (settings.has_reverb)
    {
        reverbType = settings.reverb_type;
        if (reverbType == 0)
            reverbType = 1;
    }
    if (settings.has_loop)
    {
        loopPlay = settings.loop_enabled;
    }
    if (settings.has_volume_curve)
    {
        g_volume_curve = (settings.volume_curve >= 0 && settings.volume_curve <= 4) ? settings.volume_curve : 0;
    }
    if (settings.has_sample_rate)
    {
        g_sample_rate_hz = map_rate_from_hz(settings.sample_rate_hz);
    }
    if (settings.has_show_keyboard)
    {
        g_show_virtual_keyboard = settings.show_keyboard;
    }
#if USE_NATIVE_DLS == TRUE
    extern bool g_use_dls_compatiblity_mode;
    if (settings.has_dls_compatibility_mode)
    {
        g_use_dls_compatiblity_mode = settings.dls_compatibility_mode;
    }
#endif
    if (settings.has_normalize)
    {
        g_normalize_enabled = settings.normalize_enabled;
    }
    if (settings.has_export_codec)
    {
        g_exportCodecIndex = settings.export_codec_index;
        if (g_exportCodecIndex < 0)
            g_exportCodecIndex = 0;
    }
    if (settings.has_webtv)
    {
        g_disable_webtv_progress_bar = settings.disable_webtv_progress_bar;
    }
    if (settings.has_stereo)
    {
        g_stereo_output = settings.stereo_output;
    }
    if (settings.has_output_16bit)
    {
        g_output_16bit = settings.output_16bit;
    }
    if (settings.has_terp_mode)
    {
        g_terp_mode = settings.terp_mode;
        if (g_terp_mode < BAE_DROP_SAMPLE || g_terp_mode > BAE_SINC_INTERPOLATION)
            g_terp_mode = BAE_LINEAR_INTERPOLATION;
    }
    if (settings.has_classic_lfo)
    {
        g_classic_lfo_enabled = settings.classic_lfo_enabled;
    }
#if BAE_CLASSIC_CHORUS
    if (settings.has_classic_chorus)
    {
        g_classic_chorus_enabled = settings.classic_chorus_enabled;
    }
#endif
#if USE_WEBTV_MODE
    if (settings.has_webtv_mode)
    {
        g_webtv_mode_enabled = settings.webtv_mode_enabled;
    }
#endif
#if BAE_FIX_SPAN_DC
    if (settings.has_panfix)
    {
        g_panfix_enabled = settings.panfix_enabled;
    }
#endif
    // Apply stored default velocity (aka volume) curve to global engine setting so new songs adopt it
    if (settings.has_volume_curve)
    {
        BAE_SetDefaultVelocityCurve(g_volume_curve);
    }
    if (!bae_init(g_sample_rate_hz))
    {
        BAE_PRINTF("NeoBAE init failed (rate=%d)\n", g_sample_rate_hz);
        if (!bae_init(g_sample_rate_hz))
        {
            BAE_PRINTF("NeoBAE init failed (2nd try)\n");
        }
    }

    // Load bank database AFTER mixer so load_bank can succeed
    load_bankinfo();

#if BAE_FIX_SPAN_DC
    // Apply pan LFO DC fix setting (default is on; user can disable in settings)
    BAE_SetSpanDCFix(g_panfix_enabled ? TRUE : FALSE);
#endif

#if BAE_CLASSIC_CHORUS
    BAE_SetClassicChorus(g_classic_chorus_enabled ? TRUE : FALSE);
#endif
#if USE_WEBTV_MODE
    BAE_SetWebTVMode(g_webtv_mode_enabled ? TRUE : FALSE);
#endif
    BAE_SetClassicLFO(g_classic_lfo_enabled ? TRUE : FALSE);

#if SUPPORT_BAESCRIPT == TRUE
    // Restore script editor state from settings
    if (settings.has_script_path || settings.has_script_text || settings.has_script_enabled)
    {
        script_editor_restore_state(
            settings.has_script_path ? settings.script_path : NULL,
            settings.has_script_text ? settings.script_text : NULL,
            settings.has_script_enabled ? settings.script_enabled : false
        );
    }
#endif

#if SUPPORT_PLAYLIST == TRUE // Initialize playlist system
    playlist_init();

    // Apply playlist settings AFTER playlist_init() to avoid being reset
    if (settings.has_shuffle)
    {
        g_playlist.shuffle_enabled = settings.shuffle_enabled;
    }
    if (settings.has_repeat)
    {
        g_playlist.repeat_mode = settings.repeat_mode;
    }
    if (settings.has_playlist_enabled) {
        g_playlist.enabled = settings.playlist_enabled;
    } else {
        g_playlist.enabled = true; // default to enabled
    }

#endif

    // Initialize export subsystem
    export_init();
    
    // Load custom reverb presets
#if USE_NEO_EFFECTS
    load_custom_reverb_preset_list();

    // Apply saved custom reverb preset now that the list is available
    if (settings.has_custom_reverb_preset && settings.custom_reverb_preset_name[0])
    {
        int preset_list_idx = get_custom_reverb_preset_index(settings.custom_reverb_preset_name);
        if (preset_list_idx >= 0)
        {
            load_custom_reverb_preset(settings.custom_reverb_preset_name);
            extern int g_custom_reverb_preset_count;
            int base_count = get_reverb_count() - g_custom_reverb_preset_count;
            reverbType = base_count + preset_list_idx + 1; // UI index is 1-based
        }
    }
#endif

    // Load custom EQ presets
    load_custom_eq_preset_list();

    // Apply saved active EQ preset and set selected index
    g_selected_eq_preset = 7; // Default to Custom
    g_current_custom_eq_preset[0] = '\0';
    if (settings.has_eq_preset && settings.eq_preset_name[0])
    {
        // Check if it's one of the standard presets
        const char *eq_standard_names[] = {
            "Flat", "Bass Boost", "Acoustic", "Rock", "Pop", "Classical", "Vocal"
        };
        bool found_std = false;
        for (int i = 0; i < 7; i++)
        {
            if (strcmp(settings.eq_preset_name, eq_standard_names[i]) == 0)
            {
                g_selected_eq_preset = i;
                found_std = true;
                break;
            }
        }
        if (!found_std)
        {
            if (strcmp(settings.eq_preset_name, "Custom") == 0)
            {
                g_selected_eq_preset = 7;
            }
            else
            {
                int preset_list_idx = get_custom_eq_preset_index(settings.eq_preset_name);
                if (preset_list_idx >= 0)
                {
                    load_custom_eq_preset(settings.eq_preset_name);
                    g_selected_eq_preset = 8 + preset_list_idx;
                }
            }
        }
    }

    // Auto-load playlist.m3u from NeoBAE config root (migrates from exeDir once)
#if SUPPORT_PLAYLIST == TRUE
    {
        char playlist_path[768];
        zefidi_get_playlist_path(playlist_path, sizeof(playlist_path));

        FILE *test_file = fopen(playlist_path, "r");
        if (test_file)
        {
            fclose(test_file);
            BAE_PRINTF("Auto-loading playlist: %s\n", playlist_path);
            playlist_load(playlist_path);
        }
    }
#endif

    if (!g_bae.bank_loaded)
    {
        BAE_PRINTF("WARNING: No patch bank loaded. Place patches.hsb/.zsb/.dls/.sf2 next to the executable or in the NeoBAE config dir, or use built-in patches.\n");
    }

    // Calculate correct window height including playlist panel
    g_window_h = calculate_window_height();

    // Use saved window position if available, otherwise center with 200px offset
    int window_x = SDL_WINDOWPOS_CENTERED;
    int window_y = SDL_WINDOWPOS_CENTERED + 200;
    if (settings.has_window_pos)
    {
        window_x = settings.window_x;
        window_y = settings.window_y;
    }
    const float TARGET_FPS = 60.0f;
    float frame_time_ms = 1000.0f / TARGET_FPS;

#if USE_SDL2 == TRUE
    SDL_Window *win = SDL_CreateWindow("zefidi Media Player", window_x, window_y, 900, g_window_h, 0);
#else
    SDL_Window *win = SDL_CreateWindow("zefidi Media Player", 900, g_window_h, 0);
#endif
    if (!win)
    {
        BAE_PRINTF("Window failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    g_main_window = win; // Store global reference for settings saving
    setWindowTitle(win);
    setWindowIcon(win);
#if USE_SDL2 == TRUE
    SDL_SetWindowResizable(win, SDL_FALSE);
#else
    SDL_SetWindowResizable(win, false);
#endif
    SDL_SetWindowPosition(win, window_x, window_y);

    // Query display refresh rate for adaptive frame timing.
    // Fall back to 60 Hz if the query fails.  Clamp to a sensible range.
    {
#if USE_SDL2 == TRUE        
        int display_index = SDL_GetWindowDisplayIndex(win);
#else
        int display_index = SDL_GetDisplayForWindow(win);
#endif
        if (display_index >= 0)
        {
#if USE_SDL2 == TRUE
            SDL_DisplayMode dm;
            if (SDL_GetDesktopDisplayMode(display_index, &dm) == 0)
#else
            const SDL_DisplayMode *dm = SDL_GetDesktopDisplayMode(display_index);
            if (dm)
#endif
            {
                #if USE_SDL2 == TRUE
                    int32_t refresh_rate = dm.refresh_rate;
                #else
                    int32_t refresh_rate = dm->refresh_rate;
                #endif
                if (refresh_rate >= 60 && refresh_rate <= 240)
                {
                    frame_time_ms = 1000.0f / (float)refresh_rate;
                    BAE_PRINTF("Display refresh rate: %d Hz, frame time: %.2f ms\n",
                               (int32_t)refresh_rate, frame_time_ms);
                }
            }
        }
    }

#if USE_SDL2 == TRUE
    SDL_Renderer *R = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!R)
        R = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    if (!R)
        R = SDL_CreateRenderer(win, -1, 0);
#else
    SDL_Renderer *R = SDL_CreateRenderer(win, NULL);
#endif

    if (!R)
    {
        BAE_PRINTF("Renderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }

#if _DEBUG == TRUE
#if USE_SDL2 == TRUE
    {
        SDL_RendererInfo ri;
        if (SDL_GetRendererInfo(R, &ri) == 0)
            BAE_PRINTF("SDL renderer: %s (flags=0x%x)\n", ri.name ? ri.name : "?", (unsigned)ri.flags);
    }
#else
    {
        const char *rname = SDL_GetRendererName(R);
        BAE_PRINTF("SDL renderer: %s\n", rname ? rname : "?");
        if (!SDL_SetRenderVSync(R, 1))
            BAE_PRINTF("SDL_SetRenderVSync failed: %s\n", SDL_GetError());
    }
#endif
#endif

    bool running = true;
    duration = bae_get_len_ms();
    g_bae.loop_enabled_gui = loopPlay;
    bae_set_volume(volume);
    bae_set_tempo(tempo);
    bae_set_transpose(transpose);
    bae_set_loop(loopPlay);
    bae_set_reverb(reverbType);
#if USE_NATIVE_DLS == TRUE
    GM_DLS_SetMobileBAEQuirks(g_use_dls_compatiblity_mode ? false : true); // inverted
#endif

    // Load bank (use saved bank if available, otherwise fallback)
    if (settings.has_bank && strlen(settings.bank_path) > 0)
    {
        BAE_PRINTF("Loading saved bank: %s\n", settings.bank_path);
        bool loaded_saved = load_bank_simple(settings.bank_path, false, reverbType, loopPlay); // false = don't save to settings (it's already saved)

        if (!loaded_saved)
        {
            BAE_PRINTF("Saved bank missing or failed to load. Trying Default Bank (app/config), then built-in...\n");
            (void)load_bank_simple(NULL, false, reverbType, loopPlay);
        }
        else
        {
            // Only record the saved path if that specific bank actually loaded
            safe_strncpy(g_current_bank_path, settings.bank_path, sizeof(g_current_bank_path) - 1);
            g_current_bank_path[sizeof(g_current_bank_path) - 1] = '\0';
        }

    }
    else
    {
        BAE_PRINTF("No saved bank found, trying Default Bank (app/config), then built-in...\n");
        (void)load_bank_simple(NULL, false, reverbType, loopPlay);
    }

    // Initialize Bank/Program values for the default channel
    update_bank_program_for_channel();

    // Load command line file if provided
    if (argc > 1)
    {
        if (bae_load_song_with_settings(argv[1], transpose, tempo, volume, loopPlay, reverbType, ch_enable, true))
        {
#if SUPPORT_PLAYLIST == TRUE
            // Add file to playlist and set as current
            if (g_playlist.enabled) {
                playlist_update_current_file(argv[1]);
            }
#endif
            duration = bae_get_len_ms();
            g_bae.loop_enabled_gui = loopPlay;
            playing = false;    // Ensure we start from stopped state
            bae_play(&playing); // Auto-start playback
        }
    }

#ifdef _WIN32
    gui_file_open_install_wndproc(win);
#endif

    Uint32 lastTick = SDL_GetTicks();
    bool mdown = false;
    bool mclick = false;
    bool rclick = false; // right mouse button click
    int mx = 0, my = 0;
    int last_drag_progress = -1;      // Track last dragged position to avoid repeated seeks

    while (running)
    {
        Uint64 frame_start = SDL_GetPerformanceCounter();
        SDL_Event e;
        mclick = false;
        rclick = false;
        while (SDL_PollEvent(&e))
        {
#if _DEBUG == TRUE
            // Let debug console handle its events first
            if (debug_console_handle_event(&e)) {
                continue; // Event consumed by debug console, skip main window processing
            }
#endif
#if SUPPORT_BAESCRIPT == TRUE
            if (script_editor_handle_event(&e)) {
                continue;
            }
#endif
            if (settings_handle_event(&e)) {
                continue;
            }
            switch (e.type)
            {
#if USE_SDL2 == TRUE
            case SDL_USEREVENT:
#else
            case SDL_EVENT_USER:
#endif
            {
                if (e.user.code == 1 && e.user.data1)
                {
                    char *incoming = (char *)e.user.data1;
                    GuiFileOpenCtx open_ctx = {
                        .playing = &playing,
                        .progress = &progress,
                        .duration = &duration,
                        .transpose = transpose,
                        .tempo = tempo,
                        .volume = volume,
                        .reverb_type = reverbType,
                        .loop_enabled = loopPlay,
                        .ch_enable = ch_enable,
                    };
                    gui_file_open_path(incoming, GUI_FILE_OPEN_EXTERNAL_IPC, &open_ctx, 0, 0);
                    free(incoming);
                }
            }
            break;

#if USE_SDL2 == TRUE
            case SDL_WINDOWEVENT:
                if (e.window.event == SDL_WINDOWEVENT_CLOSE)
                {
                    settings_window_hide();
                    running = false;
                }
                break;
#else
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                settings_window_hide();
                running = false;
                break;
#endif
#if USE_SDL2 == TRUE
            case SDL_QUIT:
#else
            case SDL_EVENT_QUIT:
#endif
                running = false;
                break;
#if USE_SDL2 == TRUE
            case SDL_MOUSEBUTTONDOWN:
#else
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
#endif
                if (e.button.button == SDL_BUTTON_LEFT)
                {
                    mdown = true;
                }
                break;
#if USE_SDL2 == TRUE
            case SDL_MOUSEBUTTONUP:
#else
            case SDL_EVENT_MOUSE_BUTTON_UP:
#endif
                if (e.button.button == SDL_BUTTON_LEFT)
                {
                    mdown = false;
                    mclick = true;

#if SUPPORT_PLAYLIST == TRUE
                    // Handle playlist drag end
                    if (g_playlist.enabled) {
                        playlist_handle_drag_end();
                    }
#endif
                }
                else if (e.button.button == SDL_BUTTON_RIGHT)
                {
                    rclick = true;
                }
                break;
#if USE_SDL2 == TRUE
            case SDL_MOUSEMOTION:
#else
            case SDL_EVENT_MOUSE_MOTION:
#endif
                mx = e.motion.x;
                my = e.motion.y;

#if SUPPORT_PLAYLIST == TRUE
                // Handle playlist drag update
                if (g_playlist.enabled) {
                    playlist_handle_drag_update(mx, my);
                }
#endif
                break;
#if USE_SDL2 == TRUE
            case SDL_MOUSEWHEEL:
#else
            case SDL_EVENT_MOUSE_WHEEL:
#endif
            {
                // Mouse wheel: when hovered over certain controls, change selection/value by 1
                // For dropdowns we keep existing semantics; for sliders we apply +1 for wheel up, -1 for wheel down.
                int wy = e.wheel.y; // positive = scroll up, negative = scroll down
                if (wy != 0)
                {
                    // Custom reverb modal: allow wheel to adjust sliders by 1 even while modal is open.
                    // (Do not let the wheel leak to the underlying UI.)
                    extern int g_custom_reverb_wheel_delta;
                    extern bool g_show_preset_name_dialog;
                    extern bool g_show_preset_delete_confirm_dialog;
                    if (g_show_custom_reverb_dialog && !g_show_preset_name_dialog && !g_show_preset_delete_confirm_dialog)
                    {
                        g_custom_reverb_wheel_delta += (wy > 0) ? 1 : -1;
                        break;
                    }
                    if (g_show_about_dialog)
                    {
                        /* Licenses page scrolls; Info page ignores wheel. */
                        extern int g_about_wheel_delta;
                        if (g_about_page == 1)
                        {
                            g_about_wheel_delta += (wy > 0) ? -3 : 3;
                        }
                        break;
                    }

                    if (!ui_modal_blocking())
                    {
                        // Recompute rects/layout used by UI so hit tests match rendering
                        UiLayout L;
                        compute_ui_layout(&L);
                        Rect transportPanel = L.transportPanel;
                        Rect chanDD = L.chanDD;
                        Rect ddRect = L.ddRect;
                        int reverbCount = get_reverb_count();

                        // Dropdown delta preserves previous behavior (wheel up -> move up in list)
                        int delta = (wy > 0) ? -1 : 1; // wheel up -> move up (decrement index)
                        // Slider delta: user requested +1 for wheel up, -1 for wheel down
                        int sdelta = (wy > 0) ? 1 : -1;

                        // First handle dropdowns (existing behavior)
                        // Disable reverb dropdown when playing audio files
                        bool reverb_enabled_wheel = !(g_bae.is_audio_file && g_bae.sound);
                        if (point_in(mx, my, ddRect) && reverb_enabled_wheel)
                        {
                            int nt = reverbType + delta;
                            if (nt < 1)
                                nt = 1;
                            if (nt > reverbCount)
                                nt = reverbCount;
                            if (nt != reverbType)
                            {
                                reverbType = nt;

#if USE_NEO_EFFECTS
                                // Mirror click-selection behavior: load preset data when selecting a custom preset.
                                // UI indices are 1-based; get_reverb_name expects 0-based.
                                extern int g_custom_reverb_preset_count;
                                extern char g_current_custom_reverb_preset[64];
                                int base_count = get_reverb_count() - g_custom_reverb_preset_count;
                                int idx0 = reverbType - 1;
                                if (idx0 >= base_count)
                                {
                                    const char *preset_name = get_reverb_name(idx0);
                                    load_custom_reverb_preset(preset_name);
                                }
                                else
                                {
                                    g_current_custom_reverb_preset[0] = '\0';
                                }
#endif

                                bae_set_reverb(reverbType);
                                if (g_current_bank_path[0] != '\0')
                                {
                                    save_settings(g_current_bank_path, reverbType, loopPlay);
                                }
                            }
                        }
                        else if (point_in(mx, my, chanDD))
                        {
                            int nt = g_keyboard_channel + (delta < 0 ? -1 : 1);
                            if (nt < 0)
                                nt = 0;
                            if (nt > 15)
                                nt = 15;
                            if (nt != g_keyboard_channel)
                            {
                                g_keyboard_channel = nt;
                                // Update Bank/Program display values for the newly selected channel
                                update_bank_program_for_channel();
                            }
                        }
                        else
                        {
                            // Slider handling: respect the same modal/enable rules used elsewhere
                            bool playback_controls_enabled_local =
#if SUPPORT_MIDI_HW == TRUE
                                !g_midi_input_enabled && !(g_bae.is_audio_file && g_bae.sound);
#else
                                !(g_bae.is_audio_file && g_bae.sound);
#endif
                            // While the reverb dropdown is expanded, disable transpose/tempo interactions.
                            bool pitch_tempo_enabled_local = playback_controls_enabled_local && !g_reverbDropdownOpen;
                            /* Allow the user to adjust master volume even when MIDI input
                                is enabled so incoming MIDI velocity can be scaled. Keep
                                other playback controls disabled as before. */
                            bool volume_enabled_local = !g_reverbDropdownOpen;

                            // Calculate the Program/Bank rects the same way as in rendering
                            int keyboardPanelY_wheel = transportPanel.y + transportPanel.h + 10;
                            Rect keyboardPanel_wheel = {10, keyboardPanelY_wheel, 880, 110};
                            bool showKeyboard_wheel = g_show_virtual_keyboard && g_bae.song && !g_bae.is_audio_file && g_bae.song_loaded;
                            bool showWaveform_wheel = g_bae.is_audio_file && g_bae.sound;
                            if (showWaveform_wheel)
                                showKeyboard_wheel = false;

                            bool handled_wheel_event = false;

                            // Program/Bank number pickers wheel handling - check first
                            if (showKeyboard_wheel && !(g_bae.is_audio_file && g_bae.sound))
                            {
                                // Calculate Bank/Program rects to match the actual rendering coordinates
                                int picker_y_wheel = keyboardPanel_wheel.y + 56; // below channel dropdown
                                int picker_w_wheel = 35;                         // compact width for 3-digit numbers
                                int picker_h_wheel = 18;
                                int spacing_wheel = 5;

                                // Bank and Program number picker rects (match rendering exactly)
                                Rect bankRect_wheel = {keyboardPanel_wheel.x + 10, picker_y_wheel, picker_w_wheel, picker_h_wheel};
                                Rect programRect_wheel = {bankRect_wheel.x + picker_w_wheel + spacing_wheel, picker_y_wheel, picker_w_wheel, picker_h_wheel};

                                if (point_in(mx, my, bankRect_wheel))
                                {
                                    change_bank_value_for_current_channel(true, sdelta);
                                    handled_wheel_event = true;
                                }
                                else if (point_in(mx, my, programRect_wheel))
                                {
                                    change_bank_value_for_current_channel(false, sdelta);
                                    handled_wheel_event = true;
                                }
                            }

                            // Try transpose/tempo/volume helpers in order (only if Bank/Program didn't handle it)
                            if (!handled_wheel_event)
                            {
                                if (!ui_adjust_transpose(mx, my, sdelta, pitch_tempo_enabled_local, &transpose))
                                {
                                    if (!ui_adjust_tempo(mx, my, sdelta, pitch_tempo_enabled_local, &tempo, &duration, &progress))
                                    {
                                        // For volume we pass the ddRect width as currently used in rendering
                                        // ui_adjust_volume will test using a fixed rect matching rendering
                                        if (!ui_adjust_volume(mx, my, sdelta, volume_enabled_local, &volume))
                                        {
                                            // For volume we pass the ddRect width as currently used in rendering
                                            // ui_adjust_volume will test using a fixed rect matching rendering
                                            if (!ui_adjust_volume(mx, my, sdelta, volume_enabled_local, &volume))
                                            {
#if SUPPORT_PLAYLIST == TRUE
                                                if (g_playlist.enabled && !g_reverbDropdownOpen) {
                                                    // Handle playlist scroll if no other controls handled the wheel event
                                                    // Compute the exact playlist panel rect the same way it is calculated
                                                    // during rendering so wheel handling matches the visible list area.
                                                    int playlistPanelHeight = 300; // same as rendering
                                                    int keyboardPanelY_local = transportPanel.y + transportPanel.h + 10;
                                                    Rect keyboardPanel_local = {10, keyboardPanelY_local, 880, 110};
                                                    bool showKeyboard_local = g_show_virtual_keyboard && g_bae.song && !g_bae.is_audio_file && g_bae.song_loaded;
                                                    bool showWaveform_local = g_bae.is_audio_file && g_bae.sound;
                                                    if (showWaveform_local)
                                                        showKeyboard_local = false;
#if SUPPORT_KARAOKE == TRUE
                                                    bool showKaraoke_local = g_karaoke_enabled && !g_karaoke_suspended &&
                                                                            (g_lyric_count > 0 || g_karaoke_line_current[0] || g_karaoke_line_previous[0]) &&
                                                                            g_bae.song_loaded && !g_bae.is_audio_file;
                                                    int karaokePanelHeight_local = 40;
#endif
                                                    int statusY_local = ((showKeyboard_local || showWaveform_local) ? (keyboardPanel_local.y + keyboardPanel_local.h + 10) : (transportPanel.y + transportPanel.h + 10));
#if SUPPORT_KARAOKE == TRUE
                                                    if (showKaraoke_local)
                                                        statusY_local = statusY_local + karaokePanelHeight_local + 5;
#endif
                                                    int playlistPanelY = statusY_local;
                                                    Rect playlistPanel = {10, playlistPanelY, 880, playlistPanelHeight};

                                                    // Let the playlist module handle the wheel event
                                                    playlist_handle_mouse_wheel(mx, my, wy, playlistPanel);
                                                }
#endif
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
            break;
#if USE_SDL2 == TRUE
            case SDL_DROPFILE:
#else
            case SDL_EVENT_DROP_FILE:
#endif
            {
#if USE_SDL2 == TRUE
                const char *dropped = e.drop.file;
#else
                const char *dropped = e.drop.data;
#endif
                if (dropped)
                {
#if USE_SDL2 == TRUE
                    int drop_mx, drop_my;
#else
                    float drop_mx, drop_my;
#endif
                    SDL_GetMouseState(&drop_mx, &drop_my);
                    GuiFileOpenCtx open_ctx = {
                        .playing = &playing,
                        .progress = &progress,
                        .duration = &duration,
                        .transpose = transpose,
                        .tempo = tempo,
                        .volume = volume,
                        .reverb_type = reverbType,
                        .loop_enabled = loopPlay,
                        .ch_enable = ch_enable,
                    };
                    gui_file_open_path(dropped, GUI_FILE_OPEN_DROP, &open_ctx, (int)drop_mx, (int)drop_my);
                }
            }
            break;
#if USE_SDL2 == TRUE
            case SDL_TEXTINPUT:
#else
            case SDL_EVENT_TEXT_INPUT:
#endif
            {
                extern bool g_show_preset_name_dialog;
                extern char g_preset_name_input[64];
                extern int g_preset_name_cursor;
                if (g_show_preset_name_dialog)
                {
                    const char *txt = e.text.text;
                    if (txt && txt[0])
                    {
                        int len = (int)strlen(g_preset_name_input);
                        for (int i = 0; txt[i] && len < 63; i++)
                        {
                            unsigned char c = (unsigned char)txt[i];
                            if (c >= 32 && c < 127)
                            {
                                g_preset_name_input[len++] = (char)c;
                                g_preset_name_input[len] = '\0';
                            }
                        }
                        g_preset_name_cursor = len;
                    }
                    break; // don't route text input to global shortcuts
                }
            }
            break;
#if USE_SDL2 == TRUE
            case SDL_KEYDOWN:
            case SDL_KEYUP:
#else
            case SDL_EVENT_KEY_DOWN:
            case SDL_EVENT_KEY_UP:
#endif
            {
#if USE_SDL2 == TRUE
                bool isDown = (e.type == SDL_KEYDOWN);
                SDL_Keycode sym = e.key.keysym.sym;
#else
                bool isDown = (e.type == SDL_EVENT_KEY_DOWN);
                SDL_Keycode sym = e.key.key;
#endif
                
                // Handle text input for preset name dialog
                // Handle text input for preset name dialog
                extern bool g_show_preset_name_dialog;
                extern char g_preset_name_input[64];
                extern int g_preset_name_cursor;
                if (g_show_preset_name_dialog && isDown)
                {
                    int len = strlen(g_preset_name_input);
                    if (sym == SDLK_BACKSPACE && len > 0)
                    {
                        g_preset_name_input[len - 1] = '\0';
                        g_preset_name_cursor = len - 1;
                    }
                    else if (sym == SDLK_RETURN || sym == SDLK_KP_ENTER)
                    {
                        if (len > 0)
                        {
                            extern bool g_show_eq_dialog;
                            if (g_show_eq_dialog)
                            {
                                save_custom_eq_preset(g_preset_name_input);
                                
                                int preset_list_idx = get_custom_eq_preset_index(g_preset_name_input);
                                if (preset_list_idx >= 0)
                                {
                                    load_custom_eq_preset(g_preset_name_input);
                                    g_selected_eq_preset = 8 + preset_list_idx;
                                    save_settings(g_current_bank_path[0] ? g_current_bank_path : NULL, reverbType, loopPlay);
                                }
                            }
                            else
                            {
#if USE_NEO_EFFECTS
                                save_custom_reverb_preset(g_preset_name_input);

                                // After saving, switch to the new/updated preset and load it immediately
                                {
                                    int preset_list_idx = get_custom_reverb_preset_index(g_preset_name_input);
                                    if (preset_list_idx >= 0)
                                    {
                                        load_custom_reverb_preset(g_preset_name_input);
                                        extern int g_custom_reverb_preset_count;
                                        int base_count = get_reverb_count() - g_custom_reverb_preset_count;
                                        reverbType = base_count + preset_list_idx + 1; // UI index is 1-based
                                        bae_set_reverb(reverbType);
                                        if (g_current_bank_path[0] != '\0')
                                        {
                                            save_settings(g_current_bank_path, reverbType, loopPlay);
                                        }
                                    }
                                }
#endif
                            }

                            g_show_preset_name_dialog = false;
                            memset(g_preset_name_input, 0, sizeof(g_preset_name_input));
                            g_preset_name_cursor = 0;
                        }
                    }
                    else if (sym == SDLK_ESCAPE)
                    {
                        g_show_preset_name_dialog = false;
                        memset(g_preset_name_input, 0, sizeof(g_preset_name_input));
                        g_preset_name_cursor = 0;
                    }
                    break; // Don't process other keyboard shortcuts when dialog is open
                }

                extern bool g_show_preset_delete_confirm_dialog;
                if (g_show_preset_delete_confirm_dialog && isDown)
                {
                    extern bool g_preset_delete_confirmed;
                    extern char g_preset_delete_name[64];
                    if (sym == SDLK_RETURN || sym == SDLK_KP_ENTER)
                    {
                        g_preset_delete_confirmed = true;
                        g_show_preset_delete_confirm_dialog = false;
                    }
                    else if (sym == SDLK_ESCAPE)
                    {
                        g_show_preset_delete_confirm_dialog = false;
                        g_preset_delete_confirmed = false;
                        memset(g_preset_delete_name, 0, sizeof(g_preset_delete_name));
                    }
                    break; // Don't process other keyboard shortcuts when delete confirm is open
                }

                extern bool g_show_eq_dialog;
                if (g_show_eq_dialog && isDown)
                {
                    if (sym == SDLK_ESCAPE)
                    {
                        g_show_eq_dialog = false;
                    }
                    break; // Don't process other keyboard shortcuts when EQ dialog is open
                }
                
                // Initialize mapping table once
                if (!g_keyboard_map_initialized)
                {
#if USE_SDL2 == TRUE
                    for (int i = 0; i < SDL_NUM_SCANCODES; i++)
#else
                    for (int i = 0; i < SDL_SCANCODE_COUNT; i++)
#endif
                        g_keyboard_pressed_note[i] = -1;
                    g_keyboard_map_initialized = true;
                }

                // Octave shift: ',' -> down, '.' -> up (on keydown only)
                if (isDown)
                {
#if _FULL_DEBUG == TRUE
                    // D toggles debug console
                    if (sym == SDLK_D)
                    {
                        debug_console_toggle();
                        break;
                    }
#elif _DEBUG == TRUE
                    // F12 toggles debug console
                    if (sym == SDLK_F12)
                    {
                        debug_console_toggle();
                        break;
                    }
#endif
                    if (sym == SDLK_COMMA)
                    {
                        g_keyboard_base_octave = MAX(0, g_keyboard_base_octave - 1);
                    }
                    else if (sym == SDLK_PERIOD)
                    {
                        g_keyboard_base_octave = MIN(8, g_keyboard_base_octave + 1);
                    }
                }

                // Slider keyboard adjustment: when hovering a slider, left/right arrows
                // nudge its value by 1 (keydown only). Honor modal/dialog locks and
                // the same enable/disable rules used when rendering the sliders.
                if (isDown && (sym == SDLK_LEFT || sym == SDLK_RIGHT))
                {
                    if (!ui_modal_blocking())
                    {
                        // Match playback_controls_enabled logic used during rendering
#if SUPPORT_MIDI_HW == TRUE
                        bool playback_controls_enabled_local = !g_midi_input_enabled;
#else
                        bool playback_controls_enabled_local = true;
#endif
                        /* Same as wheel handler: allow volume adjustments while MIDI in is enabled */
                        bool volume_enabled_local = !g_reverbDropdownOpen;

                        int delta = (sym == SDLK_RIGHT) ? 1 : -1;

                        // Try centralized slider handlers in order (transpose, tempo, volume)
                        if (!ui_adjust_transpose(mx, my, delta, playback_controls_enabled_local, &transpose))
                        {
                            if (!ui_adjust_tempo(mx, my, delta, playback_controls_enabled_local, &tempo, &duration, &progress))
                            {
                                ui_adjust_volume(mx, my, delta, volume_enabled_local, &volume);
                            }
                        }
                    }
                }

                // Map qwerty-friendly sequence to MIDI notes starting at C4.
                // Sequence (chromatic including black keys):
                // a w s e d f t g y h u j k o
                // Mapping: a=C, w=C#, s=D, e=D#, d=E, f=F, t=F#, g=G, y=G#, h=A, u=A#, j=B,
                // k=C (next octave), o=C#
#if USE_SDL2 == TRUE
                int sc = e.key.keysym.scancode;
#else
                int sc = e.key.scancode;
#endif
                if (sc < 0 || sc >= 512)
                {
                    break;
                }

                // Always process key release for tracked scancodes, even if current
                // key symbol mapping differs (layout/modifier differences under some builds).
                if (!isDown && g_keyboard_pressed_note[sc] != -1)
                {
                    int heldMidi = g_keyboard_pressed_note[sc];
                    g_keyboard_pressed_note[sc] = -1;

                    BAESong target = g_bae.song ? g_bae.song : g_live_song;
                    if (target)
                        BAESong_NoteOff(target, (unsigned char)g_keyboard_channel, (unsigned char)heldMidi, 0, 0);
#if SUPPORT_MIDI_HW == TRUE
                    if (g_midi_output_enabled)
                    {
                        unsigned char mmsg[3];
                        mmsg[0] = (unsigned char)(0x80 | (g_keyboard_channel & 0x0F));
                        mmsg[1] = (unsigned char)heldMidi;
                        mmsg[2] = 0;
                        midi_output_send(mmsg, 3);
                    }
#endif
                    g_keyboard_active_notes_by_channel[g_keyboard_channel][heldMidi] = 0;
                    break;
                }

                int note = -1;
                if (sc == SDL_SCANCODE_A)
                    note = 0; // C
                else if (sc == SDL_SCANCODE_W)
                    note = 1; // C#
                else if (sc == SDL_SCANCODE_S)
                    note = 2; // D
                else if (sc == SDL_SCANCODE_E)
                    note = 3; // D#
                else if (sc == SDL_SCANCODE_D)
                    note = 4; // E
                else if (sc == SDL_SCANCODE_F)
                    note = 5; // F
                else if (sc == SDL_SCANCODE_T)
                    note = 6; // F#
                else if (sc == SDL_SCANCODE_G)
                    note = 7; // G
                else if (sc == SDL_SCANCODE_Y)
                    note = 8; // G#
                else if (sc == SDL_SCANCODE_H)
                    note = 9; // A
                else if (sc == SDL_SCANCODE_U)
                    note = 10; // A#
                else if (sc == SDL_SCANCODE_J)
                    note = 11; // B
                else if (sc == SDL_SCANCODE_K)
                    note = 12; // C (next octave)
                else if (sc == SDL_SCANCODE_O)
                    note = 13; // C#

                if (note != -1)
                {
                    // While exporting we want to disable the virtual keyboard so
                    // user key presses don't affect the export audio. Preserve
                    // other keys like Escape-only ignore piano mapping here.
                    if (g_exporting)
                    {
                        break;
                    }
                    // Only allow musical typing when the virtual keyboard is
                    // actually visible. Match the same visibility rules used
                    // when rendering the keyboard to avoid surprises.
                    bool keyboard_visible_for_typing = false;
#if SUPPORT_MIDI_HW == TRUE
                    keyboard_visible_for_typing = g_show_virtual_keyboard && (g_midi_input_enabled || (g_bae.song_loaded && !g_bae.is_audio_file));
#else
                    keyboard_visible_for_typing = g_show_virtual_keyboard && (g_bae.song_loaded && !g_bae.is_audio_file);
#endif
                    if (!keyboard_visible_for_typing)
                    {
                        break;
                    }
                    // Compute MIDI note number: C4 = 60
                    int midi = 60 + (g_keyboard_base_octave - 4) * 12 + note;
                    if (midi < 0)
                        midi = 0;
                    if (midi > 127)
                        midi = 127;
                    if (isDown)
                    {
                        // Avoid retrigger if already held by keyboard
                        if (g_keyboard_pressed_note[sc] == midi)
                            break;
                        g_keyboard_pressed_note[sc] = midi;
                        if (keyboard_visible_for_typing)
                        {
                            BAESong target = g_bae.song ? g_bae.song : g_live_song;
                            if (target)
                                BAESong_NoteOnWithLoad(target, (unsigned char)g_keyboard_channel, (unsigned char)midi, 100, 0);
#if SUPPORT_MIDI_HW == TRUE
                            if (g_midi_output_enabled)
                            {
                                unsigned char mmsg[3];
                                mmsg[0] = (unsigned char)(0x90 | (g_keyboard_channel & 0x0F));
                                mmsg[1] = (unsigned char)midi;
                                mmsg[2] = 100;
                                midi_output_send(mmsg, 3);
                            }
#endif
                            // Mark active in per-channel UI array so key lights up immediately
                            g_keyboard_active_notes_by_channel[g_keyboard_channel][midi] = 1;
                            // also update VU/peak for virtual keyboard (use velocity 100)
                            {
                                float lvl = 100.0f / 127.0f;
                                int ch = g_keyboard_channel;
                                if (lvl > g_channel_vu[ch])
                                    g_channel_vu[ch] = lvl;
                                if (lvl > g_channel_peak_level[ch])
                                {
                                    g_channel_peak_level[ch] = lvl;
                                    g_channel_peak_hold_until[ch] = SDL_GetTicks() + g_channel_peak_hold_ms;
                                }
                            }
                        }
                    }
                    break;
                }

                // Escape still quits
                if (sym == SDLK_ESCAPE)
                    running = false;

                // Up/Down arrow control for hovered dropdowns (keydown only)
                if (isDown && (sym == SDLK_UP || sym == SDLK_DOWN))
                {
                    if (!ui_modal_blocking())
                    {
                        UiLayout L;
                        compute_ui_layout(&L);
                        Rect chanDD = L.chanDD;
                        Rect ddRect = L.ddRect;
                        int reverbCount = get_reverb_count();

                        int delta = (sym == SDLK_DOWN) ? 1 : -1;

                        if (point_in(mx, my, ddRect))
                        {
                            int nt = reverbType + delta;
                            if (nt < 1)
                                nt = 1;
                            if (nt > reverbCount)
                                nt = reverbCount;
                            if (nt != reverbType)
                            {
                                reverbType = nt;
                                bae_set_reverb(reverbType);
                                if (g_current_bank_path[0] != '\0')
                                {
                                        save_settings(g_current_bank_path, reverbType, loopPlay);
                                }
                            }
                        }
                        else if (point_in(mx, my, chanDD))
                        {
                            int nt = g_keyboard_channel + delta;
                            if (nt < 0)
                                nt = 0;
                            if (nt > 15)
                                nt = 15;
                            if (nt != g_keyboard_channel)
                            {
                                g_keyboard_channel = nt;
                                // Update Bank/Program display values for the newly selected channel
                                update_bank_program_for_channel();
                            }
                        }
                    }
                }
            }
            break;
            }
        }

        // If RMF Info dialog is visible, treat it as modal for input: swallow clicks
        // that occur outside the dialog so underlying UI elements are not activated.
        if (g_show_rmf_info_dialog && g_bae.is_rmf_file)
        {
            // Ensure info is loaded so we can compute dialog height for hit testing
            rmf_info_load_if_needed();
            int pad = 8;
            int dlgW = 340;
            int lineH = 16;
            int totalLines = 0;
            for (int i = 0; i < INFO_TYPE_COUNT; i++)
            {
                if (g_rmf_info_values[i][0])
                {
                    char tmp[1024];
                    snprintf(tmp, sizeof(tmp), "%s: %s", rmf_info_label((BAEInfoType)i), g_rmf_info_values[i]);
                    int c = count_wrapped_lines(tmp, dlgW - pad * 2 - 8);
                    if (c <= 0)
                        c = 1;
                    totalLines += c;
                }
            }
            if (totalLines == 0)
                totalLines = 1;
            int dlgH = pad * 2 + 24 + totalLines * lineH + 10; // same formula as rendering
            Rect dlg = {WINDOW_W - dlgW - 10, 10, dlgW, dlgH};

            // Swallow mouse click/down if outside dialog
            if ((mclick || mdown) && !point_in(mx, my, dlg))
            {
                mclick = false;
                mdown = false;
            }
        }

        // Sync local 'playing' variable with engine state after export or any external change
        // This ensures progress bar resumes when playback auto-restarts (e.g., after WAV export).
        if (playing != g_bae.is_playing)
        {
            if (!g_bae.is_playing)
                progress = bae_get_pos_ms(); // sync progress so bar reflects stop/seek
            playing = g_bae.is_playing;
        }

#if SUPPORT_MIDI_HW == TRUE
        // Clear playing state when MIDI input is enabled to ensure Play button shows "Play" not "Pause"
        if (g_midi_input_enabled && playing)
        {
            playing = false;
            g_bae.is_playing = false;
        }
#endif

        // timing update
        Uint32 now = SDL_GetTicks();
        (void)now;
        (void)lastTick;
        lastTick = now;
        gui_transport_update_progress(playing, &progress, &duration, tempo);

#if SUPPORT_MIDI_HW == TRUE
        // Publish current channel enables to the MIDI thread (plain byte store is fine)
        for (int _ci = 0; _ci < BAE_MAX_MIDI_CHANNELS; ++_ci)
        {
            g_thread_ch_enabled[_ci] = ch_enable[_ci] ? 1 : 0;
        }

        // Poll MIDI input and route Note On/Off to the virtual keyboard channel.
        // We do not directly toggle g_keyboard_active_notes here because the
        // keyboard drawing code queries the engine via BAESong_GetActiveNotes
        // later each frame; sending events to the engine is sufficient.
        // When the background MIDI service thread is active, it handles polling; avoid double-processing here.
        if (!g_midi_service_thread && g_midi_input_enabled && (g_bae.song || g_live_song))
#else
        if (g_bae.song || g_live_song)
#endif
        {
            unsigned char midi_buf[1024];
            unsigned int midi_sz = 0;
            double midi_ts = 0.0;
#if SUPPORT_MIDI_HW == TRUE
            while (midi_input_poll(midi_buf, &midi_sz, &midi_ts))
            {
                if (midi_sz < 1)
                    continue;
                unsigned char status = midi_buf[0];
                unsigned char mtype = status & 0xF0;
                unsigned char mch = status & 0x0F; // incoming channel

                BAESong target = g_bae.song ? g_bae.song : g_live_song;
                if (!target)
                    continue;
                // Helper lambda-style macros to forward to optional MIDI out
#define FORWARD_OUT(buf, len)               \
    do                                      \
    {                                       \
        if (g_midi_output_enabled)          \
            midi_output_send((buf), (len)); \
    } while (0)

                switch (mtype)
                {
                case 0x80: // Note Off
                    if (midi_sz >= 3)
                    {
                        unsigned char note = midi_buf[1];
                        unsigned char vel = midi_buf[2];
                        unsigned char target_ch = (unsigned char)mch;
                        // Always forward Note Off to engine to prevent stuck notes even if muted
                        if (target)
                            BAESong_NoteOff(target, target_ch, note, 0, 0);
                        unsigned char out[3] = {(unsigned char)(0x80 | (mch & 0x0F)), note, vel};
                        FORWARD_OUT(out, 3);
                        // Clear active flag regardless so stale notes don't persist
                        g_keyboard_active_notes_by_channel[mch][note] = 0;
                    }
                    break;
                case 0x90: // Note On
                    if (midi_sz >= 3)
                    {
                        unsigned char note = midi_buf[1];
                        unsigned char vel = midi_buf[2];
                        if (vel != 0)
                        {
                            unsigned char target_ch = (unsigned char)mch;
                            // Only send NoteOn to internal engine when channel is enabled (not muted)
                            if (ch_enable[mch])
                            {
                                if (target)
                                    BAESong_NoteOnWithLoad(target, target_ch, note, vel, 0);
                                g_keyboard_active_notes_by_channel[mch][note] = 1;
                                // Update VU/peak from incoming MIDI velocity
                                float lvl_in = (float)vel / 127.0f;
                                if (lvl_in > g_channel_vu[mch])
                                    g_channel_vu[mch] = lvl_in;
                                if (lvl_in > g_channel_peak_level[mch])
                                {
                                    g_channel_peak_level[mch] = lvl_in;
                                    g_channel_peak_hold_until[mch] = SDL_GetTicks() + g_channel_peak_hold_ms;
                                }
                            }
                            unsigned char out[3] = {(unsigned char)(0x90 | (mch & 0x0F)), note, vel};
                            FORWARD_OUT(out, 3);
                        }
                        else
                        {
                            // Note On with velocity 0 == Note Off
                            unsigned char target_ch = (unsigned char)mch;
                            // Always deliver NoteOff even if muted to prevent stuck notes
                            if (target)
                                BAESong_NoteOff(target, target_ch, note, 0, 0);
                            unsigned char out[3] = {(unsigned char)(0x80 | (mch & 0x0F)), note, 0};
                            FORWARD_OUT(out, 3);
                            g_keyboard_active_notes_by_channel[mch][note] = 0;
                        }
                    }
                    break;
                case 0xA0: // Polyphonic Key Pressure (Aftertouch)
                    if (midi_sz >= 3)
                    {
                        unsigned char note = midi_buf[1];
                        unsigned char pressure = midi_buf[2];
                        // Respect channel mute: only apply when enabled
                        if (ch_enable[mch])
                        {
                            if (target)
                                BAESong_KeyPressure(target, (unsigned char)mch, note, pressure, 0);
                        }
                        unsigned char out[3] = {(unsigned char)(0xA0 | (mch & 0x0F)), note, pressure};
                        FORWARD_OUT(out, 3);
                    }
                    break;
                case 0xB0: // Control Change
                    if (midi_sz >= 3)
                    {
                        unsigned char cc = midi_buf[1];
                        unsigned char val = midi_buf[2];
                        // Track Bank Select Bank/Program (CC 0 and 32)
                        if (cc == 0)
                        {
                            g_midi_bank[mch] = val;
                            // Update Bank/Program display if this affects the current keyboard channel
                            if (mch == g_keyboard_channel)
                            {
                                update_bank_program_for_channel();
                            }
                        }
                        else if (cc == 32)
                        {
                            g_midi_bank_program[mch] = val;
                            // Update Bank/Program display if this affects the current keyboard channel
                            if (mch == g_keyboard_channel)
                            {
                                update_bank_program_for_channel();
                            }
                        }
                        {
                            unsigned char target_ch = (unsigned char)mch;
                            // Always route All Notes Off / All Sound Off regardless of mute state to prevent hangs
                            if (cc == 120 || cc == 123)
                            {
                                if (target)
                                    BAESong_ControlChange(target, target_ch, cc, val, 0);
                            }
                            else if (ch_enable[mch])
                            {
                                if (target)
                                    BAESong_ControlChange(target, target_ch, cc, val, 0);
                            }
                        }
                        unsigned char out[3] = {(unsigned char)(0xB0 | (mch & 0x0F)), cc, val};
                        FORWARD_OUT(out, 3);

                        // MIDI All Notes Off (CC 123) or All Sound Off (120) - always clear engine notes
                        if (cc == 123 || cc == 120)
                        {
                            if (target)
                                BAESong_AllNotesOff(target, 0);
                            // Also clear our active-note book-keeping for that channel
                            for (int n = 0; n < BAE_MAX_NOTES; n++)
                                g_keyboard_active_notes_by_channel[mch][n] = 0;
                        }
                    }
                    break;
                case 0xC0: // Program Change
                    if (midi_sz >= 2)
                    {
                        unsigned char program = midi_buf[1];
                        // Program changes are applied even when channel is muted so the instrument
                        // will be correct when unmuted.
                        {
                            unsigned char target_ch = (unsigned char)mch;
                            if (target)
                            {
                                // Send both Bank and Program before program change
                                BAESong_ControlChange(target, target_ch, 0, g_midi_bank[mch], 0);  // CC 0 = Bank Select
                                BAESong_ControlChange(target, target_ch, 32, g_midi_bank_program[mch], 0); // CC 32 = Bank Select Program
                                BAESong_ProgramChange(target, target_ch, program, 0);
                            }
                        }
                        unsigned char out[2] = {(unsigned char)(0xC0 | (mch & 0x0F)), program};
                        FORWARD_OUT(out, 2);
                        // Update Bank/Program display if this program change affects the current keyboard channel
                        if (mch == g_keyboard_channel)
                        {
                            update_bank_program_for_channel();
                        }
                    }
                    break;
                case 0xD0: // Channel Pressure (Aftertouch)
                    if (midi_sz >= 2)
                    {
                        unsigned char pressure = midi_buf[1];
                        if (ch_enable[mch])
                        {
                            if (target)
                                BAESong_ChannelPressure(target, (unsigned char)mch, pressure, 0);
                        }
                        unsigned char out[2] = {(unsigned char)(0xD0 | (mch & 0x0F)), pressure};
                        FORWARD_OUT(out, 2);
                    }
                    break;
                case 0xE0: // Pitch Bend (14-bit LSB + MSB)
                    if (midi_sz >= 3)
                    {
                        unsigned char lsb = midi_buf[1];
                        unsigned char msb = midi_buf[2];
                        if (ch_enable[mch])
                        {
                            if (target)
                                BAESong_PitchBend(target, (unsigned char)mch, lsb, msb, 0);
                        }
                        unsigned char out[3] = {(unsigned char)(0xE0 | (mch & 0x0F)), lsb, msb};
                        FORWARD_OUT(out, 3);
                    }
                    break;
                case 0xF0: // System messages - ignore or handle SysEx if needed
                    // Currently ignore system realtime and sysex messages from input
                    break;
                default:
                    // Unhandled type
                    break;
                }

#undef FORWARD_OUT
            }
#endif
        }

        if (!g_exporting) {
            // Run the mixer after MIDI input is queued so short notes are processed in the same frame.
            BAEMixer_Idle(g_bae.mixer);
            bae_update_channel_mutes(ch_enable);
        }

        // Check for end-of-playback to update UI state correctly. We removed the
        // previous "force restart" block; looping is now handled entirely by
        // the engine via BAESong_SetLoops. If loops are set >0 the song should
        // not report done until all loops are exhausted.
        if (playing && g_bae.song_loaded)
        {
            bool song_finished = false;

            if (g_bae.is_audio_file && g_bae.sound)
            {
                BAE_BOOL is_done = FALSE;
                if (BAESound_IsDone(g_bae.sound, &is_done) == BAE_NO_ERROR && is_done)
                {
                    song_finished = true;
                }
            }
            else if (!g_bae.is_audio_file && g_bae.song)
            {
                BAE_BOOL is_done = FALSE;
                if (BAESong_IsDone(g_bae.song, &is_done) == BAE_NO_ERROR && is_done)
                {
                    song_finished = true;
                }
            }

            if (song_finished)
            {
                BAE_PRINTF("Song finished, stopping playback\n");
                playing = false;
                g_bae.is_playing = false;
                g_bae.song_finished = true;
                progress = 0;
                if (!g_bae.is_audio_file && g_bae.song)
                {
                    // Workaround for loop issue: explicitly stop the song to ensure it's fully stopped
                    BAESong_Stop(g_bae.song, FALSE);
                    BAESong_SetMicrosecondPosition(g_bae.song, 0);
                }

#if SUPPORT_PLAYLIST == TRUE
                if (g_playlist.enabled) {
                    // If we're exporting, do not auto-advance the playlist. This prevents the GUI
                    // from briefly starting the next track while rendering/exporting the current one.
                    if (g_exporting) {
                        BAE_PRINTF("Playlist: advancement suppressed while exporting\n");
                    } else {
                        // Handle playlist advancement
                        if (g_playlist.count > 0 && g_playlist.current_index >= 0)
                        {
                            int next_index = playlist_get_next_song_for_end_of_song();

                            if (next_index >= 0 && next_index < g_playlist.count)
                            {
                                // Load and play the next song
                                const char *next_file = g_playlist.entries[next_index].filename;

                                BAE_PRINTF("Playlist: advancing to index %d: %s\n", next_index, next_file);

                                if (bae_load_song_with_settings(next_file, transpose, tempo, volume, loopPlay, reverbType, ch_enable, true))
                                {
                                    // Update playlist current index
                                    g_playlist.current_index = next_index;

                                    // Start playback
                                    duration = bae_get_len_ms();
                                    progress = 0;
                                    playing = false;
                                    // Robust auto-start sequence: ensure at position 0, preroll again (defensive), then start
                                    if (!g_bae.is_audio_file && g_bae.song)
                                    {                                    
                                        if (bae_play(&playing))
                                        {
                                            g_bae.is_playing = true;
                                            g_bae.song_finished = false;
                                            BAE_PRINTF("Playlist: next song started successfully\n");
                                        }
                                        else
                                        {
                                            BAE_PRINTF("Playlist: failed to start next song\n");
                                        }
                                    }
                                    else if (g_bae.is_audio_file && g_bae.sound)
                                    {
                                        if (bae_play(&playing))
                                        {
                                            g_bae.is_playing = true;
                                            g_bae.song_finished = false;
                                            BAE_PRINTF("Playlist: next audio file started successfully\n");
                                        }
                                        else
                                        {
                                            BAE_PRINTF("Playlist: failed to start next audio file\n");
                                        }
                                    }
                                }
                                else
                                {
                                    BAE_PRINTF("Playlist: failed to load next song: %s\n", next_file);
                                }
                            }
                            else
                            {
                                BAE_PRINTF("Playlist: end of playlist reached\n");
                            }
                        }
                    }
                }
#endif
            }
        }

        // Service WAV export if active
        if (g_exporting) {
            bae_service_wav_export();
        }

#if SUPPORT_PLAYLIST == TRUE
        // Handle pending playlist loads
        if (playlist_has_pending_load() && g_playlist.enabled)
        {
            const char *pending_file = playlist_get_pending_load_file();
            if (pending_file && bae_load_song_with_settings(pending_file, transpose, tempo, volume, loopPlay, reverbType, ch_enable, true))
            {
#if SUPPORT_PLAYLIST == TRUE
                // Successfully loaded the song from playlist
                playlist_update_current_file(pending_file);
#endif
                duration = bae_get_len_ms();
                progress = 0;

                playing = false; // force toggle logic
                if (!bae_play(&playing))
                {
                    BAE_PRINTF("Autoplay after playlist double-click failed for '%s'\n", pending_file);
                }
            }
            playlist_clear_pending_load();
        }
#endif

        // Draw UI with improved layout and styling
#ifdef _WIN32
        SDL_SetRenderDrawColor(R, g_theme.bg_color.r, g_theme.bg_color.g, g_theme.bg_color.b, 255);
#else
        SDL_SetRenderDrawColor(R, g_bg_color.r, g_bg_color.g, g_bg_color.b, g_bg_color.a);
#endif
        SDL_RenderClear(R);

        // Toggle SDL text input based on the preset name dialog state.
        // (Use SDL_EVENT_TEXT_INPUT so uppercase works.)
        {
            extern bool g_show_preset_name_dialog;
            static bool s_preset_name_text_input_active = false;
            if (g_show_preset_name_dialog && !s_preset_name_text_input_active)
            {
#if USE_SDL2 == TRUE
                SDL_StartTextInput();
#else
                SDL_StartTextInput(win);
#endif
                s_preset_name_text_input_active = true;
            }
            else if (!g_show_preset_name_dialog && s_preset_name_text_input_active)
            {
#if USE_SDL2 == TRUE
                SDL_StopTextInput();
#else
                SDL_StopTextInput(win);
#endif
                s_preset_name_text_input_active = false;
            }
        }

        // Clear tooltips each frame
        ui_clear_tooltip(&g_program_tooltip_visible);
        ui_clear_tooltip(&g_bank_tooltip_visible);
        ui_clear_tooltip(&g_reverb_tooltip_visible);
        ui_clear_tooltip(&g_dls_compat_tooltip_visible);

        // Colors driven by theme globals
        SDL_Color labelCol = g_text_color;
        SDL_Color headerCol = g_header_color;
        SDL_Color panelBg = g_panel_bg;
        SDL_Color panelBorder = g_panel_border;

        // Draw main panels
        Rect channelPanel = {10, 10, 380, 140};
        Rect controlPanel = {400, 10, 490, 140};
        Rect transportPanel = {10, 160, 880, 85}; // increased height by 5px

        // Keyboard panel comes after transport
        int keyboardPanelY = transportPanel.y + transportPanel.h + 10;
        Rect keyboardPanel = {10, keyboardPanelY, 880, 110};
        
        // Declare statusPanel here so it's accessible throughout the function
        Rect statusPanel;
#if SUPPORT_MIDI_HW == TRUE
        bool showKeyboard = g_show_virtual_keyboard && (g_midi_input_enabled || (g_bae.song_loaded && !g_bae.is_audio_file));
#else
        bool showKeyboard = g_show_virtual_keyboard && (g_bae.song_loaded && !g_bae.is_audio_file);
#endif
#if SUPPORT_KARAOKE == TRUE
        // Insert karaoke panel (if active) above status panel; dynamic window height
        int karaokePanelHeight = 40;
        // Show karaoke if enabled and not suspended for the current song.
        // Previously this required g_lyric_count>0 which hid the panel while
        // fragments were being accumulated (no committed lines yet). Include
        // the transient current/previous buffers so the panel appears as soon
        // as any lyric text exists.
        bool showKaraoke = g_karaoke_enabled && !g_karaoke_suspended && !g_exporting &&
                           (g_lyric_count > 0 || g_karaoke_line_current[0] != '\0' || g_karaoke_line_previous[0] != '\0') &&
                           g_bae.song_loaded && !g_bae.is_audio_file;
#endif
        // Karaoke now appears after keyboard panel or waveform (for audio files)
        bool showWaveform = g_bae.is_audio_file && g_bae.sound;
        // Always hide the virtual keyboard when waveform is active (including when MIDI input is enabled)
        if (showWaveform)
        {
            showKeyboard = false;
        }
#if SUPPORT_KARAOKE == TRUE
        Rect karaokePanel = {10, ((showKeyboard || showWaveform) ? (keyboardPanel.y + keyboardPanel.h + 10) : (transportPanel.y + transportPanel.h + 10)), 880, karaokePanelHeight};
#endif

        int statusY = ((showKeyboard || showWaveform) ? (keyboardPanel.y + keyboardPanel.h + 10) : (transportPanel.y + transportPanel.h + 10));
#if SUPPORT_KARAOKE == TRUE
        if (showKaraoke)
        {
            statusY = karaokePanel.y + karaokePanel.h + 5;
        }
#endif
#if SUPPORT_PLAYLIST == TRUE
        // Add playlist panel right above status panel
        Rect playlistPanel = {0,0,0,0}; // dummy
        if (g_playlist.enabled) {
            int playlistPanelHeight = 300; // Reduced from 500px to 300px
            int playlistPanelY = statusY;
            statusY += playlistPanelHeight + 10; // Move status down by playlist height + gap
            playlistPanel = (Rect){10, playlistPanelY, 880, playlistPanelHeight};
        } 
#endif
        int neededH = statusY + 115; // status panel + bottom padding
        if (neededH != g_window_h)
        {
            g_window_h = neededH;
            SDL_SetWindowSize(win, WINDOW_W, g_window_h);
        }
        statusPanel = (Rect){10, statusY, 880, 100}; // Now assign to pre-declared variable
        // Block background interactions when a modal is active or when exporting.
        // Exporting will dim and lock most UI, but the Stop button remains active.
        extern bool g_show_preset_delete_confirm_dialog;
        extern bool g_show_eq_dialog;
        bool modal_block = g_show_about_dialog || (g_show_rmf_info_dialog && g_bae.is_rmf_file) || g_exporting || g_show_custom_reverb_dialog || g_show_preset_name_dialog || g_show_preset_delete_confirm_dialog || g_show_eq_dialog; // block when any modal/dialog open or export in progress
        bool modal_block_transport = g_show_about_dialog || (g_show_rmf_info_dialog && g_bae.is_rmf_file) || g_exporting || g_show_preset_name_dialog || g_show_preset_delete_confirm_dialog || g_show_eq_dialog; // Custom reverb dialog doesn't block transport controls
        // When a modal is active we fully swallow background hover/drag/click by using off-screen, inert inputs
        int ui_mx = mx, ui_my = my;
        bool ui_mdown = mdown;
        bool ui_mclick = mclick;
        bool ui_rclick = rclick;
        // Transport controls use separate mouse tracking that isn't blocked by custom reverb dialog
        int transport_mx = mx, transport_my = my;
        bool transport_mdown = mdown;
        bool transport_mclick = mclick;
        if (modal_block)
        {
            ui_mx = ui_my = -10000;
            ui_mdown = ui_mclick = ui_rclick = false;
        }
        if (modal_block_transport)
        {
            transport_mx = transport_my = -10000;
            transport_mdown = transport_mclick = false;
        }
        // Also disable transport controls while the expanded reverb dropdown is open.
        if (g_reverbDropdownOpen)
        {
            transport_mx = transport_my = -10000;
            transport_mdown = transport_mclick = false;
        }

        GuiFrameCtx frame = {0};
        frame.R = R;
        frame.win = win;
        frame.mx = mx;
        frame.my = my;
        frame.mdown = mdown;
        frame.mclick = &mclick;
        frame.ui_mx = ui_mx;
        frame.ui_my = ui_my;
        frame.ui_mdown = ui_mdown;
        frame.ui_mclick = ui_mclick;
        frame.ui_rclick = ui_rclick;
        frame.transport_mx = transport_mx;
        frame.transport_my = transport_my;
        frame.transport_mdown = transport_mdown;
        frame.transport_mclick = transport_mclick;
        frame.modal_block = modal_block;
        frame.modal_block_transport = modal_block_transport;
        frame.playing = &playing;
        frame.progress = &progress;
        frame.duration = &duration;
        frame.transpose = &transpose;
        frame.tempo = &tempo;
        frame.volume = &volume;
        frame.reverb_type = &reverbType;
        frame.loop_enabled = &loopPlay;
        frame.ch_enable = ch_enable;
        frame.last_drag_progress = &last_drag_progress;
        frame.channelPanel = channelPanel;
        frame.controlPanel = controlPanel;
        frame.transportPanel = transportPanel;
        frame.keyboardPanel = keyboardPanel;
        frame.statusPanel = statusPanel;
        frame.labelCol = labelCol;
        frame.headerCol = headerCol;
        frame.panelBg = panelBg;
        frame.panelBorder = panelBorder;
        frame.showKeyboard = showKeyboard;
        frame.showWaveform = showWaveform;

        gui_channels_draw(&frame);
        gui_playback_controls_draw(&frame);
        gui_transport_draw(&frame);
        gui_keyboard_panel_draw(&frame);
        gui_export_draw_controls(&frame);

        /* Sync click consume / state mutated by peels back into locals used below. */
        ui_mclick = frame.ui_mclick;
        ui_rclick = frame.ui_rclick;
        ui_mdown = frame.ui_mdown;
        ui_mx = frame.ui_mx;
        ui_my = frame.ui_my;

#if SUPPORT_KARAOKE == TRUE
        karaoke_render(R, karaokePanel, showKaraoke);
#endif
#if SUPPORT_PLAYLIST == TRUE
        // Playlist panel - sync with currently playing file first
        if (g_playlist.enabled) {
            if (g_bae.song_loaded && g_bae.loaded_path[0])
            {
                playlist_update_current_file(g_bae.loaded_path);
            }
            // While the reverb dropdown is expanded, don't allow interaction with the playlist underneath it.
            playlist_render(R, playlistPanel,
                           g_reverbDropdownOpen ? -10000 : ui_mx,
                           g_reverbDropdownOpen ? -10000 : ui_my,
                           g_reverbDropdownOpen ? false : ui_mdown,
                           g_reverbDropdownOpen ? false : ui_mclick,
                           g_reverbDropdownOpen ? false : ui_rclick,
                           modal_block);
        }
#endif

        frame.ui_mclick = ui_mclick;
        frame.ui_rclick = ui_rclick;
        frame.ui_mdown = ui_mdown;
        gui_status_bank_draw(&frame);
        ui_mclick = frame.ui_mclick;
        ui_rclick = frame.ui_rclick;
        ui_mdown = frame.ui_mdown;

        int statusBaseY = statusPanel.y + 10;
        int lineY3 = statusBaseY + 60;

        // Status indicator (use theme-safe highlight color for playing state)
        const char *status;
        SDL_Color statusCol;
#if SUPPORT_MIDI_HW == TRUE
        if (g_midi_input_enabled)
        {
            // When MIDI Input mode is enabled, present the transport status as External
            status = "External";
            statusCol = g_highlight_color;
        }
        else
        {
#endif
            if (playing)
            {
                status = "♪ Playing";
                statusCol = g_highlight_color;
            }
            else
            {
                // Draw a square instead of the '■' character for 'Stopped'
                int stoppedBoxSize = 8;
                int stoppedBoxX = 20;
                int stoppedBoxY = lineY3 + 5;
                SDL_Color stoppedCol = g_header_color;
                draw_rect(R, (Rect){stoppedBoxX, stoppedBoxY, stoppedBoxSize, stoppedBoxSize}, stoppedCol);
                // Draw the text 'Stopped' next to the box
                int text_w = 0, text_h = 0;
                measure_text("Stopped", &text_w, &text_h);
                draw_text(R, stoppedBoxX + stoppedBoxSize + 8, lineY3, "Stopped", stoppedCol);
                status = NULL; // Don't draw status below
            }
#if SUPPORT_MIDI_HW == TRUE
        }
#endif
        if (status)
        {
            draw_text(R, 20, lineY3, status, statusCol);
        }

        // Show status message if recent (within 3 seconds)
        if (g_bae.status_message[0] != '\0' && (now - g_bae.status_message_time) < 3000)
        {
            // Use accent color for transient status messages so they stand out
            draw_text(R, 120, lineY3, g_bae.status_message, g_highlight_color);
        }
        else
        {
            // Muted fallback text that adapts to theme; darker on light backgrounds for readability
            SDL_Color muted = g_is_dark_mode ? (SDL_Color){150, 150, 150, 255} : (SDL_Color){80, 80, 80, 255};
#if _FULL_DEBUG == TRUE
            draw_text(R, 120, lineY3, "(Debug build, press D for console)", muted);
#elif _DEBUG == TRUE
            draw_text(R, 120, lineY3, "(Debug build, press F12 for console)", muted);
#else
            draw_text(R, 120, lineY3, "(Drag & drop media/bank files here)", muted);
#endif
        }

        // Draw deferred file tooltip (full path)
        // Draw 'All' checkbox for virtual keyboard channel merging. Placed here
        // so it renders on top of the keyboard panel (correct z-order). Only
        // show when the virtual keyboard is visible.
        if (showKeyboard)
        {
            {
                Rect allR = {20, keyboardPanel.y + 82, 16, 16}; // position relative to keyboard panel
                bool allHover = point_in(ui_mx, ui_my, allR);
                bool allClickable = (!g_keyboard_channel_dd_open && !modal_block);
                if (allClickable && ui_mclick && allHover)
                {
                    g_keyboard_show_all_channels = !g_keyboard_show_all_channels;
                }
                SDL_Color cb_border = g_button_border;
                draw_rect(R, allR, g_panel_bg);
                draw_frame(R, allR, cb_border);
                Rect inner = {allR.x + 3, allR.y + 3, allR.w - 6, allR.h - 6};
                if (g_keyboard_show_all_channels)
                {
                    draw_rect(R, inner, g_accent_color);
                    draw_frame(R, inner, g_button_text);
                    SDL_SetRenderDrawColor(R, g_button_text.r, g_button_text.g, g_button_text.b, g_button_text.a);
                    int x1 = inner.x + 2;
                    int y1 = inner.y + inner.h / 2;
                    int x2 = inner.x + inner.w / 2 - 1;
                    int y2 = inner.y + inner.h - 3;
                    int x3 = inner.x + inner.w - 3;
                    int y3 = inner.y + 3;
#if USE_SDL2 == TRUE
                    SDL_RenderDrawLine(R, x1, y1, x2, y2);
                    SDL_RenderDrawLine(R, x2, y2, x3, y3);
#else
                    SDL_RenderLine(R, x1, y1, x2, y2);
                    SDL_RenderLine(R, x2, y2, x3, y3);
#endif
                }
                else
                {
                    draw_rect(R, inner, g_panel_bg);
                    draw_frame(R, inner, cb_border);
                }
                int _tw = 0, _th = 0;
                measure_text("All Ch.", &_tw, &_th);
                draw_text(R, allR.x + allR.w + 10, allR.y + (allR.h - _th) / 2, "All Ch.", labelCol);
            }
        }

        if (g_file_tooltip_visible)
        {
            ui_draw_tooltip(R, g_file_tooltip_rect, g_file_tooltip_text, true, true);
        }

        if (g_reverb_tooltip_visible)
        {
            ui_draw_tooltip(R, g_reverb_tooltip_rect, g_reverb_tooltip_text, true, true);
        }

        // Draw deferred bank tooltip last so it appears above status text and other UI
        if (g_bank_tooltip_visible)
        {
            ui_draw_tooltip(R, g_bank_tooltip_rect, g_bank_tooltip_text, true, true);
        }

        // Draw loop tooltip
        if (g_loop_tooltip_visible)
        {
            ui_draw_tooltip(R, g_loop_tooltip_rect, g_loop_tooltip_text, false, false);
        }

        // Draw voice tooltip
        if (g_voice_tooltip_visible)
        {
            ui_draw_tooltip(R, g_voice_tooltip_rect, g_voice_tooltip_text, false, false);
        }

        // Draw Program tooltip
        if (g_program_tooltip_visible)
        {
            int tooltip_w = 0, tooltip_h = 0;
            measure_text(g_program_tooltip_text, &tooltip_w, &tooltip_h);
            Rect tipRect = {g_program_tooltip_rect.x, g_program_tooltip_rect.y, tooltip_w + 8, tooltip_h + 8};
            ui_draw_tooltip(R, tipRect, g_program_tooltip_text, false, false);
        }

        // Draw Bank tooltip
        if (g_bank_tooltip_visible)
        {
            int tooltip_w = 0, tooltip_h = 0;
            measure_text(g_bank_tooltip_text, &tooltip_w, &tooltip_h);
            Rect tipRect = {g_bank_tooltip_rect.x, g_bank_tooltip_rect.y, tooltip_w + 8, tooltip_h + 8};
            ui_draw_tooltip(R, tipRect, g_bank_tooltip_text, false, false);
        }

        // Render dropdown list on top of everything else if open
        if (g_reverbDropdownOpen)
        {
            int reverbCount = get_reverb_count();
            Rect ddRect = {687, 38, 160, 24}; // Match closed dropdown position

            // Draw the dropdown list using theme globals
            int itemH = ddRect.h;
            const int itemsPerCol = 25;
            const int colGap = 0;
            int cols = (reverbCount + itemsPerCol - 1) / itemsPerCol;
            if (cols < 1) cols = 1;

            bool overAnyBox = false;
            int startY = ddRect.y + ddRect.h + 1;

            // Column 0 is the rightmost (aligned with the dropdown). Additional columns expand left.
            for (int col = 0; col < cols; col++)
            {
                int startIdx = col * itemsPerCol;
                if (startIdx >= reverbCount)
                    break;
                int remaining = reverbCount - startIdx;
                int rows = (remaining > itemsPerCol) ? itemsPerCol : remaining;

                int colX = ddRect.x - (col * (ddRect.w + colGap));
                Rect box = {colX, startY, ddRect.w, itemH * rows};
                draw_rect(R, box, g_panel_bg);
                draw_frame(R, box, g_panel_border);

                if (point_in(mx, my, box))
                    overAnyBox = true;

                for (int row = 0; row < rows; row++)
                {
                    int i = startIdx + row;
                    Rect ir = {box.x, box.y + row * itemH, box.w, itemH};
                    bool over = point_in(mx, my, ir);
                    SDL_Color ibg = ((i + 1) == reverbType) ? g_highlight_color : g_panel_bg;
                    if (over)
                        ibg = g_button_hover;
                    draw_rect(R, ir, ibg);

                    if (row < rows - 1)
                    { // separator line
                        SDL_Color sep = g_panel_border;
                        sep.a = 255;
                        SDL_SetRenderDrawColor(R, sep.r, sep.g, sep.b, sep.a);
#if USE_SDL2 == TRUE
                        SDL_RenderDrawLine(R, ir.x, ir.y + ir.h, ir.x + ir.w, ir.y + ir.h);
#else
                        SDL_RenderLine(R, ir.x, ir.y + ir.h, ir.x + ir.w, ir.y + ir.h);
#endif
                    }

                    // Choose text color: use button text on selected/hover, otherwise normal text
                    SDL_Color itemTxt = g_text_color;
                    if ((i + 1) == reverbType)
                        itemTxt = g_button_text;
                    if (over)
                        itemTxt = g_button_text;
                    draw_text(R, ir.x + 6, ir.y + 6, get_reverb_name(i), itemTxt);

                    if (over && mclick)
                    {
                        reverbType = i + 1;
                        g_reverbDropdownOpen = false;

#if USE_NEO_EFFECTS
                        // Check if this is a custom preset and load it
                        int base_count = 15; // Default reverb types with Custom
                        extern char g_current_custom_reverb_preset[64];
                        if (i >= base_count)
                        {
                            const char *preset_name = get_reverb_name(i);
                            load_custom_reverb_preset(preset_name);
                        }
                        else
                        {
                            // Clear current preset name when switching to non-custom reverb
                            g_current_custom_reverb_preset[0] = '\0';
                        }
#endif

                        bae_set_reverb(reverbType);
                        // Save settings when reverb is changed
                        if (g_current_bank_path[0] != '\0')
                        {
                            save_settings(g_current_bank_path, reverbType, loopPlay);
                        }
                    }
                }
            }

            // Click outside closes without change
            if (mclick && !point_in(mx, my, ddRect) && !overAnyBox)
            {
                g_reverbDropdownOpen = false;
            }
        }

        // Render keyboard channel dropdown list last so it appears above status panel
        if (g_reverbDropdownOpen)
        {
            g_keyboard_channel_dd_open = false;
        }
        if (g_keyboard_channel_dd_open && showKeyboard)
        {
            // Reconstruct minimal needed rect & dropdown trigger
            Rect transportPanel_tmp = (Rect){10, 160, 880, 80};
            int keyboardPanelY_tmp = transportPanel_tmp.y + transportPanel_tmp.h + 10;
            Rect keyboardPanel_tmp2 = (Rect){10, keyboardPanelY_tmp, 880, 110};
            Rect chanDD = {keyboardPanel_tmp2.x + 10, keyboardPanel_tmp2.y + 28, 90, 22};
            // Layout: 2 columns x 8 rows (channels 1-8 left, 9-16 right)
            int columns = 2;
            int rows = 8;         // 16 / 2
            int itemW = chanDD.w; // reuse base width per column
            int itemH = chanDD.h;
            int gapX = 6; // spacing between columns
            int boxW = columns * itemW + (columns - 1) * gapX;
            int boxH = rows * itemH;
            Rect box = {chanDD.x, chanDD.y + chanDD.h + 1, boxW, boxH};
            // Ensure box stays on screen horizontally
            if (box.x + box.w > WINDOW_W - 10)
            {
                box.x = WINDOW_W - 10 - box.w;
            }
            draw_rect(R, box, g_panel_bg);
            draw_frame(R, box, g_panel_border);
            char chanBuf[BAE_MAX_MIDI_CHANNELS][8];
            for (int i = 0; i < BAE_MAX_MIDI_CHANNELS; i++)
            {
                snprintf(chanBuf[i], sizeof(chanBuf[i]), "Ch %d", i + 1);
            }
            for (int i = 0; i < BAE_MAX_MIDI_CHANNELS; i++)
            {
                int col = i / rows; // 0 or 1
                int row = i % rows;
                Rect ir = {box.x + col * (itemW + gapX), box.y + row * itemH, itemW, itemH};
                bool over = point_in(mx, my, ir);
                SDL_Color ibg = (i == g_keyboard_channel) ? g_highlight_color : g_panel_bg;
                if (over)
                    ibg = g_button_hover;
                draw_rect(R, ir, ibg);
                SDL_Color itxt = (i == g_keyboard_channel || over) ? g_button_text : g_text_color;
                draw_text(R, ir.x + 6, ir.y + 4, chanBuf[i], itxt);
                if (mclick && over)
                {
                    if (g_keyboard_mouse_note != -1 && g_bae.song)
                    {
                        BAESong_NoteOff(g_bae.song, (unsigned char)g_keyboard_channel, (unsigned char)g_keyboard_mouse_note, 0, 0);
                        g_keyboard_mouse_note = -1;
                    }
                    g_keyboard_channel = i;
                    g_keyboard_channel_dd_open = false;
                    // Update Bank/Program display values for the newly selected channel
                    update_bank_program_for_channel();
                }
            }
            if (mclick && !point_in(mx, my, box) && !point_in(mx, my, chanDD))
            {
                g_keyboard_channel_dd_open = false;
            }
        }

        // RMF Info dialog (modal overlay with dimming)
        if (g_show_rmf_info_dialog && g_bae.is_rmf_file)
        {
            render_rmf_info_dialog(R, mx, my, mclick);
        }

        // Custom reverb dialog (must render on top of everything)
#if USE_NEO_EFFECTS
        if (g_show_custom_reverb_dialog)
        {
            render_custom_reverb_dialog(R, mx, my, mclick, mdown, g_window_h);
        }
#endif
        
        if (g_show_eq_dialog)
        {
            render_eq_dialog(R, mx, my, mclick, mdown, g_window_h);
        }

        // Preset name input dialog (must render on top of custom reverb/EQ dialogs)
        extern bool g_show_preset_name_dialog;
        if (g_show_preset_name_dialog)
        {
            render_preset_name_dialog(R, mx, my, mclick, mdown, g_window_h, &reverbType);
        }

        // Delete confirmation dialog (must render on top of everything)
        {
            extern bool g_show_preset_delete_confirm_dialog;
            extern bool g_preset_delete_confirmed;
            extern char g_preset_delete_name[64];

            if (g_show_preset_delete_confirm_dialog)
            {
                render_preset_delete_confirm_dialog(R, mx, my, mclick, mdown, g_window_h);
            }

            if (g_preset_delete_confirmed)
            {
                if (g_preset_delete_name[0])
                {
                    extern bool g_show_eq_dialog;
                    if (g_show_eq_dialog)
                    {
                        delete_custom_eq_preset(g_preset_delete_name);
                        
                        // Reset to "Custom" default (index 7)
                        g_selected_eq_preset = 7;
                        g_current_custom_eq_preset[0] = '\0';
                        save_settings(g_current_bank_path[0] ? g_current_bank_path : NULL, reverbType, loopPlay);
                    }
                    else
                    {
#if USE_NEO_EFFECTS
                        delete_custom_reverb_preset(g_preset_delete_name);

                        // Reset to "Custom" default
                        reverbType = BAE_REVERB_TYPE_19;
                        bae_set_reverb(reverbType);
                        if (g_current_bank_path[0] != '\0')
                        {
                            save_settings(g_current_bank_path, reverbType, loopPlay);
                        }
#endif
                    }
                }

                g_preset_delete_confirmed = false;
                memset(g_preset_delete_name, 0, sizeof(g_preset_delete_name));
            }
        }

        if (g_show_about_dialog)
        {
            render_about_dialog(R, mx, my, mclick);
        }

        // If exporting, render a slight dim overlay that disables everything except the Stop button.
        if (g_exporting)
        {
            SDL_Color dim = g_is_dark_mode ? (SDL_Color){0, 0, 0, 100} : (SDL_Color){0, 0, 0, 100};
            draw_rect(R, (Rect){0, 0, WINDOW_W, g_window_h}, dim);
            // Re-draw an active Stop button on top of the dim overlay so the user can cancel export.
            Rect stopRect = {90, 215, 60, 22};
            // Use raw mouse coords so the Stop button remains clickable even when modal_block is true
            if (ui_button(R, stopRect, "Stop", mx, my, mdown) && mclick)
            {
                bae_stop(&playing, &progress);
                // Also stop export if active
                if (g_exporting)
                {
                    bae_stop_wav_export();
                }
                // consume the click so underlying UI doesn't react to the same event
                mclick = false;
            }

            // Clear visible virtual keyboard notes when stopping from export overlay too
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
                memset(g_keyboard_active_notes, 0, sizeof(g_keyboard_active_notes));
                g_keyboard_suppress_until = SDL_GetTicks() + 33;
            }
        }
        SDL_RenderPresent(R);

        settings_window_render(&transpose, &tempo, &volume, &loopPlay,
                               &reverbType, ch_enable, &progress, &duration, &playing);
        
#if _DEBUG == TRUE
        // Render debug console if visible
        debug_console_render();
#endif

#if SUPPORT_BAESCRIPT == TRUE
        script_editor_render();
        // Tick script engine during playback (export thread handles its own ticks)
        if (playing && !g_bae.paused && !g_exporting) {
            script_editor_tick();
        }
#endif
        
        Uint64 freq = SDL_GetPerformanceFrequency();
        Uint64 frame_end = SDL_GetPerformanceCounter();
        float elapsed_ms = (frame_end - frame_start) * 1000.0f / freq;

        if (elapsed_ms < frame_time_ms)
        {
#if USE_SDL2 == TRUE
            SDL_WaitEventTimeout(NULL, (int)(frame_time_ms - elapsed_ms));
#else
            SDL_WaitEventTimeout(NULL, (Sint32)(frame_time_ms - elapsed_ms));
#endif
        }
        static int lastTranspose = 123456, lastTempo = 123456, lastVolume = 123456, lastReverbType = -1;
        static bool lastLoop = false;
        if (transpose != lastTranspose)
        {
            bae_set_transpose(transpose);
            lastTranspose = transpose;
        }
        if (tempo != lastTempo)
        {
            // Apply engine change
            bae_set_tempo(tempo);
            // If lastTempo looks uninitialized (sentinel), query engine for authoritative
            // timeline values instead of attempting a mathematical scale.
            if (lastTempo < 25 || lastTempo > 200)
            {
                duration = bae_get_len_ms();
                g_last_engine_pos_ms = bae_get_pos_ms();
            }
            lastTempo = tempo;
        }
        if (volume != lastVolume)
        {
            bae_set_volume(volume);
            lastVolume = volume;
        }
        if (loopPlay != lastLoop)
        {
            bae_set_loop(loopPlay);
            lastLoop = loopPlay;
            g_bae.loop_enabled_gui = loopPlay;

            // Update loop count on currently loaded audio file if any
            if (g_bae.is_audio_file && g_bae.sound)
            {
                uint32_t loopCount = loopPlay ? 0xFFFFFFFF : 0;
                BAESound_SetLoopCount(g_bae.sound, loopCount);
            }
        }
        if (reverbType != lastReverbType)
        {
            bae_set_reverb(reverbType);
            lastReverbType = reverbType;
        }
    }

    // Stop MIDI service and close devices before tearing down SDL
#if SUPPORT_MIDI_HW == TRUE
    midi_service_stop();
    midi_input_shutdown();
#endif
    // Save window position and playlist before cleanup
    if (g_main_window)
    {
        int x, y;
        SDL_GetWindowPosition(g_main_window, &x, &y);
        Settings current_settings = load_settings();
        current_settings.has_window_pos = true;
        current_settings.window_x = x;
        current_settings.window_y = y;

#if SUPPORT_BAESCRIPT == TRUE
        /* Save script editor state */
        current_settings.has_script_enabled = true;
        current_settings.script_enabled = script_editor_get_enabled();
        const char *se_path = script_editor_get_path();
        if (se_path && se_path[0]) {
            safe_strncpy(current_settings.script_path, se_path, sizeof(current_settings.script_path) - 1);
            current_settings.script_path[sizeof(current_settings.script_path) - 1] = '\0';
            current_settings.has_script_path = true;
        } else {
            current_settings.script_path[0] = '\0';
            current_settings.has_script_path = false;
        }
        /* Always save the current text buffer (fallback if file is moved/deleted) */
        const char *se_text = script_editor_get_text();
        if (se_text && se_text[0]) {
            size_t tlen = strlen(se_text);
            if (tlen > sizeof(current_settings.script_text) - 1)
                tlen = sizeof(current_settings.script_text) - 1;
            memcpy(current_settings.script_text, se_text, tlen);
            current_settings.script_text[tlen] = '\0';
            current_settings.has_script_text = true;
        } else {
            current_settings.script_text[0] = '\0';
            current_settings.has_script_text = false;
        }
#endif

    #if USE_NATIVE_DLS == TRUE
        extern bool g_use_dls_compatiblity_mode;
        current_settings.has_dls_compatibility_mode = true;
        current_settings.dls_compatibility_mode = g_use_dls_compatiblity_mode;
    #endif

        save_full_settings(&current_settings);
    }

#if SUPPORT_PLAYLIST == TRUE // Auto-save playlist under NeoBAE config root
    {
        char playlist_path[768];
        zefidi_get_playlist_path(playlist_path, sizeof(playlist_path));

        if (g_playlist.count > 0)
        {
            BAE_PRINTF("Auto-saving playlist: %s\n", playlist_path);
            playlist_save(playlist_path);
        }
        else
        {
            // If playlist is empty, remove the file if it exists
            if (remove(playlist_path) == 0)
            {
                BAE_PRINTF("Removed empty playlist file: %s\n", playlist_path);
            }
        }
    }
#endif

    // Stop any active exports before audio shutdown
    export_cleanup();

    // Clean up karaoke subsystem (destroys g_lyric_mutex)
#if SUPPORT_KARAOKE == TRUE 
    karaoke_cleanup();
#endif

    // Clean up UI state subsystems
    settings_cleanup();
    dialogs_cleanup();

    // Shut down audio engine BEFORE destroying SDL resources.
    // On macOS/SDL3 (Cocoa), destroying the window first can disturb the
    // main run loop before the audio callback is stopped, causing a crash.
    bae_shutdown();

    gui_text_cache_clear();
    SDL_DestroyRenderer(R);
    SDL_DestroyWindow(win);
    g_main_window = NULL; // Clear global reference

#if SUPPORT_PLAYLIST == TRUE
    playlist_cleanup();
#endif
#if _DEBUG == TRUE
    debug_console_shutdown();
#endif
#if SUPPORT_BAESCRIPT == TRUE
    script_editor_shutdown();
#endif
    if (g_font)
        TTF_CloseFont(g_font);
    TTF_Quit();
    SDL_Quit();
    return 0;
}

#if defined(_WIN32) && USE_SDL2 == TRUE
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmdLine, int nShowCmd)
{
    return main(__argc, __argv);
}
#endif