// SDL3 GUI for miniBAE – inspired by the BXPlayer GUI.
// zefie
// 2025-08-22: This file is still a hot mess even after refactor,
//             but we shaved nearly 5000 lines from it
// 2025-12-02: Updated to SDL3

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
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
#include <SDL3/SDL_opengl.h>
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
#include "MiniBAE.h"
#include "gui_bae.h"
#include "BAE_API.h" // for BAE_GetDeviceSamplesPlayedPosition diagnostics
#include "X_Assert.h"

// GUI includes
#include "gui_common.h"
#include "gui_settings.h"
#include "gui_dialogs.h"
#include "gui_logging.h"
#include "gui_bae.h"
#include "gui_export.h"
#include "gui_theme.h"    // for theme globals and detection functions
#include "gui_widgets.h"  // for UI widget functions
#include "gui_text.h"     // for text rendering functions
#include "gui_midi.h"     // for virtual keyboard functions
#include "gui_playlist.h" // for playlist panel functions
#include "gui_panels.h"

#if USE_SF2_SUPPORT == TRUE
    #if _USING_FLUIDSYNTH == TRUE
        #include "GenSF2_FluidSynth.h"
    #endif
#endif

int g_thread_ch_enabled[16] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
/* Forward-declare dialog renderer from gui_dialogs.c to avoid including the
    full header (which defines globals that conflict with this file's statics). */
void render_about_dialog(SDL_Renderer *R, int mx, int my, bool mclick);

#ifdef SUPPORT_MIDI_HW
#include "gui_midi_hw.h"
#endif

#ifdef SUPPORT_KARAOKE
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
typedef short XSWORD; // 16-bit signed used by engine for track index

// Forward declarations for functions
bool bae_load_song(const char *path);
bool bae_load_song_with_settings(const char *path, int transpose, int tempo, int volume, bool loop_enabled, int reverb_type, bool ch_enable[16]);
void bae_seek_ms(int ms);
int bae_get_pos_ms(void);
bool bae_play(bool *playing);
void bae_apply_current_settings(int transpose, int tempo, int volume, bool loop_enabled, int reverb_type, bool ch_enable[16]);
bool recreate_mixer_and_restore(int sampleRateHz, bool stereo, int reverbType,
                                int transpose, int tempo, int volume, bool loopPlay,
                                bool ch_enable[16]);
bool load_bank(const char *path, bool current_playing_state, int transpose, int tempo, int volume, bool loop_enabled, int reverb_type, bool ch_enable[16], bool save_to_settings);
bool load_bank_simple(const char *path, bool save_to_settings, int reverb_type, bool loop_enabled);

void safe_strncpy(char *dst, const char *src, size_t size) {
    strncpy(dst, src, size - 1);
    dst[size - 1] = '\0';
}

// Helper function to update MSB/LSB display values based on current channel's bank settings
void update_msb_lsb_for_channel(void)
{
#ifdef SUPPORT_MIDI_HW
    // Check if we're actually in MIDI input mode
    if (g_midi_input_enabled)
    {
        // Use the tracked MIDI bank values for the current channel when MIDI input is active
        g_keyboard_msb = g_midi_bank_msb[g_keyboard_channel];
        g_keyboard_lsb = g_midi_bank_lsb[g_keyboard_channel];
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
        BAEResult result = BAESong_GetProgramBank(target, (unsigned char)g_keyboard_channel, &program, &bank);
        if (result == BAE_NO_ERROR)
        {
            // For General MIDI compatibility, bank 0 typically maps to MSB=0, LSB=0
            // Higher banks may use different MSB/LSB combinations depending on the sound bank
            g_keyboard_msb = bank;
            g_keyboard_lsb = program;
        }
        else
        {
            // Fallback to default values if query fails
            g_keyboard_msb = 0;
            g_keyboard_lsb = 0;
        }
    }
    else
    {
        // No song available, use default values
        g_keyboard_msb = 0;
        g_keyboard_lsb = 0;
    }
}

