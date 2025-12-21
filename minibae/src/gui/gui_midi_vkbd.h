#ifndef GUI_MIDI_VKBD_H
#define GUI_MIDI_VKBD_H

#include "gui_common.h"
#include "MiniBAE.h"

// Virtual keyboard state
extern bool g_show_virtual_keyboard;
extern int g_keyboard_channel;
extern bool g_keyboard_channel_dd_open;
extern bool g_keyboard_show_all_channels;
extern bool g_keyboard_active_notes[BAE_MAX_NOTES];
extern bool g_keyboard_active_notes_by_channel[BAE_MAX_MIDI_CHANNELS][BAE_MAX_NOTES];
extern int g_keyboard_mouse_note;
extern Uint32 g_keyboard_suppress_until;
extern int g_keyboard_pressed_note[512]; // SDL_NUM_SCANCODES
extern int g_keyboard_base_octave;
extern bool g_keyboard_map_initialized;
extern int g_keyboard_program;
extern int g_keyboard_msb;

// Function declarations
void gui_panic_all_notes(BAESong s);
void gui_panic_channel_notes(BAESong s, int ch);
void gui_clear_virtual_keyboard_channel(int ch);
void gui_clear_virtual_keyboard_all_channels(void);
void gui_refresh_virtual_keyboard_channel_from_engine(int ch);

#endif // GUI_MIDI_VKBD_H
