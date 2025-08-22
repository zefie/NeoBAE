#ifndef GUI_MIDI_VKBD_H
#define GUI_MIDI_VKBD_H

#include "gui_common.h"
#include "MiniBAE.h"

// Virtual keyboard state
extern bool g_show_virtual_keyboard;
extern int g_keyboard_channel;
extern bool g_keyboard_channel_dd_open;
extern bool g_keyboard_show_all_channels;
extern unsigned char g_keyboard_active_notes[128];
extern unsigned char g_keyboard_active_notes_by_channel[16][128];
extern int g_keyboard_mouse_note;
extern Uint32 g_keyboard_suppress_until;
extern int g_keyboard_pressed_note[512]; // SDL_NUM_SCANCODES
extern int g_keyboard_base_octave;
extern bool g_keyboard_map_initialized;

// Function declarations
void gui_panic_all_notes(BAESong s);
void gui_panic_channel_notes(BAESong s, int ch);

#endif // GUI_MIDI_VKBD_H
