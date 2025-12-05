// gui_midi_vkbd.c - Virtual MIDI keyboard

#include "gui_midi_vkbd.h"
#include "gui_bae.h" // for BAEGUI type and g_bae, g_live_song
#if USE_SF2_SUPPORT == TRUE
    #if _USING_FLUIDSYNTH == TRUE
        #include "GenSF2_FluidSynth.h"
    #endif
#endif

// Virtual keyboard state
bool g_show_virtual_keyboard = false; // user toggle (default off)
int g_keyboard_channel = 0;           // 0..15
bool g_keyboard_channel_dd_open = false;
bool g_keyboard_show_all_channels = false;  // default: show only selected channel
bool g_keyboard_active_notes[128]; // temp buffer each frame
bool g_keyboard_active_notes_by_channel[16][128];
int g_keyboard_mouse_note = -1; // currently held note by mouse, -1 if none
Uint32 g_keyboard_suppress_until = 0;
int g_keyboard_pressed_note[512]; // SDL_NUM_SCANCODES
int g_keyboard_base_octave = 4;
bool g_keyboard_map_initialized = false;
int g_keyboard_lsb = 0; // LSB (Least Significant Byte) 0-127
int g_keyboard_msb = 0; // MSB (Most Significant Byte) 0-127

// Functions that should always be available (not MIDI hardware dependent)
void gui_panic_all_notes(BAESong s)
{
    if (!s)
        return;
    for (int ch = 0; ch < BAE_MAX_MIDI_CHANNELS; ++ch)
    {
        BAESong_ControlChange(s, (unsigned char)ch, 64, 0, 0);  // Sustain Off
        BAESong_ControlChange(s, (unsigned char)ch, 120, 0, 0); // All Sound Off
        BAESong_ControlChange(s, (unsigned char)ch, 123, 0, 0); // All Notes Off
    }
    for (int ch = 0; ch < BAE_MAX_MIDI_CHANNELS; ++ch)
    {
#if USE_SF2_SUPPORT == TRUE
        GM_SF2_KillChannelNotes(ch);
#endif
        for (int n = 0; n < 128; ++n)
        {
            BAESong_NoteOff(s, (unsigned char)ch, (unsigned char)n, 0, 0);
        }
    }
#if USE_SF2_SUPPORT == TRUE
    GM_ResetSF2();
#endif
}

void gui_panic_channel_notes(BAESong s, int ch)
{
    if (!s)
        return;
    if (ch < 0 || ch >= BAE_MAX_MIDI_CHANNELS)
        return;
    // Safety controls first
    BAESong_ControlChange(s, (unsigned char)ch, 64, 0, 0);  // Sustain Off
    BAESong_ControlChange(s, (unsigned char)ch, 120, 0, 0); // All Sound Off
    BAESong_ControlChange(s, (unsigned char)ch, 123, 0, 0); // All Notes Off
    // Explicit NoteOff for any keys we believe are active from MIDI-in
#if USE_SF2_SUPPORT == TRUE
    GM_SF2_KillAllNotes();
#endif
    for (int n = 0; n < 128; ++n)
    {
        if (g_keyboard_active_notes_by_channel[ch][n])
        {
            BAESong_NoteOff(s, (unsigned char)ch, (unsigned char)n, 0, 0);
        }
    }
}

void gui_clear_virtual_keyboard_channel(int ch)
{
    if (ch < 0 || ch >= BAE_MAX_MIDI_CHANNELS)
        return;
       
    // Clear UI bookkeeping for highlighted keys on this channel
    memset(g_keyboard_active_notes_by_channel[ch], 0, sizeof(g_keyboard_active_notes_by_channel[ch]));
    if (ch == g_keyboard_channel || g_keyboard_show_all_channels)
    {
        memset(g_keyboard_active_notes, 0, sizeof(g_keyboard_active_notes));
        if (g_keyboard_show_all_channels) {
            // Rebuild overall active notes array from other channels
            memset(g_keyboard_active_notes, 0, sizeof(g_keyboard_active_notes));
            for (int c = 0; c < BAE_MAX_MIDI_CHANNELS; ++c)
            {
                if (c == ch)
                    continue;
                for (int n = 0; n < 128; ++n)
                {
                    if (g_keyboard_active_notes_by_channel[c][n])
                    {
                        g_keyboard_active_notes[n] = true;
                    }
                }
            }
        }
    }
}

void gui_clear_virtual_keyboard_all_channels(void)
{
    // Send NoteOff to audio engine for all active notes on all channels
    BAESong target = g_bae.song ? g_bae.song : g_live_song;
    if (target)
    {
        for (int ch = 0; ch < BAE_MAX_MIDI_CHANNELS; ++ch)
        {
            gui_clear_virtual_keyboard_channel(ch);
        }
        memset(g_keyboard_active_notes, 0, sizeof(g_keyboard_active_notes));
    }
}

void gui_refresh_virtual_keyboard_channel_from_engine(int ch)
{
    if (ch < 0 || ch >= BAE_MAX_MIDI_CHANNELS)
        return;
    
    // Query current engine state and update UI bookkeeping to match
    BAESong target = g_bae.song ? g_bae.song : g_live_song;
    if (target)
    {
        unsigned char engine_notes[128];
        memset(engine_notes, 0, sizeof(engine_notes));
        BAESong_GetActiveNotes(target, (unsigned char)ch, engine_notes);
        
        // Update UI bookkeeping to match current engine state
        for (int n = 0; n < 128; ++n)
            g_keyboard_active_notes_by_channel[ch][n] = (engine_notes[n] != 0);
    }
}

