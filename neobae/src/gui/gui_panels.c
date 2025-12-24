#include "gui_panels.h"
#include "gui_common.h"
#include "NeoBAE.h"
#include "gui_bae.h"
#include "gui_midi_vkbd.h" // for g_show_virtual_keyboard and keyboard globals
#include "gui_theme.h"
#include "gui_text.h"
#include "gui_widgets.h"
#if SUPPORT_KARAOKE == TRUE
#include "gui_karaoke.h"
#endif
// Dialog and export flags
#include "gui_settings.h"
#include "gui_dialogs.h"
#include "gui_export.h"

const int16_t g_max_bank = 128;
const int16_t g_max_program = 127;

// send_bank_select_for_current_channel is defined in gui_main.c
extern void send_bank_select_for_current_channel(void);

bool ui_modal_blocking(void)
{
    // Mirror the modal blocking condition used throughout gui_main.c
    if (g_show_settings_dialog || g_show_about_dialog || (g_show_rmf_info_dialog && g_bae.is_rmf_file) || g_exporting)
        return true;
    return false;
}

void change_bank_value_for_current_channel(bool is_bank, int delta)
{
    if (is_bank)
    {
        g_keyboard_bank += delta;
        if (g_keyboard_bank < 0)
            g_keyboard_bank = g_max_bank;
        if (g_keyboard_bank > g_max_bank)
            g_keyboard_bank = 0;
    }
    else
    {
        g_keyboard_program += delta;
        if (g_keyboard_program < 0)
            g_keyboard_program = g_max_program;
        if (g_keyboard_program > g_max_program)
            g_keyboard_program = 0;
    }
    send_bank_select_for_current_channel();
}

// Generic tooltip helper: set tooltip visible, copy text safely and set rect
void ui_set_tooltip(Rect r, const char *text, bool *visible_ptr, Rect *rect_ptr, char *text_buf, size_t text_buf_len)
{
    if (!visible_ptr || !rect_ptr || !text_buf || !text)
        return;
    *rect_ptr = r;
    strncpy(text_buf, text, text_buf_len - 1);
    text_buf[text_buf_len - 1] = '\0';
    *visible_ptr = true;
}

void ui_clear_tooltip(bool *visible_ptr)
{
    if (!visible_ptr)
        return;
    *visible_ptr = false;
}

// Centralized tooltip drawing used by gui_main.c
void ui_draw_tooltip(SDL_Renderer *R, Rect tipRect, const char *text, bool center_vertically, bool use_panel_border)
{
    if (!text)
        return;
    SDL_Color shadow = {0, 0, 0, g_is_dark_mode ? 140 : 100};
    Rect shadowRect = {tipRect.x + 2, tipRect.y + 2, tipRect.w, tipRect.h};
    draw_rect(R, shadowRect, shadow);
    SDL_Color tbg;
    if (g_is_dark_mode)
    {
        int r = g_panel_bg.r + 25;
        if (r > 255)
            r = 255;
        int g = g_panel_bg.g + 25;
        if (g > 255)
            g = 255;
        int b = g_panel_bg.b + 25;
        if (b > 255)
            b = 255;
        tbg = (SDL_Color){(Uint8)r, (Uint8)g, (Uint8)b, 255};
    }
    else
    {
        // Slightly different defaults used by some tooltips; leave caller to adjust tipRect if needed
        tbg = (SDL_Color){255, 255, 225, 255};
    }
    SDL_Color tbd = use_panel_border ? g_panel_border : g_button_border;
    SDL_Color tfg = g_is_dark_mode ? g_text_color : (SDL_Color){32, 32, 32, 255};
    draw_rect(R, tipRect, tbg);
    draw_frame(R, tipRect, tbd);

    int text_w = 0, text_h = 0;
    measure_text(text, &text_w, &text_h);
    int text_y = center_vertically ? (tipRect.y + (tipRect.h - text_h) / 2) : (tipRect.y + 4);
    draw_text(R, tipRect.x + 4, text_y, text, tfg);
}

// Reverb names shared across the UI. Centralize to avoid duplicates.
#if USE_NEO_EFFECTS
static const char *kReverbNames[] = {"None", "Igor's Closet", "Igor's Garage", "Igor's Acoustic Lab", "Igor's Cavern", "Igor's Dungeon", "Small Reflections", "Early Reflections", "Basement", "Banquet Hall", "Catacombs", "Neo Room", "Neo Hall", "Neo Tap Delay"};
#else
static const char *kReverbNames[] = {"None", "Igor's Closet", "Igor's Garage", "Igor's Acoustic Lab", "Igor's Cavern", "Igor's Dungeon", "Small Reflections", "Early Reflections", "Basement", "Banquet Hall", "Catacombs"};
#endif

int get_reverb_count(void)
{
    int reverbCount = (int)(sizeof(kReverbNames) / sizeof(kReverbNames[0]));
    if (reverbCount > (BAE_REVERB_TYPE_COUNT - 1))
        reverbCount = (BAE_REVERB_TYPE_COUNT - 1);
    return reverbCount;
}

const char *get_reverb_name(int idx)
{
    int rc = get_reverb_count();
    if (idx < 0 || idx >= rc)
        return "?";
    return kReverbNames[idx];
}

