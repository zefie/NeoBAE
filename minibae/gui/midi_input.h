// Lightweight MIDI input wrapper using RtMidi C API

#ifndef MIDI_INPUT_H
#define MIDI_INPUT_H

#include <stdbool.h>

// Initialize MIDI input. Returns true on success.
// client_name: optional display name for virtual port, may be NULL.
// Initialize MIDI input. Returns true on success.
// client_name: optional display name for virtual port, may be NULL.
// api_index: if >=0, attempts to use that RtMidi compiled API (see rtmidi_get_compiled_api). Use -1 for default.
// port_index: if >=0, open that device port for the chosen API. Use -1 to open the first available or virtual port.
bool midi_input_init(const char *client_name, int api_index, int port_index);

// Shutdown MIDI input and free resources.
void midi_input_shutdown(void);

// Poll for a pending MIDI message. Returns true if a message was returned.
// buffer should be at least 3 bytes for short messages; size_out will contain
// the message length (1..1024). timestamp is seconds since some epoch (as double).
bool midi_input_poll(unsigned char *buffer, unsigned int *size_out, double *timestamp);

#endif // MIDI_INPUT_H
