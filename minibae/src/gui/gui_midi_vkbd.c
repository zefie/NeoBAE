// gui_midi_vkbd.c - Virtual MIDI keyboard

#include "gui_midi_vkbd.h"
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
    for (int ch = 0; ch < 16; ++ch)
    {
        BAESong_ControlChange(s, (unsigned char)ch, 64, 0, 0);  // Sustain Off
        BAESong_ControlChange(s, (unsigned char)ch, 120, 0, 0); // All Sound Off
        BAESong_ControlChange(s, (unsigned char)ch, 123, 0, 0); // All Notes Off
    }
    for (int ch = 0; ch < 16; ++ch)
    {
#if USE_SF2_SUPPORT == TRUE
        GM_SF2_KillChannelNotes(ch);
#endif
        for (int n = 0; n < 128; ++n)
        {
            BAESong_NoteOff(s, (unsigned char)ch, (unsigned char)n, 0, 0);
        }
    }
}

void gui_panic_channel_notes(BAESong s, int ch)
{
    if (!s)
        return;
    if (ch < 0 || ch >= 16)
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
