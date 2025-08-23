#ifndef GUI_MIDI_H
#define GUI_MIDI_H

#include "gui_midi_vkbd.h"
#include "gui_midi_hw.h"

/* Declaration only - actual definition is provided in gui_bae.c.
	Use extern to avoid multiple-definition linker errors. */
extern double g_last_requested_master_volume;

#endif // GUI_MIDI_H