// Helper function to send bank select messages when MSB/LSB values change
void send_bank_select_for_current_channel(void)
{
#ifdef SUPPORT_MIDI_HW
    // Update the tracked bank values for the current channel
    g_midi_bank_msb[g_keyboard_channel] = g_keyboard_msb;
    g_midi_bank_lsb[g_keyboard_channel] = g_keyboard_lsb;
#endif

    BAE_PRINTF("Bank Select - Channel %d: MSB=%d, LSB=%d\n", g_keyboard_channel + 1, g_keyboard_msb, g_keyboard_lsb);

    // Send bank select messages to both the engine and external MIDI output
    BAESong target = g_bae.song ? g_bae.song : g_live_song;
    if (target)
    {
        BAESong_ProgramBankChange(target, (unsigned char)g_keyboard_channel, g_keyboard_lsb, g_keyboard_msb, 0);
        BAESong_LoadInstrument(target, g_keyboard_lsb);
    }

#ifdef SUPPORT_MIDI_HW
    // Also send to external MIDI output if enabled
    if (g_midi_output_enabled)
    {
        unsigned char msb_msg[3] = {(unsigned char)(0xB0 | (g_keyboard_channel & 0x0F)), 0, (unsigned char)g_keyboard_msb};
        unsigned char lsb_msg[3] = {(unsigned char)(0xB0 | (g_keyboard_channel & 0x0F)), 32, (unsigned char)g_keyboard_lsb};
        midi_output_send(msb_msg, 3);
        midi_output_send(lsb_msg, 3);
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
// #ifdef EMBED_TTF_FONT
#include "embedded_font.h" // provides embedded_font_data[], embedded_font_size
// #endif

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

#ifdef _WIN32
// Single-instance support: mutex name must be stable across runs.
static const char *g_single_instance_mutex_name = "zefidi_single_instance_mutex_v1";
// Previous window proc so we can chain messages we don't handle
static WNDPROC g_prev_wndproc = NULL;

// Helper for EnumWindows: find window with title containing desired substring
struct EnumCtx
{
    const char *want;
    HWND found;
};
static BOOL CALLBACK zefidi_EnumProc(HWND hwnd, LPARAM lparam)
{
    struct EnumCtx *ctx = (struct EnumCtx *)lparam;
    char title[512];
    if (GetWindowTextA(hwnd, title, sizeof(title)) > 0)
    {
        if (strstr(title, ctx->want))
        {
            ctx->found = hwnd;
            return FALSE; // stop enumeration
        }
    }
    return TRUE; // continue
}

// Custom window proc to receive WM_COPYDATA and forward to SDL event queue.
static LRESULT CALLBACK zefidi_WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (uMsg == WM_COPYDATA)
    {
        PCOPYDATASTRUCT cds = (PCOPYDATASTRUCT)lParam;
        if (cds && cds->lpData && cds->cbData > 0)
        {
            // Allocate a null-terminated copy and push it as an SDL user event.
            char *s = (char *)malloc(cds->cbData + 1);
            if (s)
            {
                memcpy(s, cds->lpData, cds->cbData);
                s[cds->cbData] = '\0';
                SDL_Event ev;
                memset(&ev, 0, sizeof(ev));
                ev.type = SDL_EVENT_USER;
                ev.user.code = 1; // code 1 == external file open
                ev.user.data1 = s;
                ev.user.data2 = NULL;
                SDL_PushEvent(&ev);
                // Bring window to foreground and restore if minimized
                ShowWindow(hwnd, SW_RESTORE);
                SetForegroundWindow(hwnd);
                return 1; // handled
            }
        }
    }
    // Chain to previous proc for unhandled messages
    if (g_prev_wndproc)
        return CallWindowProc(g_prev_wndproc, hwnd, uMsg, wParam, lParam);
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}
#endif

// Global variable to track current bank path for settings saving
char g_current_bank_path[512] = "";

#define WINDOW_W 900
int g_window_h = WINDOW_BASE_H; // dynamic height (expands when karaoke visible)

// Global window reference for settings saving
SDL_Window *g_main_window = NULL;

// ------------- miniBAE integration -------------

BAEGUI g_bae = {0};
bool g_reverbDropdownOpen = false;

// Guard to avoid recursive mixer recreation when load_bank is invoked by
// recreate_mixer_and_restore (which itself calls load_bank to restore banks).
// This prevents infinite recursion while still allowing an initial recreate
// when a bank is loaded while MIDI input is enabled.
bool g_in_bank_load_recreate = false;

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

#ifdef SUPPORT_KARAOKE
#define KARAOKE_MAX_LINES 256

// Forward declaration (defined later) so helpers can call it
void karaoke_commit_line(uint32_t time_us, const char *line);
#endif

// Total playtime globals (ms) tracked across the session — used by transport UI
// This timer accumulates playback time even when the song loops and is
// advanced using deltas of the engine position so it does not reset on loops.
static int g_total_play_ms = 0;
static int g_last_engine_pos_ms = 0;

// Progress bar stripe animation state
static int g_progress_stripe_offset = 0;
static const int g_progress_stripe_width = 28;
// Toggle for WebTV-style progress bar (Settings -> "WebTV Style Bar")
bool g_disable_webtv_progress_bar = false; // default: WebTV enabled

// VU meter state (smoothed levels 0.0 .. 1.0 and peak hold)
static float g_vu_left_level = 0.0f;
static float g_vu_right_level = 0.0f;
static int g_vu_peak_left = 0;
static int g_vu_peak_right = 0;
static Uint32 g_vu_peak_hold_until = 0; // universal peak hold timeout (ms)
// Visual gain applied to raw sample amplitudes (linear multiplier)
static float g_vu_gain = 6.0f;

// VU smoothing configuration
// MAIN_VU_ALPHA: lower = smoother (slower response). CHANNEL_VU_ALPHA: higher = more responsive.
static const float MAIN_VU_ALPHA = 0.12f; // main/master VU smoother
// Make per-channel VUs snappier: higher alpha (faster attack) and faster activity decay
static const float CHANNEL_VU_ALPHA = 0.85f; // per-channel VU very responsive
// Activity/decay tuning for the activity-driven channel VU path
static const float CHANNEL_ACTIVITY_DECAY = 0.60f; // lower -> faster decay (more responsive)

// Dialog state (defined in gui_dialogs.c) — reference via externs so both
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

extern bool g_loop_tooltip_visible;
extern Rect g_loop_tooltip_rect;
extern char g_loop_tooltip_text[520];

extern bool g_voice_tooltip_visible;
extern Rect g_voice_tooltip_rect;
extern char g_voice_tooltip_text[520];

extern bool g_lsb_tooltip_visible;
extern Rect g_lsb_tooltip_rect;
extern char g_lsb_tooltip_text[520];

extern bool g_msb_tooltip_visible;
extern Rect g_msb_tooltip_rect;
extern char g_msb_tooltip_text[520];

// Map integer Hz to BAERate enum (subset offered in UI)
static BAERate map_rate_from_hz(int hz)
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

// Recreate mixer with new sample rate / stereo setting preserving current playback state where possible.
bool recreate_mixer_and_restore(int sampleRateHz, bool stereo, int reverbType,
                                int transpose, int tempo, int volume, bool loopPlay,
                                bool ch_enable[16])
{
#ifdef SUPPORT_MIDI_HW
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
        strncpy(last_song_path, g_bae.loaded_path, sizeof(last_song_path) - 1);
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
    if (g_bae.mixer)
    {
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
        GM_SF2_SetStereoMode(stereo, FALSE); // Don't apply now because we are going to apply next
        GM_SF2_SetSampleRate(sampleRateHz);
    }
#endif    
    // Create new mixer
#if USE_SF2_SUPPORT == TRUE
    XBOOL wasSF2 = GM_GetMixerSF2Mode();
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
    BAEAudioModifiers mods = BAE_USE_16 | (stereo ? BAE_USE_STEREO : 0);
    BAEResult mr = BAEMixer_Open(g_bae.mixer, rate, BAE_LINEAR_INTERPOLATION, mods, 32, 8, 32, TRUE);
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
    BAEMixer_SetDefaultReverb(g_bae.mixer, (BAEReverbType)reverbType);
    BAEMixer_SetMasterVolume(g_bae.mixer, FLOAT_TO_UNSIGNED_FIXED(1.0));

    // Ensure the lightweight live song is recreated so external MIDI
    // input continues to route into the new mixer. If a previous
    // g_live_song exists it referenced the old mixer and must be
    // deleted before creating a new one bound to the new mixer.
    if (g_live_song)
    {
        BAESong_Stop(g_live_song, FALSE);
        BAESong_Delete(g_live_song);
        g_live_song = NULL;
    }
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
        if (bae_load_song_with_settings(last_song_path, transpose, tempo, volume, loopPlay, reverbType, ch_enable))
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
#ifdef SUPPORT_MIDI_HW
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

void setWindowTitle(SDL_Window *window)
{
    const char *libMiniBAECPUArch = BAE_GetCurrentCPUArchitecture();
    const char *libMiniBAEVersion = BAE_GetVersion();
    char windowTitle[128];
    snprintf(windowTitle, sizeof(windowTitle), "zefidi Media Player - %s - %s", libMiniBAECPUArch, libMiniBAEVersion);
    SDL_SetWindowTitle(window, windowTitle);
}

void setWindowIcon(SDL_Window *window)
{
#ifdef _WIN32
    // On Windows, the icon will be automatically loaded from the resource file
    // when the executable is built with the resource compiled in.
    // The window icon is typically handled by the system for applications with embedded icons.

    // Try to get the window handle and set the icon manually as a fallback
    SDL_PropertiesID props = SDL_GetWindowProperties(window);
    if (props) {
        void *hw = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
        if (hw) {
            HWND hwnd = (HWND)hw;
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
    }
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
bool isWindows10LTSC2021(void) {
    HKEY hKey;
    char productName[256] = {0};
    char releaseId[256] = {0};
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

    size = sizeof(releaseId);
    if (RegGetValueA(hKey, NULL, "ReleaseId", RRF_RT_REG_SZ, NULL, releaseId, &size) != ERROR_SUCCESS) {
        RegCloseKey(hKey);
        return false;
    }

    RegCloseKey(hKey);

    return (strstr(productName, "LTSC") != NULL && strcmp(releaseId, "21H2") == 0);
}
#endif



// Playlist export functions
int main(int argc, char *argv[])
{
    // Single-instance check (Windows): if another instance exists, forward any file arg and exit.
#ifdef _WIN32
    HANDLE singleMutex = CreateMutexA(NULL, FALSE, g_single_instance_mutex_name);
    if (singleMutex)
    {
        if (GetLastError() == ERROR_ALREADY_EXISTS)
        {
            // Another instance running: find its main window by enumerating top-level windows and match title
            if (argc > 1)
            {
                // Build a single string with the first argument (path). We only forward first file for simplicity.
                const char *pathToSend = argv[1];
                HWND found = NULL;
                // Title we expect
                const char *want = "zefidi Media Player";
                // Enumerator callback
                struct EnumCtx ctx;
                ctx.want = want;
                ctx.found = NULL;
                // Enumerate windows using the global helper
                EnumWindows(zefidi_EnumProc, (LPARAM)&ctx);
                found = ctx.found;
                if (found)
                {
                    COPYDATASTRUCT cds;
                    cds.dwData = 0xBAE1; // magic
                    // Include terminating NUL so receiver can rely on it
                    cds.cbData = (DWORD)(strlen(pathToSend) + 1);
                    cds.lpData = (PVOID)pathToSend;
                    SendMessageA(found, WM_COPYDATA, (WPARAM)NULL, (LPARAM)&cds);
                }
            }
            CloseHandle(singleMutex);
            return 0; // exit second instance
        }
    }
#endif
    #ifdef _WIN32
        if (isWindows10LTSC2021()) {
            SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengl");
        }
    #endif
    if (SDL_Init(SDL_INIT_VIDEO) != true)
    {
        BAE_PRINTF("SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    if (TTF_Init() != true)
    {
        BAE_PRINTF("SDL_ttf init failed: %s (continuing with bitmap font)\n", SDL_GetError());
    }
    else
    {
        static int ttf_font_size = 14;

#ifdef EMBED_TTF_FONT
#if defined(SDL_IOFromConstMem)
        g_font = TTF_OpenFontIO(SDL_IOFromConstMem(embedded_font_data, embedded_font_size), false, ttf_font_size);
#else
        /* Fall back to SDL_RWFromMem: cast away const to match the API and silence warnings. */
        g_font = TTF_OpenFontIO(SDL_IOFromMem((void *)embedded_font_data, embedded_font_size), false, ttf_font_size);
#endif
#endif
        if (!g_font) {
            const char *tryFonts[] = {"C:/Windows/Fonts/arial.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", NULL};
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
    bool ch_enable[16];
    for (int i = 0; i < 16; i++)
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
    if (settings.has_stereo)
    {
        g_stereo_output = settings.stereo_output;
    }
    if (settings.has_sample_rate)
    {
        g_sample_rate_hz = map_rate_from_hz(settings.sample_rate_hz);
    }
    if (settings.has_show_keyboard)
    {
        g_show_virtual_keyboard = settings.show_keyboard;
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
    // Apply stored default velocity (aka volume) curve to global engine setting so new songs adopt it
    if (settings.has_volume_curve)
    {
        BAE_SetDefaultVelocityCurve(g_volume_curve);
    }
    if (!bae_init(g_sample_rate_hz, g_stereo_output))
    {
        BAE_PRINTF("miniBAE init failed (rate=%d, stereo=%d)\n", g_sample_rate_hz, g_stereo_output);
        if (!bae_init(g_sample_rate_hz, g_stereo_output))
        {
            BAE_PRINTF("miniBAE init failed (2nd try)\n");
        }
    }

    // Load bank database AFTER mixer so load_bank can succeed
    load_bankinfo();

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

    // Auto-load playlist.m3u from application directory if it exists
#endif

    // Initialize export subsystem
    export_init();

    char exe_dir[512];
    get_executable_directory(exe_dir, sizeof(exe_dir));
#if SUPPORT_PLAYLIST == TRUE // Initialize playlist system
    char playlist_path[768];
#ifdef _WIN32
    snprintf(playlist_path, sizeof(playlist_path), "%s\\playlist.m3u", exe_dir);
#else
    snprintf(playlist_path, sizeof(playlist_path), "%s/playlist.m3u", exe_dir);
#endif

    // Check if file exists and load it
    FILE *test_file = fopen(playlist_path, "r");
    if (test_file)
    {
        fclose(test_file);
        BAE_PRINTF("Auto-loading playlist: %s\n", playlist_path);
        playlist_load(playlist_path);
    }
#endif

    if (!g_bae.bank_loaded)
    {
        BAE_PRINTF("WARNING: No patch bank loaded. Place patches.hsb next to executable or use built-in patches.\n");
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
    const float FRAME_TIME_MS = 1000.0f / TARGET_FPS;

    SDL_Window *win = SDL_CreateWindow("zefidi Media Player", 900, g_window_h, 0);
    if (!win)
    {
        BAE_PRINTF("Window failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    g_main_window = win; // Store global reference for settings saving
    setWindowTitle(win);
    setWindowIcon(win);
    SDL_SetWindowResizable(win, false);
    SDL_SetWindowPosition(win, window_x, window_y);

    SDL_Renderer *R = SDL_CreateRenderer(win, NULL);

    if (!R)
    {
        BAE_PRINTF("Renderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }
    
    bool running = true;
    duration = bae_get_len_ms();
    g_bae.loop_enabled_gui = loopPlay;
    bae_set_volume(volume);
    bae_set_tempo(tempo);
    bae_set_transpose(transpose);
    bae_set_loop(loopPlay);
    bae_set_reverb(reverbType);

    // Load bank (use saved bank if available, otherwise fallback)
    if (settings.has_bank && strlen(settings.bank_path) > 0)
    {
        BAE_PRINTF("Loading saved bank: %s\n", settings.bank_path);
        bool loaded_saved = load_bank_simple(settings.bank_path, false, reverbType, loopPlay); // false = don't save to settings (it's already saved)

        if (!loaded_saved)
        {
            BAE_PRINTF("Saved bank missing or failed to load. Trying patches.hsb in executable dir...\n");

            // Next priority: patches.hsb located next to the executable
            char exe_dir_try[512];
            char patches_try[1024];
            get_executable_directory(exe_dir_try, sizeof(exe_dir_try));
#ifdef _WIN32
            snprintf(patches_try, sizeof(patches_try), "%s\\patches.hsb", exe_dir_try);
#else
            snprintf(patches_try, sizeof(patches_try), "%s/patches.hsb", exe_dir_try);
#endif
            FILE *tf = fopen(patches_try, "r");
            if (tf)
            {
                fclose(tf);
                if (load_bank_simple(patches_try, false, reverbType, loopPlay))
                {
                    // Remember which bank we loaded for UI (do not overwrite user settings file)
                    strncpy(g_current_bank_path, patches_try, sizeof(g_current_bank_path) - 1);
                    g_current_bank_path[sizeof(g_current_bank_path) - 1] = '\0';
                    loaded_saved = true; // treat as loaded to skip built-in fallback
                }
                else
                {
                    BAE_PRINTF("Found patches.hsb but failed to load it: %s\n", patches_try);
                }
            }

            if (!loaded_saved)
            {
                BAE_PRINTF("Falling back to built-in/default discovery...\n");
#ifdef _BUILT_IN_PATCHES
                // Try built-in bank when compiled in
                if (!load_bank_simple("__builtin__", false, reverbType, loopPlay))
                {
                    // Final fallback to default discovery
                    (void)load_bank_simple(NULL, false, reverbType, loopPlay);
                }
#else
                // No built-in bank compiled. Try default discovery
                (void)load_bank_simple(NULL, false, reverbType, loopPlay);
#endif
            }
        }
        else
        {
            // Only record the saved path if that specific bank actually loaded
            strncpy(g_current_bank_path, settings.bank_path, sizeof(g_current_bank_path) - 1);
            g_current_bank_path[sizeof(g_current_bank_path) - 1] = '\0';
        }
    }
    else
    {
        BAE_PRINTF("No saved bank found, trying patches.hsb in executable dir then built-in...\n");

        // First try patches.hsb next to the executable
        char exe_dir_try[512];
        char patches_try[1024];
        get_executable_directory(exe_dir_try, sizeof(exe_dir_try));
#ifdef _WIN32
        snprintf(patches_try, sizeof(patches_try), "%s\\patches.hsb", exe_dir_try);
#else
        snprintf(patches_try, sizeof(patches_try), "%s/patches.hsb", exe_dir_try);
#endif
        FILE *tf = fopen(patches_try, "r");
        if (tf)
        {
            fclose(tf);
            if (load_bank_simple(patches_try, false, reverbType, loopPlay))
            {
                strncpy(g_current_bank_path, patches_try, sizeof(g_current_bank_path) - 1);
                g_current_bank_path[sizeof(g_current_bank_path) - 1] = '\0';
            }
            else
            {
                BAE_PRINTF("Found patches.hsb but failed to load it: %s\n", patches_try);
#ifdef _BUILT_IN_PATCHES
                if (!load_bank_simple("__builtin__", false, reverbType, loopPlay))
                {
                    (void)load_bank_simple(NULL, false, reverbType, loopPlay);
                }
#else
                (void)load_bank_simple(NULL, false, reverbType, loopPlay);
#endif
            }
        }
        else
        {
#ifdef _BUILT_IN_PATCHES
            // No patches.hsb; try built-in if available
            if (!load_bank_simple("__builtin__", false, reverbType, loopPlay))
            {
                (void)load_bank_simple(NULL, false, reverbType, loopPlay);
            }
#else
            // Final fallback: auto-discovery (npatches/patches)
            (void)load_bank_simple(NULL, false, reverbType, loopPlay);
#endif
        }
    }

    // Initialize MSB/LSB values for the default channel
    update_msb_lsb_for_channel();

    // Load command line file if provided
    if (argc > 1)
    {
        if (bae_load_song_with_settings(argv[1], transpose, tempo, volume, loopPlay, reverbType, ch_enable))
        {
#if SUPPORT_PLAYLIST == TRUE
            // Add file to playlist and set as current
            if (g_playlist.enabled) {
                playlist_update_current_file(argv[1]);
            }
#endif
            duration = bae_get_len_ms();
            playing = false;    // Ensure we start from stopped state
            bae_play(&playing); // Auto-start playback
        }
    }

#ifdef _WIN32
    // Subclass the native HWND to receive WM_COPYDATA messages from subsequent instances
    SDL_PropertiesID props = SDL_GetWindowProperties(win);
    if (props) {
        void *hw = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
        if (hw) {
            HWND hwnd = (HWND)hw;
            g_prev_wndproc = (WNDPROC)SetWindowLongPtr(hwnd, GWLP_WNDPROC, (LONG_PTR)zefidi_WndProc);
            BAE_PRINTF("Installed zefidi_WndProc chain (prev=%p)\n", (void *)g_prev_wndproc);
        }
    }
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
            switch (e.type)
            {
            case SDL_EVENT_USER:
            {
                if (e.user.code == 1 && e.user.data1)
                {
                    char *incoming = (char *)e.user.data1;
                    BAE_PRINTF("Received external open request: %s\n", incoming);
                    // Try loading as bank or media file depending on extension
                    const char *ext = strrchr(incoming, '.');
                    bool is_bank_file = false;
                    if (ext)
                    {
#ifdef _WIN32
                        is_bank_file = _stricmp(ext, ".hsb") == 0;
#if USE_SF2_SUPPORT == TRUE
                        if (!is_bank_file)
                            is_bank_file = _stricmp(ext, ".sf2") == 0;
#if USE_VORBIS_DECODER == TRUE
                        if (!is_bank_file) {
                            is_bank_file = _stricmp(ext, ".sf3") == 0;
                        }
                        if (!is_bank_file) {
                            is_bank_file = _stricmp(ext, ".sfo") == 0;
                        }
#if _USING_FLUIDSYNTH == TRUE                        
                        if (!is_bank_file) {
                            is_bank_file = _stricmp(ext, ".dls") == 0;
                        }
#endif                        
#endif
#endif

#else
                        is_bank_file = strcasecmp(ext, ".hsb") == 0;
#if USE_SF2_SUPPORT == TRUE
                        if (!is_bank_file)
                            is_bank_file = strcasecmp(ext, ".sf2") == 0;
#if USE_VORBIS_DECODER == TRUE
                        if (!is_bank_file)
                            is_bank_file = strcasecmp(ext, ".sf3") == 0;
                        if (!is_bank_file)
                            is_bank_file = strcasecmp(ext, ".sfo") == 0;
#if _USING_FLUIDSYNTH == TRUE                        
                        if (!is_bank_file)
                            is_bank_file = strcasecmp(ext, ".dls") == 0;
                
#endif
#endif
#endif
#endif
                    }
                    if (is_bank_file)
                    {
                        if (load_bank(incoming, playing, transpose, tempo, volume, loopPlay, reverbType, ch_enable, true))
                        {
                            set_status_message("Loaded bank from external request");
                        }
                        else
                        {
                            set_status_message("Failed to load external bank file");
                        }
                    }
                    else
                    {
#ifdef SUPPORT_MIDI_HW
                        // If MIDI input is enabled we must ignore external IPC open requests for media
                        if (g_midi_input_enabled)
                        {
                            BAE_PRINTF("External open request: MIDI input enabled - ignoring: %s\n", incoming);
                            set_status_message("MIDI input enabled: external open ignored");
                        }
                        else
                        {
#endif
                            if (bae_load_song_with_settings(incoming, transpose, tempo, volume, loopPlay, reverbType, ch_enable))
                            {
#if SUPPORT_PLAYLIST == TRUE
                                // Add file to playlist and set as current
                                if (g_playlist.enabled) {
                                    playlist_update_current_file(incoming);
                                }
#endif
                                duration = bae_get_len_ms();
                                progress = 0;
                                playing = false;
                                bae_play(&playing);
                            }
                            else
                            {
                                set_status_message("Failed to load external media file");
                            }
#ifdef SUPPORT_MIDI_HW
                        }
#endif
                    }
                    free(incoming);
                }
            }
            break;

            case SDL_EVENT_QUIT:
                running = false;
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (e.button.button == SDL_BUTTON_LEFT)
                {
                    mdown = true;
                }
                break;
            case SDL_EVENT_MOUSE_BUTTON_UP:
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
            case SDL_EVENT_MOUSE_MOTION:
                mx = e.motion.x;
                my = e.motion.y;

#if SUPPORT_PLAYLIST == TRUE
                // Handle playlist drag update
                if (g_playlist.enabled) {
                    playlist_handle_drag_update(mx, my);
                }
#endif
                break;
            case SDL_EVENT_MOUSE_WHEEL:
            {
                // Mouse wheel: when hovered over certain controls, change selection/value by 1
                // For dropdowns we keep existing semantics; for sliders we apply +1 for wheel up, -1 for wheel down.
                int wy = e.wheel.y; // positive = scroll up, negative = scroll down
                if (wy != 0)
                {
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
                                // Update MSB/LSB display values for the newly selected channel
                                update_msb_lsb_for_channel();
                            }
                        }
                        else
                        {
                            // Slider handling: respect the same modal/enable rules used elsewhere
                            bool playback_controls_enabled_local =
#ifdef SUPPORT_MIDI_HW
                                !g_midi_input_enabled;
#else
                                true;
#endif
                            /* Allow the user to adjust master volume even when MIDI input
                                is enabled so incoming MIDI velocity can be scaled. Keep
                                other playback controls disabled as before. */
                            bool volume_enabled_local = !g_reverbDropdownOpen;

                            // Calculate the LSB/MSB rects the same way as in rendering
                            int keyboardPanelY_wheel = transportPanel.y + transportPanel.h + 10;
                            Rect keyboardPanel_wheel = {10, keyboardPanelY_wheel, 880, 110};
                            bool showKeyboard_wheel = g_show_virtual_keyboard && g_bae.song && !g_bae.is_audio_file && g_bae.song_loaded;
                            bool showWaveform_wheel = g_bae.is_audio_file && g_bae.sound;
                            if (showWaveform_wheel)
                                showKeyboard_wheel = false;

                            bool handled_wheel_event = false;

                            // LSB/MSB number pickers wheel handling - check first
                            if (showKeyboard_wheel && !(g_bae.is_audio_file && g_bae.sound))
                            {
                                // Calculate MSB/LSB rects to match the actual rendering coordinates
                                int picker_y_wheel = keyboardPanel_wheel.y + 56; // below channel dropdown
                                int picker_w_wheel = 35;                         // compact width for 3-digit numbers
                                int picker_h_wheel = 18;
                                int spacing_wheel = 5;

                                // MSB and LSB number picker rects (match rendering exactly)
                                Rect msbRect_wheel = {keyboardPanel_wheel.x + 10, picker_y_wheel, picker_w_wheel, picker_h_wheel};
                                Rect lsbRect_wheel = {msbRect_wheel.x + picker_w_wheel + spacing_wheel, picker_y_wheel, picker_w_wheel, picker_h_wheel};

                                if (point_in(mx, my, msbRect_wheel))
                                {
                                    change_bank_value_for_current_channel(true, sdelta);
                                    handled_wheel_event = true;
                                }
                                else if (point_in(mx, my, lsbRect_wheel))
                                {
                                    change_bank_value_for_current_channel(false, sdelta);
                                    handled_wheel_event = true;
                                }
                            }

                            // Try transpose/tempo/volume helpers in order (only if MSB/LSB didn't handle it)
                            if (!handled_wheel_event)
                            {
                                if (!ui_adjust_transpose(mx, my, sdelta, playback_controls_enabled_local, &transpose))
                                {
                                    if (!ui_adjust_tempo(mx, my, sdelta, playback_controls_enabled_local, &tempo, &duration, &progress))
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
                                                if (g_playlist.enabled) {
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
            case SDL_EVENT_DROP_FILE:
            {
                const char *dropped = e.drop.data;
                if (dropped)
                {
                    // Get current mouse position to check if drop is over playlist
                    float drop_mx, drop_my;
                    SDL_GetMouseState(&drop_mx, &drop_my);

                    // Check file extension
                    const char *ext = strrchr(dropped, '.');
                    bool is_bank_file = false;
                    bool is_playlist_file = false;
                    if (ext)
                    {
#ifdef _WIN32
                        is_bank_file = _stricmp(ext, ".hsb") == 0;
#if USE_SF2_SUPPORT == TRUE
                        if (!is_bank_file)
                            is_bank_file = _stricmp(ext, ".sf2") == 0;
#if USE_VORBIS_DECODER == TRUE
                        if (!is_bank_file)
                            is_bank_file = _stricmp(ext, ".sf3") == 0;
                        if (!is_bank_file)
                            is_bank_file = _stricmp(ext, ".sfo") == 0;
#if _USING_FLUIDSYNTH == TRUE
                        if (!is_bank_file)
                            is_bank_file = _stricmp(ext, ".dls") == 0;
#endif
#endif
#endif
                        is_playlist_file = (_stricmp(ext, ".m3u") == 0);
#else
                        is_bank_file = strcasecmp(ext, ".hsb") == 0;
#if USE_SF2_SUPPORT == TRUE
                        if (!is_bank_file)
                            is_bank_file = strcasecmp(ext, ".sf2") == 0;
#if USE_VORBIS_DECODER == TRUE
                        if (!is_bank_file)
                            is_bank_file = strcasecmp(ext, ".sf3") == 0;
                        if (!is_bank_file)
                            is_bank_file = strcasecmp(ext, ".sfo") == 0;
#if _USING_FLUIDSYNTH == TRUE
                        if (!is_bank_file)
                            is_bank_file = strcasecmp(ext, ".dls") == 0;
#endif
#endif
                        is_playlist_file = (strcasecmp(ext, ".m3u") == 0);
#endif
#endif
                    }

                    if (is_bank_file)
                    {
                        // Load as patch bank
                        BAE_PRINTF("Drag and drop: Loading bank file: %s\n", dropped);
                        if (load_bank(dropped, playing, transpose, tempo, volume, loopPlay, reverbType, ch_enable, true))
                        {
                            BAE_PRINTF("Successfully loaded dropped bank: %s\n", dropped);
                        }
                        else
                        {
                            BAE_PRINTF("Failed to load dropped bank: %s\n", dropped);
                            set_status_message("Failed to load dropped bank file");
                        }
                    }
#if SUPPORT_PLAYLIST == TRUE
                    else if (is_playlist_file)
                    {
                        // Load as playlist
                        BAE_PRINTF("Drag and drop: Loading playlist file: %s\n", dropped);
                        playlist_load(dropped);
                        set_status_message("Playlist loaded");
                    }
#endif
                    else
                    {
#ifdef SUPPORT_MIDI_HW
                        // If MIDI input is enabled we don't accept dropped media files
                        if (g_midi_input_enabled)
                        {
                            BAE_PRINTF("Drag and drop: MIDI input enabled - ignoring dropped media: %s\n", dropped);
                            set_status_message("MIDI input enabled: media drop ignored");
                        }
                        else
                        {
#endif
#if SUPPORT_PLAYLIST == TRUE
                            if (g_playlist.enabled) {
                                // Calculate playlist panel bounds to check if drop is over playlist
                                // Use same calculation as in rendering section
                                int playlistPanelHeight = 300;
                                Rect transportPanel_local = {10, 160, 880, 85}; // same as rendering
                                int keyboardPanelY_local = transportPanel_local.y + transportPanel_local.h + 10;
                                Rect keyboardPanel_local = {10, keyboardPanelY_local, 880, 110};
#ifdef SUPPORT_MIDI_HW
                                bool showKeyboard_local = g_show_virtual_keyboard && (g_midi_input_enabled || (g_bae.song_loaded && !g_bae.is_audio_file));
#else
                                bool showKeyboard_local = g_show_virtual_keyboard && (g_bae.song_loaded && !g_bae.is_audio_file);
#endif
                                bool showWaveform_local = g_bae.is_audio_file && g_bae.sound;
                                if (showWaveform_local)
                                    showKeyboard_local = false;
#if SUPPORT_KARAOKE == TRUE
                                bool showKaraoke_local = g_karaoke_enabled && !g_karaoke_suspended &&
                                                        (g_lyric_count > 0 || g_karaoke_line_current[0] || g_karaoke_line_previous[0]) &&
                                                        g_bae.song_loaded && !g_bae.is_audio_file;
                                int karaokePanelHeight_local = 40;
#endif
                                int statusY_local = ((showKeyboard_local || showWaveform_local) ? (keyboardPanel_local.y + keyboardPanel_local.h + 10) : (transportPanel_local.y + transportPanel_local.h + 10));
#if SUPPORT_KARAOKE == TRUE
                                if (showKaraoke_local)
                                    statusY_local = statusY_local + karaokePanelHeight_local + 5;
#endif
                                int playlistPanelY = statusY_local;
                                Rect playlistPanel = {10, playlistPanelY, 880, playlistPanelHeight};

                                // Check if drop is over playlist panel
                                if (point_in(drop_mx, drop_my, playlistPanel))
                                {
                                    // Add to playlist instead of playing immediately
                                    BAE_PRINTF("Drag and drop: Adding media file to playlist: %s\n", dropped);
                                    playlist_add_file(dropped);
                                    set_status_message("File added to playlist");
                                }
                                else
#endif
                                {
                                    // Try to load as media file (original behavior)
                                    BAE_PRINTF("Drag and drop: Loading media file: %s\n", dropped);
                                    if (bae_load_song_with_settings(dropped, transpose, tempo, volume, loopPlay, reverbType, ch_enable))
                                    {
#if SUPPORT_PLAYLIST == TRUE
                                        if (g_playlist.enabled) {
                                            // Add file to playlist and set as current
                                            playlist_update_current_file(dropped);
                                        }
#endif
                                        duration = bae_get_len_ms();
                                        progress = 0;
                                        playing = false;    // Ensure we start from stopped state
                                        bae_play(&playing); // Auto-start playback
                                        BAE_PRINTF("Successfully loaded dropped media: %s\n", dropped);
                                        // Status message is set by bae_load_song_with_settings function
                                    }
                                    else
                                    {
                                        BAE_PRINTF("Failed to load dropped media: %s\n", dropped);
                                        set_status_message("Failed to load dropped media file");
                                    }
                                }
#if SUPPORT_PLAYLIST == TRUE                                
                            }
#endif                            
#ifdef SUPPORT_MIDI_HW
                        }
#endif
                    }
                }
            }
            break;
            case SDL_EVENT_KEY_DOWN:
            case SDL_EVENT_KEY_UP:
            {
                bool isDown = (e.type == SDL_EVENT_KEY_DOWN);
                SDL_Keycode sym = e.key.key;
                // Initialize mapping table once
                if (!g_keyboard_map_initialized)
                {
                    for (int i = 0; i < SDL_SCANCODE_COUNT; i++)
                        g_keyboard_pressed_note[i] = -1;
                    g_keyboard_map_initialized = true;
                }

                // Octave shift: ',' -> down, '.' -> up (on keydown only)
                if (isDown)
                {
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
#ifdef SUPPORT_MIDI_HW
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
                int sc = e.key.scancode;
                int note = -1;
                if (sym == SDLK_A)
                    note = 0; // C
                else if (sym == SDLK_W)
                    note = 1; // C#
                else if (sym == SDLK_S)
                    note = 2; // D
                else if (sym == SDLK_E)
                    note = 3; // D#
                else if (sym == SDLK_D)
                    note = 4; // E
                else if (sym == SDLK_F)
                    note = 5; // F
                else if (sym == SDLK_T)
                    note = 6; // F#
                else if (sym == SDLK_G)
                    note = 7; // G
                else if (sym == SDLK_Y)
                    note = 8; // G#
                else if (sym == SDLK_H)
                    note = 9; // A
                else if (sym == SDLK_U)
                    note = 10; // A#
                else if (sym == SDLK_J)
                    note = 11; // B
                else if (sym == SDLK_K)
                    note = 12; // C (next octave)
                else if (sym == SDLK_O)
                    note = 13; // C#

                if (note != -1)
                {
                    // While exporting we want to disable the virtual keyboard so
                    // user key presses don't affect the export audio. Preserve
                    // other keys like Escape—only ignore piano mapping here.
                    if (g_exporting)
                    {
                        break;
                    }
                    // Only allow musical typing when the virtual keyboard is
                    // actually visible. Match the same visibility rules used
                    // when rendering the keyboard to avoid surprises.
                    bool keyboard_visible_for_typing = false;
#ifdef SUPPORT_MIDI_HW
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
#ifdef SUPPORT_MIDI_HW
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
                    else
                    {
                        // Key up: send note off if we had recorded it
                        if (g_keyboard_pressed_note[sc] != -1)
                        {
                            int heldMidi = g_keyboard_pressed_note[sc];
                            g_keyboard_pressed_note[sc] = -1;
                            if (keyboard_visible_for_typing)
                            {
                                BAESong target = g_bae.song ? g_bae.song : g_live_song;
                                if (target)
                                    BAESong_NoteOff(target, (unsigned char)g_keyboard_channel, (unsigned char)heldMidi, 0, 0);
#ifdef SUPPORT_MIDI_HW
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
                                // Update MSB/LSB display values for the newly selected channel
                                update_msb_lsb_for_channel();
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
        // This ensures progress bar resumes when playback auto-restarts (e.g., after WAV export)
        if (playing != g_bae.is_playing)
        {
            playing = g_bae.is_playing;
        }

#ifdef SUPPORT_MIDI_HW
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
        if (playing)
        {
            progress = bae_get_pos_ms();
            duration = bae_get_len_ms();
        }
        if (!g_exporting) {
            BAEMixer_Idle(g_bae.mixer); // ensure processing if needed
            bae_update_channel_mutes(ch_enable);
        }
#ifdef SUPPORT_MIDI_HW
        // Publish current channel enables to the MIDI thread (plain byte store is fine)
        for (int _ci = 0; _ci < 16; ++_ci)
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
#ifdef SUPPORT_MIDI_HW
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
                        // Track Bank Select MSB/LSB (CC 0 and 32)
                        if (cc == 0)
                        {
                            g_midi_bank_msb[mch] = val;
                            // Update MSB/LSB display if this affects the current keyboard channel
                            if (mch == g_keyboard_channel)
                            {
                                update_msb_lsb_for_channel();
                            }
                        }
                        else if (cc == 32)
                        {
                            g_midi_bank_lsb[mch] = val;
                            // Update MSB/LSB display if this affects the current keyboard channel
                            if (mch == g_keyboard_channel)
                            {
                                update_msb_lsb_for_channel();
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
                            for (int n = 0; n < 128; n++)
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
                                // Send both MSB and LSB before program change
                                BAESong_ControlChange(target, target_ch, 0, g_midi_bank_msb[mch], 0);  // CC 0 = Bank Select MSB
                                BAESong_ControlChange(target, target_ch, 32, g_midi_bank_lsb[mch], 0); // CC 32 = Bank Select LSB
                                BAESong_ProgramChange(target, target_ch, program, 0);
                            }
                        }
                        unsigned char out[2] = {(unsigned char)(0xC0 | (mch & 0x0F)), program};
                        FORWARD_OUT(out, 2);
                        // Update MSB/LSB display if this program change affects the current keyboard channel
                        if (mch == g_keyboard_channel)
                        {
                            update_msb_lsb_for_channel();
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
                    // Handle playlist advancement
                    if (g_playlist.count > 0 && g_playlist.current_index >= 0)
                    {
                        int next_index = playlist_get_next_song_for_end_of_song();

                        if (next_index >= 0 && next_index < g_playlist.count)
                        {
                            // Load and play the next song
                            const char *next_file = g_playlist.entries[next_index].filename;

                            BAE_PRINTF("Playlist: advancing to index %d: %s\n", next_index, next_file);

                            if (bae_load_song_with_settings(next_file, transpose, tempo, volume, loopPlay, reverbType, ch_enable))
                            {
                                // Update playlist current index
                                g_playlist.current_index = next_index;

                                // Start playback
                                duration = bae_get_len_ms();
                                progress = 0;

                                // Robust auto-start sequence: ensure at position 0, preroll again (defensive), then start
                                if (!g_bae.is_audio_file && g_bae.song)
                                {
                                    BAESong_SetMicrosecondPosition(g_bae.song, 0);
                                    BAESong_Preroll(g_bae.song);
                                    if (BAESong_Start(g_bae.song, 0) == BAE_NO_ERROR)
                                    {
                                        playing = true;
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
                                    if (BAESound_Start(g_bae.sound, 0, FLOAT_TO_UNSIGNED_FIXED(g_last_applied_sound_volume), 0) == BAE_NO_ERROR)
                                    {
                                        playing = true;
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
            if (pending_file && bae_load_song_with_settings(pending_file, transpose, tempo, volume, loopPlay, reverbType, ch_enable))
            {
#if SUPPORT_PLAYLIST == TRUE
                // Successfully loaded the song from playlist
                playlist_update_current_file(pending_file);
#endif
                duration = bae_get_len_ms();
                progress = 0;

                // Robust auto-start sequence: ensure at position 0, preroll again (defensive), then start
                if (!g_bae.is_audio_file && g_bae.song)
                {
                    BAESong_SetMicrosecondPosition(g_bae.song, 0);
                    BAESong_Preroll(g_bae.song);
                }
                playing = false; // force toggle logic
                if (!bae_play(&playing))
                {
                    BAE_PRINTF("Autoplay after playlist double-click failed for '%s'\n", pending_file);
                }
                if (playing && g_bae.mixer)
                {
                    for (int i = 0; i < 3; i++)
                    {
                        BAEMixer_Idle(g_bae.mixer);
                    }
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

        // Clear tooltips each frame
        ui_clear_tooltip(&g_lsb_tooltip_visible);
        ui_clear_tooltip(&g_msb_tooltip_visible);

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
#ifdef SUPPORT_MIDI_HW
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
                           (g_lyric_count > 0 || g_karaoke_line_current[0] || g_karaoke_line_previous[0]) &&
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
        // Channel panel
        draw_rect(R, channelPanel, panelBg);
        draw_frame(R, channelPanel, panelBorder);
        draw_text(R, 20, 20, "MIDI CHANNELS", headerCol);

        // Channel toggles in a neat grid (with measured label centering)
        // Block background interactions when a modal is active or when exporting.
        // Exporting will dim and lock most UI, but the Stop button remains active.
        bool modal_block = g_show_settings_dialog || g_show_about_dialog || (g_show_rmf_info_dialog && g_bae.is_rmf_file) || g_exporting; // block when any modal/dialog open or export in progress
        // When a modal is active we fully swallow background hover/drag/click by using off-screen, inert inputs
        int ui_mx = mx, ui_my = my;
        bool ui_mdown = mdown;
        bool ui_mclick = mclick;
        bool ui_rclick = rclick;
        if (modal_block)
        {
            ui_mx = ui_my = -10000;
            ui_mdown = ui_mclick = ui_rclick = false;
        }
        int chStartX = 20, chStartY = 40;
        // Precompute estimated per-channel levels from mixer realtime info when available.
        float realtime_channel_level[16];
        for (int _i = 0; _i < 16; ++_i)
            realtime_channel_level[_i] = 0.0f;
        bool have_realtime_levels = false;
#ifdef SUPPORT_MIDI_HW
        if (g_bae.mixer && !g_exporting && (playing || g_midi_input_enabled))
#else
        if (g_bae.mixer && !g_exporting && playing)
#endif
        {
            // Get realtime levels when playing files or when MIDI input is active
            // Prefer PCM-derived per-channel estimates when available from the engine.
            float chL[16], chR[16];
            GM_GetRealtimeChannelLevels(chL, chR);
            // Merge stereo channels into a single mono level per MIDI channel
            for (int ch = 0; ch < 16; ++ch)
            {
                float lvl = (chL[ch] + chR[ch]) * 0.5f;
                realtime_channel_level[ch] = lvl;
            }
            // Always trust the engine's realtime levels when playing or MIDI input is active
            have_realtime_levels = true;
        }

        for (int i = 0; i < 16; i++)
        {
            int col = i % 8;
            int row = i / 8;
            Rect r = {chStartX + col * 45, chStartY + row * 35, 16, 16};
            // Move checkboxes/labels/VU for channels 9-16 (row == 1) down by 1 pixel
            if (row == 1)
            {
                r.y += 1;
            }
            char buf[4];
            snprintf(buf, sizeof(buf), "%d", i + 1);
            // Handle toggle and clear VU when channel is muted
            // Disable channel toggles when playing audio files (sounds, not songs)
            bool channel_toggle_enabled = !(playing && g_bae.is_audio_file && g_bae.sound);
            bool toggled = ui_toggle(R, r, &ch_enable[i], NULL,
                                     channel_toggle_enabled ? ui_mx : -1,
                                     channel_toggle_enabled ? ui_my : -1,
                                     ui_mclick && !modal_block && channel_toggle_enabled);
            if (toggled && !ch_enable[i])
            {
                // Muted -> immediately empty visible VU
                g_channel_vu[i] = 0.0f;
                // If MIDI-in is active, proactively send NoteOff for any active notes on this channel
                // to prevent stuck notes from live input. Use the current target song (loaded or live).
#ifdef SUPPORT_MIDI_HW
                if (g_midi_input_enabled)
                {
                    BAESong target = g_bae.song ? g_bae.song : g_live_song;
                    if (target)
                    {
                        // Send per-channel panic to engine
                        gui_panic_channel_notes(target, i);
                        // Also mirror NoteOff to external MIDI out if enabled
                        if (g_midi_output_enabled)
                        {
                            for (int n = 0; n < 128; ++n)
                            {
                                if (g_keyboard_active_notes_by_channel[i][n])
                                {
                                    unsigned char msg[3] = {(unsigned char)(0x80 | (i & 0x0F)), (unsigned char)n, 0};
                                    midi_output_send(msg, 3);
                                }
                            }
                        }
                    }
                    // Clear UI bookkeeping for that channel regardless
                    for (int n = 0; n < 128; ++n)
                        g_keyboard_active_notes_by_channel[i][n] = 0;
                }
#endif
            }
            int tw = 0, th = 0;
            measure_text(buf, &tw, &th);
            // Reserve a few pixels to the right of checkbox for the VU meter so
            // the number doesn't visually collide with it. Center within checkbox width.
            int cx = r.x + (r.w - tw) / 2;
            int ty = r.y + r.h + 2; // label below box
            // If this is the second row (channels 9-16) we nudged the checkbox down;
            // apply the same 1px offset to the label so it stays aligned.
            if (r.y != chStartY + row * 35)
                ty += 0; // r.y already includes adjustment; keep explicit comment for clarity
            draw_text(R, cx, ty, buf, labelCol);

            // Draw a tiny vertical VU meter immediately to the right of the checkbox.
            // Height = checkbox height + gap (2) + number text height so it aligns with both.
            int meterW = 6; // narrow vertical meter
            int meterH = r.h + 2 + th;
            // Move 3px to the left from previous placement: use +5 instead of +8
            int meterX = r.x + r.w + 5; // slightly closer to checkbox
            int meterY = r.y;           // align top with checkbox
            Rect meterBg = {meterX, meterY, meterW, meterH};
            // Background / frame
            draw_rect(R, meterBg, g_panel_bg);
            draw_frame(R, meterBg, g_panel_border);

            // Prefer realtime estimated per-channel levels when available. Otherwise fall back to
            // the previous activity-driven heuristic (incoming MIDI or engine active notes).

            // Immediately clear VU meters when not playing, regardless of other state
            if (!playing)
            {
                g_channel_vu[i] = 0.0f;
                // Also clear peaks when stopped
                g_channel_peak_level[i] = 0.0f;
                g_channel_peak_hold_until[i] = 0;
            }
            else if (have_realtime_levels)
            {
                float lvl = realtime_channel_level[i];
                if (lvl < 0.f)
                    lvl = 0.f;
                if (lvl > 1.f)
                    lvl = 1.f;
                // Use a higher alpha for per-channel meters so they respond quickly to changes
                const float alpha = CHANNEL_VU_ALPHA; // more responsive
                g_channel_vu[i] = g_channel_vu[i] * (1.0f - alpha) + lvl * alpha;
                // update peak from realtime level
                if (lvl > g_channel_peak_level[i])
                {
                    g_channel_peak_level[i] = lvl;
                    g_channel_peak_hold_until[i] = SDL_GetTicks() + g_channel_peak_hold_ms;
                }
            }
            else
            {
                // Simple activity-based VU: set to full when any active notes on that channel
                // (from incoming MIDI UI array or engine active notes), otherwise decay.
                bool active = false;
                // Check per-channel incoming MIDI UI state
                for (int n = 0; n < 128 && !active; n++)
                {
                    if (g_keyboard_active_notes_by_channel[i][n])
                        active = true;
                }
                // Also query engine active notes when playing or when no MIDI input.
                // Prevent querying the engine when playback is stopped AND MIDI input
                // is enabled so cleared VUs are not immediately repopulated.
#ifdef SUPPORT_MIDI_HW
                if (!active && !g_exporting && (playing || !g_midi_input_enabled))
#else
                if (!active && !g_exporting && playing)
#endif
                {
                    BAESong target = g_bae.song ? g_bae.song : g_live_song;
                    if (target)
                    {
                        unsigned char ch_notes[128];
                        memset(ch_notes, 0, sizeof(ch_notes));
                        BAESong_GetActiveNotes(target, (unsigned char)i, ch_notes);
                        for (int n = 0; n < 128; n++)
                        {
                            if (ch_notes[n])
                            {
                                active = true;
                                break;
                            }
                        }
                    }
                }
                // Update channel VU with simple attack/decay
                if (active)
                {
                    g_channel_vu[i] = 1.0f;
                }
                else
                {
                    g_channel_vu[i] *= CHANNEL_ACTIVITY_DECAY;
                    if (g_channel_vu[i] < 0.005f)
                        g_channel_vu[i] = 0.0f;
                }
            }

            // Fill level from bottom using per-channel VU value (clamped)
            float lvl = g_channel_vu[i];
            if (lvl < 0.f)
                lvl = 0.f;
            if (lvl > 1.f)
                lvl = 1.f;
            int innerPad = 2;
            int innerH = meterH - (innerPad * 2);
            int fillH = (int)(lvl * innerH);
            if (fillH > 0)
            {
                // Draw a simple vertical gradient: green (low) -> yellow (mid) -> red (high)
                int gx = meterX + innerPad;
                int gw = meterW - (innerPad * 2);
                for (int yoff = 0; yoff < fillH; yoff++)
                {
                    // map to gradient from green->yellow->red based on relative height
                    float frac = (float)yoff / (float)(innerH > 0 ? innerH : 1);
                    SDL_Color col;
                    if (frac < 0.5f)
                    { // green to yellow
                        float p = frac / 0.5f;
                        col.r = (Uint8)(g_highlight_color.r * p + 20 * (1.0f - p));
                        col.g = (Uint8)(200 * (1.0f - (1.0f - p) * 0.2f));
                        col.b = (Uint8)(20);
                    }
                    else
                    { // yellow to red
                        float p = (frac - 0.5f) / 0.5f;
                        col.r = (Uint8)(200 + (55 * p));
                        col.g = (Uint8)(200 * (1.0f - p));
                        col.b = 20;
                    }
                    // Draw one horizontal scanline of the gradient from bottom upwards
                    SDL_SetRenderDrawColor(R, col.r, col.g, col.b, 255);
                    SDL_RenderLine(R, gx, meterY + meterH - innerPad - 1 - yoff, gx + gw - 1, meterY + meterH - innerPad - 1 - yoff);
                }
            }
            // Channel peak markers intentionally removed — we only draw the realtime fill.
            // Decay the realtime meter value gradually (small additional smoothing pass)
            // But skip this extra decay when MIDI input is active, since the MIDI service thread
            // manages VU levels directly and this interferes with that.
#ifdef SUPPORT_MIDI_HW
            if (!g_midi_input_enabled)
            {
#endif
                g_channel_vu[i] *= 0.92f;
                if (g_channel_vu[i] < 0.0005f)
                    g_channel_vu[i] = 0.0f;
#ifdef SUPPORT_MIDI_HW
            }
#endif
        }

        // 'All' checkbox: moved to render after the virtual keyboard so it appears on top.

        // Channel control buttons in a row
        int btnY = chStartY + 75;
        bool channel_controls_enabled = !(playing && g_bae.is_audio_file && g_bae.sound);
        if (ui_button(R, (Rect){20, btnY, 80, 26}, "Invert", channel_controls_enabled ? ui_mx : -1, channel_controls_enabled ? ui_my : -1, channel_controls_enabled ? ui_mdown : false) && ui_mclick && !modal_block && channel_controls_enabled)
        {
            for (int i = 0; i < 16; i++)
                ch_enable[i] = !ch_enable[i];
        }
        if (ui_button(R, (Rect){110, btnY, 80, 26}, "Mute All", channel_controls_enabled ? ui_mx : -1, channel_controls_enabled ? ui_my : -1, channel_controls_enabled ? ui_mdown : false) && ui_mclick && !modal_block && channel_controls_enabled)
        {
            for (int i = 0; i < 16; i++)
                ch_enable[i] = false;
        }
        if (ui_button(R, (Rect){200, btnY, 90, 26}, "Unmute All", channel_controls_enabled ? ui_mx : -1, channel_controls_enabled ? ui_my : -1, channel_controls_enabled ? ui_mdown : false) && ui_mclick && !modal_block && channel_controls_enabled)
        {
            for (int i = 0; i < 16; i++)
                ch_enable[i] = true;
        }

        if (!g_exporting) {
            // Voice count VU meter - vertical meter aligned with channel VUs
            BAEAudioInfo audioInfo;
            int voiceCount = 0;
            if (g_bae.mixer && BAEMixer_GetRealtimeStatus(g_bae.mixer, &audioInfo) == BAE_NO_ERROR)
            {
                voiceCount = audioInfo.voicesActive;
            }

            // Calculate VU position and dimensions to align with channel VUs
            int vuX = 375;      // positioned to the right of "Unmute All" button
            int vuY = chStartY; // align with top of channel grid
            int vuW = 8;        // slightly wider than channel VUs for visibility
            int vuH = 71;       // span both rows: row height + checkbox + gap + text + row spacing adjustment

            // Draw VU background/frame
            Rect vuBg = {vuX, vuY, vuW, vuH};
            draw_rect(R, vuBg, g_panel_bg);
            draw_frame(R, vuBg, g_panel_border);

            // Calculate fill level (0-MAX_VOICES voices mapped to 0-1)
            float voiceFill = (float)voiceCount / (float)MAX_VOICES;
            if (voiceFill > 1.0f)
                voiceFill = 1.0f;
            if (voiceFill < 0.0f)
                voiceFill = 0.0f;

            int innerPad = 2;
            int innerH = vuH - (innerPad * 2);
            int fillH = (int)(voiceFill * innerH);

            if (fillH > 0)
            {
                // Draw voice VU with gradient similar to channel VUs
                int gx = vuX + innerPad;
                int gw = vuW - (innerPad * 2);
                for (int yoff = 0; yoff < fillH; yoff++)
                {
                    float frac = (float)yoff / (float)(innerH > 0 ? innerH : 1);
                    SDL_Color col;
                    if (frac < 0.5f)
                    { // green to yellow
                        float p = frac / 0.5f;
                        col.r = (Uint8)(g_highlight_color.r * p + 20 * (1.0f - p));
                        col.g = (Uint8)(200 * (1.0f - (1.0f - p) * 0.2f));
                        col.b = (Uint8)(20);
                    }
                    else
                    { // yellow to red
                        float p = (frac - 0.5f) / 0.5f;
                        col.r = (Uint8)(200 + (55 * p));
                        col.g = (Uint8)(200 * (1.0f - p));
                        col.b = 20;
                    }
                    // Draw from bottom upwards
                    SDL_SetRenderDrawColor(R, col.r, col.g, col.b, 255);
                    SDL_RenderLine(R, gx, vuY + vuH - innerPad - 1 - yoff, gx + gw - 1, vuY + vuH - innerPad - 1 - yoff);
                }
            }

            // Handle tooltip for Voice VU meter
            if (point_in(ui_mx, ui_my, vuBg) && !modal_block)
            {
                char tooltip_text[64];
                snprintf(tooltip_text, sizeof(tooltip_text), "Active Voices: %d", voiceCount);

                // Measure actual text width and height
                int text_w, text_h;
                measure_text(tooltip_text, &text_w, &text_h);

                int tooltip_w = text_w + 8; // 4px padding on each side
                int tooltip_h = text_h + 8; // 4px padding top and bottom
                if (tooltip_w > 500)
                    tooltip_w = 500; // Maximum width constraint

                int tooltip_x = ui_mx + 10;
                int tooltip_y = ui_my - 30;

                // Keep tooltip on screen
                if (tooltip_x + tooltip_w > WINDOW_W - 4)
                    tooltip_x = WINDOW_W - tooltip_w - 4;
                if (tooltip_y < 4)
                    tooltip_y = ui_my + 25; // Show below cursor if no room above

                ui_set_tooltip((Rect){tooltip_x, tooltip_y, tooltip_w, tooltip_h}, tooltip_text, &g_voice_tooltip_visible, &g_voice_tooltip_rect, g_voice_tooltip_text, sizeof(g_voice_tooltip_text));
            }
            else
            {
                ui_clear_tooltip(&g_voice_tooltip_visible);
            }
        }
        
        // If playing an audio file (sound, not song), dim the MIDI channels panel
        if (playing && g_bae.is_audio_file && g_bae.sound)
        {
            SDL_Color dim = g_is_dark_mode ? (SDL_Color){0, 0, 0, 160} : (SDL_Color){255, 255, 255, 160};
            draw_rect(R, channelPanel, dim);
        }

        // Control panel
        draw_rect(R, controlPanel, panelBg);
        draw_frame(R, controlPanel, panelBorder);
        draw_text(R, 410, 20, "PLAYBACK CONTROLS", headerCol);

#ifdef SUPPORT_MIDI_HW
        // When external MIDI input is active or playing audio files, dim and disable most playback controls
        bool playback_controls_enabled = !g_midi_input_enabled && !(playing && g_bae.is_audio_file && g_bae.sound);
#else
        bool playback_controls_enabled = !(playing && g_bae.is_audio_file && g_bae.sound);
#endif

        // Transpose control
        draw_text(R, 410, 45, "Transpose:", labelCol);
        ui_slider(R, (Rect){410, 63, 160, 14}, &transpose, -24, 24, playback_controls_enabled ? ui_mx : -1, playback_controls_enabled ? ui_my : -1, playback_controls_enabled ? ui_mdown : false, playback_controls_enabled ? ui_mclick : false);
        char tbuf[64];
        snprintf(tbuf, sizeof(tbuf), "%+d", transpose);
        draw_text(R, 577, 61, tbuf, labelCol);
        if (playback_controls_enabled && ui_button(R, (Rect){620, 59, 50, 20}, "Reset", ui_mx, ui_my, ui_mdown) && ui_mclick && !modal_block)
        {
            transpose = 0;
            bae_set_transpose(transpose);
        }

        // Tempo control
        draw_text(R, 410, 85, "Tempo:", labelCol);
        ui_slider(R, (Rect){410, 103, 160, 14}, &tempo, 25, 200, playback_controls_enabled ? ui_mx : -1, playback_controls_enabled ? ui_my : -1, playback_controls_enabled ? ui_mdown : false, playback_controls_enabled ? ui_mclick : false);
        snprintf(tbuf, sizeof(tbuf), "%d%%", tempo);
        draw_text(R, 577, 101, tbuf, labelCol);
        if (playback_controls_enabled && ui_button(R, (Rect){620, 99, 50, 20}, "Reset", ui_mx, ui_my, ui_mdown) && ui_mclick && !modal_block)
        {
            int oldTempo = tempo;
            int newTempo = 100;
            if (newTempo != oldTempo)
            {
                tempo = newTempo;
                bae_set_tempo(tempo);
                // Get original song duration from BAE and calculate tempo-adjusted duration
                // Higher tempo = shorter duration, lower tempo = longer duration
                // Formula: adjusted_duration = original_duration * (100 / tempo_percent)
                if (g_bae.song)
                {
                    uint32_t original_length_us = 0;
                    BAESong_GetMicrosecondLength(g_bae.song, &original_length_us);
                    int original_duration_ms = (int)(original_length_us / 1000UL);
                    int old_duration = duration;
                    duration = (int)((double)original_duration_ms * (100.0 / (double)tempo));
                    g_bae.song_length_us = duration * 1000UL;
                    // Calculate progress bar position based on percentage through song
                    // without seeking BAE - preserve relative position
                    if (old_duration > 0)
                    {
                        float percent_through = (float)progress / (float)old_duration;
                        progress = (int)(percent_through * duration);
                    }
                }
                if (g_bae.preserve_position_on_next_start && g_bae.preserved_start_position_us)
                {
                    uint32_t us = g_bae.preserved_start_position_us;
                    uint32_t newus = (uint32_t)((double)us * (100.0 / (double)tempo));
                    g_bae.preserved_start_position_us = newus;
                }
            }
        }

        // Reverb controls (we always leave Reverb interactive even when MIDI input is enabled)
        draw_text(R, 687, 25, "Reverb:", labelCol);
        // Removed non-functional 'No Change' option; first entry now 'Default' (engine type 0)
        // Removed engine reverb index 0 (NO_CHANGE). UI list now maps i -> engine index (i+1)
        int reverbCount = get_reverb_count();
        Rect ddRect = {687, 43, 160, 24}; // moved left 3px and down 3px
        // Closed dropdown: use theme globals
        SDL_Color dd_bg = g_button_base;
        SDL_Color dd_txt = g_button_text;
        SDL_Color dd_frame = g_button_border;
        bool overMain = point_in(ui_mx, ui_my, ddRect);
        // Disable reverb dropdown when playing audio files (sounds, not songs)
        bool reverb_enabled = !(playing && g_bae.is_audio_file && g_bae.sound);
        if (overMain && reverb_enabled)
            dd_bg = g_button_hover;
        if (!reverb_enabled)
        {
            dd_bg.a = 180;
            dd_txt.a = 180;
        }
        draw_rect(R, ddRect, dd_bg);
        draw_frame(R, ddRect, dd_frame);
        const char *cur = (reverbType >= 1 && reverbType <= reverbCount) ? get_reverb_name(reverbType - 1) : "?";
        draw_text(R, ddRect.x + 6, ddRect.y + 3, cur, dd_txt);
        draw_text(R, ddRect.x + ddRect.w - 16, ddRect.y + 3, g_reverbDropdownOpen ? "^" : "v", dd_txt);
        if (overMain && ui_mclick && reverb_enabled)
        {
            g_reverbDropdownOpen = !g_reverbDropdownOpen;
        }

        // Volume control (aligned with Tempo)
        draw_text(R, 687, 85, "Volume:", labelCol);
        // Allow volume slider interaction when reverb dropdown is closed. We want
        // users to adjust master volume even while external MIDI input is active.
        // Disable volume interaction when a modal/dialog is open
        bool volume_enabled = !g_reverbDropdownOpen && !modal_block;
        ui_slider(R, (Rect){687, 103, ddRect.w, 14}, &volume, 0, NEW_MAX_VOLUME_PCT,
                  volume_enabled ? ui_mx : -1, volume_enabled ? ui_my : -1,
                  volume_enabled ? ui_mdown : false, volume_enabled ? ui_mclick : false);
        char vbuf[32];
        snprintf(vbuf, sizeof(vbuf), "%d%%", volume);
        int vtxt_x = ddRect.x + ddRect.w + 3;
        int vtxt_y = 101;
        draw_text(R, vtxt_x, vtxt_y, vbuf, labelCol);
        /* Volume value is now non-interactive; clicking the percent label no longer resets to 100% */

#ifdef SUPPORT_MIDI_HW
        // If MIDI input is enabled, paint a semi-transparent overlay over the control panel to dim it
        if (g_midi_input_enabled)
        {
            SDL_Color dim = g_is_dark_mode ? (SDL_Color){0, 0, 0, 160} : (SDL_Color){255, 255, 255, 160};
            draw_rect(R, controlPanel, dim);
            // Redraw Reverb controls on top so they remain active/visible
            draw_rect(R, ddRect, dd_bg);
            draw_frame(R, ddRect, dd_frame);
            draw_text(R, ddRect.x + 6, ddRect.y + 3, cur, dd_txt);
            draw_text(R, ddRect.x + ddRect.w - 16, ddRect.y + 3, g_reverbDropdownOpen ? "^" : "v", dd_txt);
            // Also redraw Volume controls on top so they remain active/visible
            draw_text(R, 687, 85, "Volume:", labelCol);
            ui_slider(R, (Rect){687, 103, ddRect.w, 14}, &volume, 0, NEW_MAX_VOLUME_PCT,
                      volume_enabled ? ui_mx : -1, volume_enabled ? ui_my : -1,
                      volume_enabled ? ui_mdown : false, volume_enabled ? ui_mclick : false);
            draw_text(R, vtxt_x, vtxt_y, vbuf, labelCol);
            // Draw the external MIDI notice in the bottom-right of the control panel
            const char *notice = "External MIDI Input Enabled";
            int n_w = 0, n_h = 0;
            measure_text(notice, &n_w, &n_h);
            int n_x = controlPanel.x + controlPanel.w - n_w - 8;
            int n_y = controlPanel.y + controlPanel.h - n_h - 6;
            draw_text(R, n_x, n_y, notice, g_highlight_color);
            // Ensure the "Reverb:" label itself is also drawn above the dim layer
            draw_text(R, 687, 25, "Reverb:", labelCol);
        }
#endif
        // If playing an audio file (sound, not song), dim the control panel except volume-related controls
        if (playing && g_bae.is_audio_file && g_bae.sound)
        {
            SDL_Color dim = g_is_dark_mode ? (SDL_Color){0, 0, 0, 160} : (SDL_Color){255, 255, 255, 160};
            draw_rect(R, controlPanel, dim);
            // Only redraw Volume controls on top so they remain active/visible
            // (Reverb is disabled when playing audio files)
            // Respect modal_block here so volume cannot be adjusted while a dialog is open
            bool audio_volume_enabled = !g_reverbDropdownOpen && !modal_block; // Volume should work when playing audio files
            draw_text(R, 687, 85, "Volume:", labelCol);
            ui_slider(R, (Rect){687, 103, ddRect.w, 14}, &volume, 0, NEW_MAX_VOLUME_PCT,
                      audio_volume_enabled ? ui_mx : -1, audio_volume_enabled ? ui_my : -1,
                      audio_volume_enabled ? ui_mdown : false, audio_volume_enabled ? ui_mclick : false);
            draw_text(R, vtxt_x, vtxt_y, vbuf, labelCol);
            // Draw a notice in the bottom-right of the control panel
            const char *notice = "Audio File Playing";
            int n_w = 0, n_h = 0;
            measure_text(notice, &n_w, &n_h);
            int n_x = controlPanel.x + controlPanel.w - n_w - 8;
            int n_y = controlPanel.y + controlPanel.h - n_h - 6;
            draw_text(R, n_x, n_y, notice, g_highlight_color);
        }
        // Transport panel
        draw_rect(R, transportPanel, panelBg);
        draw_frame(R, transportPanel, panelBorder);
        draw_text(R, 20, 170, "TRANSPORT & PROGRESS", headerCol);

        // Progress bar with better styling
        Rect bar = {20, 190, 650, 20};
#ifdef _WIN32
        draw_rect(R, bar, g_theme.is_dark_mode ? (SDL_Color){25, 25, 30, 255} : (SDL_Color){240, 240, 240, 255});
#else
        draw_rect(R, bar, (SDL_Color){25, 25, 30, 255});
#endif
        draw_frame(R, bar, panelBorder);
        if (duration != bae_get_len_ms())
            duration = bae_get_len_ms();
        progress = playing ? bae_get_pos_ms() : progress;
        float pct = (duration > 0) ? (float)progress / duration : 0.f;
        if (pct < 0)
            pct = 0;
        if (pct > 1)
            pct = 1;
        if (pct > 0)
        {
            // Animated striped progress fill using accent color
            int fillW = (int)((bar.w - 4) * pct);
            Rect fillRect = {bar.x + 2, bar.y + 2, fillW, bar.h - 4};
            // Background for fill area (darker strip base)
            SDL_Color accent_dark = g_accent_color;
            int tr = (int)accent_dark.r - 36;
            if (tr < 0)
                tr = 0;
            accent_dark.r = (Uint8)tr;
            int tg = (int)accent_dark.g - 36;
            if (tg < 0)
                tg = 0;
            accent_dark.g = (Uint8)tg;
            int tb = (int)accent_dark.b - 36;
            if (tb < 0)
                tb = 0;
            accent_dark.b = (Uint8)tb;
            if (g_disable_webtv_progress_bar)
            {
                // Simple solid accent fill when WebTV style is disabled
                draw_rect(R, fillRect, g_accent_color);
            }
            else
            {
                draw_rect(R, fillRect, accent_dark);

                // Clip drawing to the fill area so stripes don't bleed outside
                SDL_Rect clip = {fillRect.x, fillRect.y, fillRect.w, fillRect.h};
                SDL_SetRenderClipRect(R, &clip);

                // Draw diagonal stripes (leaning down-right) with equal dark/light band sizes.
                SDL_SetRenderDrawBlendMode(R, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(R, g_accent_color.r, g_accent_color.g, g_accent_color.b, 220);
                int bandW = (g_progress_stripe_width / 2) + 4; // light band width (widened)
                if (bandW < 6)
                    bandW = 6;
                int stripeStep = bandW * 2; // dark+light equal
                int thickness = 18;         // make the light band visibly wider
                int off = g_progress_stripe_offset % stripeStep;
                // Draw slanted bands by drawing multiple parallel lines per band
                // Draw slanted bands leaning up-left (reverse of previous)
                for (int sx = -fillRect.h - bandW - off; sx < fillRect.w + fillRect.h; sx += stripeStep)
                {
                    int x0 = fillRect.x + sx;
                    int x1 = x0 + bandW;
                    for (int t = 0; t < thickness; ++t)
                    {
                        // Draw from bottom to top so the slant opposes previous direction
                        SDL_RenderLine(R, x0 + t, fillRect.y + fillRect.h, x1 + t, fillRect.y);
                    }
                }

                // Restore clip
                SDL_SetRenderClipRect(R, NULL);
                // Advance stripe animation only every other frame to slow it down
                static int g_progress_frame_counter = 0;
                g_progress_frame_counter++;
                // Advance more slowly: 1 unit every N frames (keeps frame rate unchanged)
                const int advanceInterval = 3; // increase to slow the perceived scroll speed
                if ((g_progress_frame_counter % advanceInterval) == 0)
                {
                    // Reverse scrolling direction by subtracting a single unit
                    g_progress_stripe_offset = (g_progress_stripe_offset - 1) % (stripeStep);
                    if (g_progress_stripe_offset < 0)
                        g_progress_stripe_offset += stripeStep;
                }
            }
        }
        if (ui_mdown && point_in(ui_mx, ui_my, bar))
        {
            int rel = ui_mx - bar.x;
            if (rel < 0)
                rel = 0;
            if (rel > bar.w)
                rel = bar.w;
            int new_progress = (int)((double)rel / bar.w * duration);
            if (new_progress != last_drag_progress)
            {
                progress = new_progress;
                last_drag_progress = new_progress;
                bae_seek_ms(progress);
                // User-initiated seek -> set total-play timer to the new position
                g_total_play_ms = progress;
                g_last_engine_pos_ms = progress;
                // When the user seeks, ensure any sounding notes on the virtual
                // keyboard are silenced and UI-held state cleared so keys don't
                // remain lit for the wrong position.
                if (g_show_virtual_keyboard)
                {
                    BAESong target = g_bae.song ? g_bae.song : g_live_song;
                    if (target)
                    {
                        for (int n = 0; n < 128; n++)
                        {
                            BAESong_NoteOff(target, (unsigned char)g_keyboard_channel, (unsigned char)n, 0, 0);
                        }
                    }
                    g_keyboard_mouse_note = -1;
                    memset(g_keyboard_active_notes, 0, sizeof(g_keyboard_active_notes));
                    g_keyboard_suppress_until = SDL_GetTicks() + 250;
                }
            }
        }
        else
        {
            // Reset when not dragging
            last_drag_progress = -1;
        }

        // Time display (add milliseconds to current position)
        int prog_ms = progress % 1000;
        int prog_sec = (progress / 1000) % 60;
        int prog_min = (progress / 1000) / 60;
        char pbuf[64];
        snprintf(pbuf, sizeof(pbuf), "%02d:%02d.%03d", prog_min, prog_sec, prog_ms);
        char dbuf[64];
        snprintf(dbuf, sizeof(dbuf), "%02d:%02d", (duration / 1000) / 60, (duration / 1000) % 60);

        int pbuf_w = 0, pbuf_h = 0;
        measure_text(pbuf, &pbuf_w, &pbuf_h);
        int dbuf_w = 0, dbuf_h = 0;
        measure_text(dbuf, &dbuf_w, &dbuf_h);
        int time_y = 191; // moved up 3px from 194
        int pbuf_x = 680;
        // Clickable region just around current time text
        Rect progressRect = {pbuf_x, time_y, pbuf_w, pbuf_h > 0 ? pbuf_h : 16};
        // Transport controls (Play/seek) are disabled when external MIDI input is active.
#ifdef SUPPORT_MIDI_HW
        bool transport_enabled = !g_midi_input_enabled;
#else
        bool transport_enabled = true;
#endif
        bool progressInteract = !g_reverbDropdownOpen && transport_enabled;
        bool progressHover = progressInteract && point_in(ui_mx, ui_my, progressRect);
        if (progressInteract && progressHover && ui_mclick)
        {
            progress = 0;
            bae_seek_ms(0);
            g_total_play_ms = progress;
            g_last_engine_pos_ms = progress;
            // Clear any sounding virtual keyboard notes on user-initiated seek
            if (g_show_virtual_keyboard)
            {
                BAESong target = g_bae.song ? g_bae.song : g_live_song;
                if (target)
                {
                    for (int n = 0; n < 128; n++)
                    {
                        BAESong_NoteOff(target, (unsigned char)g_keyboard_channel, (unsigned char)n, 0, 0);
                    }
                }
                g_keyboard_mouse_note = -1;
                memset(g_keyboard_active_notes, 0, sizeof(g_keyboard_active_notes));
                g_keyboard_suppress_until = SDL_GetTicks() + 250;
            }
        }
        SDL_Color progressColor = progressHover ? g_highlight_color : labelCol;
        draw_text(R, pbuf_x, time_y, pbuf, progressColor);
        int slash_x = pbuf_x + pbuf_w + 6; // gap
        draw_text(R, slash_x, time_y, "/", labelCol);
        draw_text(R, slash_x + 10, time_y, dbuf, labelCol);

        // Update session total-played time using engine position deltas so it
        // doesn't reset when the song loops. We only update while actively
        // playing a MIDI/RMF song (not raw audio files) because audio files
        // use frame-based positions and their seeking behavior differs.
        if (playing && g_bae.song_loaded && !g_bae.is_audio_file)
        {
            int curPos = bae_get_pos_ms();
            if (g_last_engine_pos_ms == 0)
            {
                // Initialize to current engine pos when we first start playing
                g_last_engine_pos_ms = curPos;
            }
            int delta = curPos - g_last_engine_pos_ms;
            if (delta < 0)
            {
                // Negative delta indicates a loop or seek backwards; treat as
                // continuation and do not subtract — assume a loop advanced total
                // by (curPos) since engine wrapped to start. In that case add curPos.
                delta = curPos;
            }
            // Only account reasonably-sized deltas to avoid spikes from seeks
            if (delta >= 0 && delta < 5 * 60 * 1000)
            { // ignore >5 minutes jumps
                g_total_play_ms += delta;
            }
            g_last_engine_pos_ms = curPos;
        }
        else if (!playing)
        {
            // When not playing, keep last engine pos synced so resume deltas
            // are computed correctly and don't double-count paused intervals.
            g_last_engine_pos_ms = bae_get_pos_ms();
        }

        // Draw total-played session timer below the progress time
        int total_ms = g_total_play_ms;
        int t_ms = total_ms % 1000;
        int t_sec = (total_ms / 1000) % 60;
        int t_min = (total_ms / 1000) / 60;
        char total_time_buf[64];
        snprintf(total_time_buf, sizeof(total_time_buf), "%02d:%02d.%03d", t_min, t_sec, t_ms);
        int total_w = 0, total_h = 0;
        measure_text(total_time_buf, &total_w, &total_h);
        draw_text(R, pbuf_x, time_y + 18, total_time_buf, labelCol);

        // Transport buttons
        if (!transport_enabled)
        {
            // Draw disabled Play button (no interaction)
            Rect playRect = {20, 215, 60, 22};
            SDL_Color disabledBg = g_panel_bg;
            SDL_Color disabledTxt = g_panel_border;
            draw_rect(R, playRect, disabledBg);
            draw_frame(R, playRect, g_panel_border);
            draw_text(R, playRect.x + 6, playRect.y + 4, playing ? "Pause" : "Play", disabledTxt);
        }
        else
        {
            if (ui_button(R, (Rect){20, 215, 60, 22}, playing ? "Pause" : "Play", ui_mx, ui_my, ui_mdown) && ui_mclick && !modal_block)
            {
                if (bae_play(&playing))
                {
                    // If the play call resulted in a pause (playing==false), clear visible notes on the virtual keyboard
                    if (!playing)
                    {
                        if (g_keyboard_mouse_note != -1)
                        {
                            BAESong target = g_bae.song ? g_bae.song : g_live_song;
                            if (target)
                                BAESong_NoteOff(target, (unsigned char)g_keyboard_channel, (unsigned char)g_keyboard_mouse_note, 0, 0);
                            g_keyboard_mouse_note = -1;
                        }
                        memset(g_keyboard_active_notes_by_channel, 0, sizeof(g_keyboard_active_notes_by_channel));
                        memset(g_keyboard_active_notes, 0, sizeof(g_keyboard_active_notes));
                    }
                }
            }
        }
        // Stop remains active even when transport is dimmed for MIDI input mode
        if (ui_button(R, (Rect){90, 215, 60, 22}, "Stop", ui_mx, ui_my, ui_mdown) && ui_mclick && !modal_block)
        {
            bae_stop(&playing, &progress);
#ifdef SUPPORT_MIDI_HW
            // Ensure engine releases any held notes when user stops playback (panic)
            midi_output_send_all_notes_off(); // silence any external device too
#endif
            if (g_bae.song)
            {
                gui_panic_all_notes(g_bae.song);
            }
            if (g_live_song)
            {
                gui_panic_all_notes(g_live_song);
            }
            // Also ensure the virtual keyboard UI and per-channel note state are cleared
            // immediately after the engine AllNotesOff so the UI can't show lingering notes.
            if (g_show_virtual_keyboard)
            {
                BAESong target = g_bae.song ? g_bae.song : g_live_song;
                if (target)
                {
                    // Send NoteOff for every channel/note to be extra-safe
                    for (int ch = 0; ch < 16; ++ch)
                    {
                        for (int n = 0; n < 128; ++n)
                        {
                            BAESong_NoteOff(target, (unsigned char)ch, (unsigned char)n, 0, 0);
                        }
                    }
                }
                // Clear UI-held state so keys render as released immediately
                g_keyboard_mouse_note = -1;
                memset(g_keyboard_active_notes_by_channel, 0, sizeof(g_keyboard_active_notes_by_channel));
                memset(g_keyboard_active_notes, 0, sizeof(g_keyboard_active_notes));
                g_keyboard_suppress_until = SDL_GetTicks() + 250;
            }
            // Reset total-play timer on user Stop
            g_total_play_ms = 0;
            g_last_engine_pos_ms = 0;
            // Clear visible virtual keyboard notes on Stop (use live song fallback)
            if (g_show_virtual_keyboard)
            {
                BAESong target = g_bae.song ? g_bae.song : g_live_song;
                if (target)
                {
                    for (int n = 0; n < 128; n++)
                    {
                        BAESong_NoteOff(target, (unsigned char)g_keyboard_channel, (unsigned char)n, 0, 0);
                    }
                }
                g_keyboard_mouse_note = -1;
                memset(g_keyboard_active_notes, 0, sizeof(g_keyboard_active_notes));
                g_keyboard_suppress_until = SDL_GetTicks() + 250;
            }
            // Also stop export if active
            if (g_exporting)
            {
                bae_stop_wav_export();
            }
        }

        // Virtual MIDI Keyboard Panel (shown for songs) or waveform for audio files
        if (showKeyboard || (g_bae.is_audio_file && g_bae.sound))
        {
            draw_rect(R, keyboardPanel, panelBg);
            draw_frame(R, keyboardPanel, panelBorder);
            if (g_bae.is_audio_file && g_bae.sound)
            {
                draw_text(R, keyboardPanel.x + 10, keyboardPanel.y + 8, "WAVEFORM", headerCol);
                // Waveform area
                int wfX = keyboardPanel.x + 10;
                int wfY = keyboardPanel.y + 32;
                int wfW = keyboardPanel.w - 20;
                int wfH = keyboardPanel.h - 46;
                Rect wfRect = {wfX, wfY, wfW, wfH};
                draw_rect(R, wfRect, g_panel_bg);
                draw_frame(R, wfRect, g_panel_border);
                // Inset inner drawing rect by 1px on each side so waveform lines don't overwrite the border
                int inWfX = wfX + 1;
                int inWfY = wfY + 1;
                int inWfW = wfW - 2;
                int inWfH = wfH - 2;
                if (inWfW < 1)
                    inWfW = 1;
                if (inWfH < 1)
                    inWfH = 1;

                // Try to get raw sample pointer and length (in frames)
                uint32_t frames = 0;
                void *raw = BAESound_GetSamplePlaybackPointer(g_bae.sound, &frames);
                if (raw && frames > 0)
                {
                    // Determine bit depth/channels via BAESound_GetInfo
                    BAESampleInfo info;
                    if (BAESound_GetInfo(g_bae.sound, &info) == BAE_NO_ERROR)
                    {
                        int channels = info.channels;
                        int bitDepth = info.bitSize;
                        // We'll treat samples as 16-bit signed if bitDepth>=16, else 8-bit unsigned
                        if (bitDepth >= 16)
                        {
                            int16_t *s16 = (int16_t *)raw; // interleaved if stereo
                            // Compute samples per pixel (use frames -> pixels)
                            uint32_t frames_per_px = frames / (uint32_t)inWfW;
                            if (frames_per_px < 1)
                                frames_per_px = 1;
                            for (int x = 0; x < inWfW; x++)
                            {
                                uint32_t start = (uint32_t)x * frames_per_px;
                                uint32_t end = start + frames_per_px;
                                if (end > frames)
                                    end = frames;
                                int16_t minv = 0, maxv = 0;
                                bool inited = false;
                                for (uint32_t f = start; f < end; f++)
                                {
                                    int idx = (int)f * channels; // take first channel for visualization
                                    int16_t v = s16[idx];
                                    if (!inited)
                                    {
                                        minv = maxv = v;
                                        inited = true;
                                    }
                                    if (v < minv)
                                        minv = v;
                                    if (v > maxv)
                                        maxv = v;
                                }
                                // Map min/max to pixel Y
                                float minf = (float)minv / 32768.0f;
                                float maxf = (float)maxv / 32768.0f;
                                int y0 = inWfY + (int)((1.0f - (maxf * 0.5f + 0.5f)) * (inWfH - 2));
                                int y1 = inWfY + (int)((1.0f - (minf * 0.5f + 0.5f)) * (inWfH - 2));
                                if (y0 < wfY)
                                    y0 = inWfY;
                                if (y1 > inWfY + inWfH - 1)
                                    y1 = inWfY + inWfH - 1;
                                SDL_SetRenderDrawColor(R, g_accent_color.r, g_accent_color.g, g_accent_color.b, 255);
                                SDL_RenderLine(R, inWfX + x, y0, inWfX + x, y1);
                            }
                        }
                        else
                        {
                            // 8-bit samples (unsigned)
                            uint8_t *s8 = (uint8_t *)raw;
                            uint32_t frames_per_px = frames / (uint32_t)inWfW;
                            if (frames_per_px < 1)
                                frames_per_px = 1;
                            for (int x = 0; x < inWfW; x++)
                            {
                                uint32_t start = (uint32_t)x * frames_per_px;
                                uint32_t end = start + frames_per_px;
                                if (end > frames)
                                    end = frames;
                                uint8_t minv = 128, maxv = 128;
                                for (uint32_t f = start; f < end; f++)
                                {
                                    uint8_t v = s8[f * info.channels];
                                    if (v < minv)
                                        minv = v;
                                    if (v > maxv)
                                        maxv = v;
                                }
                                float minf = ((float)minv - 128.0f) / 128.0f;
                                float maxf = ((float)maxv - 128.0f) / 128.0f;
                                int y0 = inWfY + (int)((1.0f - (maxf * 0.5f + 0.5f)) * (inWfH - 2));
                                int y1 = inWfY + (int)((1.0f - (minf * 0.5f + 0.5f)) * (inWfH - 2));
                                if (y0 < inWfY)
                                    y0 = inWfY;
                                if (y1 > inWfY + inWfH - 1)
                                    y1 = inWfY + inWfH - 1;
                                SDL_SetRenderDrawColor(R, g_accent_color.r, g_accent_color.g, g_accent_color.b, 255);
                                SDL_RenderLine(R, inWfX + x, y0, inWfX + x, y1);
                            }
                        }
                    }
                }

                // Waveform click-to-seek handling: compute target ms for hover/drag preview
                // and only perform the actual engine seek on mouse-up. This avoids
                // repeated seeks while the user holds the mouse in place.
                int wf_relx = -1;
                // Use inner waveform rect for hit-testing so clicks near the border
                // don't map onto waveform pixels that would overdraw the frame.
                if (ui_mx >= inWfX && ui_mx < inWfX + inWfW && ui_my >= inWfY && ui_my < inWfY + inWfH)
                {
                    wf_relx = ui_mx - inWfX;
                    if (wf_relx < 0)
                        wf_relx = 0;
                    if (wf_relx > inWfW - 1)
                        wf_relx = inWfW - 1;
                }

                // Helper to map pixel->ms
                int computed_ms = -1;
                if (wf_relx >= 0)
                {
                    BAESampleInfo info2;
                    if (BAESound_GetInfo(g_bae.sound, &info2) == BAE_NO_ERROR)
                    {
                        double sampleRate = (double)(info2.sampledRate >> 16) + (double)(info2.sampledRate & 0xFFFF) / 65536.0;
                        if (sampleRate > 0 && audio_total_frames > 0)
                        {
                            double frac = (double)wf_relx / (double)(inWfW);
                            if (frac < 0.0)
                                frac = 0.0;
                            if (frac > 1.0)
                                frac = 1.0;
                            uint32_t frame_position = (uint32_t)((double)audio_total_frames * frac);
                            if (frame_position >= audio_total_frames)
                                frame_position = audio_total_frames - 1;
                            computed_ms = (int)((double)frame_position * 1000.0 / sampleRate);
                        }
                    }
                }

                // Dragging: update preview but don't call engine seek until mouse-up
                static int waveform_preview_ms = -1;
                const int WAVEFORM_SEEK_DEADZONE_MS = 40; // avoid tiny-seek jitter while holding
                if (ui_mdown && computed_ms >= 0)
                {
                    // Only update preview/progress if it changed beyond deadzone
                    if (waveform_preview_ms < 0 || abs(computed_ms - waveform_preview_ms) > WAVEFORM_SEEK_DEADZONE_MS)
                    {
                        waveform_preview_ms = computed_ms;
                        progress = waveform_preview_ms; // show preview position in UI
                    }
                }
                else if (!mdown)
                {
                    // If mouse released over waveform, perform actual seek on mouse-up
                    if (ui_mclick && computed_ms >= 0)
                    {
                        int target_ms = computed_ms;
                        // prefer preview if it exists
                        if (waveform_preview_ms >= 0)
                            target_ms = waveform_preview_ms;
                        bae_seek_ms(target_ms);
                        progress = target_ms;
                        // User-initiated seek -> set total-play timer to the new position
                        g_total_play_ms = target_ms;
                        g_last_engine_pos_ms = target_ms;
                        // Clear any sounding virtual keyboard notes on user-initiated seek
                        if (g_show_virtual_keyboard)
                        {
                            BAESong target = g_bae.song ? g_bae.song : g_live_song;
                            if (target)
                            {
                                for (int n = 0; n < 128; n++)
                                {
                                    BAESong_NoteOff(target, (unsigned char)g_keyboard_channel, (unsigned char)n, 0, 0);
                                }
                            }
                            g_keyboard_mouse_note = -1;
                            memset(g_keyboard_active_notes, 0, sizeof(g_keyboard_active_notes));
                            g_keyboard_suppress_until = SDL_GetTicks() + 250;
                        }
                    }
                    // Reset preview state when not dragging
                    waveform_preview_ms = -1;
                }

                // Draw a playhead indicator for the preview or current position
                int playhead_ms = (waveform_preview_ms >= 0) ? waveform_preview_ms : progress;
                if (playhead_ms >= 0 && audio_total_frames > 0)
                {
                    BAESampleInfo info3;
                    if (BAESound_GetInfo(g_bae.sound, &info3) == BAE_NO_ERROR)
                    {
                        double sampleRate = (double)(info3.sampledRate >> 16) + (double)(info3.sampledRate & 0xFFFF) / 65536.0;
                        if (sampleRate > 0)
                        {
                            uint32_t frame_pos = (uint32_t)((double)playhead_ms * sampleRate / 1000.0);
                            if (frame_pos >= audio_total_frames)
                                frame_pos = audio_total_frames - 1;
                            double frac = (double)frame_pos / (double)audio_total_frames;
                            int phx = inWfX + (int)(frac * inWfW);
                            SDL_SetRenderDrawColor(R, g_highlight_color.r, g_highlight_color.g, g_highlight_color.b, 220);
                            SDL_RenderLine(R, phx, inWfY, phx, inWfY + inWfH - 1);
                        }
                    }
                }
                // If we're showing waveform only, skip rest of keyboard rendering
                if (g_bae.is_audio_file && g_bae.sound)
                {
                    goto SKIP_KEYBOARD_RENDER;
                }
            }
            else
            {
                draw_text(R, keyboardPanel.x + 10, keyboardPanel.y + 8, "VIRTUAL MIDI KEYBOARD", headerCol);
            }
            // When waveform is shown we don't render the virtual keyboard controls
            if (!(g_bae.is_audio_file && g_bae.sound))
            {
                // Channel dropdown
                const char *chanItems[16];
                char chanBuf[16][8];
                for (int i = 0; i < 16; i++)
                {
                    snprintf(chanBuf[i], sizeof(chanBuf[i]), "Ch %d", i + 1);
                    chanItems[i] = chanBuf[i];
                }
                Rect chanDD = {keyboardPanel.x + 10, keyboardPanel.y + 28, 90, 22};
                // Render main dropdown box
                SDL_Color ddBg = g_button_base;
                SDL_Color ddTxt = g_button_text;
                SDL_Color ddFrame = g_button_border;
                bool overDD = point_in(ui_mx, ui_my, chanDD);
                if (overDD)
                    ddBg = g_button_hover;
                draw_rect(R, chanDD, ddBg);
                draw_frame(R, chanDD, ddFrame);
                draw_text(R, chanDD.x + 6, chanDD.y + 2, chanItems[g_keyboard_channel], ddTxt);
                draw_text(R, chanDD.x + chanDD.w - 16, chanDD.y + 1, g_keyboard_channel_dd_open ? "^" : "v", ddTxt);
                if (!modal_block && ui_mclick && overDD)
                {
                    g_keyboard_channel_dd_open = !g_keyboard_channel_dd_open;
                }
                // (Dropdown list itself drawn later for proper z-order)
                if (!g_keyboard_channel_dd_open && ui_mclick && !overDD)
                { /* no-op */
                }

                // LSB/MSB number pickers - compact layout below channel dropdown
                int picker_y = keyboardPanel.y + 56; // below channel dropdown
                int picker_w = 35;                   // compact width for 3-digit numbers
                int picker_h = 18;
                int spacing = 5;

                // MSB picker (now first)
                Rect msbRect = {keyboardPanel.x + 10, picker_y, picker_w, picker_h};
                bool msbHover = !g_keyboard_channel_dd_open && point_in(ui_mx, ui_my, msbRect);
                SDL_Color msbBg = msbHover ? g_button_hover : g_button_base;
                if (g_keyboard_channel_dd_open)
                    msbBg.a = 180; // Make it appear disabled when channel dropdown is open
                draw_rect(R, msbRect, msbBg);
                draw_frame(R, msbRect, g_button_border);

                char msbText[8];
                snprintf(msbText, sizeof(msbText), "%d", g_keyboard_msb);
                int msb_tw = 0, msb_th = 0;
                measure_text(msbText, &msb_tw, &msb_th);
                SDL_Color msbTextColor = g_button_text;
                if (g_keyboard_channel_dd_open)
                    msbTextColor.a = 180; // Dim text when disabled
                draw_text(R, msbRect.x + (msbRect.w - msb_tw) / 2, msbRect.y + (msbRect.h - msb_th) / 2, msbText, msbTextColor);

                // LSB picker (now second)
                Rect lsbRect = {msbRect.x + picker_w + spacing, picker_y, picker_w, picker_h};
                bool lsbHover = !g_keyboard_channel_dd_open && point_in(ui_mx, ui_my, lsbRect);
                SDL_Color lsbBg = lsbHover ? g_button_hover : g_button_base;
                if (g_keyboard_channel_dd_open)
                    lsbBg.a = 180; // Make it appear disabled when channel dropdown is open
                draw_rect(R, lsbRect, lsbBg);
                draw_frame(R, lsbRect, g_button_border);

                char lsbText[8];
                snprintf(lsbText, sizeof(lsbText), "%d", g_keyboard_lsb);
                int lsb_tw = 0, lsb_th = 0;
                measure_text(lsbText, &lsb_tw, &lsb_th);
                SDL_Color lsbTextColor = g_button_text;
                if (g_keyboard_channel_dd_open)
                    lsbTextColor.a = 180; // Dim text when disabled
                draw_text(R, lsbRect.x + (lsbRect.w - lsb_tw) / 2, lsbRect.y + (lsbRect.h - lsb_th) / 2, lsbText, lsbTextColor);

                // Handle tooltips (disabled when channel dropdown is open)
                if (!g_keyboard_channel_dd_open)
                {
                    if (lsbHover)
                    {
                        ui_set_tooltip((Rect){ui_mx + 10, ui_my - 25, 0, 0}, "LSB", &g_lsb_tooltip_visible, &g_lsb_tooltip_rect, g_lsb_tooltip_text, sizeof(g_lsb_tooltip_text));
                    }
                    else if (msbHover)
                    {
                        ui_set_tooltip((Rect){ui_mx + 10, ui_my - 25, 0, 0}, "MSB", &g_msb_tooltip_visible, &g_msb_tooltip_rect, g_msb_tooltip_text, sizeof(g_msb_tooltip_text));
                    }
                }

                // Handle clicks (disabled when channel dropdown is open)
                if (!modal_block && !g_keyboard_channel_dd_open)
                {
                    if (msbHover)
                    {
                        if (ui_mclick) // Left click increments
                        {
                            change_bank_value_for_current_channel(true, +1);
                        }
                        else if (ui_rclick) // Right click decrements
                        {
                            change_bank_value_for_current_channel(true, -1);
                        }
                    }
                    else if (lsbHover)
                    {
                        if (ui_mclick) // Left click increments
                        {
                            change_bank_value_for_current_channel(false, +1);
                        }
                        else if (ui_rclick) // Right click decrements
                        {
                            change_bank_value_for_current_channel(false, -1);
                        }
                    }
                }
            }
            // Merge engine-driven active notes with notes coming from external
            // MIDI input so incoming MIDI lights the virtual keys even when
            // playback is stopped or when using the live fallback song.
            unsigned char merged_notes[128];
            memset(merged_notes, 0, sizeof(merged_notes));
#ifdef SUPPORT_MIDI_HW
            // If MIDI input is enabled, fill merged_notes from per-channel state
            if (g_midi_input_enabled)
            {
                if (g_keyboard_show_all_channels)
                {
                    // OR all channels together
                    for (int ch = 0; ch < 16; ++ch)
                    {
                        for (int n = 0; n < 128; ++n)
                            merged_notes[n] |= g_keyboard_active_notes_by_channel[ch][n];
                    }
                }
                else
                {
                    // Only show the currently selected channel
                    for (int n = 0; n < 128; ++n)
                        merged_notes[n] |= g_keyboard_active_notes_by_channel[g_keyboard_channel][n];
                }
            }
#endif
            // Also query engine active notes when appropriate and OR them in.
            if (!g_exporting)
            {
                BAESong target = g_bae.song ? g_bae.song : g_live_song;
                if (target && g_bae.is_playing)
                {
                    Uint32 nowms = SDL_GetTicks();
                    if (nowms >= g_keyboard_suppress_until)
                    {
                        if (g_keyboard_show_all_channels)
                        {
                            // Query each channel and OR them together so engine-driven
                            // activity on any channel lights the virtual keyboard when
                            // the 'All' option is enabled.
                            for (int ch = 0; ch < 16; ++ch)
                            {
                                unsigned char ch_notes[128];
                                memset(ch_notes, 0, sizeof(ch_notes));
                                BAESong_GetActiveNotes(target, (unsigned char)ch, ch_notes);
                                for (int i = 0; i < 128; i++)
                                    merged_notes[i] |= ch_notes[i];
                            }
                        }
                        else
                        {
                            unsigned char engine_notes[128];
                            memset(engine_notes, 0, sizeof(engine_notes));
                            BAESong_GetActiveNotes(target, (unsigned char)g_keyboard_channel, engine_notes);
                            for (int i = 0; i < 128; i++)
                                merged_notes[i] |= engine_notes[i];
                        }
                    }
                }
            }
            // Copy merged result back into the UI array used for drawing/interaction.
            memcpy(g_keyboard_active_notes, merged_notes, sizeof(g_keyboard_active_notes));
            // Build a quick lookup of notes triggered by typed (qwerty) keys so
            // we can render them with the highlight color instead of the
            // accent color used for incoming MIDI/engine activity.
            unsigned char typed_notes[128];
            memset(typed_notes, 0, sizeof(typed_notes));
            for (int sc = 0; sc < 512; ++sc)
            {
                int mn = g_keyboard_pressed_note[sc];
                if (mn >= 0 && mn < 128)
                    typed_notes[mn] = 1;
            }
            // Keyboard drawing region
            int kbX = keyboardPanel.x + 110;
            int kbY = keyboardPanel.y + 28;
            int kbW = keyboardPanel.w - 120;
            int kbH = keyboardPanel.h - 38;
            // Define note range (61-key C2..C7)
            int firstNote = 36;
            int lastNote = 96; // inclusive
            // Count white keys
            int whiteCount = 0;
            for (int n = firstNote; n <= lastNote; n++)
            {
                int m = n % 12;
                if (m == 0 || m == 2 || m == 4 || m == 5 || m == 7 || m == 9 || m == 11)
                    whiteCount++;
            }
            if (whiteCount < 1)
                whiteCount = 1;
            float whiteWf = (float)kbW / whiteCount;
            // First pass draw white keys
            int wIndex = 0;
            int mouseNoteCandidateWhite = -1;
            int mouseNoteCandidateBlack = -1; // track hover note (black wins)
            for (int n = firstNote; n <= lastNote; n++)
            {
                int m = n % 12;
                bool isWhite = (m == 0 || m == 2 || m == 4 || m == 5 || m == 7 || m == 9 || m == 11);
                if (isWhite)
                {
                    int x = kbX + (int)(wIndex * whiteWf);
                    int nextX = kbX + (int)((wIndex + 1) * whiteWf);
                    int w = nextX - x - 1;
                    if (w < 4)
                        w = 4;
                    SDL_Color keyCol = g_is_dark_mode ? (SDL_Color){200, 200, 205, 255} : (SDL_Color){245, 245, 245, 255};
                    // Default: engine/MIDI-driven active notes use accent color.
                    if (g_keyboard_active_notes[n])
                        keyCol = g_accent_color;
                    // Override: typed (qwerty) notes or mouse-held notes use highlight.
                    if (typed_notes[n] || g_keyboard_mouse_note == n)
                        keyCol = g_highlight_color;
                    draw_rect(R, (Rect){x, kbY, w, kbH}, keyCol);
                    draw_frame(R, (Rect){x, kbY, w, kbH}, g_panel_border);
                    // Optional note name for C notes
                    if (m == 0)
                    {
                        char nb[8];
                        int octave = (n / 12) - 1;
                        snprintf(nb, sizeof(nb), "C%d", octave);
                        int tw, th;
                        measure_text(nb, &tw, &th);
                        draw_text(R, x + 2, kbY + kbH - (th + 2), nb, g_is_dark_mode ? (SDL_Color){20, 20, 25, 255} : (SDL_Color){30, 30, 30, 255});
                    }
                    if (!g_keyboard_channel_dd_open && !modal_block && !g_reverbDropdownOpen && !g_exportDropdownOpen &&
                         !g_exporting && ui_mx >= x && ui_mx < x + w && ui_my >= kbY && ui_my < kbY + kbH)
                    {
                        mouseNoteCandidateWhite = n;
                    }
                    wIndex++;
                }
            }
            // Second pass draw black keys
            wIndex = 0; // re-evaluate positions
            // Build array mapping note->x base for white key underneath (use float to reduce truncation)
            float whitePosF[128];
            for (int i = 0; i < 128; ++i)
                whitePosF[i] = 0.0f;
            for (int n = firstNote; n <= lastNote; n++)
            {
                int m = n % 12;
                bool isWhite = (m == 0 || m == 2 || m == 4 || m == 5 || m == 7 || m == 9 || m == 11);
                if (isWhite)
                {
                    whitePosF[n] = (float)kbX + (wIndex * whiteWf);
                    wIndex++;
                }
            }
            for (int n = firstNote; n <= lastNote; n++)
            {
                int m = n % 12;
                bool isBlack = (m == 1 || m == 3 || m == 6 || m == 8 || m == 10);
                if (isBlack)
                {
                    // position relative to previous white key
                    int prevWhite = n - 1;
                    while (prevWhite >= firstNote)
                    {
                        int mm = prevWhite % 12;
                        if (mm == 0 || mm == 2 || mm == 4 || mm == 5 || mm == 7 || mm == 9 || mm == 11)
                            break;
                        prevWhite--;
                    }
                    if (prevWhite < firstNote)
                        continue;
                    float wxf = whitePosF[prevWhite];
                    float wxNextf = wxf + whiteWf;
                    // Compute black key width first, then center it between wxf and wxNextf
                    int bw = (int)(whiteWf * 0.6f);
                    if (bw < 4)
                        bw = 4;
                    int bx = (int)(wxf + ((wxNextf - wxf - (float)bw) * 0.5f));
                    // Shift every black key right by 12 pixels (reduced by 6px)
                    bx += 10;
                    // Intentionally do not clamp bx to wxNext so black keys hover over whites
                    int bh = (int)(kbH * 0.62f);
                    SDL_Color keyCol = g_is_dark_mode ? (SDL_Color){40, 40, 45, 255} : (SDL_Color){50, 50, 60, 255};
                    // Engine/MIDI active notes use accent by default for contrast
                    if (g_keyboard_active_notes[n])
                        keyCol = g_accent_color;
                    // But typed/mouse-held notes should appear highlighted
                    if (typed_notes[n] || g_keyboard_mouse_note == n)
                        keyCol = g_highlight_color;
                    draw_rect(R, (Rect){bx, kbY, bw, bh}, keyCol);
                    draw_frame(R, (Rect){bx, kbY, bw, bh}, g_panel_border);
                    if (!g_keyboard_channel_dd_open && !modal_block && !g_reverbDropdownOpen && !g_exportDropdownOpen && 
#if SUPPORT_MIDI_HW == TRUE
#endif                        
                        !g_exporting && ui_mx >= bx && ui_mx < bx + bw && ui_my >= kbY && ui_my < kbY + bh)
                    {
                        mouseNoteCandidateBlack = n;
                    }
                }
            }
            // Determine hovered note (black takes precedence over white)
            int mouseNote = (mouseNoteCandidateBlack != -1) ? mouseNoteCandidateBlack : mouseNoteCandidateWhite;
            // Interaction: monophonic click-n-drag play (velocity varies by vertical position)
            if (!modal_block && !g_keyboard_channel_dd_open && !g_reverbDropdownOpen && !g_exportDropdownOpen && 
#if SUPPORT_MIDI_HW == TRUE
#endif
                !g_exporting)
            {
                if (ui_mdown)
                {
                    if (mouseNote != -1 && mouseNote != g_keyboard_mouse_note)
                    {
                        // Release previous
                        if (g_keyboard_mouse_note != -1)
                        {
                            BAESong target = g_bae.song ? g_bae.song : g_live_song;
                            if (target)
                            {
                                BAESong_NoteOff(target, (unsigned char)g_keyboard_channel, (unsigned char)g_keyboard_mouse_note, 0, 0);
                            }
                        }
                        // Compute velocity based on Y position inside the key: quiet near top, loud near bottom.
                        // Bottom 15 pixels always map to max velocity (127).
                        int keyHeight = kbH; // default white key height
                        int mod = mouseNote % 12;
                        bool isBlack = (mod == 1 || mod == 3 || mod == 6 || mod == 8 || mod == 10);
                        if (isBlack)
                        {
                            keyHeight = (int)(kbH * 0.62f);
                        }
                        int relY = ui_my - kbY;
                        if (relY < 0)
                            relY = 0;
                        if (relY >= keyHeight)
                            relY = keyHeight - 1;
                        int fromBottom = keyHeight - 1 - relY;
                        int vel;
                        if (fromBottom < 15)
                        {
                            vel = 127; // bottom 15px -> max
                        }
                        else
                        {
                            int effectiveRange = keyHeight - 15;
                            if (effectiveRange < 1)
                                effectiveRange = 1;
                            float t = (float)relY / (float)effectiveRange; // 0 (top) .. 1 (just above bottom zone)
                            if (t < 0.f)
                                t = 0.f;
                            if (t > 1.f)
                                t = 1.f;
                            vel = (int)(t * 112.0f); // map into 0..112
                            if (vel < 8)
                                vel = 8; // floor so very top still audible
                            if (vel > 112)
                                vel = 112;
                        }
                        {
                            BAESong target = g_bae.song ? g_bae.song : g_live_song;
                            if (target)
                            {
                                BAESong_NoteOnWithLoad(target, (unsigned char)g_keyboard_channel, (unsigned char)mouseNote, (unsigned char)vel, 0);
                            }
                        }
#ifdef SUPPORT_MIDI_HW
                        if (g_midi_output_enabled)
                        {
                            unsigned char m[3];
                            m[0] = (unsigned char)(0x90 | (g_keyboard_channel & 0x0F));
                            m[1] = (unsigned char)mouseNote;
                            m[2] = (unsigned char)vel;
                            midi_output_send(m, 3);
                        }
#endif
                        g_keyboard_mouse_note = mouseNote;
                    }
                    else if (mouseNote == -1 && g_keyboard_mouse_note != -1)
                    {
                        // Dragged outside – stop sounding note
                        {
                            BAESong target = g_bae.song ? g_bae.song : g_live_song;
                            if (target)
                            {
                                BAESong_NoteOff(target, (unsigned char)g_keyboard_channel, (unsigned char)g_keyboard_mouse_note, 0, 0);
                            }
                        }
#ifdef SUPPORT_MIDI_HW
                        if (g_midi_output_enabled && g_keyboard_mouse_note != -1)
                        {
                            unsigned char m[3];
                            m[0] = (unsigned char)(0x80 | (g_keyboard_channel & 0x0F));
                            m[1] = (unsigned char)g_keyboard_mouse_note;
                            m[2] = 0;
                            midi_output_send(m, 3);
                        }
#endif
                        g_keyboard_mouse_note = -1;
                    }
                }
                else
                {
                    // Mouse released anywhere
                    if (g_keyboard_mouse_note != -1)
                    {
                        BAESong target = g_bae.song ? g_bae.song : g_live_song;
                        if (target)
                        {
                            BAESong_NoteOff(target, (unsigned char)g_keyboard_channel, (unsigned char)g_keyboard_mouse_note, 0, 0);
                        }
#ifdef SUPPORT_MIDI_HW
                        if (g_midi_output_enabled)
                        {
                            unsigned char m[3];
                            m[0] = (unsigned char)(0x80 | (g_keyboard_channel & 0x0F));
                            m[1] = (unsigned char)g_keyboard_mouse_note;
                            m[2] = 0;
                            midi_output_send(m, 3);
                        }
                        g_keyboard_mouse_note = -1;
#endif
                    }
                }
            }
            else
            {
                // If dropdown/modal opens while holding a note, release it
                if (g_keyboard_mouse_note != -1)
                {
                    BAESong target = g_bae.song ? g_bae.song : g_live_song;
                    if (target)
                    {
                        BAESong_NoteOff(target, (unsigned char)g_keyboard_channel, (unsigned char)g_keyboard_mouse_note, 0, 0);
                    }
#ifdef SUPPORT_MIDI_HW
                    if (g_midi_output_enabled)
                    {
                        unsigned char m[3];
                        m[0] = (unsigned char)(0x80 | (g_keyboard_channel & 0x0F));
                        m[1] = (unsigned char)g_keyboard_mouse_note;
                        m[2] = 0;
                        midi_output_send(m, 3);
                    }
#endif
                    g_keyboard_mouse_note = -1;
                }
            }
        }
    SKIP_KEYBOARD_RENDER:

    {
        Rect loopR = {160, 215, 20, 20};
        // Create tooltip area that includes checkbox and label
        Rect loopTooltipArea = {160, 215, 70, 20}; // Wider to include "BAE Loop" text
        bool clicked = false;
#ifdef SUPPORT_MIDI_HW
        // When MIDI input is enabled, render a disabled Loop checkbox (no interaction) so it appears under the dim overlay
        if (g_midi_input_enabled)
        {
            SDL_Color disabledBg = g_panel_bg;
            SDL_Color disabledTxt = g_panel_border;
            // Draw checkbox background and border
            draw_rect(R, loopR, disabledBg);
            draw_frame(R, loopR, g_panel_border);
            Rect inner = {loopR.x + 3, loopR.y + 3, loopR.w - 6, loopR.h - 6};
            if (loopPlay)
            {
                draw_rect(R, inner, g_accent_color);
                draw_frame(R, inner, g_button_text);
            }
            else
            {
                draw_rect(R, inner, g_panel_bg);
                draw_frame(R, inner, g_panel_border);
            }
            // Label
            draw_text(R, loopR.x + loopR.w + 6, loopR.y + 2, "BAE Loop", disabledTxt);
        }
        else
        {
#endif
            if (!modal_block)
            {
                if (ui_toggle(R, loopR, &loopPlay, "BAE Loop", ui_mx, ui_my, ui_mclick))
                    clicked = true;
            }
            else if (g_exporting)
            {
                // While exporting allow loop toggle using real mouse coords so user can uncheck loop
                if (ui_toggle(R, loopR, &loopPlay, "BAE Loop", mx, my, mclick))
                    clicked = true;
            }
            else
            {
                // When modal is open, render disabled checkbox (same as MIDI input disabled style)
                SDL_Color disabledBg = g_panel_bg;
                SDL_Color disabledTxt = g_panel_border;
                // Draw checkbox background and border
                draw_rect(R, loopR, disabledBg);
                draw_frame(R, loopR, g_panel_border);
                Rect inner = {loopR.x + 3, loopR.y + 3, loopR.w - 6, loopR.h - 6};
                if (loopPlay)
                {
                    draw_rect(R, inner, g_accent_color);
                    draw_frame(R, inner, g_button_text);
                }
                else
                {
                    draw_rect(R, inner, g_panel_bg);
                    draw_frame(R, inner, g_panel_border);
                }
                // Label
                draw_text(R, loopR.x + loopR.w + 6, loopR.y + 2, "BAE Loop", disabledTxt);
            }

            // Handle tooltip for BAE Loop
            if (point_in(ui_mx, ui_my, loopTooltipArea) && !modal_block)
            {
                const char *tooltip_text = "BAE Loop will interfere with the playlist, disable it to use the playlist.";

                // Measure actual text width and height
                int text_w, text_h;
                measure_text(tooltip_text, &text_w, &text_h);

                int tooltip_w = text_w + 8; // 4px padding on each side
                int tooltip_h = text_h + 8; // 4px padding top and bottom
                if (tooltip_w > 500)
                    tooltip_w = 500; // Maximum width constraint

                int tooltip_x = ui_mx + 10;
                int tooltip_y = ui_my - 30;

                // Keep tooltip on screen
                if (tooltip_x + tooltip_w > WINDOW_W - 4)
                    tooltip_x = WINDOW_W - tooltip_w - 4;
                if (tooltip_y < 4)
                    tooltip_y = ui_my + 25; // Show below cursor if no room above

                ui_set_tooltip((Rect){tooltip_x, tooltip_y, tooltip_w, tooltip_h}, tooltip_text, &g_loop_tooltip_visible, &g_loop_tooltip_rect, g_loop_tooltip_text, sizeof(g_loop_tooltip_text));
            }
            else
            {
                ui_clear_tooltip(&g_loop_tooltip_visible);
            }

            if (clicked)
            {
                bae_set_loop(loopPlay);
                g_bae.loop_enabled_gui = loopPlay;

                // Update loop count on currently loaded audio file if any
                if (g_bae.is_audio_file && g_bae.sound)
                {
                    uint32_t loopCount = loopPlay ? 0xFFFFFFFF : 0;
                    BAESound_SetLoopCount(g_bae.sound, loopCount);
                }

                // Save settings when loop is changed
                if (g_current_bank_path[0] != '\0')
                {
                    save_settings(g_current_bank_path, reverbType, loopPlay);
                }
            }
#ifdef SUPPORT_MIDI_HW
        }
#endif
    }
#ifdef SUPPORT_MIDI_HW
        if (g_midi_input_enabled)
        {
            // Draw disabled Open... button (no interaction)
            Rect openRect = {258, 215, 80, 22}; // Moved from 230 to 258 to accommodate "BAE Loop"
            SDL_Color disabledBg = g_panel_bg;
            SDL_Color disabledTxt = g_panel_border;
            draw_rect(R, openRect, disabledBg);
            draw_frame(R, openRect, g_panel_border);
            int text_w = 0, text_h = 0;
            measure_text("Open...", &text_w, &text_h);
            int text_x = openRect.x + (openRect.w - text_w) / 2;
            int text_y = openRect.y + (openRect.h - text_h) / 2;
            draw_text(R, text_x, text_y, "Open...", disabledTxt);
        }
        else
        {
#endif
            if (ui_button(R, (Rect){258, 215, 80, 22}, "Open...", ui_mx, ui_my, ui_mdown) && ui_mclick && !modal_block) // Moved from 230 to 258
            {
                char *sel = open_file_dialog();
                if (sel)
                {
                    if (bae_load_song_with_settings(sel, transpose, tempo, volume, loopPlay, reverbType, ch_enable))
                    {
#if SUPPORT_PLAYLIST == TRUE
                        if (g_playlist.enabled) {
                            // Add file to playlist and set as current
                            playlist_update_current_file(sel);
                        }
#endif
                        duration = bae_get_len_ms();
                        progress = 0;
                        // Robust auto-start sequence: ensure at position 0, preroll again (defensive), then start
                        if (!g_bae.is_audio_file && g_bae.song)
                        {
                            BAESong_SetMicrosecondPosition(g_bae.song, 0);
                            BAESong_Preroll(g_bae.song);
                        }
                        playing = false; // force toggle logic
                        if (!bae_play(&playing))
                        {
                            BAE_PRINTF("Autoplay after Open failed for '%s'\n", sel);
                        }
                        if (playing && g_bae.mixer)
                        {
                            for (int i = 0; i < 3; i++)
                            {
                                BAEMixer_Idle(g_bae.mixer);
                            }
                        }
                    }
                    free(sel);
                }
            }
#ifdef SUPPORT_MIDI_HW
        }
#endif

        // Export controls (only for MIDI/RMF files) or MIDI-in recording when enabled
#ifdef SUPPORT_MIDI_HW
        if (g_midi_input_enabled || (!g_bae.is_audio_file && g_bae.song_loaded))
#else
        if (!g_bae.is_audio_file && g_bae.song_loaded)
#endif
        {
            // If MIDI input is enabled, show Record/Stop instead of Export
#ifdef SUPPORT_MIDI_HW
            if (g_midi_input_enabled)
            {
                // When MIDI-in is enabled the overlay provides recording controls. Draw a disabled placeholder here.
                SDL_Color disabledBg = g_panel_bg;
                SDL_Color disabledTxt = g_panel_border;
                Rect placeholder = {348, 215, 80, 22}; // Moved from 320 to 348
                draw_rect(R, placeholder, disabledBg);
                draw_frame(R, placeholder, g_panel_border);
                int text_w = 0, text_h = 0;
                measure_text("Record", &text_w, &text_h);
                int text_x = placeholder.x + (placeholder.w - text_w) / 2;
                int text_y = placeholder.y + (placeholder.h - text_h) / 2;
                draw_text(R, text_x, text_y, "Record", disabledTxt);
            }
            else
            {
#endif
                // Export button: mutually exclusive with external MIDI Output. When MIDI Output
                // is enabled the Export button is shown disabled and does not accept clicks.
#ifdef SUPPORT_MIDI_HW
                bool export_allowed = !g_midi_output_enabled && !g_exporting && !modal_block;
#else
            bool export_allowed = !g_exporting && !modal_block;
#endif
                if (export_allowed)
                {
                    if (ui_button(R, (Rect){340, 215, 80, 22}, "Export", ui_mx, ui_my, ui_mdown) && ui_mclick)
                    {
#ifdef SUPPORT_MIDI_HW
                        /* export_allowed already ensures g_midi_output_enabled == false */
#endif

                        // Single song export
                        // When export button clicked, open save dialog using extension depending on codec
                        int export_dialog_type = 0; // Default to WAV

                        // Determine export type based on codec index
                        if (g_exportCodecIndex == 0)
                        {
                            export_dialog_type = 0; // WAV
                        }
#if USE_FLAC_ENCODER == TRUE
                        else if (g_exportCodecIndex == 1)
                        {
                            export_dialog_type = 1; // FLAC
                        }
#endif
#if USE_MPEG_ENCODER == TRUE
                        else if (g_exportCodecIndex == 2 || g_exportCodecIndex == 3 || g_exportCodecIndex == 4 || g_exportCodecIndex == 5)
                        {
                            export_dialog_type = 2; // MP3
                        }
#endif
#if USE_VORBIS_ENCODER == TRUE
                        else
                        {
                            export_dialog_type = 3; // Vorbis
                        }
#endif
                        char *export_file = save_export_dialog(export_dialog_type);
                        if (export_file)
                        {
                            // Ensure correct file extension based on export type
                            size_t L = strlen(export_file);
                            const char *expected_ext = NULL;

                            if (export_dialog_type == 1)
                            {
                                expected_ext = ".flac";
                            }
                            else if (export_dialog_type == 2)
                            {
                                expected_ext = ".mp3";
                            }
                            else if (export_dialog_type == 3)
                            {
                                expected_ext = ".ogg";
                            }
                            else
                            {
                                expected_ext = ".wav";
                            }

                            int ext_len = strlen(expected_ext);
                            if (L < ext_len || strcasecmp(export_file + L - ext_len, expected_ext) != 0)
                            {
                                // Append the correct extension
                                size_t n = L + ext_len + 1;
                                char *tmp = malloc(n);
                                if (tmp)
                                {
                                    snprintf(tmp, n, "%s%s", export_file, expected_ext);
                                    free(export_file);
                                    export_file = tmp;
                                }
                            }

                            // Start export using selected codec mapping
                            // Map our index to BAEMixer compression enums using table
                            BAECompressionType compression = BAE_COMPRESSION_NONE;
#if USE_MPEG_ENCODER != FALSE
                            if (g_exportCodecIndex >= 0 && g_exportCodecIndex < g_exportCompressionCount)
                            {
                                compression = g_exportCompressionMap[g_exportCodecIndex];
                            }
#endif
                            if (!g_bae.song_loaded || g_bae.is_audio_file)
                            {
                                set_status_message("Cannot export: No MIDI/RMF loaded");
                            }
                            else
                            {
                                // Save current state
                                BAEFileType export_file_type = BAE_WAVE_TYPE;

                                // Determine file type based on compression type
                                if (compression == BAE_COMPRESSION_NONE)
                                {
                                    export_file_type = BAE_WAVE_TYPE;
                                }
#if USE_FLAC_ENCODER == TRUE
                                else if (compression == BAE_COMPRESSION_LOSSLESS)
                                {
                                    export_file_type = BAE_FLAC_TYPE;
                                }
#endif
#if USE_MPEG_ENCODER == TRUE
                                else if (compression >= BAE_COMPRESSION_MPEG_64 && compression <= BAE_COMPRESSION_MPEG_320)
                                {
                                    export_file_type = BAE_MPEG_TYPE;
                                }
#endif
#if USE_VORBIS_ENCODER == TRUE
                                else if (compression >= BAE_COMPRESSION_VORBIS_96 && compression <= BAE_COMPRESSION_VORBIS_320)
                                {
                                    export_file_type = BAE_VORBIS_TYPE;
                                }
#endif

                                // Determine export file type so service loop can apply MPEG heuristics
                                g_export_file_type = export_file_type;
                                bae_start_export(export_file, export_file_type, compression);
                            }
                            free(export_file);
                        }
                        ui_mclick = false; // consume click
                    }
                }
                else
                {
                    // Draw disabled Export button (no interaction)
                    Rect disr = {340, 215, 80, 22}; // Moved from 320 to 340
                    SDL_Color disabledBg = g_panel_bg;
                    SDL_Color disabledTxt = g_panel_border;
                    disabledBg.a = 200;
                    disabledTxt.a = 200;
                    draw_rect(R, disr, disabledBg);
                    draw_frame(R, disr, g_panel_border);
                    int text_w = 0, text_h = 0;
                    measure_text("Export", &text_w, &text_h);
                    int text_x = disr.x + (disr.w - text_w) / 2;
                    int text_y = disr.y + (disr.h - text_h) / 2;
                    draw_text(R, text_x, text_y, "Export", disabledTxt);
                }

#ifdef SUPPORT_MIDI_HW
            }
#endif
            // RMF Info button (only for RMF files)
            if (g_bae.is_rmf_file)
            {
                // RMF Info button position (moved from 440 to 640 to avoid overlap with Record Format button)
                int rmf_x_pos = 798;

                if (ui_button(R, (Rect){rmf_x_pos, 215, 80, 22}, "RMF Info", ui_mx, ui_my, ui_mdown) && ui_mclick && !modal_block)
                {
                    if (g_show_rmf_info_dialog)
                    {
                        g_show_rmf_info_dialog = false;
                    }
                    else
                    {
                        g_show_rmf_info_dialog = true;
                        rmf_info_load_if_needed();
                    }
                }
            }
        }
#ifdef SUPPORT_MIDI_HW
        // If MIDI input is enabled, paint a semi-transparent overlay over the transport panel
        // to dim it and disable interactions except the Stop button (which we keep active).
        if (g_midi_input_enabled)
        {
            SDL_Color dim = g_is_dark_mode ? (SDL_Color){0, 0, 0, 160} : (SDL_Color){255, 255, 255, 160};
            draw_rect(R, transportPanel, dim);
            // Redraw Stop button on top of the dim overlay so the user can stop
            Rect stopRect = {90, 215, 60, 22};
            // Use raw mouse coords so the Stop button remains clickable even when modal_block is true
            // When MIDI-in is active the Stop button actually stops external playback (external/"♪s"), so label it accordingly.
            const char *stop_label = g_midi_input_enabled ? "Stop ♪s" : "Stop";
            if (ui_button(R, stopRect, stop_label, mx, my, mdown) && mclick)
            {
                bae_stop(&playing, &progress);
                // Ensure engine releases any held notes when user stops playback (panic)
                midi_output_send_all_notes_off(); // silence any external device too
                if (g_bae.song)
                {
                    gui_panic_all_notes(g_bae.song);
                }
                if (g_live_song)
                {
                    gui_panic_all_notes(g_live_song);
                }
                if (g_show_virtual_keyboard)
                {
                    BAESong target = g_bae.song ? g_bae.song : g_live_song;
                    if (target)
                    {
                        for (int n = 0; n < 128; n++)
                        {
                            BAESong_NoteOff(target, (unsigned char)g_keyboard_channel, (unsigned char)n, 0, 0);
                        }
                    }
                    g_keyboard_mouse_note = -1;
                    memset(g_keyboard_active_notes, 0, sizeof(g_keyboard_active_notes));
                    g_keyboard_suppress_until = SDL_GetTicks() + 250;
                }
                // Reset total-play timer on user Stop
                g_total_play_ms = 0;
                g_last_engine_pos_ms = 0;
                // Also stop export if active
                if (g_exporting)
                {
                    bae_stop_wav_export();
                }
                // consume the click so underlying UI doesn't react to the same event
                mclick = false;
            }
            // Also draw Record/Stop in the Export slot so MIDI users can record even when panel is dimmed
            Rect recRect = {348, 215, 80, 22}; // Moved from 320 to 348
            if (!g_midi_recording)
            {
                // Disable Record button when a dialog/modal is open
                if (!modal_block && ui_button(R, recRect, "Record", mx, my, mdown) && mclick)
                {
                    // Behavior depends on selected format
                    if (g_midiRecordFormatIndex == 0)
                    {
                        // MIDI: existing flow
                        char *export_file = save_midi_dialog();
                        if (export_file)
                        {
                            // ensure .mid extension
                            size_t L = strlen(export_file);
                            if (L < 4 || strcasecmp(export_file + L - 4, ".mid") != 0)
                            {
                                size_t n = L + 5;
                                char *tmp = malloc(n);
                                if (tmp)
                                {
                                    snprintf(tmp, n, "%s.mid", export_file);
                                    free(export_file);
                                    export_file = tmp;
                                }
                            }
                            if (export_file)
                            {
                                bool started = midi_record_start(export_file);
                                free(export_file);
                                if (!started)
                                    set_status_message("Failed to start MIDI record");
                            }
                        }
                    }
                    else
                    {
                        // WAV or MP3: start mixer export to file
                        // If no file is loaded, we can still record from MIDI-in using the lightweight live song.
                        if (!g_bae.song_loaded && !g_live_song)
                        {
                            set_status_message("Cannot record audio: No MIDI/RMF loaded and no live song available");
                        }
                        else
                        {
                            // Choose save dialog and extension based on format using helper function
                            MidiRecordFormatInfo format_info = get_midi_record_format_info(g_midiRecordFormatIndex);
                            int export_dialog_type = 0; // Default to WAV

                            switch (format_info.type)
                            {
                            case MIDI_RECORD_FORMAT_WAV:
                                export_dialog_type = 0; // WAV
                                break;
                            case MIDI_RECORD_FORMAT_FLAC:
                                export_dialog_type = 1; // FLAC
                                break;
                            case MIDI_RECORD_FORMAT_MP3:
                                export_dialog_type = 2; // MP3
                                break;
                            case MIDI_RECORD_FORMAT_VORBIS:
                                export_dialog_type = 3; // OGG/Vorbis
                                break;
                            default:
                                export_dialog_type = 0; // fallback to WAV
                                break;
                            }
                            char *export_file = save_export_dialog(export_dialog_type);
                            if (export_file)
                            {
                                // ensure correct extension
                                size_t L = strlen(export_file);
                                if (g_midiRecordFormatIndex == 1)
                                { // WAV
                                    if (L < 4 || strcasecmp(export_file + L - 4, ".wav") != 0)
                                    {
                                        size_t n = L + 5;
                                        char *tmp = malloc(n);
                                        if (tmp)
                                        {
                                            snprintf(tmp, n, "%s.wav", export_file);
                                            free(export_file);
                                            export_file = tmp;
                                        }
                                    }
                                }
#if USE_FLAC_ENCODER != FALSE
                                else if (g_midiRecordFormatIndex == 2)
                                { // FLAC
                                    if (L < 5 || strcasecmp(export_file + L - 5, ".flac") != 0)
                                    {
                                        size_t n = L + 6;
                                        char *tmp = malloc(n);
                                        if (tmp)
                                        {
                                            snprintf(tmp, n, "%s.flac", export_file);
                                            free(export_file);
                                            export_file = tmp;
                                        }
                                    }
                                }
#endif
                                else if (g_midiRecordFormatIndex >= 3)
                                {
                                    // Use helper function to determine format and extension
                                    MidiRecordFormatInfo format_info = get_midi_record_format_info(g_midiRecordFormatIndex);
                                    const char *ext = format_info.extension;
                                    int ext_len = strlen(ext);

                                    if (L < ext_len || strcasecmp(export_file + L - ext_len, ext) != 0)
                                    {
                                        size_t n = L + ext_len + 1;
                                        char *tmp = malloc(n);
                                        if (tmp)
                                        {
                                            snprintf(tmp, n, "%s%s", export_file, ext);
                                            free(export_file);
                                            export_file = tmp;
                                        }
                                    }
                                }

                                // Save current state for restore. Use live song if no file-loaded song present.
                                BAESong target = g_bae.song ? g_bae.song : g_live_song;
                                uint32_t curPosUs = 0;
                                // If we're in MIDI-in mode, do not modify song playback (do not stop/seek/start).
                                if (!g_midi_input_enabled)
                                {
                                    if (target)
                                        BAESong_GetMicrosecondPosition(target, &curPosUs);
                                }
                                g_bae.position_us_before_export = curPosUs;
                                g_bae.was_playing_before_export = g_bae.is_playing;
                                g_bae.loop_was_enabled_before_export = g_bae.loop_enabled_gui;
                                if (!g_midi_input_enabled && g_bae.is_playing && target)
                                {
                                    BAESong_Stop(target, FALSE);
                                    g_bae.is_playing = false;
                                }

                                // Map selected format to BAE types using helper function
                                BAECompressionType compression = BAE_COMPRESSION_NONE;
                                MidiRecordFormatInfo format_info = get_midi_record_format_info(g_midiRecordFormatIndex);

                                if (format_info.type == MIDI_RECORD_FORMAT_WAV)
                                {
                                    compression = BAE_COMPRESSION_NONE;
                                }
#if USE_FLAC_ENCODER != FALSE
                                else if (format_info.type == MIDI_RECORD_FORMAT_FLAC)
                                {
                                    compression = BAE_COMPRESSION_LOSSLESS;
                                }
#endif
#if USE_MPEG_ENCODER != FALSE
                                else if (format_info.type == MIDI_RECORD_FORMAT_MP3)
                                {
                                    // Map MP3 bitrate to compression type
                                    switch (format_info.bitrate)
                                    {
                                    case 128000:
                                        compression = BAE_COMPRESSION_MPEG_128;
                                        break;
                                    case 192000:
                                        compression = BAE_COMPRESSION_MPEG_192;
                                        break;
                                    case 256000:
                                        compression = BAE_COMPRESSION_MPEG_256;
                                        break;
                                    case 320000:
                                        compression = BAE_COMPRESSION_MPEG_320;
                                        break;
                                    default:
                                        compression = BAE_COMPRESSION_MPEG_128;
                                        break;
                                    }
                                }
#endif
#if USE_VORBIS_ENCODER == TRUE
                                else if (format_info.type == MIDI_RECORD_FORMAT_VORBIS)
                                {
                                    // Map Vorbis bitrate to compression type
                                    switch (format_info.bitrate)
                                    {
                                    case 96000:
                                        compression = BAE_COMPRESSION_VORBIS_96;
                                        break;
                                    case 128000:
                                        compression = BAE_COMPRESSION_VORBIS_128;
                                        break;
                                    case 256000:
                                        compression = BAE_COMPRESSION_VORBIS_256;
                                        break;
                                    case 320000:
                                        compression = BAE_COMPRESSION_VORBIS_320;
                                        break;
                                    default:
                                        compression = BAE_COMPRESSION_VORBIS_128;
                                        break;
                                    }
                                }
#endif

                                if (export_file)
                                {
                                    // If WAV or FLAC selected, use our PCM capture path instead of BAEMixer file output
                                    if (g_midiRecordFormatIndex == 1) // WAV
                                    {
                                        // start our own PCM WAV writer and start song/live-song to drive audio
                                        int wav_channels = g_stereo_output ? 2 : 1;
                                        int wav_sr = g_sample_rate_hz > 0 ? g_sample_rate_hz : 44100;
                                        bool started = pcm_wav_start(export_file, wav_channels, wav_sr, 16);
                                        if (!started)
                                        {
                                            set_status_message("Failed to open WAV file for recording");
                                            free(export_file);
                                        }
                                        else
                                        {
                                            // If not in MIDI-in mode, start/seek/preroll the target song to drive engine audio.
                                            if (!g_midi_input_enabled)
                                            {
                                                if (target)
                                                {
                                                    BAESong_Stop(target, FALSE);
                                                    BAESong_SetMicrosecondPosition(target, 0);
                                                    BAESong_Preroll(target);
                                                }
                                                BAEResult rs = target ? BAESong_Start(target, 0) : BAE_NO_ERROR;
                                                if (rs != BAE_NO_ERROR)
                                                {
                                                    set_status_message("Failed to start song for WAV recording");
                                                    pcm_wav_finalize();
                                                    free(export_file);
                                                }
                                                else
                                                {
                                                    g_bae.is_playing = true;
                                                    g_pcm_wav_recording = true;
                                                    g_exporting = false; // use our own writer
                                                    strncpy(g_export_path, export_file, sizeof(g_export_path) - 1);
                                                    g_export_path[sizeof(g_export_path) - 1] = '\0';
                                                    set_status_message("WAV recording started");
                                                    free(export_file);
                                                }
                                            }
                                            else
                                            {
                                                // MIDI-in mode: do not change engine playback; just enable PCM writer.
                                                g_pcm_wav_recording = true;
                                                g_exporting = false; // use our own writer
                                                strncpy(g_export_path, export_file, sizeof(g_export_path) - 1);
                                                g_export_path[sizeof(g_export_path) - 1] = '\0';
                                                set_status_message("WAV recording started");
                                                free(export_file);
                                            }
                                        }
                                    }
#if USE_FLAC_ENCODER != FALSE
                                    else if (g_midiRecordFormatIndex == 2) // FLAC
                                    {
                                        // start our own PCM FLAC writer and start song/live-song to drive audio
                                        int flac_channels = g_stereo_output ? 2 : 1;
                                        int flac_sr = g_sample_rate_hz > 0 ? g_sample_rate_hz : 44100;
                                        set_status_message("Attempting FLAC recording...");
                                        BAE_PRINTF("GUI: Attempting FLAC recording - channels=%d, sr=%d, file=%s\n", flac_channels, flac_sr, export_file);
                                        bool started = pcm_flac_start(export_file, flac_channels, flac_sr, 16);
                                        if (!started)
                                        {
                                            set_status_message("Failed to open FLAC file for recording");
                                            free(export_file);
                                        }
                                        else
                                        {
                                            // If not in MIDI-in mode, start/seek/preroll the target song to drive engine audio.
                                            if (!g_midi_input_enabled)
                                            {
                                                if (target)
                                                {
                                                    BAESong_Stop(target, FALSE);
                                                    BAESong_SetMicrosecondPosition(target, 0);
                                                    BAESong_Preroll(target);
                                                }
                                                BAEResult rs = target ? BAESong_Start(target, 0) : BAE_NO_ERROR;
                                                if (rs != BAE_NO_ERROR)
                                                {
                                                    set_status_message("Failed to start song for FLAC recording");
                                                    pcm_flac_finalize();
                                                    free(export_file);
                                                }
                                                else
                                                {
                                                    g_bae.is_playing = true;
                                                    g_pcm_flac_recording = true;
                                                    g_midi_recording = true;
                                                    g_exporting = false; // use our own writer
                                                    strncpy(g_export_path, export_file, sizeof(g_export_path) - 1);
                                                    g_export_path[sizeof(g_export_path) - 1] = '\0';
                                                    set_status_message("FLAC recording started");
                                                    free(export_file);
                                                }
                                            }
                                            else
                                            {
                                                // MIDI-in mode: do not change engine playback; just enable PCM writer.
                                                g_pcm_flac_recording = true;
                                                g_midi_recording = true;
                                                g_exporting = false; // use our own writer
                                                strncpy(g_export_path, export_file, sizeof(g_export_path) - 1);
                                                g_export_path[sizeof(g_export_path) - 1] = '\0';
                                                set_status_message("FLAC recording started");
                                                free(export_file);
                                            }
                                        }
                                    }
#endif
                                    else
                                    {
                                        // MP3 or Vorbis selected - check format type
                                        if (format_info.type == MIDI_RECORD_FORMAT_MP3)
                                        {
#if USE_MPEG_ENCODER != FALSE
                                            if (g_midi_input_enabled)
                                            {
                                                // Use platform MP3 recorder for MIDI-in; do not start BAESong
                                                int mp3_channels = g_stereo_output ? 2 : 1;
                                                int mp3_sr = g_sample_rate_hz > 0 ? g_sample_rate_hz : 44100;
                                                int bitrate = format_info.bitrate;
                                                int rc = BAE_Platform_MP3Recorder_Start(export_file, (uint32_t)mp3_channels, (uint32_t)mp3_sr, 16, (uint32_t)bitrate);
                                                if (rc != 0)
                                                {
                                                    set_status_message("Failed to start MP3 recorder");
                                                    free(export_file);
                                                }
                                                else
                                                {
                                                    g_pcm_mp3_recording = true;
                                                    g_midi_recording = true;
                                                    g_exporting = false;
                                                    strncpy(g_export_path, export_file, sizeof(g_export_path) - 1);
                                                    g_export_path[sizeof(g_export_path) - 1] = '\0';
                                                    set_status_message("MP3 recording started");
                                                    free(export_file);
                                                }
                                            }
                                            else
                                            {
                                                // Fallback to BAEMixer path for normal song export to MP3
                                                BAEResult result = BAEMixer_StartOutputToFile(g_bae.mixer, (BAEPathName)export_file,
                                                                                              BAE_MPEG_TYPE,
                                                                                              compression);
                                                if (result != BAE_NO_ERROR)
                                                {
                                                    char msg[128];
                                                    snprintf(msg, sizeof(msg), "MP3 export failed to start (%d)", result);
                                                    set_status_message(msg);
                                                    free(export_file);
                                                }
                                                else
                                                {
                                                    g_exporting = true;
                                                    g_export_file_type = BAE_MPEG_TYPE;
                                                    strncpy(g_export_path, export_file, sizeof(g_export_path) - 1);
                                                    g_export_path[sizeof(g_export_path) - 1] = '\0';
                                                    set_status_message("MP3 export started");
                                                    free(export_file);
                                                }
                                            }
#else
                    set_status_message("MP3 export not supported in this build");
                    free(export_file);
#endif
                                        }
                                        else if (format_info.type == MIDI_RECORD_FORMAT_VORBIS)
                                        {
#if USE_VORBIS_ENCODER == TRUE
                                            // Use our own PCM Vorbis writer for real-time recording
                                            int vorbis_channels = g_stereo_output ? 2 : 1;
                                            int vorbis_sr = g_sample_rate_hz > 0 ? g_sample_rate_hz : 44100;
                                            int bitrate = format_info.bitrate;
                                            bool started = pcm_vorbis_start(export_file, vorbis_channels, vorbis_sr, 16, bitrate);
                                            if (!started)
                                            {
                                                set_status_message("Failed to open Vorbis file for recording");
                                                free(export_file);
                                            }
                                            else
                                            {
                                                // If not in MIDI-in mode, start/seek/preroll the target song to drive engine audio.
                                                if (!g_midi_input_enabled)
                                                {
                                                    if (target)
                                                    {
                                                        BAESong_Stop(target, FALSE);
                                                        BAESong_SetMicrosecondPosition(target, 0);
                                                        BAESong_Preroll(target);
                                                    }
                                                    BAEResult rs = target ? BAESong_Start(target, 0) : BAE_NO_ERROR;
                                                    if (rs != BAE_NO_ERROR)
                                                    {
                                                        set_status_message("Failed to start song for Vorbis recording");
                                                        pcm_vorbis_finalize();
                                                        free(export_file);
                                                    }
                                                    else
                                                    {
                                                        g_bae.is_playing = true;
                                                        g_pcm_vorbis_recording = true;
                                                        g_midi_recording = true;
                                                        g_exporting = false; // use our own writer
                                                        strncpy(g_export_path, export_file, sizeof(g_export_path) - 1);
                                                        g_export_path[sizeof(g_export_path) - 1] = '\0';
                                                        set_status_message("Vorbis recording started");
                                                        free(export_file);
                                                    }
                                                }
                                                else
                                                {
                                                    // MIDI-in mode: do not change engine playback; just enable PCM writer.
                                                    g_pcm_vorbis_recording = true;
                                                    g_midi_recording = true;
                                                    g_exporting = false; // use our own writer
                                                    strncpy(g_export_path, export_file, sizeof(g_export_path) - 1);
                                                    g_export_path[sizeof(g_export_path) - 1] = '\0';
                                                    set_status_message("Vorbis recording started");
                                                    free(export_file);
                                                }
                                            }
#else
                                        set_status_message("Vorbis export not supported in this build");
                                        free(export_file);
#endif
                                        }
                                        else
                                        {
                                            set_status_message("Unsupported export format");
                                            free(export_file);
                                        }
                                    }
                                }
                            }
                        }
                    }
                    mclick = false;
                }
                else if (modal_block)
                {
                    // Draw disabled Record button (no interaction)
                    SDL_Color disabledBg = g_panel_bg;
                    SDL_Color disabledTxt = g_panel_border;
                    disabledBg.a = 200;
                    disabledTxt.a = 200;
                    draw_rect(R, recRect, disabledBg);
                    draw_frame(R, recRect, g_panel_border);
                    int text_w = 0, text_h = 0;
                    measure_text("Record", &text_w, &text_h);
                    int text_x = recRect.x + (recRect.w - text_w) / 2;
                    int text_y = recRect.y + (recRect.h - text_h) / 2;
                    draw_text(R, text_x, text_y, "Record", disabledTxt);
                }
            }
            else
            {
                // Stop either MIDI recording or active export
                if (ui_button(R, recRect, "Stop", mx, my, mdown) && mclick)
                {
                    // If MIDI recording is active, stop and save - use helper function
                    MidiRecordFormatInfo stop_format_info = get_midi_record_format_info(g_midiRecordFormatIndex);

                    if (stop_format_info.type == MIDI_RECORD_FORMAT_MIDI)
                    {
                        if (!midi_record_stop())
                        {
                            set_status_message("Failed to finalize MIDI file");
                        }
                    }
                    else if (stop_format_info.type == MIDI_RECORD_FORMAT_WAV)
                    {
                        // WAV: if using our PCM writer, finalize it
                        if (g_pcm_wav_recording)
                        {
                            pcm_wav_finalize();
                        }
                        else if (g_exporting)
                        {
                            bae_stop_wav_export();
                        }
                        else
                        {
                            set_status_message("No WAV export in progress");
                        }
                    }
#if USE_FLAC_ENCODER != FALSE
                    else if (stop_format_info.type == MIDI_RECORD_FORMAT_FLAC)
                    {
                        // FLAC: if using our PCM writer, finalize it
                        if (g_pcm_flac_recording)
                        {
                            pcm_flac_finalize();
                        }
                        else if (g_exporting)
                        {
                            bae_stop_wav_export(); // Falls back to normal export stop
                        }
                        else
                        {
                            set_status_message("No FLAC export in progress");
                        }
                    }
#endif
                    else if (stop_format_info.type == MIDI_RECORD_FORMAT_MP3)
                    {
                        // MP3 format
                        if (g_midi_input_enabled && g_pcm_mp3_recording)
                        {
                            BAE_Platform_MP3Recorder_Stop();
                            g_pcm_mp3_recording = false;
                            g_midi_recording = false;
                            set_status_message("MP3 recording saved");
                        }
                        else if (g_exporting)
                        {
                            // BAEMixer-based export
                            bae_stop_wav_export();
                        }
                        else
                        {
                            set_status_message("No MP3 export in progress");
                        }
                    }
                    else if (stop_format_info.type == MIDI_RECORD_FORMAT_VORBIS)
                    {
#if USE_VORBIS_ENCODER == TRUE
                        // Vorbis: if using our PCM writer, finalize it
                        if (g_pcm_vorbis_recording)
                        {
                            pcm_vorbis_finalize();
                        }
                        else if (g_exporting)
                        {
                            bae_stop_wav_export(); // Generic export stop function
                        }
                        else
                        {
                            set_status_message("No Vorbis export in progress");
                        }
#else
            set_status_message("Vorbis not supported in this build");
#endif
                    }
                    else
                    {
                        // Unknown format - fallback
                        if (g_exporting)
                        {
                            bae_stop_wav_export();
                        }
                        else
                        {
                            set_status_message("No export in progress");
                        }
                    }
                    mclick = false;
                }
            }
        }
#endif

#ifdef SUPPORT_KARAOKE
        // Karaoke panel rendering (two lines: current + next)
        if (showKaraoke)
        {
            draw_rect(R, karaokePanel, panelBg);
            draw_frame(R, karaokePanel, panelBorder);
            if (g_lyric_mutex)
                SDL_LockMutex(g_lyric_mutex);
            const char *current = g_karaoke_line_current;
            const char *previous = g_karaoke_line_previous;
            const char *lastFrag = g_karaoke_last_fragment;
            int cw = 0, ch = 0, pw = 0, ph = 0;
            measure_text(current, &cw, &ch);
            measure_text(previous, &pw, &ph);
            int prevY = karaokePanel.y + 4;
            int curY = karaokePanel.y + karaokePanel.h / 2;
            int prevX = karaokePanel.x + (karaokePanel.w - pw) / 2;
            int curX = karaokePanel.x + (karaokePanel.w - cw) / 2;
            SDL_Color prevCol = g_text_color;
            prevCol.a = 180;
            draw_text(R, prevX, prevY, previous, prevCol);
            // Draw current line with only latest fragment highlighted
            if (current[0])
            {
                size_t curLen = strlen(current);
                size_t fragLen = lastFrag ? strlen(lastFrag) : 0;
                bool suffixMatch = (fragLen > 0 && fragLen <= curLen && strncmp(current + (curLen - fragLen), lastFrag, fragLen) == 0);
                if (suffixMatch && fragLen < curLen)
                {
                    size_t prefixLen = curLen - fragLen;
                    if (prefixLen >= sizeof(g_karaoke_last_fragment))
                        prefixLen = sizeof(g_karaoke_last_fragment) - 1; // reuse size cap
                    char prefixBuf[256];
                    if (prefixLen > sizeof(prefixBuf) - 1)
                        prefixLen = sizeof(prefixBuf) - 1;
                    memcpy(prefixBuf, current, prefixLen);
                    prefixBuf[prefixLen] = '\0';
                    int prefixW = 0, prefixH = 0;
                    measure_text(prefixBuf, &prefixW, &prefixH);
                    // Draw prefix in normal text color
                    draw_text(R, curX, curY, prefixBuf, g_text_color);
                    // Draw fragment highlighted
                    draw_text(R, curX + prefixW, curY, lastFrag, g_highlight_color);
                }
                else
                {
                    // Fallback highlight whole line (e.g., cumulative extension or no fragment info)
                    draw_text(R, curX, curY, current, g_highlight_color);
                }
            }
            if (g_lyric_mutex)
                SDL_UnlockMutex(g_lyric_mutex);
        }
#endif
#if SUPPORT_PLAYLIST == TRUE
        // Playlist panel - sync with currently playing file first
        if (g_playlist.enabled) {
            if (g_bae.song_loaded && g_bae.loaded_path[0])
            {
                playlist_update_current_file(g_bae.loaded_path);
            }
            playlist_render(R, playlistPanel, ui_mx, ui_my, ui_mdown, ui_mclick, ui_rclick, modal_block);
        }
#endif

        // Status panel
        draw_rect(R, statusPanel, panelBg);
        draw_frame(R, statusPanel, panelBorder);
        int statusBaseY = statusPanel.y + 10;
        draw_text(R, 20, statusBaseY, "STATUS & BANK", headerCol);
        int lineY1 = statusBaseY + 20;
        int lineY2 = statusBaseY + 40;
        int lineY3 = statusBaseY + 60;

        // Current file
        draw_text(R, 20, lineY1, "File:", labelCol);
        if (g_bae.song_loaded)
        {
            // Show just filename, not full path
            const char *fn = g_bae.loaded_path;
            const char *base = fn;
            for (const char *p = fn; *p; ++p)
            {
                if (*p == '/' || *p == '\\')
                    base = p + 1;
            }
            draw_text(R, 60, lineY1, base, g_highlight_color);
            // Tooltip hover region approximate width (mono 8px * len) like bank tooltip
            int textLen = (int)strlen(base);
            if (textLen < 1)
                textLen = 1;
            int approxW = textLen * 8;
            if (approxW > 480)
                approxW = 480;
            Rect fileTextRect = {60, lineY1, approxW, 16};
            if (!g_keyboard_channel_dd_open && point_in(ui_mx, ui_my, fileTextRect))
            {
                // Use full path as tooltip; if path equals base then show clarifying label
                char tip[512];
                if (strcmp(base, fn) == 0)
                {
                    snprintf(tip, sizeof(tip), "File: %s", fn);
                }
                else
                {
                    snprintf(tip, sizeof(tip), "%s", fn);
                }
                int tipLen = (int)strlen(tip);
                if (tipLen > 0)
                {
                    // Measure actual text width and height
                    int text_w, text_h;
                    measure_text(tip, &text_w, &text_h);

                    int tw = text_w + 8; // 4px padding on each side
                    int th = text_h + 8; // 4px padding top and bottom
                    if (tw > 480)
                        tw = 480; // Maximum width constraint

                    int tx = mx + 12;
                    int ty = my + 12;
                    if (tx + tw > WINDOW_W - 4)
                        tx = WINDOW_W - tw - 4;
                    if (ty + th > g_window_h - 4)
                        ty = g_window_h - th - 4;
                    ui_set_tooltip((Rect){tx, ty, tw, th}, tip, &g_file_tooltip_visible, &g_file_tooltip_rect, g_file_tooltip_text, sizeof(g_file_tooltip_text));
                }
            }
            else
            {
                ui_clear_tooltip(&g_file_tooltip_visible);
            }
        }
        else
        {
            // muted text for empty file
            SDL_Color muted = g_is_dark_mode ? (SDL_Color){150, 150, 150, 255} : (SDL_Color){120, 120, 120, 255};
            draw_text(R, 60, lineY1, "<none>", muted);
        }

        // Bank info with tooltip (friendly name shown, filename/path on hover)
        draw_text(R, 20, lineY2, "Bank:", labelCol);
        if (g_bae.bank_loaded)
        {
            // g_bae.bank_name now contains the display name (friendly name or filename)
            draw_text(R, 60, lineY2, g_bae.bank_name, g_highlight_color);
            // Simple tooltip region (approx width based on char count * 8px mono font)
            int textLen = (int)strlen(g_bae.bank_name);
            int approxW = textLen * 8;
            if (approxW < 8)
                approxW = 8;
            if (approxW > 400)
                approxW = 400; // crude clamp
            Rect bankTextRect = {60, lineY2, approxW, 16};
            // Prepare deferred tooltip drawing at end of frame (post status text)
            if (!g_keyboard_channel_dd_open && point_in(ui_mx, ui_my, bankTextRect))
            {
                char tip[512];
                // Show the full path in tooltip
                if (strcmp(g_current_bank_path, "__builtin__") == 0)
                {
                    snprintf(tip, sizeof(tip), "Built-in patches");
                }
                else
                {
                    snprintf(tip, sizeof(tip), "%s", g_current_bank_path);
                }
                int tipLen = (int)strlen(tip);
                if (tipLen > 0)
                {
                    // Measure actual text width and height
                    int text_w, text_h;
                    measure_text(tip, &text_w, &text_h);

                    int tw = text_w + 8; // 4px padding on each side
                    int th = text_h + 8; // 4px padding top and bottom
                    if (tw > 450)
                        tw = 450; // Maximum width constraint

                    int tx = mx + 12;
                    int ty = my + 12; // initial placement near cursor
                    if (tx + tw > WINDOW_W - 4)
                        tx = WINDOW_W - tw - 4;
                    if (ty + th > g_window_h - 4)
                        ty = g_window_h - th - 4;
                    ui_set_tooltip((Rect){tx, ty, tw, th}, tip, &g_bank_tooltip_visible, &g_bank_tooltip_rect, g_bank_tooltip_text, sizeof(g_bank_tooltip_text));
                }
            }
            else
            {
                ui_clear_tooltip(&g_bank_tooltip_visible);
            }
        }
        else
        {
            // Muted text: slightly darker in light mode for better contrast on pale panels
            SDL_Color muted = g_is_dark_mode ? (SDL_Color){150, 150, 150, 255} : (SDL_Color){80, 80, 80, 255};
            draw_text(R, 60, lineY2, "<none>", muted);
        }

        {
            int pad_local = 4;
            int btnH_local = 30;
            int metersW = 300; // leave room for buttons and labels at right
            int vuX = statusPanel.x + statusPanel.w - metersW - 20;
            if (metersW < 40)
                metersW = 40;
            int meterH = 12; // each meter height
            int spacing = 6;
            int vuY = statusPanel.y + statusPanel.h - pad_local - btnH_local - 12 - (meterH + spacing) * 2 + 3;

            // stacked meters (reuse existing sampling logic)
            // Avoid pulling frames from the mixer for VU sampling while we're
            // using the PCM WAV recorder (it consumes the same rendered frames).
            // Only call BAEMixer_GetAudioSampleFrame here when not exporting and
            // when the PCM writer is inactive to prevent draining audio.
#ifdef SUPPORT_MIDI_HW
            if (!g_exporting && !g_pcm_wav_recording && g_bae.mixer)
#else
    if (!g_exporting && g_bae.mixer)
#endif
            {
                short sL = 0, sR = 0, out = 0;
                if (BAEMixer_GetAudioSampleFrame(g_bae.mixer, &sL, &sR, &out) == BAE_NO_ERROR)
                {
                    // VU sampling: use the single-sample snapshot returned above to
                    // update smoothed VU levels. The PCM writer handles bulk
                    // captures elsewhere.
                    if (!g_stereo_output)
                    {
                        float mono = (fabsf((float)sL) + fabsf((float)sR)) * 0.5f / 32768.0f * g_vu_gain;
                        float v = sqrtf(MIN(1.0f, mono));
                        const float alpha = MAIN_VU_ALPHA;
                        g_vu_left_level = g_vu_left_level * (1.0f - alpha) + v * alpha;
                        g_vu_right_level = g_vu_right_level * (1.0f - alpha) + v * alpha;
                        Uint32 now = SDL_GetTicks();
                        int iv = (int)(v * 100.0f);
                        if (iv > g_vu_peak_left)
                        {
                            g_vu_peak_left = iv;
                            g_vu_peak_hold_until = now + 600;
                        }
                        if (iv > g_vu_peak_right)
                        {
                            g_vu_peak_right = iv;
                            g_vu_peak_hold_until = now + 600;
                        }
                        if (now > g_vu_peak_hold_until)
                        {
                            g_vu_peak_left = (int)(g_vu_left_level * 100.0f);
                            g_vu_peak_right = (int)(g_vu_right_level * 100.0f);
                        }
                    }
                    else
                    {
                        float rawL = fabsf((float)sL) / 32768.0f * g_vu_gain;
                        float rawR = fabsf((float)sR) / 32768.0f * g_vu_gain;
                        float fL = sqrtf(MIN(1.0f, rawL));
                        float fR = sqrtf(MIN(1.0f, rawR));
                        const float alpha = MAIN_VU_ALPHA;
                        g_vu_left_level = g_vu_left_level * (1.0f - alpha) + fL * alpha;
                        g_vu_right_level = g_vu_right_level * (1.0f - alpha) + fR * alpha;
                        Uint32 now = SDL_GetTicks();
                        int il = (int)(g_vu_left_level * 100.0f);
                        int ir = (int)(g_vu_right_level * 100.0f);
                        if (il > g_vu_peak_left)
                        {
                            g_vu_peak_left = il;
                            g_vu_peak_hold_until = now + 600;
                        }
                        if (ir > g_vu_peak_right)
                        {
                            g_vu_peak_right = ir;
                            g_vu_peak_hold_until = now + 600;
                        }
                        if (now > g_vu_peak_hold_until)
                        {
                            g_vu_peak_left = (int)(g_vu_left_level * 100.0f);
                            g_vu_peak_right = (int)(g_vu_right_level * 100.0f);
                        }
                    }
                }
            }
            else if (g_exporting)
            {
                const float decay = (1.0f - MAIN_VU_ALPHA); // keep exporting decay consistent with main smoothing (smoother)
                g_vu_left_level = g_vu_left_level * (1.0f - decay);
                g_vu_right_level = g_vu_right_level * (1.0f - decay);
                if (g_vu_left_level < 0.001f)
                    g_vu_left_level = 0.0f;
                if (g_vu_right_level < 0.001f)
                    g_vu_right_level = 0.0f;
            }

// Render stacked meters (same visuals as before)
#define METER_COLOR_FROM_LEVEL(v, outcol)  \
    do                                     \
    {                                      \
        float _t = (v);                    \
        if (_t < 0.f)                      \
            _t = 0.f;                      \
        if (_t > 1.f)                      \
            _t = 1.f;                      \
        SDL_Color _c;                      \
        _c.a = 255;                        \
        if (_t <= 0.6f)                    \
        {                                  \
            float u = _t / 0.6f;           \
            _c.r = (Uint8)(0 + u * 255);   \
            _c.g = (Uint8)(200);           \
            _c.b = 0;                      \
        }                                  \
        else                               \
        {                                  \
            float u = (_t - 0.6f) / 0.4f;  \
            _c.r = (Uint8)(255 - u * 55);  \
            _c.g = (Uint8)(200 - u * 160); \
            _c.b = 0;                      \
        }                                  \
        (outcol) = _c;                     \
    } while (0)
            SDL_Color trackBg = g_panel_bg;
            trackBg.a = 220;
            draw_rect(R, (Rect){vuX, vuY, metersW, meterH}, trackBg);
            draw_frame(R, (Rect){vuX, vuY, metersW, meterH}, g_panel_border);
            int leftFill = (int)(g_vu_left_level * (metersW - 6));
            if (leftFill < 0)
                leftFill = 0;
            if (leftFill > metersW - 6)
                leftFill = metersW - 6;
            // Draw a left-to-right gradient fill (green -> yellow -> red) for the left meter
            int innerX = vuX + 3;
            int innerW = metersW - 6;
            int innerY = vuY + 3;
            int innerH = meterH - 6;
            if (leftFill > 0)
            {
                for (int xoff = 0; xoff < leftFill; ++xoff)
                {
                    float frac = (float)xoff / (float)(innerW > 0 ? innerW : 1); // 0..1 left->right
                    SDL_Color col;
                    if (frac < 0.5f)
                    { // green -> yellow
                        float p = frac / 0.5f;
                        col.r = (Uint8)(g_highlight_color.r * p + 20 * (1.0f - p));
                        col.g = (Uint8)(200 * (1.0f - (1.0f - p) * 0.2f));
                        col.b = 20;
                    }
                    else
                    { // yellow -> red
                        float p = (frac - 0.5f) / 0.5f;
                        col.r = (Uint8)(200 + (55 * p));
                        col.g = (Uint8)(200 * (1.0f - p));
                        col.b = 20;
                    }
                    SDL_SetRenderDrawColor(R, col.r, col.g, col.b, 255);
                    SDL_RenderLine(R, innerX + xoff, innerY, innerX + xoff, innerY + innerH - 1);
                }
            }
            int pL = vuX + 3 + (int)((g_vu_peak_left / 100.0f) * (metersW - 6));
            if (pL < vuX + 3)
                pL = vuX + 3;
            if (pL > vuX + 3 + metersW - 6)
                pL = vuX + 3 + metersW - 6;
            // Draw white-ish peak marker similar to per-channel meters
            draw_rect(R, (Rect){pL - 1, vuY + 1, 2, meterH - 2}, (SDL_Color){255, 255, 255, 200});
            int vuY2 = vuY + meterH + spacing;
            draw_rect(R, (Rect){vuX, vuY2, metersW, meterH}, trackBg);
            draw_frame(R, (Rect){vuX, vuY2, metersW, meterH}, g_panel_border);
            int rightFill = (int)(g_vu_right_level * (metersW - 6));
            if (rightFill < 0)
                rightFill = 0;
            if (rightFill > metersW - 6)
                rightFill = metersW - 6;
            // Right meter gradient (same mapping as left)
            int innerX2 = vuX + 3;
            int innerW2 = metersW - 6;
            int innerY2 = vuY2 + 3;
            int innerH2 = meterH - 6;
            if (rightFill > 0)
            {
                for (int xoff = 0; xoff < rightFill; ++xoff)
                {
                    float frac = (float)xoff / (float)(innerW2 > 0 ? innerW2 : 1);
                    SDL_Color col;
                    if (frac < 0.5f)
                    {
                        float p = frac / 0.5f;
                        col.r = (Uint8)(g_highlight_color.r * p + 20 * (1.0f - p));
                        col.g = (Uint8)(200 * (1.0f - (1.0f - p) * 0.2f));
                        col.b = 20;
                    }
                    else
                    {
                        float p = (frac - 0.5f) / 0.5f;
                        col.r = (Uint8)(200 + (55 * p));
                        col.g = (Uint8)(200 * (1.0f - p));
                        col.b = 20;
                    }
                    SDL_SetRenderDrawColor(R, col.r, col.g, col.b, 255);
                    SDL_RenderLine(R, innerX2 + xoff, innerY2, innerX2 + xoff, innerY2 + innerH2 - 1);
                }
            }
            int pR = vuX + 3 + (int)((g_vu_peak_right / 100.0f) * (metersW - 6));
            if (pR < vuX + 3)
                pR = vuX + 3;
            if (pR > vuX + 3 + metersW - 6)
                pR = vuX + 3 + metersW - 6;
            draw_rect(R, (Rect){pR - 1, vuY2 + 1, 2, meterH - 2}, (SDL_Color){255, 255, 255, 200});
            int labelX = vuX + metersW + 6;
            SDL_Color labelCol = g_text_color;
            draw_text(R, labelX, vuY - 3, "L", labelCol);
            draw_text(R, labelX, vuY2 - 3, "R", labelCol);
#undef METER_COLOR_FROM_LEVEL
        }

        {
            int pad = 4; // panel-relative padding
            int btnW = 90;
            int btnH = 30;            // fixed size (uniform for Settings and bank buttons)
            int builtinW = btnW + 10; // make Default Bank button width appropriate for text
            // Anchor buttons to bottom-right corner of statusPanel
            int baseX = statusPanel.x + statusPanel.w - pad - btnW;
            int baseY = statusPanel.y + statusPanel.h - pad - btnH;
            // Settings button sits at baseX, baseY
            Rect settingsBtn = {baseX, baseY, btnW, btnH};
            // Spacing between buttons
            int gap = 8;
            // Default Bank immediately left of Settings (wider)
            Rect builtinBtn = {baseX - gap - builtinW, baseY, builtinW, btnH};
            // Load Bank to the left of Default Bank
            Rect loadBankBtn = {builtinBtn.x - gap - btnW, baseY, btnW, btnH};
            bool settingsEnabled = !g_reverbDropdownOpen;
            bool overSettings = settingsEnabled && point_in(ui_mx, ui_my, settingsBtn);
            SDL_Color sbg = settingsEnabled ? (overSettings ? g_button_hover : g_button_base) : g_button_base;
            if (!settingsEnabled)
            {
                sbg.a = 180;
            }
            if (g_show_settings_dialog)
                sbg = g_button_base;
            draw_rect(R, settingsBtn, sbg);
            draw_frame(R, settingsBtn, g_button_border);
            int tw = 0, th = 0;
            measure_text("Settings", &tw, &th);
            draw_text(R, settingsBtn.x + (settingsBtn.w - tw) / 2, settingsBtn.y + (settingsBtn.h - th) / 2, "Settings", g_button_text);
            if (settingsEnabled && !modal_block && ui_mclick && overSettings)
            {
                g_show_settings_dialog = !g_show_settings_dialog;
                if (g_show_settings_dialog)
                {
                    g_volumeCurveDropdownOpen = false;
                    g_show_rmf_info_dialog = false;
                }
            }

            // About button (left of Load Bank) - same size as settings
            Rect aboutBtn = {loadBankBtn.x - gap - btnW, baseY, btnW, btnH};
            draw_rect(R, aboutBtn, g_button_base);
            draw_frame(R, aboutBtn, g_button_border);
            int abtw = 0, abth = 0;
            measure_text("About", &abtw, &abth);
            draw_text(R, aboutBtn.x + (aboutBtn.w - abtw) / 2, aboutBtn.y + (aboutBtn.h - abth) / 2, "About", g_button_text);
            if (point_in(ui_mx, ui_my, aboutBtn) && ui_mclick && !modal_block)
            {
                g_show_about_dialog = !g_show_about_dialog;
                if (g_show_about_dialog)
                {
                    g_show_settings_dialog = false;
                    g_show_rmf_info_dialog = false;
                    g_about_page = 0;
                }
            }

            // Load Bank button (left of Settings). Label trimmed to "Load Bank" per request.
            if (ui_button(R, loadBankBtn, "Load Bank", ui_mx, ui_my, ui_mdown) && ui_mclick && !modal_block)
            {
                char fileBuf[1024] = {0};

#ifdef _WIN32
                OPENFILENAMEA ofn;
                ZeroMemory(&ofn, sizeof(ofn));
                ofn.lStructSize = sizeof(ofn);
                ofn.hwndOwner = NULL;
                ofn.lpstrFilter =
#if USE_SF2_SUPPORT == TRUE
    #if _USING_FLUIDSYNTH == TRUE
                    "Bank Files (*.hsb;*.sf2;*.sf3;*.sfo;*.dls)\0*.hsb;*.sf2;*.sf3;*.sfo;*.dls\0HSB Banks\0*.hsb\0SF2 SoundFonts\0*.sf2\0SF3 SoundFonts\0*.sf3\0SFO SoundFonts\0*.sfo\0DLS Banks\0*.dls\0All Files\0*.*\0"
    #else
        #if USE_VORBIS_DECODER == TRUE
                    "Bank Files (*.hsb;*.sf2;*.sf3;*.sfo)\0*.hsb;*.sf2;*.sf3;*.sfo\0HSB Banks\0*.hsb\0SF2 SoundFonts\0*.sf2\0SF3 SoundFonts\0*.sf3\0SFO SoundFonts\0*.sfo\0All Files\0*.*\0"
        #else
                    "Bank Files (*.hsb;*.sf2)\0*.hsb;*.sf2\0HSB Banks\0*.hsb\0SF2 SoundFonts\0*.sf2\0All Files\0*.*\0"
        #endif
    #endif
#else
                    "Bank Files (*.hsb)\0*.hsb\0HSB Banks\0*.hsb\0All Files\0*.*\0"
#endif
                    ;
                ofn.lpstrFile = fileBuf;
                ofn.nMaxFile = sizeof(fileBuf);
                ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
                ofn.lpstrDefExt = "hsb";
                if (GetOpenFileNameA(&ofn))
                    load_bank(fileBuf, playing, transpose, tempo, volume, loopPlay, reverbType, ch_enable, true);
#else
        const char *cmds[] = {
#if USE_SF2_SUPPORT == TRUE
    #if _USING_FLUIDSYNTH == TRUE
            "zenity --file-selection --title='Load Patch Bank' --file-filter='Bank Files | *.hsb *.sf2 *.sf3 *.sfo *.dls' 2>/dev/null",
            "kdialog --getopenfilename . '*.hsb *.sf2 *.sf3 *.sfo *.dls' 2>/dev/null",
            "yad --file-selection --title='Load Patch Bank' --file-filter='Bank Files | *.hsb *.sf2 *.sf3 *.sfo *.dls' 2>/dev/null",
    #else
        #if USE_VORBIS_DECODER == TRUE
            "zenity --file-selection --title='Load Patch Bank' --file-filter='Bank Files | *.hsb *.sf2 *.sf3 *.sfo' 2>/dev/null",
            "kdialog --getopenfilename . '*.hsb *.sf2 *.sf3 *.sfo' 2>/dev/null",
            "yad --file-selection --title='Load Patch Bank' --file-filter='Bank Files | *.hsb *.sf2 *.sf3 *.sfo' 2>/dev/null",
        #else
            "zenity --file-selection --title='Load Patch Bank' --file-filter='Bank Files | *.hsb *.sf2' 2>/dev/null",
            "kdialog --getopenfilename . '*.hsb *.sf2' 2>/dev/null",
            "yad --file-selection --title='Load Patch Bank' --file-filter='Bank Files | *.hsb *.sf2' 2>/dev/null",
        #endif
    #endif
#else
            "zenity --file-selection --title='Load Patch Bank' --file-filter='Bank Files | *.hsb' 2>/dev/null",
            "kdialog --getopenfilename . '*.hsb' 2>/dev/null",
            "yad --file-selection --title='Load Patch Bank' --file-filter='Bank Files | *.hsb' 2>/dev/null",
#endif
            NULL};            
        for (int ci = 0; cmds[ci]; ++ci)
        {
            FILE *p = popen(cmds[ci], "r");
            if (!p)
                continue;
            if (fgets(fileBuf, sizeof(fileBuf), p))
            {
                pclose(p);
                size_t l = strlen(fileBuf);
                while (l > 0 && (fileBuf[l - 1] == '\n' || fileBuf[l - 1] == '\r'))
                    fileBuf[--l] = '\0';
                if (l > 0)
                {
                    if ((l > 4 && strcasecmp(fileBuf + l - 4, ".hsb") == 0)
#if USE_SF2_SUPPORT == TRUE
                        || (l > 4 && strcasecmp(fileBuf + l - 4, ".sf2") == 0)
#if USE_VORBIS_DECODER == TRUE                        
                        || (l > 4 && strcasecmp(fileBuf + l - 4, ".sf3") == 0)
                        || (l > 4 && strcasecmp(fileBuf + l - 4, ".sfo") == 0)
#endif                        
#if _USING_FLUIDSYNTH == TRUE
                        || (l > 4 && strcasecmp(fileBuf + l - 4, ".dls") == 0)
#endif
#endif
                    )
                    {
                        load_bank(fileBuf, playing, transpose, tempo, volume, loopPlay, reverbType, ch_enable, true);
                    }
                    else
                    {
#if USE_SF2_SUPPORT == TRUE
    #if _USING_FLUIDSYNTH == TRUE
                        BAE_PRINTF("Not a bank file (.hsb, .sf2, .sf3, .sfo, or .dls): %s\n", fileBuf);
    #else
        #if USE_VORBIS_DECODER == TRUE                        
                        BAE_PRINTF("Not a bank file (.hsb, .sf2, .sf3, or .sfo): %s\n", fileBuf);
        #else
                        BAE_PRINTF("Not a bank file (.hsb or .sf2): %s\n", fileBuf);
        #endif
    #endif
#else
                        BAE_PRINTF("Not a bank file (.hsb): %s\n", fileBuf);
#endif
                    }
                }
                break;
            }
            pclose(p);
        }
#endif
            }

            // Default Bank button (right of Load Bank)
            bool defaultBankExists = false;
#if defined(_WIN32) || defined(__linux__) || defined(__APPLE__)
            // Check if we should show the Default Bank button

            // Check if patches.hsb exists in executable directory

            char exe_dir[512];
            char patches_path[1024];
            get_executable_directory(exe_dir, sizeof(exe_dir));
#ifdef _WIN32
            snprintf(patches_path, sizeof(patches_path), "%s\\patches.hsb", exe_dir);
#else
            snprintf(patches_path, sizeof(patches_path), "%s/patches.hsb", exe_dir);
#endif
            FILE *test_file = fopen(patches_path, "r");
            if (test_file)
            {
                fclose(test_file);
                defaultBankExists = true;
            }

#endif

            bool default_loaded = false;
            bool builtin_loaded = false;
            if (defaultBankExists)
            {
                default_loaded = (g_current_bank_path[0] && strcmp(g_current_bank_path, patches_path) == 0);
            }
#if _BUILT_IN_PATCHES == TRUE
            builtin_loaded = (g_current_bank_path[0] && strcmp(g_current_bank_path, "__builtin__") == 0);
#endif

            bool builtinEnabled = !modal_block && !g_reverbDropdownOpen;
            bool overBuiltin = builtinEnabled && point_in(ui_mx, ui_my, builtinBtn);
            SDL_Color bbg = builtinEnabled ? (overBuiltin ? g_button_hover : g_button_base) : g_button_base;
            if (!builtinEnabled)
                bbg.a = 180;
            draw_rect(R, builtinBtn, bbg);
            draw_frame(R, builtinBtn, g_button_border);
            int btw = 0, bth = 0;
            measure_text("Default Bank", &btw, &bth);
            draw_text(R, builtinBtn.x + (builtinBtn.w - btw) / 2, builtinBtn.y + (builtinBtn.h - bth) / 2, "Default Bank", g_button_text);
            if (builtinEnabled && ui_mclick && overBuiltin)
            {
                if (defaultBankExists && !default_loaded)
                {
                    if (!load_bank(patches_path, playing, transpose, tempo, volume, loopPlay, reverbType, ch_enable, true))
                    {
                        set_status_message("Failed to load default bank");
                    }
#if _BUILT_IN_PATCHES == TRUE
                }
                else if (!builtin_loaded && !defaultBankExists)
                {
                    if (!load_bank("__builtin__", playing, transpose, tempo, volume, loopPlay, reverbType, ch_enable, true))
                    {
                        set_status_message("Failed to load default bank");
                    }
#endif
                }
            }
            // Right-click loads built-in bank (if available) regardless of default bank existence
#if _BUILT_IN_PATCHES == TRUE
            if (overBuiltin && ui_rclick && !builtin_loaded)
            {
                if (!load_bank("__builtin__", playing, transpose, tempo, volume, loopPlay, reverbType, ch_enable, true))
                {
                    set_status_message("Failed to load built-in bank");
                }
            }
#endif
        }

        // Status indicator (use theme-safe highlight color for playing state)
        const char *status;
        SDL_Color statusCol;
#ifdef SUPPORT_MIDI_HW
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
#ifdef SUPPORT_MIDI_HW
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
            draw_text(R, 120, lineY3, "(Drag & drop media/bank files here)", muted);
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
                    SDL_RenderLine(R, x1, y1, x2, y2);
                    SDL_RenderLine(R, x2, y2, x3, y3);
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

        // Draw LSB tooltip
        if (g_lsb_tooltip_visible)
        {
            int tooltip_w = 0, tooltip_h = 0;
            measure_text(g_lsb_tooltip_text, &tooltip_w, &tooltip_h);
            Rect tipRect = {g_lsb_tooltip_rect.x, g_lsb_tooltip_rect.y, tooltip_w + 8, tooltip_h + 8};
            ui_draw_tooltip(R, tipRect, g_lsb_tooltip_text, false, false);
        }

        // Draw MSB tooltip
        if (g_msb_tooltip_visible)
        {
            int tooltip_w = 0, tooltip_h = 0;
            measure_text(g_msb_tooltip_text, &tooltip_w, &tooltip_h);
            Rect tipRect = {g_msb_tooltip_rect.x, g_msb_tooltip_rect.y, tooltip_w + 8, tooltip_h + 8};
            ui_draw_tooltip(R, tipRect, g_msb_tooltip_text, false, false);
        }

        // Render dropdown list on top of everything else if open
        if (g_reverbDropdownOpen)
        {
            int reverbCount = get_reverb_count();
            Rect ddRect = {690, 40, 160, 24}; // Moved up 20 pixels from y=60 to y=40

            // Draw the dropdown list using theme globals
            int itemH = ddRect.h;
            int totalH = itemH * reverbCount;
            Rect box = {ddRect.x, ddRect.y + ddRect.h + 1, ddRect.w, totalH};
            draw_rect(R, box, g_panel_bg);
            draw_frame(R, box, g_panel_border);

            for (int i = 0; i < reverbCount; i++)
            {
                Rect ir = {box.x, box.y + i * itemH, box.w, itemH};
                bool over = point_in(mx, my, ir);
                SDL_Color ibg = ((i + 1) == reverbType) ? g_highlight_color : g_panel_bg;
                if (over)
                    ibg = g_button_hover;
                draw_rect(R, ir, ibg);
                if (i < reverbCount - 1)
                { // separator line
                    SDL_Color sep = g_panel_border;
                    sep.a = 255; // use panel border as separator
                    SDL_SetRenderDrawColor(R, sep.r, sep.g, sep.b, sep.a);
                    SDL_RenderLine(R, ir.x, ir.y + ir.h, ir.x + ir.w, ir.y + ir.h);
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
                    bae_set_reverb(reverbType);
                    // Save settings when reverb is changed
                    if (g_current_bank_path[0] != '\0')
                    {
                        save_settings(g_current_bank_path, reverbType, loopPlay);
                    }
                }
            }

            // Click outside closes without change
            if (mclick && !point_in(mx, my, ddRect) && !point_in(mx, my, box))
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
            char chanBuf[16][8];
            for (int i = 0; i < 16; i++)
            {
                snprintf(chanBuf[i], sizeof(chanBuf[i]), "Ch %d", i + 1);
            }
            for (int i = 0; i < 16; i++)
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
                    // Update MSB/LSB display values for the newly selected channel
                    update_msb_lsb_for_channel();
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

        // Settings dialog rendering moved to gui_settings.c
        if (g_show_settings_dialog)
        {
            render_settings_dialog(R, mx, my, mclick, mdown,
                                   &transpose, &tempo, &volume, &loopPlay,
                                   &reverbType, ch_enable, &progress, &duration, &playing);
        }

        if (g_show_about_dialog)
        {
            render_about_dialog(R, mx, my, mclick);
        }

        // Render export dropdown when Settings dialog is open and the export dropdown was triggered there
#if USE_MPEG_ENCODER != FALSE
        if (g_show_settings_dialog && g_exportDropdownOpen)
        {
            // expRect defined in settings dialog: position dropdown beneath it
            // Compute using same dialog math as the settings dialog so dropdown aligns with the control
            int dlgW = 560;
            int dlgH = 280; // must match settings dialog above (wider)
            int pad = 10;
            int controlW = 150;
            int dlgX = (WINDOW_W - dlgW) / 2;
            int dlgY = (g_window_h - dlgH) / 2;
            int colW = (dlgW - pad * 3) / 2;
            int leftX = dlgX + pad;
            int controlRightX = leftX + colW - controlW;
            Rect expRect = {controlRightX, dlgY + 104, controlW, 24};
            int codecCount = g_exportCodecCount;
            int cols = 2;
            int rows = (codecCount + cols - 1) / cols;
            int gapX = 6;
            int itemH = expRect.h;
            int itemW = expRect.w;
            int boxW = itemW * cols + gapX * (cols - 1);
            int boxH = itemH * rows;
            Rect box = {expRect.x, expRect.y + expRect.h + 1, boxW, boxH};
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
                    SDL_RenderLine(R, ir.x, ir.y + ir.h, ir.x + ir.w, ir.y + ir.h);
                }
                draw_text(R, ir.x + 6, ir.y + 6, g_exportCodecNames[i], g_button_text);
                if (over && mclick)
                {
                    int oldExportIdx = g_exportCodecIndex;
                    g_exportCodecIndex = i;
                    g_exportDropdownOpen = false;
                    if (oldExportIdx != g_exportCodecIndex)
                    {
                        // Persist user's chosen export codec so it survives restarts
                        save_settings(g_current_bank_path[0] ? g_current_bank_path : NULL, reverbType, loopPlay);
                    }
                }
            }
            // Close dropdown if clicked outside
            if (mclick && !point_in(mx, my, box) && !point_in(mx, my, expRect))
                g_exportDropdownOpen = false;
        }
#endif
#if SUPPORT_MIDI_HW == TRUE
#endif
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
                    for (int n = 0; n < 128; n++)
                    {
                        if (g_keyboard_active_notes[n]) {
                            BAESong_NoteOff(target, (unsigned char)g_keyboard_channel, (unsigned char)n, 0, 0);
                        }
                    }
                }
                g_keyboard_mouse_note = -1;
                memset(g_keyboard_active_notes, 0, sizeof(g_keyboard_active_notes));
                g_keyboard_suppress_until = SDL_GetTicks() + 250;
            }
        }
        SDL_RenderPresent(R);
        Uint64 freq = SDL_GetPerformanceFrequency();
        Uint64 frame_end = SDL_GetPerformanceCounter();
        float elapsed_ms = (frame_end - frame_start) * 1000.0f / freq;

        if (elapsed_ms < FRAME_TIME_MS) {
            SDL_Delay((Uint32)(FRAME_TIME_MS - elapsed_ms));
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
            else
            {
                // Get original song duration from BAE and calculate tempo-adjusted duration
                // Higher tempo = shorter duration, lower tempo = longer duration
                // Formula: adjusted_duration = original_duration * (100 / tempo_percent)
                if (g_bae.song)
                {
                    uint32_t original_length_us = 0;
                    BAESong_GetMicrosecondLength(g_bae.song, &original_length_us);
                    int original_duration_ms = (int)(original_length_us / 1000UL);
                    int old_duration = duration;
                    duration = (int)((double)original_duration_ms * (100.0 / (double)tempo));
                    g_bae.song_length_us = duration * 1000UL;
                    // Calculate progress bar position based on percentage through song
                    // without seeking BAE - preserve relative position
                    if (old_duration > 0)
                    {
                        float percent_through = (float)progress / (float)old_duration;
                        progress = (int)(percent_through * duration);
                    }
                }
                if (g_bae.preserve_position_on_next_start && g_bae.preserved_start_position_us)
                {
                    uint32_t us = g_bae.preserved_start_position_us;
                    uint32_t newus = (uint32_t)((double)us * (100.0 / (double)tempo));
                    g_bae.preserved_start_position_us = newus;
                }
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
#ifdef SUPPORT_MIDI_HW
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
        save_full_settings(&current_settings);
    }

    // Auto-save playlist to application directory
    get_executable_directory(exe_dir, sizeof(exe_dir));
#if SUPPORT_PLAYLIST == TRUE // Initialize playlist system

#ifdef _WIN32
    snprintf(playlist_path, sizeof(playlist_path), "%s\\playlist.m3u", exe_dir);
#else
    snprintf(playlist_path, sizeof(playlist_path), "%s/playlist.m3u", exe_dir);
#endif

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
#endif

    SDL_DestroyRenderer(R);
    SDL_DestroyWindow(win);
    g_main_window = NULL; // Clear global reference
    bae_shutdown();
#if SUPPORT_PLAYLIST == TRUE
    playlist_cleanup();
#endif
    if (g_font)
        TTF_CloseFont(g_font);
    TTF_Quit();
    SDL_Quit();
    return 0;
}