void compute_ui_layout(UiLayout *L)
{
    if (!L)
        return;
    // Transport panel
    L->transportPanel = (Rect){10, 160, 880, 85};
    int keyboardPanelY = L->transportPanel.y + L->transportPanel.h + 10;
    L->chanDD = (Rect){10 + 10, keyboardPanelY + 28, 90, 22};
    L->ddRect = (Rect){687, 43, 160, 24};

    // Keyboard panel
    L->keyboardPanel = (Rect){10, keyboardPanelY, 880, 110};

    // Bank/Program picker positions inside keyboard panel (match rendering math)
    int picker_y = L->keyboardPanel.y + 56; // below channel dropdown
    int picker_w = 35;
    int picker_h = 18;
    int spacing = 5;
    L->bankRect = (Rect){L->keyboardPanel.x + 10, picker_y, picker_w, picker_h};
    L->programRect = (Rect){L->bankRect.x + picker_w + spacing, picker_y, picker_w, picker_h};

#if SUPPORT_PLAYLIST == TRUE
    // Playlist panel
    L->playlistPanelHeight = 300;
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
    int statusY_local = ((showKeyboard_local || showWaveform_local) ? (L->keyboardPanel.y + L->keyboardPanel.h + 10) : (L->transportPanel.y + L->transportPanel.h + 10));
#if SUPPORT_KARAOKE == TRUE
    if (showKaraoke_local)
        statusY_local = statusY_local + karaokePanelHeight_local + 5;
#endif
    int playlistPanelY = statusY_local;
    L->playlistPanel = (Rect){10, playlistPanelY, 880, L->playlistPanelHeight};
#endif
}

// Helpers to centralize slider adjustments used by wheel and keyboard handlers.
// Return true if the event was handled (mouse/key was over the control).
bool ui_adjust_transpose(int mx, int my, int delta, bool playback_controls_enabled_local, int *transpose_ptr)
{
    if (!playback_controls_enabled_local)
        return false;
    Rect r = (Rect){410, 63, 160, 14};
    if (!point_in(mx, my, r))
        return false;
    int nt = (transpose_ptr ? *transpose_ptr : 0) + delta;
    if (nt < -24)
        nt = -24;
    if (nt > 24)
        nt = 24;
    if (transpose_ptr && nt != *transpose_ptr)
    {
        *transpose_ptr = nt;
        bae_set_transpose(*transpose_ptr);
    }
    return true;
}

bool ui_adjust_tempo(int mx, int my, int delta, bool playback_controls_enabled_local, int *tempo_ptr, int *duration_out, int *progress_out)
{
    if (!playback_controls_enabled_local)
        return false;
    Rect r = (Rect){410, 103, 160, 14};
    if (!point_in(mx, my, r))
        return false;
    int nt = (tempo_ptr ? *tempo_ptr : 0) + delta;
    if (nt < 25)
        nt = 25;
    if (nt > 200)
        nt = 200;
    if (tempo_ptr && nt != *tempo_ptr)
    {
        *tempo_ptr = nt;
        bae_set_tempo(*tempo_ptr);
        if (g_bae.song)
        {
            uint32_t original_length_us = 0;
            BAESong_GetMicrosecondLength(g_bae.song, &original_length_us);
            int original_duration_ms = (int)(original_length_us / 1000UL);
            int old_duration = (duration_out ? *duration_out : (int)(g_bae.song_length_us / 1000UL));
            int new_duration = (int)((double)original_duration_ms * (100.0 / (double)(tempo_ptr ? *tempo_ptr : 100)));
            if (duration_out)
                *duration_out = new_duration;
            else
                ; // caller didn't supply duration_out; nothing to update locally
            g_bae.song_length_us = (uint32_t)(new_duration * 1000UL);
            if (old_duration > 0)
            {
                int current_progress = (progress_out ? *progress_out : 0);
                float percent_through = (float)current_progress / (float)old_duration;
                int newprog = (int)(percent_through * new_duration);
                if (progress_out)
                    *progress_out = newprog;
                else
                    ; // caller didn't supply progress_out; nothing to update
            }
        }
        if (g_bae.preserve_position_on_next_start && g_bae.preserved_start_position_us)
        {
            uint32_t us = g_bae.preserved_start_position_us;
            uint32_t newus = (uint32_t)((double)us * (100.0 / (double)(tempo_ptr ? *tempo_ptr : 100)));
            g_bae.preserved_start_position_us = newus;
        }
    }
    return true;
}

bool ui_adjust_volume(int mx, int my, int delta, bool volume_enabled_local, int *volume_ptr)
{
    if (!volume_enabled_local)
        return false;
    Rect r = (Rect){687, 103, 160, 14};
    if (!point_in(mx, my, r))
        return false;
    int nt = (volume_ptr ? *volume_ptr : 0) + delta;
    if (nt < 0)
        nt = 0;
    if (nt > NEW_MAX_VOLUME_PCT)
        nt = NEW_MAX_VOLUME_PCT;
    if (volume_ptr && nt != *volume_ptr)
    {
        *volume_ptr = nt;
        bae_set_volume(*volume_ptr);
    }
    return true;
}
