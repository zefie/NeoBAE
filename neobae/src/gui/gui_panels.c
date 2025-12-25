#include "gui_panels.h"
#include "gui_common.h"
#include "NeoBAE.h"
#include "GenPriv.h"
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

// Custom reverb modal state
bool g_show_custom_reverb_dialog = false;
bool g_custom_reverb_button_visible = false;

// Bump this to force the custom reverb dialog to refresh its cached slider values
int g_custom_reverb_dialog_sync_serial = 0;

// Mouse wheel ticks captured while the custom reverb dialog is open.
int g_custom_reverb_wheel_delta = 0;

// send_bank_select_for_current_channel is defined in gui_main.c
extern void send_bank_select_for_current_channel(void);

bool ui_modal_blocking(void)
{
    // Mirror the modal blocking condition used throughout gui_main.c
    if (g_show_settings_dialog || g_show_about_dialog || (g_show_rmf_info_dialog && g_bae.is_rmf_file) || g_exporting || g_show_custom_reverb_dialog)
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
static const char *kReverbNames[] = {"None", "Igor's Closet", "Igor's Garage", "Igor's Acoustic Lab", "Igor's Cavern", "Igor's Dungeon", "Small Reflections", "Early Reflections", "Basement", "Banquet Hall", "Catacombs", "Neo Room", "Neo Hall", "Neo Tap Delay", "Custom"};
#else
static const char *kReverbNames[] = {"None", "Igor's Closet", "Igor's Garage", "Igor's Acoustic Lab", "Igor's Cavern", "Igor's Dungeon", "Small Reflections", "Early Reflections", "Basement", "Banquet Hall", "Catacombs"};
#endif

int get_reverb_count(void)
{
    int reverbCount = (int)(sizeof(kReverbNames) / sizeof(kReverbNames[0]));
    if (reverbCount > (BAE_REVERB_TYPE_COUNT - 1))
        reverbCount = (BAE_REVERB_TYPE_COUNT - 1);
    
#if USE_NEO_EFFECTS
    // Add custom presets to the count
    extern int g_custom_reverb_preset_count;
    reverbCount += g_custom_reverb_preset_count;
#endif
    
    return reverbCount;
}

const char *get_reverb_name(int idx)
{
    int base_count = (int)(sizeof(kReverbNames) / sizeof(kReverbNames[0]));
    if (base_count > (BAE_REVERB_TYPE_COUNT - 1))
        base_count = (BAE_REVERB_TYPE_COUNT - 1);
    
    if (idx < 0 || idx >= get_reverb_count())
        return "?";
    
#if USE_NEO_EFFECTS
    // Check if this is a custom preset
    extern CustomReverbPreset *g_custom_reverb_presets;
    extern int g_custom_reverb_preset_count;
    if (idx >= base_count && idx < base_count + g_custom_reverb_preset_count)
    {
        return g_custom_reverb_presets[idx - base_count].name;
    }
#endif
    
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
    L->ddRect = (Rect){687, 38, 160, 24};

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

// Custom reverb dialog rendering and logic
void render_custom_reverb_dialog(SDL_Renderer *R, int mx, int my, bool mclick, bool mdown, int window_h)
{
    // Static state to cache values and avoid constant Get/Set every frame
    static bool initialized = false;
    static int last_sync_serial = -1;
    static int cached_comb_count = 4;
    static int cached_delays[8] = {50, 75, 100, 125, 150, 175, 200, 225};
    static int cached_feedback[8] = {90, 90, 90, 90, 90, 90, 90, 90};
    static int cached_gain[8] = {127, 127, 127, 127, 127, 127, 127, 127};
    static int cached_lowpass = 64;

    // Consume pending wheel ticks for this frame (so they can't apply later after the mouse moves).
    int wheel = g_custom_reverb_wheel_delta;
    g_custom_reverb_wheel_delta = 0;
    bool wheel_used = false;
    
    if (!g_show_custom_reverb_dialog)
    {
        initialized = false;  // Reset when dialog closes
        last_sync_serial = -1;
        return;
    }

    // Dim background
    SDL_Color dim = g_is_dark_mode ? (SDL_Color){0, 0, 0, 160} : (SDL_Color){0, 0, 0, 120};
    draw_rect(R, (Rect){0, 0, WINDOW_W, window_h}, dim);

    // Dialog dimensions
    int dlgW = 480;
    int dlgH = 650;
    int pad = 10;
    Rect dlg = {(WINDOW_W - dlgW) / 2, (window_h - dlgH) / 2, dlgW, dlgH};

    SDL_Color dlgBg = g_panel_bg;
    dlgBg.a = 250;
    SDL_Color dlgFrame = g_panel_border;

    draw_rect(R, dlg, dlgBg);
    draw_frame(R, dlg, dlgFrame);

    // Title
    draw_text(R, dlg.x + pad, dlg.y + 8, "Custom Reverb Settings", g_header_color);

    // Close button
    Rect closeBtn = {dlg.x + dlg.w - 22, dlg.y + 6, 16, 16};
    bool overClose = point_in(mx, my, closeBtn);
    draw_rect(R, closeBtn, overClose ? g_button_hover : g_button_base);
    draw_frame(R, closeBtn, g_button_border);
    draw_text(R, closeBtn.x + 4, closeBtn.y - 1, "X", g_button_text);

    if (mclick && overClose)
    {
        g_show_custom_reverb_dialog = false;
        return;
    }

    // Layout
    int labelX = dlg.x + pad + 10;
    int sliderX = dlg.x + pad + 150;
    int sliderW = dlgW - 170 - 40;
    int sliderH = 16;
    int rowH = 50;
    int startY = dlg.y + 60;
    
    // Initialize cached values from tracked state once, or when presets change
    if (!initialized || last_sync_serial != g_custom_reverb_dialog_sync_serial)
    {
        extern int g_current_custom_reverb_comb_count;
        extern int g_current_custom_reverb_delays[MAX_NEO_COMBS];
        extern int g_current_custom_reverb_feedback[MAX_NEO_COMBS];
        extern int g_current_custom_reverb_gain[MAX_NEO_COMBS];
        extern int g_current_custom_reverb_lowpass;

        // If state hasn't been initialized yet (startup edge), fall back to current engine values.
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

        cached_comb_count = g_current_custom_reverb_comb_count;
        for (int i = 0; i < MAX_NEO_COMBS; i++)
        {
            cached_delays[i] = g_current_custom_reverb_delays[i];
            cached_feedback[i] = g_current_custom_reverb_feedback[i];
            cached_gain[i] = g_current_custom_reverb_gain[i];
        }
        cached_lowpass = g_current_custom_reverb_lowpass;
        initialized = true;
        last_sync_serial = g_custom_reverb_dialog_sync_serial;
    }
    
    // Number of Comb Filters slider
    int y = startY;
    draw_text(R, labelX, y + 4, "Comb Count:", g_text_color);
    Rect combCountSlider = {sliderX, y, sliderW, sliderH};
    int old_count = cached_comb_count;
    ui_slider(R, combCountSlider, &cached_comb_count, 1, MAX_NEO_COMBS, mx, my, mdown, false);
    if (!wheel_used && wheel != 0 && point_in(mx, my, combCountSlider))
    {
        cached_comb_count += wheel;
        if (cached_comb_count < 1)
            cached_comb_count = 1;
        if (cached_comb_count > MAX_NEO_COMBS)
            cached_comb_count = MAX_NEO_COMBS;
        wheel_used = true;
    }
    if (cached_comb_count != old_count)
    {
        SetNeoCustomReverbCombCount(cached_comb_count);
        extern int g_current_custom_reverb_comb_count;
        g_current_custom_reverb_comb_count = cached_comb_count;
    }
    
    char countBuf[32];
    snprintf(countBuf, sizeof(countBuf), "%d", cached_comb_count);
    draw_text(R, sliderX + sliderW + 8, y + 2, countBuf, g_text_color);
    
    y += rowH;
    
    // Per-comb settings (show first 4 combs to fit in dialog)
    int maxDisplay = (cached_comb_count < 4) ? cached_comb_count : 4;
    for (int i = 0; i < maxDisplay; i++)
    {
        char label[64];
        
        // Comb label
        snprintf(label, sizeof(label), "Comb %d", i + 1);
        draw_text(R, dlg.x + pad, y, label, g_text_color);
        y += 25;
        
        // Delay
        snprintf(label, sizeof(label), "  Delay (ms):");
        draw_text(R, labelX + 10, y + 4, label, g_text_color);
        Rect delaySlider = {sliderX, y, sliderW - 50, sliderH};
        int old_delay = cached_delays[i];
        ui_slider(R, delaySlider, &cached_delays[i], 1, NEO_CUSTOM_MAX_DELAY_MS, mx, my, mdown, false);
        if (!wheel_used && wheel != 0 && point_in(mx, my, delaySlider))
        {
            cached_delays[i] += wheel;
            if (cached_delays[i] < 1)
                cached_delays[i] = 1;
            if (cached_delays[i] > NEO_CUSTOM_MAX_DELAY_MS)
                cached_delays[i] = NEO_CUSTOM_MAX_DELAY_MS;
            wheel_used = true;
        }
        if (cached_delays[i] != old_delay)
        {
            SetNeoCustomReverbCombDelay(i, cached_delays[i]);
            extern int g_current_custom_reverb_delays[MAX_NEO_COMBS];
            g_current_custom_reverb_delays[i] = cached_delays[i];
        }
        snprintf(label, sizeof(label), "%d ms", cached_delays[i]);
        draw_text(R, sliderX + sliderW - 40, y + 2, label, g_text_color);
        y += 28;
        
        // Feedback
        snprintf(label, sizeof(label), "  Feedback:");
        draw_text(R, labelX + 10, y + 4, label, g_text_color);
        Rect feedbackSlider = {sliderX, y, sliderW - 50, sliderH};
        int old_feedback = cached_feedback[i];
        ui_slider(R, feedbackSlider, &cached_feedback[i], 0, 127, mx, my, mdown, false);
        if (!wheel_used && wheel != 0 && point_in(mx, my, feedbackSlider))
        {
            cached_feedback[i] += wheel;
            if (cached_feedback[i] < 0)
                cached_feedback[i] = 0;
            if (cached_feedback[i] > 127)
                cached_feedback[i] = 127;
            wheel_used = true;
        }
        if (cached_feedback[i] != old_feedback)
        {
            SetNeoCustomReverbCombFeedback(i, cached_feedback[i]);
            extern int g_current_custom_reverb_feedback[MAX_NEO_COMBS];
            g_current_custom_reverb_feedback[i] = cached_feedback[i];
        }
        snprintf(label, sizeof(label), "%d", cached_feedback[i]);
        draw_text(R, sliderX + sliderW - 40, y + 2, label, g_text_color);
        y += 28;
        
        // Gain
        snprintf(label, sizeof(label), "  Gain:");
        draw_text(R, labelX + 10, y + 4, label, g_text_color);
        Rect gainSlider = {sliderX, y, sliderW - 50, sliderH};
        int old_gain = cached_gain[i];
        ui_slider(R, gainSlider, &cached_gain[i], 0, 127, mx, my, mdown, false);
        if (!wheel_used && wheel != 0 && point_in(mx, my, gainSlider))
        {
            cached_gain[i] += wheel;
            if (cached_gain[i] < 0)
                cached_gain[i] = 0;
            if (cached_gain[i] > 127)
                cached_gain[i] = 127;
            wheel_used = true;
        }
        if (cached_gain[i] != old_gain)
        {
            SetNeoCustomReverbCombGain(i, cached_gain[i]);
            extern int g_current_custom_reverb_gain[MAX_NEO_COMBS];
            g_current_custom_reverb_gain[i] = cached_gain[i];
        }
        snprintf(label, sizeof(label), "%d", cached_gain[i]);
        draw_text(R, sliderX + sliderW - 40, y + 2, label, g_text_color);
        y += 35;
    }

    // Low-pass filter
    y += 10;
    draw_text(R, labelX, y + 4, "Low-pass:", g_text_color);
    Rect lowpassSlider = {sliderX, y, sliderW - 50, sliderH};
    int old_lowpass = cached_lowpass;
    ui_slider(R, lowpassSlider, &cached_lowpass, 0, 127, mx, my, mdown, false);
    if (!wheel_used && wheel != 0 && point_in(mx, my, lowpassSlider))
    {
        cached_lowpass += wheel;
        if (cached_lowpass < 0)
            cached_lowpass = 0;
        if (cached_lowpass > 127)
            cached_lowpass = 127;
        wheel_used = true;
    }
    if (cached_lowpass != old_lowpass)
    {
        SetNeoCustomReverbLowpass(cached_lowpass);
            extern int g_current_custom_reverb_lowpass;
            g_current_custom_reverb_lowpass = cached_lowpass;
    }
    
    char lowpassBuf[64];
    snprintf(lowpassBuf, sizeof(lowpassBuf), "%d", cached_lowpass);
    draw_text(R, sliderX + sliderW - 40, y + 2, lowpassBuf, g_text_color);

    // Info text at bottom
    y += rowH - 10;
    const char *info = "Adjust parameters in real-time. Use scroll wheel for fine tuning.";
    draw_text(R, dlg.x + pad + 10, y, info, g_text_color);
}

