// Simple MIDI output using RtMidi C wrapper.

#include "gui_midi_hw_output.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "rtmidi_c.h"

#include <SDL2/SDL.h>

static RtMidiOutPtr g_rtmidi_out = NULL;
// Mutex to protect access to g_rtmidi_out when GUI toggles devices while engine thread
// may be sending messages.
static SDL_mutex *g_midi_out_mutex = NULL;

bool midi_output_init(const char *client_name, int api_index, int port_index)
{
    if (g_rtmidi_out)
        return true;
    if (!g_midi_out_mutex)
    {
        g_midi_out_mutex = SDL_CreateMutex();
    }
    SDL_LockMutex(g_midi_out_mutex);
    if (api_index >= 0)
    {
        enum RtMidiApi apis[16];
        int n = rtmidi_get_compiled_api(apis, (unsigned int)(sizeof(apis) / sizeof(apis[0])));
        if (api_index < n)
        {
            g_rtmidi_out = rtmidi_out_create(apis[api_index], client_name ? client_name : "miniBAE");
        }
    }
    if (!g_rtmidi_out)
        g_rtmidi_out = rtmidi_out_create_default();
    if (!g_rtmidi_out)
        return false;
    unsigned int cnt = rtmidi_get_port_count(g_rtmidi_out);
    if (port_index >= 0 && (unsigned int)port_index < cnt)
    {
        rtmidi_open_port(g_rtmidi_out, (unsigned int)port_index, client_name ? client_name : "miniBAE");
    }
    else if (cnt > 0)
    {
        rtmidi_open_port(g_rtmidi_out, 0, client_name ? client_name : "miniBAE");
    }
    else
    {
        // create virtual output port
        rtmidi_open_virtual_port(g_rtmidi_out, client_name ? client_name : "miniBAE");
    }
    SDL_UnlockMutex(g_midi_out_mutex);
    return true;
}

void midi_output_shutdown(void)
{
    if (!g_rtmidi_out)
        return;
    if (g_midi_out_mutex)
        SDL_LockMutex(g_midi_out_mutex);
    rtmidi_close_port(g_rtmidi_out);
    rtmidi_out_free(g_rtmidi_out);
    g_rtmidi_out = NULL;
    if (g_midi_out_mutex)
        SDL_UnlockMutex(g_midi_out_mutex);
}

bool midi_output_send(const unsigned char *msg, int len)
{
    if (!msg || len <= 0)
        return false;
    if (!g_rtmidi_out)
        return false;
    // Protect underlying rtmidi pointer from concurrent init/shutdown
    if (g_midi_out_mutex)
        SDL_LockMutex(g_midi_out_mutex);
    int r = rtmidi_out_send_message(g_rtmidi_out, msg, len);
    if (g_midi_out_mutex)
        SDL_UnlockMutex(g_midi_out_mutex);
    return (r == 0);
}

void midi_output_send_all_notes_off(void)
{
    // Send Control Change 123 (All Notes Off) and 120 (All Sound Off) to all channels
    unsigned char msg[3];
    for (int ch = 0; ch < 16; ++ch)
    {
        // All Sound Off: controller 120
        msg[0] = (unsigned char)(0xB0 | (ch & 0x0F));
        msg[1] = 120;
        msg[2] = 0;
        midi_output_send(msg, 3);
        // All Notes Off: controller 123
        msg[0] = (unsigned char)(0xB0 | (ch & 0x0F));
        msg[1] = 123;
        msg[2] = 0;
        midi_output_send(msg, 3);
        // Reset controllers (optional): controller 121
        msg[0] = (unsigned char)(0xB0 | (ch & 0x0F));
        msg[1] = 121;
        msg[2] = 0;
        midi_output_send(msg, 3);
    }
}
