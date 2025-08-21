// Lightweight MIDI output wrapper using RtMidi C API

#ifndef MIDI_OUTPUT_H
#define MIDI_OUTPUT_H

#include <stdbool.h>

// Initialize MIDI output. Returns true on success.
// client_name: optional display name for virtual port, may be NULL.
// api_index: if >=0, attempts to use that RtMidi compiled API. Use -1 for default.
// port_index: if >=0, open that device port for the chosen API. Use -1 to open the first available or virtual port.
bool midi_output_init(const char *client_name, int api_index, int port_index);

// Shutdown MIDI output and free resources.
void midi_output_shutdown(void);

// Send a short MIDI message out the opened output device. Returns true on success.
bool midi_output_send(const unsigned char *msg, int len);

// Send All Notes Off / All Sound Off across all 16 MIDI channels. Safe to call
// from any thread when midi_output is initialized.
void midi_output_send_all_notes_off(void);

#endif // MIDI_OUTPUT_H
